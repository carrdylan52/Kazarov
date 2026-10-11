#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "data/iso9660.hpp"
#include "platform_fixture.hpp"

namespace kz::test {

using Bytes = std::vector<unsigned char>;

inline Bytes Pattern(std::size_t size, unsigned seed) {
    Bytes bytes(size);
    for (std::size_t i = 0; i < size; i++) {
        bytes[i] = static_cast<unsigned char>(i * 31 + seed * 7 + (i >> 8));
    }
    return bytes;
}

inline void WriteBytes(const std::filesystem::path &path, const Bytes &bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

inline Bytes ReadBytes(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(file), {});
}

inline std::filesystem::path TempDir(std::string_view name) {
    auto dir = std::filesystem::temp_directory_path() /
               ("kz_data_" + std::string(name) + "_" + std::to_string(ProcessId()));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

inline void PutBoth32(Bytes &data, std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; i++) {
        data[offset + i] = static_cast<unsigned char>(value >> (8 * i));
        data[offset + 7 - i] = static_cast<unsigned char>(value >> (8 * i));
    }
}

inline Bytes DirectoryRecord(std::string_view name, std::uint32_t extent, std::uint32_t size, bool directory) {
    Bytes record(33 + name.size() + (name.size() % 2 == 0 ? 1 : 0), 0);
    record[0] = static_cast<unsigned char>(record.size());
    PutBoth32(record, 2, extent);
    PutBoth32(record, 10, size);
    record[25] = directory ? 2 : 0;
    record[32] = static_cast<unsigned char>(name.size());
    std::memcpy(&record[33], name.data(), name.size());
    return record;
}

struct SmallDisc {
    Bytes system = Pattern(100, 1);
    Bytes data = Pattern(5000, 2);
    Bytes movie = Pattern(3 * kzdata::kSector, 3);

    Bytes Image() const {
        constexpr std::uint32_t root = 18, movie_dir = 19, first = 20;
        std::uint32_t           data_at = first + 1;
        std::uint32_t           movie_at = data_at + 3;
        std::uint32_t           total = movie_at + 3;
        Bytes                   image(total * kzdata::kSector, 0);
        constexpr std::size_t   pvd = 16 * kzdata::kSector;
        image[pvd] = 1;
        std::memcpy(&image[pvd + 1], "CD001", 5);
        Bytes self = DirectoryRecord(std::string_view("\0", 1), root, kzdata::kSector, true);
        std::memcpy(&image[pvd + 156], self.data(), self.size());
        image[pvd + kzdata::kSector] = 255;
        std::memcpy(&image[pvd + kzdata::kSector + 1], "CD001", 5);

        auto directory = [&](std::uint32_t at, std::uint32_t parent, std::vector<Bytes> records) {
            records.insert(records.begin(), DirectoryRecord("\1", parent, kzdata::kSector, true));
            records.insert(records.begin(), DirectoryRecord(std::string_view("\0", 1), at, kzdata::kSector, true));
            std::size_t offset = at * kzdata::kSector;
            for (const Bytes &record : records) {
                std::memcpy(&image[offset], record.data(), record.size());
                offset += record.size();
            }
        };
        directory(root, root,
                  {DirectoryRecord("DATA.BIN;1", data_at, static_cast<std::uint32_t>(data.size()), false),
                   DirectoryRecord("MOVIE", movie_dir, kzdata::kSector, true),
                   DirectoryRecord("SYSTEM.CNF;1", first, static_cast<std::uint32_t>(system.size()), false)});
        directory(movie_dir, root,
                  {DirectoryRecord("OPEN1.PSS;1", movie_at, static_cast<std::uint32_t>(movie.size()), false)});
        std::memcpy(&image[first * kzdata::kSector], system.data(), system.size());
        std::memcpy(&image[data_at * kzdata::kSector], data.data(), data.size());
        std::memcpy(&image[movie_at * kzdata::kSector], movie.data(), movie.size());
        return image;
    }
};

} // namespace kz::test
