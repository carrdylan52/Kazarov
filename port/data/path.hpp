#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace kzdata {

// strcasecmp in the C locale folds ASCII only, and that is the comparison the games' lookups make.
inline char FoldChar(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

inline bool EqualsFolded(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return FoldChar(x) == FoldChar(y);
           });
}

// The key a path is stored and looked up under: forward slashes, no leading separator, folded case.
inline std::string FoldPath(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    for (char c : path) {
#if defined(__APPLE__) || defined(_WIN32)
        // Both file systems normalise or reject some byte sequences that are not UTF-8.
        if (static_cast<unsigned char>(c) > 0x7F) {
            constexpr char kHex[] = "0123456789abcdef";
            out += '%';
            out += kHex[static_cast<unsigned char>(c) >> 4];
            out += kHex[static_cast<unsigned char>(c) & 0xF];
            continue;
        }
#endif
        out.push_back(c == '\\' ? '/' : FoldChar(c));
    }
    out.erase(0, out.find_first_not_of('/'));
    return out;
}

// The path with '/' for every separator, or none when it could leave the directory it is
// relative to: empty, absolute, with a drive or device prefix, an empty, "." or ".." component, or
// a component ending in a dot or space, which Windows strips (so "... " can mean "..").
inline std::optional<std::string> SafeRelative(std::string_view path) {
    std::string normal(path);
    std::ranges::replace(normal, '\\', '/');
    if (normal.empty() || normal.find_first_of(std::string_view(":\0", 2)) != std::string::npos) {
        return std::nullopt;
    }
    std::size_t start = 0;
    while (start <= normal.size()) {
        std::size_t      end = std::min(normal.find('/', start), normal.size());
        std::string_view component = std::string_view(normal).substr(start, end - start);
        if (component.empty() || component.back() == '.' || component.back() == ' ') {
            return std::nullopt;
        }
        start = end + 1;
    }
    return normal;
}

inline bool IsSafeRelative(std::string_view path) {
    return SafeRelative(path).has_value();
}

} // namespace kzdata
