#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <vector>

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
    std::string   source; // the source file's name and size: a row is reused only from the same disc
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

inline fs::path TemporaryBeside(const fs::path &path) {
    return path.parent_path() / ("." + path.filename().string() + ".kztmp");
}

// Readers never see a half-written file: an interrupted write leaves the old one in place.
inline void WriteFile(const fs::path &path, std::span<const unsigned char> data) {
    fs::create_directories(path.parent_path());
    fs::path temporary = TemporaryBeside(path);
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
        file.close();
        if (!file) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            Fail("cannot write {}", path.string());
        }
    }
    fs::rename(temporary, path);
}

inline std::string SourceIdentity(const fs::path &file) {
    std::error_code error;
    std::uint64_t   size = fs::file_size(file, error);
    return std::format("{}@{}", file.filename().string(), error ? 0 : size);
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
        if (row.sha256.size() != 64 || !IsSafeRelative(path) || !manifest.emplace(path, row).second) {
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
    WriteFile(root / kManifestFile,
              std::span(reinterpret_cast<const unsigned char *>(text.data()), text.size()));
}

// Everything that would make the output ambiguous or escape `out` is refused before a byte is
// written: unsafe paths, two entries for one path, a file where another needs a directory, and
// extents past the end of their source.
inline void CheckEntries(std::span<const Entry> entries, const fs::path &out, const Manifest &taken = {}) {
    std::unordered_set<std::string> files;
    for (const Entry &entry : entries) {
        if (!IsSafeRelative(entry.path) || entry.path.find_first_of("\t\n\r") != std::string::npos ||
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

// Copies each entry's extent to out/<path> unless the file there already holds it, and records
// its hash in `next`. `previous` is the manifest an earlier run left, which saves rereading files
// it already proved.
inline Result ExtractEntries(std::span<const Entry> entries, const fs::path &out, const Manifest &previous,
                             Manifest &next, const ProgressCallback &progress = {}) {
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

    fs::create_directories(out);
    std::vector<unsigned char> chunk(4 << 20);
    for (const Entry &entry : entries) {
        fs::path    target = out / fs::path(entry.path);
        std::string source = SourceIdentity(entry.extent.file);
        result.bytes += entry.extent.size;
        if (IsCurrent(target, entry.extent.size)) {
            auto        known = previous.find(entry.path);
            std::string hash;
            if (known != previous.end() && known->second.source == source &&
                known->second.offset == entry.extent.offset && known->second.size == entry.extent.size) {
                hash = known->second.sha256;
            } else {
                hash = HashFile(entry.extent.file, entry.extent.offset, entry.extent.size, chunk);
                if (HashFile(target, 0, entry.extent.size, chunk) != hash) {
                    hash.clear();
                }
            }
            if (!hash.empty()) {
                next[entry.path] = {hash, entry.extent.size, source, entry.extent.offset};
                result.kept++;
                done.files++;
                done.bytes += entry.extent.size;
                report();
                continue;
            }
        }
        fs::create_directories(target.parent_path());
        fs::path temporary = TemporaryBeside(target);
        Sha256   hash;
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) {
                Fail("cannot create {}", temporary.string());
            }
            Reader        reader(entry.extent.file);
            std::uint64_t offset = entry.extent.offset;
            try {
                for (std::uint64_t left = entry.extent.size; left != 0;) {
                    std::size_t step = static_cast<std::size_t>(std::min<std::uint64_t>(left, chunk.size()));
                    reader.Read(offset, chunk.data(), step);
                    hash.Update({chunk.data(), step});
                    file.write(reinterpret_cast<const char *>(chunk.data()), static_cast<std::streamsize>(step));
                    offset += step;
                    left -= step;
                    done.bytes += step;
                    report();
                }
                file.close();
                if (!file) {
                    Fail("write error on {}", target.string());
                }
            } catch (...) {
                file.close();
                std::error_code ignored;
                fs::remove(temporary, ignored);
                throw;
            }
        }
        fs::rename(temporary, target);
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
        if (!IsCurrent(file, row.size)) {
            problems.push_back(std::format("missing or wrong size: {}", path));
        } else if (HashFile(file, 0, row.size, chunk) != row.sha256) {
            problems.push_back(std::format("differs from its extent: {}", path));
        }
    }
    Log(log, "verified {}/{} files in {}\n", manifest.size() - problems.size(), manifest.size(), root.string());
    return problems;
}

} // namespace kzdata
