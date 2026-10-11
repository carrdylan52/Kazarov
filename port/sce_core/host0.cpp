#include "host0.hpp"

#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

#include "platform/paths.hpp"

namespace sce_core::host0 {

namespace fs = std::filesystem;

namespace {

constexpr int kAccessMask = 0x0003;

// The CRT's descriptor calls under one name on each system, with a 64-bit offset.
namespace native {
#ifdef _WIN32
constexpr int kReadOnly = _O_RDONLY, kWriteOnly = _O_WRONLY, kReadWrite = _O_RDWR, kAppend = _O_APPEND,
              kCreate = _O_CREAT, kTruncate = _O_TRUNC, kExclusive = _O_EXCL;
// CRT text mode translates CR/LF and treats Ctrl-Z as EOF, corrupting binary game files.
constexpr int kAlways = _O_BINARY | _O_NOINHERIT;

int Open(const fs::path &path, int flags) {
    int fd = -1;
    _wsopen_s(&fd, path.c_str(), flags, _SH_DENYNO, _S_IREAD | _S_IWRITE);
    return fd;
}

int          Close(int fd) { return _close(fd); }
std::int64_t Seek(int fd, std::int64_t offset, int whence) { return _lseeki64(fd, offset, whence); }
std::int64_t Read(int fd, void *buffer, unsigned size) { return _read(fd, buffer, size); }
std::int64_t Write(int fd, const void *buffer, unsigned size) { return _write(fd, buffer, size); }
#else
constexpr int kReadOnly = O_RDONLY, kWriteOnly = O_WRONLY, kReadWrite = O_RDWR, kAppend = O_APPEND,
              kCreate = O_CREAT, kTruncate = O_TRUNC, kExclusive = O_EXCL;
constexpr int kAlways = O_CLOEXEC;

int          Open(const fs::path &path, int flags) { return ::open(path.c_str(), flags, 0644); }
int          Close(int fd) { return ::close(fd); }
std::int64_t Seek(int fd, std::int64_t offset, int whence) { return ::lseek(fd, static_cast<off_t>(offset), whence); }
std::int64_t Read(int fd, void *buffer, unsigned size) { return ::read(fd, buffer, size); }
std::int64_t Write(int fd, const void *buffer, unsigned size) { return ::write(fd, buffer, size); }
#endif
} // namespace native

// Held across each call, so no thread uses a descriptor another is closing (whose number the
// system may already have given a new file).
std::mutex              g_mutex;
std::unordered_set<int> g_open;

bool HostPath(const char *name, fs::path &out) {
    if (name == nullptr) {
        return false;
    }
    std::string_view path = name;
    if (std::size_t colon = path.rfind(':'); colon != std::string_view::npos) {
        path = path.substr(colon + 1);
    }
    std::string relative(path);
    std::ranges::replace(relative, '\\', '/');
    relative.erase(0, relative.find_first_not_of('/'));
    fs::path normal = fs::path(relative).lexically_normal();
    if (relative.empty() || normal.empty() || *normal.begin() == ".." || normal.is_absolute()) {
        return false;
    }
    out = PathsSaveRoot() / "host0" / normal;
    return true;
}

int HostFlags(int flags) {
    int host;
    switch (flags & kAccessMask) {
        case kWriteOnly:
            host = native::kWriteOnly;
            break;
        case kReadWrite:
            host = native::kReadWrite;
            break;
        default:
            host = native::kReadOnly;
            break;
    }
    if (flags & kAppend) {
        host |= native::kAppend;
    }
    if (flags & kCreate) {
        host |= native::kCreate;
    }
    if (flags & kTruncate) {
        host |= native::kTruncate;
    }
    if (flags & kExclusive) {
        host |= native::kExclusive;
    }
    return host | native::kAlways;
}

bool Known(int fd) {
    return g_open.contains(fd);
}

} // namespace

int Open(const char *name, int flags) {
    fs::path path;
    if (!HostPath(name, path)) {
        return -ENOENT;
    }
    if (flags & kCreate) {
        std::error_code error;
        fs::create_directories(path.parent_path(), error);
    }
    std::lock_guard lock(g_mutex);
    int             fd = native::Open(path, HostFlags(flags));
    if (fd < 0) {
        return -errno;
    }
    g_open.insert(fd);
    return fd;
}

int Close(int fd) {
    std::lock_guard lock(g_mutex);
    if (!Known(fd)) {
        return -EBADF;
    }
    g_open.erase(fd);
    return native::Close(fd) < 0 ? -errno : 0;
}

int Lseek(int fd, int offset, int whence) {
    std::lock_guard lock(g_mutex);
    if (!Known(fd)) {
        return -EBADF;
    }
    std::int64_t before = native::Seek(fd, 0, SEEK_CUR);
    std::int64_t result = native::Seek(fd, offset, whence);
    if (result > INT_MAX) {
        // The game's offsets are ints: as lseek with off_t, refuse and keep the position.
        native::Seek(fd, before, SEEK_SET);
        return -EOVERFLOW;
    }
    return result < 0 ? -errno : static_cast<int>(result);
}

int Read(int fd, void *buffer, int size) {
    std::lock_guard lock(g_mutex);
    if (!Known(fd)) {
        return -EBADF;
    }
    std::int64_t result = native::Read(fd, buffer, static_cast<unsigned>(std::max(size, 0)));
    return result < 0 ? -errno : static_cast<int>(result);
}

int Write(int fd, const void *buffer, int size) {
    std::lock_guard lock(g_mutex);
    if (!Known(fd)) {
        return -EBADF;
    }
    std::int64_t result = native::Write(fd, buffer, static_cast<unsigned>(std::max(size, 0)));
    return result < 0 ? -errno : static_cast<int>(result);
}

} // namespace sce_core::host0
