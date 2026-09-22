#pragma once
#include <cctype>
#include <string>
namespace d2x {
inline std::string normalize(std::string path) {
    for (auto &c : path)
        c = c == '/' ? '\\' : char(std::tolower(static_cast<unsigned char>(c)));
    return path;
}
} // namespace d2x
