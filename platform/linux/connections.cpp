// Lists sockets through the kernel's sock_diag netlink interface, which also gives
// byte counts for TCP. Owners come from /proc/*/fd, so other users' stay hidden.
#include "samplers.h"

#include <arpa/inet.h>
#include <cstring>
#include <dirent.h>
#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sock_diag.h>
#include <linux/tcp.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>

namespace fg {

namespace {

struct Owner {
    ProcessId id;
    std::string name;
};

std::string tcpStateName(unsigned state)
{
    switch (state) {
    case 1: return "ESTABLISHED";
    case 2: return "SYN_SENT";
    case 3: return "SYN_RECV";
    case 4: return "FIN_WAIT1";
    case 5: return "FIN_WAIT2";
    case 6: return "TIME_WAIT";
    case 7: return "CLOSE";
    case 8: return "CLOSE_WAIT";
    case 9: return "LAST_ACK";
    case 10: return "LISTEN";
    case 11: return "CLOSING";
    default: return "UNKNOWN";
    }
}

std::string endpoint(int family, const __be32* address, __be16 port)
{
    char text[INET6_ADDRSTRLEN] = {};
    ::inet_ntop(family, address, text, sizeof text);
    const std::string host = family == AF_INET6 ? "[" + std::string(text) + "]" : std::string(text);
    return host + ":" + std::to_string(ntohs(port));
}

// socket inode -> owning process, for every process we may look into.
std::unordered_map<std::uint64_t, Owner> socketOwners(const std::string& root)
{
    std::unordered_map<std::uint64_t, Owner> owners;
    const std::string procPath = root + "/proc/";
    DIR* proc = ::opendir(procPath.c_str());
    if (!proc)
        return owners;
    std::string text;
    while (const dirent* entry = ::readdir(proc)) {
        const auto pid = parseI64(entry->d_name);
        if (!pid)
            continue;
        const std::string dir = procPath + entry->d_name;
        DIR* fds = ::opendir((dir + "/fd").c_str());
        if (!fds)
            continue; // not ours
        Owner owner;
        bool haveOwner = false;
        while (const dirent* fd = ::readdir(fds)) {
            if (fd->d_name[0] == '.')
                continue;
            char target[64];
            const ssize_t size = ::readlink((dir + "/fd/" + fd->d_name).c_str(), target, sizeof target - 1);
            if (size <= 8 || std::strncmp(target, "socket:[", 8) != 0)
                continue;
            target[size] = '\0';
            std::uint64_t inode = 0;
            if (!parseU64(std::string_view(target + 8, static_cast<std::size_t>(size) - 9)).has_value())
                continue;
            inode = *parseU64(std::string_view(target + 8, static_cast<std::size_t>(size) - 9));
            if (!haveOwner) {
                PidStat stat;
                if (readFile(dir + "/stat", text) != ReadStatus::Ok || !parsePidStat(text, stat))
                    break;
                owner.id = {static_cast<int>(*pid), stat.startTicks};
                owner.name = std::string(stat.comm);
                haveOwner = true;
            }
            owners.emplace(inode, owner);
        }
        ::closedir(fds);
    }
    ::closedir(proc);
    return owners;
}

void queryInet(int family, int protocol, const std::unordered_map<std::uint64_t, Owner>& owners, std::vector<NetworkConnection>& out)
{
    const int sock = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_SOCK_DIAG);
    if (sock < 0)
        return;

    struct {
        nlmsghdr header;
        inet_diag_req_v2 request;
    } message{};
    message.header.nlmsg_len = sizeof message;
    message.header.nlmsg_type = SOCK_DIAG_BY_FAMILY;
    message.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    message.request.sdiag_family = static_cast<__u8>(family);
    message.request.sdiag_protocol = static_cast<__u8>(protocol);
    message.request.idiag_states = 0xffffffffu;
    message.request.idiag_ext = 1u << (INET_DIAG_INFO - 1);

    if (::send(sock, &message, sizeof message, 0) < 0) {
        ::close(sock);
        return;
    }

    alignas(nlmsghdr) char buffer[32768];
    const std::string name = std::string(protocol == IPPROTO_TCP ? "tcp" : "udp") + (family == AF_INET6 ? "6" : "");
    for (bool done = false; !done;) {
        const ssize_t got = ::recv(sock, buffer, sizeof buffer, 0);
        if (got <= 0)
            break;
        std::size_t left = static_cast<std::size_t>(got);
        for (auto* header = reinterpret_cast<nlmsghdr*>(buffer); NLMSG_OK(header, left); header = NLMSG_NEXT(header, left)) {
            if (header->nlmsg_type == NLMSG_DONE || header->nlmsg_type == NLMSG_ERROR) {
                done = true;
                break;
            }
            const auto* diag = static_cast<const inet_diag_msg*>(NLMSG_DATA(header));
            NetworkConnection connection;
            connection.protocol = name;
            connection.local = endpoint(family, diag->id.idiag_src, diag->id.idiag_sport);
            connection.remote = endpoint(family, diag->id.idiag_dst, diag->id.idiag_dport);
            connection.state = protocol == IPPROTO_TCP ? tcpStateName(diag->idiag_state) : std::string("-");
            connection.uid = diag->idiag_uid;
            if (const auto owner = owners.find(diag->idiag_inode); owner != owners.end()) {
                connection.owner = owner->second.id;
                connection.ownerName = owner->second.name;
            }

            connection.sent = connection.received = Reading<std::uint64_t>::missing(Availability::Unsupported);
            auto attributeBytes = static_cast<int>(header->nlmsg_len - NLMSG_LENGTH(sizeof *diag));
            for (auto* attribute = reinterpret_cast<rtattr*>(const_cast<inet_diag_msg*>(diag) + 1); RTA_OK(attribute, attributeBytes); attribute = RTA_NEXT(attribute, attributeBytes)) {
                if (attribute->rta_type != INET_DIAG_INFO || protocol != IPPROTO_TCP)
                    continue;
                tcp_info info{};
                std::memcpy(&info, RTA_DATA(attribute), std::min<std::size_t>(sizeof info, RTA_PAYLOAD(attribute)));
                // Older kernels send a shorter struct, so check the field is really there.
                if (RTA_PAYLOAD(attribute) >= offsetof(tcp_info, tcpi_bytes_received) + sizeof info.tcpi_bytes_received) {
                    connection.sent = Reading<std::uint64_t>::measured(info.tcpi_bytes_acked);
                    connection.received = Reading<std::uint64_t>::measured(info.tcpi_bytes_received);
                }
            }
            out.push_back(std::move(connection));
        }
    }
    ::close(sock);
}

void readUnixSockets(const std::string& root, const std::unordered_map<std::uint64_t, Owner>& owners, std::vector<NetworkConnection>& out)
{
    std::string text;
    if (readFile(root + "/proc/net/unix", text) != ReadStatus::Ok)
        return;
    std::string_view rest = text;
    bool header = true;
    while (!rest.empty()) {
        const std::size_t end = rest.find('\n');
        std::string_view line = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
        if (header) {
            header = false;
            continue;
        }
        // Num RefCount Protocol Flags Type St Inode Path
        std::string_view fields[8];
        int count = 0;
        while (count < 8 && !line.empty()) {
            line = trimmed(line);
            const std::size_t gap = count == 7 ? std::string_view::npos : line.find(' ');
            fields[count++] = line.substr(0, gap);
            line = gap == std::string_view::npos ? std::string_view{} : line.substr(gap);
        }
        if (count < 7)
            continue;
        NetworkConnection connection;
        connection.protocol = "unix";
        connection.local = count > 7 ? std::string(fields[7]) : std::string();
        connection.state = fields[5] == "01" ? "LISTEN" : (fields[5] == "03" ? "CONNECTED" : std::string(fields[5]));
        connection.sent = connection.received = Reading<std::uint64_t>::missing(Availability::Unsupported);
        if (const auto inode = parseU64(fields[6]))
            if (const auto owner = owners.find(*inode); owner != owners.end()) {
                connection.owner = owner->second.id;
                connection.ownerName = owner->second.name;
            }
        out.push_back(std::move(connection));
    }
}

} // namespace

std::vector<NetworkConnection> readConnections(const std::string& root)
{
    std::vector<NetworkConnection> out;
    const auto owners = socketOwners(root);
    // With a fake root in tests there is no kernel behind it, so ask only the real one.
    if (root.empty())
        for (const int family : {AF_INET, AF_INET6})
            for (const int protocol : {IPPROTO_TCP, IPPROTO_UDP})
                queryInet(family, protocol, owners, out);
    readUnixSockets(root, owners, out);
    return out;
}

} // namespace fg
