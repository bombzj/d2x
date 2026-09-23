#include "text.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
std::string decodeText(std::span<const uint8_t> bytes) {
    if (bytes.empty())
        return {};
    if (bytes.size() < 2 || ((bytes[0] != 0xff || bytes[1] != 0xfe) &&
                             (bytes[0] != 0xfe || bytes[1] != 0xff))) {
        if (std::find(bytes.begin(), bytes.end(), uint8_t(0)) != bytes.end())
            throw std::runtime_error("MPQ text contains an unsupported NUL byte");
        size_t offset = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf
                            ? 3 : 0;
        return {reinterpret_cast<const char *>(bytes.data() + offset), bytes.size() - offset};
    }
    if (bytes.size() % 2)
        throw std::runtime_error("Odd UTF-16 MPQ text size");
    const bool little = bytes[0] == 0xff;
    auto unit = [&](size_t index) -> uint16_t {
        return little ? uint16_t(bytes[index] | (uint16_t(bytes[index + 1]) << 8))
                      : uint16_t((uint16_t(bytes[index]) << 8) | bytes[index + 1]);
    };
    std::string result;
    for (size_t index = 2; index < bytes.size(); index += 2) {
        uint32_t code = unit(index);
        if (code >= 0xd800 && code <= 0xdbff) {
            if (index + 3 >= bytes.size())
                throw std::runtime_error("Truncated UTF-16 surrogate");
            uint32_t low = unit(index + 2);
            if (low < 0xdc00 || low > 0xdfff)
                throw std::runtime_error("Invalid UTF-16 surrogate");
            code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
            index += 2;
        } else if (code >= 0xdc00 && code <= 0xdfff)
            throw std::runtime_error("Unexpected UTF-16 surrogate");
        if (code < 0x80)
            result += char(code);
        else if (code < 0x800) {
            result += char(0xc0 | (code >> 6));
            result += char(0x80 | (code & 0x3f));
        } else if (code < 0x10000) {
            result += char(0xe0 | (code >> 12));
            result += char(0x80 | ((code >> 6) & 0x3f));
            result += char(0x80 | (code & 0x3f));
        } else {
            result += char(0xf0 | (code >> 18));
            result += char(0x80 | ((code >> 12) & 0x3f));
            result += char(0x80 | ((code >> 6) & 0x3f));
            result += char(0x80 | (code & 0x3f));
        }
    }
    return result;
}
} // namespace d2x
