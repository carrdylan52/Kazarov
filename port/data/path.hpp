#pragma once

#include <algorithm>
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

inline bool IsSafeRelative(std::string_view path) {
    if (path.empty() || path.back() == '/') {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t      end = std::min(path.find('/', start), path.size());
        std::string_view component = path.substr(start, end - start);
        if (component.empty() || component == "." || component == ".." ||
            component.find(':') != std::string_view::npos) {
            return false;
        }
        start = end + 1;
    }
    return true;
}

} // namespace kzdata
