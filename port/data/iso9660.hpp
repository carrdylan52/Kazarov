#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "path.hpp"

namespace kzdata {

namespace fs = std::filesystem;

inline constexpr std::uint64_t kSector = 2048;

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

template <class... Args>
[[noreturn]] void Fail(std::format_string<Args...> format, Args &&...args) {
    throw Error(std::format(format, std::forward<Args>(args)...));
}

inline std::uint32_t Le32(const unsigned char *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

class Reader {
public:
    explicit Reader(const fs::path &path)
        : path_(path), stream_(path, std::ios::binary) {
        if (!stream_) {
            Fail("cannot open {}", path.string());
        }
        std::error_code error;
        size_ = fs::file_size(path, error);
        if (error) {
            Fail("cannot size {}: {}", path.string(), error.message());
        }
    }

    std::uint64_t Size() const {
        return size_;
    }

    const fs::path &Path() const {
        return path_;
    }

    void Read(std::uint64_t offset, void *destination, std::size_t size) {
        if (offset > size_ || size > size_ - offset) {
            Fail("{} is truncated: {} bytes at offset {} lie past its end ({} bytes)", path_.string(), size,
                 offset, size_);
        }
        stream_.clear();
        stream_.seekg(static_cast<std::streamoff>(offset));
        stream_.read(static_cast<char *>(destination), static_cast<std::streamsize>(size));
        if (static_cast<std::size_t>(stream_.gcount()) != size) {
            Fail("read error in {} at offset {}", path_.string(), offset);
        }
    }

private:
    fs::path      path_;
    std::ifstream stream_;
    std::uint64_t size_ = 0;
};

struct Extent {
    fs::path      file;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

inline std::vector<unsigned char> ReadExtent(const Extent &extent) {
    Reader                     reader(extent.file);
    std::vector<unsigned char> data(extent.size);
    reader.Read(extent.offset, data.data(), data.size());
    return data;
}

struct IsoEntry {
    std::string   name;
    std::uint32_t extent = 0;
    std::uint32_t size = 0;
    bool          directory = false;
};

// ISO 9660 only: a UDF bridge is not read, since every file a PS2 game boots from is reachable
// from the ISO 9660 tree.
class Iso9660 {
public:
    explicit Iso9660(const fs::path &image)
        : reader_(image) {
        unsigned char descriptor[kSector];
        for (std::uint64_t sector = 16;; sector++) {
            if ((sector + 1) * kSector > reader_.Size()) {
                Fail("{} has no ISO 9660 primary volume descriptor", image.string());
            }
            reader_.Read(sector * kSector, descriptor, sizeof descriptor);
            if (std::memcmp(descriptor + 1, "CD001", 5) != 0) {
                Fail("{} is not an ISO 9660 image", image.string());
            }
            if (descriptor[0] == 1) {
                break;
            }
            if (descriptor[0] == 255) {
                Fail("{} has no ISO 9660 primary volume descriptor", image.string());
            }
        }
        root_ = Parse(descriptor + 156, 34);
        if (!root_.directory) {
            Fail("{}: the root directory record is not a directory", image.string());
        }
    }

    const IsoEntry &Root() const {
        return root_;
    }

    std::uint64_t ImageSize() const {
        return reader_.Size();
    }

    std::vector<IsoEntry> List(const IsoEntry &directory) {
        std::vector<unsigned char> data(directory.size);
        reader_.Read(directory.extent * kSector, data.data(), data.size());
        std::vector<IsoEntry> entries;
        std::size_t           offset = 0;
        while (offset < data.size()) {
            std::size_t length = data[offset];
            // A record never crosses a sector; the rest of a sector after the last one is zero.
            if (length == 0) {
                offset = (offset / kSector + 1) * kSector;
                continue;
            }
            if (length < 34 || offset + length > data.size()) {
                Fail("{}: bad directory record at sector {} offset {}", reader_.Path().string(),
                     directory.extent, offset);
            }
            IsoEntry entry = Parse(&data[offset], length);
            offset += length;
            if (entry.name != std::string_view("\0", 1) && entry.name != "\1") {
                entries.push_back(std::move(entry));
            }
        }
        return entries;
    }

    std::optional<IsoEntry> Find(const IsoEntry &directory, std::string_view name) {
        for (IsoEntry &entry : List(directory)) {
            if (EqualsFolded(StripVersion(entry.name), name)) {
                return entry;
            }
        }
        return std::nullopt;
    }

    std::optional<IsoEntry> FindPath(std::string_view path) {
        IsoEntry current = root_;
        while (!path.empty()) {
            std::size_t slash = path.find('/');
            std::string_view name = path.substr(0, slash);
            path = slash == std::string_view::npos ? std::string_view() : path.substr(slash + 1);
            if (name.empty()) {
                continue;
            }
            if (!current.directory) {
                return std::nullopt;
            }
            std::optional<IsoEntry> next = Find(current, name);
            if (!next) {
                return std::nullopt;
            }
            current = *next;
        }
        return current;
    }

    // A directory that lists itself again further down is refused rather than walked forever.
    std::vector<std::pair<std::string, IsoEntry>> Walk(const IsoEntry &directory) {
        std::vector<std::pair<std::string, IsoEntry>> files;
        std::vector<std::uint32_t>                    open;
        WalkInto(directory, "", open, files);
        return files;
    }

    Extent ExtentOf(const IsoEntry &entry) const {
        Extent extent{reader_.Path(), std::uint64_t(entry.extent) * kSector, entry.size};
        if (extent.offset + extent.size > reader_.Size()) {
            Fail("{} is truncated: it holds {} bytes, its directory places a file up to byte {}",
                 reader_.Path().string(), reader_.Size(), extent.offset + extent.size);
        }
        return extent;
    }

    static std::string_view StripVersion(std::string_view identifier) {
        identifier = identifier.substr(0, identifier.find(';'));
        if (!identifier.empty() && identifier.back() == '.') {
            identifier.remove_suffix(1);
        }
        return identifier;
    }

private:
    static IsoEntry Parse(const unsigned char *record, std::size_t length) {
        std::size_t name_length = record[32];
        if (33 + name_length > length) {
            Fail("bad ISO 9660 directory record: name runs past the record");
        }
        IsoEntry entry;
        entry.extent = Le32(record + 2);
        entry.size = Le32(record + 10);
        entry.directory = (record[25] & 2) != 0;
        entry.name.assign(reinterpret_cast<const char *>(record + 33), name_length);
        return entry;
    }

    void WalkInto(const IsoEntry &directory, const std::string &prefix, std::vector<std::uint32_t> &open,
                  std::vector<std::pair<std::string, IsoEntry>> &files) {
        if (std::find(open.begin(), open.end(), directory.extent) != open.end()) {
            Fail("{}: directory at sector {} contains itself", reader_.Path().string(), directory.extent);
        }
        open.push_back(directory.extent);
        for (const IsoEntry &entry : List(directory)) {
            std::string path = prefix + std::string(StripVersion(entry.name));
            if (entry.directory) {
                WalkInto(entry, path + "/", open, files);
            } else {
                files.emplace_back(path, entry);
            }
        }
        open.pop_back();
    }

    Reader   reader_;
    IsoEntry root_;
};

} // namespace kzdata
