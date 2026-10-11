#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "iso9660.hpp"
#include "path.hpp"
#include "sha256.hpp"

namespace kzdata {

// Written at the extraction's root. The hashes are of the source extents, so `Verify` proves the
// output against the disc without the disc.
inline constexpr std::string_view kManifestFile = "manifest.sha256";

struct Entry {
    std::string path; // relative to the output, already folded by the game's rules
    Extent      extent;
};

struct ManifestRow {
    std::string   sha256;
    std::uint64_t size = 0;
    std::string   source; // the source file's name and size, for a reader: nothing trusts it
    std::uint64_t offset = 0;
};

using Manifest = std::map<std::string, ManifestRow>;

struct Progress {
    std::size_t   files = 0;
    std::size_t   total_files = 0;
    std::uint64_t bytes = 0;
    std::uint64_t total_bytes = 0;
};

// Called after each file and each chunk written; false stops the extraction with an Error, leaving
// what was written so far for a later run to keep.
using ProgressCallback = std::function<bool(const Progress &)>;

struct Result {
    std::size_t   written = 0;
    std::size_t   kept = 0;
    std::uint64_t bytes = 0;
    std::uint64_t written_bytes = 0;
};

template <class... Args>
void Log(std::FILE *log, std::format_string<Args...> format, Args &&...args) {
    if (log) {
        std::fputs(std::format(format, std::forward<Args>(args)...).c_str(), log);
    }
}

inline bool IsCurrent(const fs::path &path, std::uint64_t size) {
    std::error_code error;
    return fs::is_regular_file(path, error) && fs::file_size(path, error) == size && !error;
}

inline std::vector<unsigned char> ReadFile(const fs::path &path) {
    Reader                     reader(path);
    std::vector<unsigned char> data(reader.Size());
    reader.Read(0, data.data(), data.size());
    return data;
}

// A name beside path no other call, here or in another process, gives: two extractions into one
// directory must never write, or rename, each other's temporary file.
inline fs::path TemporaryBeside(const fs::path &path) {
    static std::atomic<std::uint64_t> counter = 0;
    static const std::uint64_t        salt = std::random_device{}();
#ifdef _WIN32
    int process = _getpid();
#else
    int process = ::getpid();
#endif
    return path.parent_path() / std::format(".{}.{}.{:x}.{}.kztmp", path.filename().string(), process, salt,
                                            counter.fetch_add(1));
}

// A new file at path, never an existing one or what a link there points to.
class NewFile {
public:
    explicit NewFile(const fs::path &path) : path_(path) {
#ifdef _WIN32
        // _O_CREAT | _O_EXCL is CreateFile's CREATE_NEW, which fails on any existing name, a link
        // included, rather than following it.
        _wsopen_s(&fd_, path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT, _SH_DENYRW,
                  _S_IREAD | _S_IWRITE);
#else
        fd_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
#endif
        if (fd_ < 0) {
            Fail("cannot create {}", path.string());
        }
    }

    NewFile(const NewFile &) = delete;
    NewFile &operator=(const NewFile &) = delete;

    ~NewFile() {
        if (fd_ >= 0) {
            CloseFd();
            std::error_code ignored;
            fs::remove(path_, ignored);
        }
    }

    void Write(std::span<const unsigned char> data) {
        while (!data.empty()) {
            unsigned step = static_cast<unsigned>(std::min<std::size_t>(data.size(), 1u << 30));
#ifdef _WIN32
            int written = _write(fd_, data.data(), step);
#else
            ssize_t written = ::write(fd_, data.data(), step);
#endif
            if (written <= 0) {
                Fail("write error on {}", path_.string());
            }
            data = data.subspan(static_cast<std::size_t>(written));
        }
    }

    // Closes the file and renames it over target; until then, or if that fails, it is removed.
    void Replace(const fs::path &target) {
        int             closed = CloseFd();
        std::error_code error;
        if (closed == 0) {
            fs::rename(path_, target, error);
        }
        if (closed != 0 || error) {
            std::error_code ignored;
            fs::remove(path_, ignored);
            Fail("cannot write {}: {}", target.string(), closed != 0 ? "write error" : error.message());
        }
    }

private:
    int CloseFd() {
        int fd = std::exchange(fd_, -1);
#ifdef _WIN32
        return _close(fd);
#else
        return ::close(fd);
#endif
    }

    fs::path path_;
    int      fd_ = -1;
};

// Readers never see a half-written file: an interrupted write leaves the old one in place.
inline void WriteFile(const fs::path &path, std::span<const unsigned char> data) {
    fs::create_directories(path.parent_path());
    NewFile file(TemporaryBeside(path));
    file.Write(data);
    file.Replace(path);
}

// Whether the path is a link, a junction or anything else neither a file nor a directory.
inline bool IsLinkLike(const fs::path &path) {
    std::error_code error;
    fs::file_status status = fs::symlink_status(path, error);
    return fs::is_symlink(status) ||
           (fs::exists(status) && !fs::is_regular_file(status) && !fs::is_directory(status));
}

// The first link (or junction) from `out` itself down to `path`: it would take a write or a read
// outside `out`.
inline std::optional<fs::path> LinkOnTheWay(const fs::path &out, std::string_view path) {
    fs::path at = out;
    if (IsLinkLike(at)) {
        return at;
    }
    for (const fs::path &component : fs::path(path)) {
        at /= component;
        if (IsLinkLike(at)) {
            return at;
        }
    }
    return std::nullopt;
}

inline void RefuseLinks(const fs::path &out, std::string_view path) {
    if (std::optional<fs::path> link = LinkOnTheWay(out, path)) {
        Fail("{} is a link", link->string());
    }
}

// The temporary file for out/<path>, its directories made. The links are looked for again after
// they are made and right before the file is created, which narrows the window a directory could
// be swapped for a link in without closing it. That is enough for the threat model: extraction is
// local and single-user, into the user's own data directory, serialised by the launcher's lock,
// so whoever can swap a directory there already controls the user's files.
inline NewFile CreateTemporary(const fs::path &out, std::string_view path) {
    fs::path target = out / fs::path(path);
    // One directory at a time, each looked at before anything is made inside it, so a link on the
    // way is refused before a directory appears at its far end.
    RefuseLinks(out, "");
    fs::create_directories(out);
    fs::path at = out;
    for (const fs::path &component : fs::path(path).parent_path()) {
        at /= component;
        if (IsLinkLike(at)) {
            Fail("{} is a link", at.string());
        }
        fs::create_directory(at);
    }
    RefuseLinks(out, path);
    return NewFile(TemporaryBeside(target));
}

inline std::string SourceIdentity(const fs::path &file) {
    std::error_code error;
    std::uint64_t   size = fs::file_size(file, error);
    return std::format("{}@{}", file.filename().string(), error ? 0 : size);
}

// The extent's hash when `target` holds exactly its bytes, or nothing. Both are read: neither a
// manifest nor a source's name and size says what either holds now.
inline std::string HashIfSame(const Extent &extent, const fs::path &target, std::vector<unsigned char> &chunk,
                              std::vector<unsigned char> &held) {
    Reader        source(extent.file);
    Reader        kept(target);
    Sha256        hash;
    std::uint64_t offset = 0;
    held.resize(chunk.size());
    for (std::uint64_t left = extent.size; left != 0;) {
        std::size_t step = static_cast<std::size_t>(std::min<std::uint64_t>(left, chunk.size()));
        source.Read(extent.offset + offset, chunk.data(), step);
        kept.Read(offset, held.data(), step);
        if (std::memcmp(chunk.data(), held.data(), step) != 0) {
            return {};
        }
        hash.Update({chunk.data(), step});
        offset += step;
        left -= step;
    }
    return hash.FinishHex();
}

inline std::string HashFile(const fs::path &path, std::uint64_t offset, std::uint64_t size,
                            std::vector<unsigned char> &chunk) {
    Reader reader(path);
    Sha256 hash;
    for (std::uint64_t left = size; left != 0;) {
        std::size_t step = static_cast<std::size_t>(std::min<std::uint64_t>(left, chunk.size()));
        reader.Read(offset, chunk.data(), step);
        hash.Update({chunk.data(), step});
        offset += step;
        left -= step;
    }
    return hash.FinishHex();
}

inline Manifest ReadManifest(const fs::path &root) {
    Manifest      manifest;
    std::ifstream file(root / kManifestFile);
    if (!file) {
        return manifest;
    }
    std::string line;
    for (int number = 1; std::getline(file, line); number++) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::size_t tabs[4];
        std::size_t at = 0;
        for (std::size_t &tab : tabs) {
            tab = line.find('\t', at);
            if (tab == std::string::npos) {
                Fail("{} line {}: expected five tab-separated fields", (root / kManifestFile).string(), number);
            }
            at = tab + 1;
        }
        ManifestRow row;
        row.sha256 = line.substr(0, tabs[0]);
        row.source = line.substr(tabs[1] + 1, tabs[2] - tabs[1] - 1);
        std::string path = line.substr(tabs[3] + 1);
        try {
            row.size = std::stoull(line.substr(tabs[0] + 1, tabs[1] - tabs[0] - 1));
            row.offset = std::stoull(line.substr(tabs[2] + 1, tabs[3] - tabs[2] - 1));
        } catch (const std::exception &) {
            Fail("{} line {}: bad number", (root / kManifestFile).string(), number);
        }
        if (row.sha256.size() != 64 || SafeRelative(path) != path || !manifest.emplace(path, row).second) {
            Fail("{} line {}: bad row", (root / kManifestFile).string(), number);
        }
    }
    return manifest;
}

inline void WriteManifest(const fs::path &root, const Manifest &manifest) {
    std::string text = "# sha256\tsize\tsource\toffset\tpath\n";
    for (const auto &[path, row] : manifest) {
        text += std::format("{}\t{}\t{}\t{}\t{}\n", row.sha256, row.size, row.source, row.offset, path);
    }
    NewFile file = CreateTemporary(root, kManifestFile);
    file.Write(std::span(reinterpret_cast<const unsigned char *>(text.data()), text.size()));
    RefuseLinks(root, kManifestFile);
    file.Replace(root / kManifestFile);
}

// Everything that would make the output ambiguous or escape `out` is refused before a byte is
// written: unsafe paths, two entries for one path, a file where another needs a directory, and
// extents past the end of their source.
inline void CheckEntries(std::span<const Entry> entries, const fs::path &out, const Manifest &taken = {}) {
    std::unordered_set<std::string> files;
    for (const Entry &entry : entries) {
        if (SafeRelative(entry.path) != entry.path || entry.path.find_first_of("\t\n\r") != std::string::npos ||
            entry.path == kManifestFile) {
            Fail("unusable path \"{}\"", entry.path);
        }
        if (!files.insert(entry.path).second || taken.contains(entry.path)) {
            Fail("two files are named {}", entry.path);
        }
        std::error_code error;
        std::uint64_t   size = fs::file_size(entry.extent.file, error);
        if (error || entry.extent.offset > size || entry.extent.size > size - entry.extent.offset) {
            Fail("{} is truncated: {} needs bytes up to {}", entry.extent.file.string(), entry.path,
                 entry.extent.offset + entry.extent.size);
        }
    }
    for (const Entry &entry : entries) {
        RefuseLinks(out, entry.path);
        for (std::size_t slash = entry.path.find('/'); slash != std::string::npos;
             slash = entry.path.find('/', slash + 1)) {
            std::string directory = entry.path.substr(0, slash);
            if (files.contains(directory) || taken.contains(directory)) {
                Fail("{} is both a file and the directory of {}", directory, entry.path);
            }
            std::error_code error;
            if (fs::exists(out / directory, error) && !fs::is_directory(out / directory, error)) {
                Fail("{} is in the way of {}", (out / directory).string(), entry.path);
            }
        }
        std::error_code error;
        if (fs::is_directory(out / entry.path, error)) {
            Fail("{} is a directory", (out / entry.path).string());
        }
    }
}

// Copies each entry's extent to out/<path> unless the file there already holds its bytes, and
// records its hash in `next`.
inline Result ExtractEntries(std::span<const Entry> entries, const fs::path &out, Manifest &next,
                             const ProgressCallback &progress = {}) {
    CheckEntries(entries, out, next);
    Result   result;
    Progress done{.total_files = entries.size()};
    for (const Entry &entry : entries) {
        done.total_bytes += entry.extent.size;
    }
    auto report = [&] {
        if (progress && !progress(done)) {
            Fail("extraction cancelled");
        }
    };

    RefuseLinks(out, "");
    fs::create_directories(out);
    std::vector<unsigned char> chunk(4 << 20);
    std::vector<unsigned char> held;
    for (const Entry &entry : entries) {
        fs::path    target = out / fs::path(entry.path);
        std::string source = SourceIdentity(entry.extent.file);
        result.bytes += entry.extent.size;
        if (IsCurrent(target, entry.extent.size)) {
            if (std::string hash = HashIfSame(entry.extent, target, chunk, held); !hash.empty()) {
                next[entry.path] = {hash, entry.extent.size, source, entry.extent.offset};
                result.kept++;
                done.files++;
                done.bytes += entry.extent.size;
                report();
                continue;
            }
        }
        NewFile       file = CreateTemporary(out, entry.path);
        Sha256        hash;
        Reader        reader(entry.extent.file);
        std::uint64_t offset = entry.extent.offset;
        for (std::uint64_t left = entry.extent.size; left != 0;) {
            std::size_t step = static_cast<std::size_t>(std::min<std::uint64_t>(left, chunk.size()));
            reader.Read(offset, chunk.data(), step);
            hash.Update({chunk.data(), step});
            file.Write({chunk.data(), step});
            offset += step;
            left -= step;
            done.bytes += step;
            report();
        }
        RefuseLinks(out, entry.path);
        file.Replace(target);
        next[entry.path] = {hash.FinishHex(), entry.extent.size, source, entry.extent.offset};
        result.written++;
        result.written_bytes += entry.extent.size;
        done.files++;
        report();
    }
    return result;
}

inline std::vector<std::string> Verify(const fs::path &root, std::FILE *log = nullptr) {
    if (!fs::is_regular_file(root / kManifestFile)) {
        Fail("{} has no {}", root.string(), kManifestFile);
    }
    Manifest                   manifest = ReadManifest(root);
    std::vector<std::string>   problems;
    std::vector<unsigned char> chunk(4 << 20);
    for (const auto &[path, row] : manifest) {
        fs::path file = root / fs::path(path);
        if (LinkOnTheWay(root, path)) {
            problems.push_back(std::format("a link, not a file: {}", path));
        } else if (!IsCurrent(file, row.size)) {
            problems.push_back(std::format("missing or wrong size: {}", path));
        } else if (HashFile(file, 0, row.size, chunk) != row.sha256) {
            problems.push_back(std::format("differs from its extent: {}", path));
        }
    }
    Log(log, "verified {}/{} files in {}\n", manifest.size() - problems.size(), manifest.size(), root.string());
    return problems;
}

} // namespace kzdata
