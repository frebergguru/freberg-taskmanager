// Asks the kernel's nl80211 interface which network a Wi-Fi link is on and how fast it
// sends. This stays on the machine: nothing goes over the network.
#include "samplers.h"

#include <cstring>
#include <linux/genetlink.h>
#include <linux/netlink.h>
#include <linux/nl80211.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace fg {

namespace {

constexpr int kReplyWaitMs = 200; // the sampler thread must never hang on this

class GenericNetlink {
public:
    GenericNetlink() : m_fd(::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_GENERIC))
    {
        if (m_fd >= 0) {
            const timeval wait{0, kReplyWaitMs * 1000};
            ::setsockopt(m_fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof wait);
        }
    }
    ~GenericNetlink()
    {
        if (m_fd >= 0)
            ::close(m_fd);
    }
    GenericNetlink(const GenericNetlink&) = delete;
    GenericNetlink& operator=(const GenericNetlink&) = delete;

    // Sends one request with one attribute, and passes the attributes of each reply on.
    // Returns false if the kernel refused or did not answer.
    template <class OnReply>
    bool ask(std::uint16_t family, std::uint8_t command, bool dump, std::uint16_t attribute, const void* value, std::size_t size, OnReply onReply)
    {
        if (m_fd < 0)
            return false;
        struct {
            nlmsghdr header;
            genlmsghdr generic;
            nlattr attributeHeader;
            char value[64];
        } message{};
        if (size > sizeof message.value)
            return false;
        message.attributeHeader.nla_type = attribute;
        message.attributeHeader.nla_len = static_cast<std::uint16_t>(NLA_HDRLEN + size);
        std::memcpy(message.value, value, size);
        message.generic.cmd = command;
        message.header.nlmsg_type = family;
        message.header.nlmsg_flags = static_cast<std::uint16_t>(NLM_F_REQUEST | (dump ? NLM_F_DUMP : 0));
        message.header.nlmsg_len = static_cast<std::uint32_t>(NLMSG_LENGTH(GENL_HDRLEN) + NLA_ALIGN(NLA_HDRLEN + size));
        if (::send(m_fd, &message, message.header.nlmsg_len, 0) < 0)
            return false;

        alignas(nlmsghdr) char buffer[32768];
        for (;;) {
            const ssize_t got = ::recv(m_fd, buffer, sizeof buffer, 0);
            if (got <= 0)
                return false;
            std::size_t left = static_cast<std::size_t>(got);
            for (auto* header = reinterpret_cast<nlmsghdr*>(buffer); NLMSG_OK(header, left); header = NLMSG_NEXT(header, left)) {
                if (header->nlmsg_type == NLMSG_DONE)
                    return true;
                if (header->nlmsg_type == NLMSG_ERROR)
                    return false;
                onReply(static_cast<const char*>(NLMSG_DATA(header)) + GENL_HDRLEN, header->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN));
                if (!dump)
                    return true;
            }
        }
    }

private:
    int m_fd;
};

template <class OnAttribute>
void forEachAttribute(const char* data, std::size_t size, OnAttribute onAttribute)
{
    while (size >= NLA_HDRLEN) {
        nlattr attribute;
        std::memcpy(&attribute, data, NLA_HDRLEN);
        if (attribute.nla_len < NLA_HDRLEN || attribute.nla_len > size)
            return;
        onAttribute(attribute.nla_type & NLA_TYPE_MASK, data + NLA_HDRLEN, attribute.nla_len - NLA_HDRLEN);
        const std::size_t step = NLA_ALIGN(attribute.nla_len);
        if (step >= size)
            return;
        data += step;
        size -= step;
    }
}

template <class T>
T valueOf(const char* data)
{
    T value;
    std::memcpy(&value, data, sizeof value);
    return value;
}

} // namespace

WifiLink readWifiLink(const std::string& interfaceName)
{
    WifiLink link;
    link.network = Reading<std::string>::missing(Availability::Unsupported);
    link.sendMbps = Reading<double>::missing(Availability::Unsupported);

    const std::uint32_t index = ::if_nametoindex(interfaceName.c_str());
    if (index == 0)
        return link;

    GenericNetlink netlink;
    std::uint16_t family = 0;
    netlink.ask(GENL_ID_CTRL, CTRL_CMD_GETFAMILY, false, CTRL_ATTR_FAMILY_NAME, NL80211_GENL_NAME, sizeof NL80211_GENL_NAME, [&family](const char* data, std::size_t size) {
        forEachAttribute(data, size, [&family](unsigned type, const char* value, std::size_t length) {
            if (type == CTRL_ATTR_FAMILY_ID && length >= sizeof family)
                family = valueOf<std::uint16_t>(value);
        });
    });
    if (family == 0)
        return link;

    // No network name in a good answer means the link is not connected.
    std::string network;
    const bool answered = netlink.ask(family, NL80211_CMD_GET_INTERFACE, false, NL80211_ATTR_IFINDEX, &index, sizeof index, [&network](const char* data, std::size_t size) {
        forEachAttribute(data, size, [&network](unsigned type, const char* value, std::size_t length) {
            if (type == NL80211_ATTR_SSID)
                network.assign(value, length);
        });
    });
    if (answered)
        link.network = Reading<std::string>::measured(network);

    std::uint32_t hundredKbit = 0;
    netlink.ask(family, NL80211_CMD_GET_STATION, true, NL80211_ATTR_IFINDEX, &index, sizeof index, [&hundredKbit](const char* data, std::size_t size) {
        forEachAttribute(data, size, [&hundredKbit](unsigned type, const char* value, std::size_t length) {
            if (type != NL80211_ATTR_STA_INFO)
                return;
            forEachAttribute(value, length, [&hundredKbit](unsigned stationType, const char* stationValue, std::size_t stationLength) {
                if (stationType != NL80211_STA_INFO_TX_BITRATE)
                    return;
                forEachAttribute(stationValue, stationLength, [&hundredKbit](unsigned rateType, const char* rate, std::size_t rateLength) {
                    if (rateType == NL80211_RATE_INFO_BITRATE32 && rateLength >= 4)
                        hundredKbit = valueOf<std::uint32_t>(rate);
                    else if (rateType == NL80211_RATE_INFO_BITRATE && rateLength >= 2 && hundredKbit == 0)
                        hundredKbit = valueOf<std::uint16_t>(rate);
                });
            });
        });
    });
    if (hundredKbit != 0)
        link.sendMbps = Reading<double>::measured(static_cast<double>(hundredKbit) / 10.0);
    return link;
}

} // namespace fg
