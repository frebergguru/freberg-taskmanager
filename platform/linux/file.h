// Small file helpers. CachedFile keeps its descriptor open and re-reads with
// pread, which avoids an open and close on every sample.
#pragma once

#include "../../core/reading.h"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace fg {

enum class ReadStatus { Ok, Missing, Denied };

inline Availability toAvailability(ReadStatus status)
{
    return status == ReadStatus::Denied ? Availability::AccessDenied : Availability::Unsupported;
}

inline ReadStatus statusFromErrno(int error)
{
    return error == EACCES || error == EPERM ? ReadStatus::Denied : ReadStatus::Missing;
}

inline ReadStatus preadAll(int fd, std::string& out)
{
    // Resizing keeps capacity, so after the first read this does not allocate.
    if (out.capacity() < 4096)
        out.reserve(4096);
    out.resize(out.capacity());
    std::size_t used = 0;
    for (;;) {
        if (used == out.size())
            out.resize(out.size() * 2);
        const ssize_t got = ::pread(fd, out.data() + used, out.size() - used, static_cast<off_t>(used));
        if (got < 0) {
            if (errno == EINTR)
                continue;
            const int error = errno;
            out.clear();
            return statusFromErrno(error);
        }
        if (got == 0)
            break;
        used += static_cast<std::size_t>(got);
    }
    out.resize(used);
    return ReadStatus::Ok;
}

class CachedFile {
public:
    CachedFile() = default;
    explicit CachedFile(std::string path) : m_path(std::move(path)) {}
    CachedFile(const CachedFile&) = delete;
    CachedFile& operator=(const CachedFile&) = delete;
    CachedFile(CachedFile&& other) noexcept : m_path(std::move(other.m_path)), m_fd(other.m_fd) { other.m_fd = -1; }
    CachedFile& operator=(CachedFile&& other) noexcept
    {
        std::swap(m_path, other.m_path);
        std::swap(m_fd, other.m_fd);
        return *this;
    }
    ~CachedFile()
    {
        if (m_fd >= 0)
            ::close(m_fd);
    }

    const std::string& path() const { return m_path; }

    ReadStatus read(std::string& out)
    {
        if (m_fd < 0) {
            m_fd = ::open(m_path.c_str(), O_RDONLY | O_CLOEXEC);
            if (m_fd < 0) {
                out.clear();
                return statusFromErrno(errno);
            }
        }
        const ReadStatus status = preadAll(m_fd, out);
        if (status != ReadStatus::Ok) {
            ::close(m_fd);
            m_fd = -1;
        }
        return status;
    }

private:
    std::string m_path;
    int m_fd = -1;
};

// One-shot read for files that are not worth keeping open.
inline ReadStatus readFile(const std::string& path, std::string& out)
{
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        out.clear();
        return statusFromErrno(errno);
    }
    const ReadStatus status = preadAll(fd, out);
    ::close(fd);
    return status;
}

} // namespace fg
