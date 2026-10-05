// Independent prefix-tree decoder for the LoD protocol codebook.
// Reference evidence and attribution: docs/licenses/OpenD2-notices.txt.
#include "network/protocol/d2gs_stream.hpp"
#include <array>
#include <iterator>

namespace d2x::net::protocol {
namespace {
#include "network/protocol/huffman_tables.inc"
struct Node { std::array<int16_t, 2> next{-1, -1}; int16_t symbol{-1}; };
consteval auto make_tree() {
    std::array<Node, 512> tree{};
    int16_t count = 1;
    for (size_t symbol = 0; symbol < std::size(codes); ++symbol) {
        const auto code = codes[symbol];
        int16_t node = 0;
        for (unsigned bit = code.length; bit; --bit) {
            if (tree[node].symbol >= 0) throw "Huffman prefix collision";
            auto &child = tree[node].next[(code.bits >> (bit - 1)) & 1];
            if (child < 0) {
                if (size_t(count) >= tree.size()) throw "Huffman tree capacity";
                child = count++;
            }
            node = child;
        }
        if (tree[node].symbol >= 0 || tree[node].next[0] >= 0 || tree[node].next[1] >= 0)
            throw "Huffman code collision";
        tree[node].symbol = int16_t(symbol);
    }
    return tree;
}
constexpr auto tree = make_tree();
}
Bytes decompress_huffman(std::span<const uint8_t> input, size_t limit) {
    Bytes output;
    int16_t node = 0;
    unsigned pending{}, suffix{};
    for (const auto byte : input) {
        for (unsigned bit = 8; bit; --bit) {
            const unsigned value = (byte >> (bit - 1)) & 1;
            node = tree[node].next[value];
            if (node < 0) throw ProtocolError("Invalid Huffman code");
            ++pending; suffix = (suffix << 1) | value;
            if (tree[node].symbol >= 0) {
                if (output.size() >= limit) throw ProtocolError("Huffman output limit exceeded");
                output.push_back(uint8_t(tree[node].symbol));
                node = 0; pending = 0; suffix = 0;
            }
        }
    }
    // The encoder rounds the final byte with zero bits; a complete extra byte
    // or a nonzero unfinished code is truncation rather than valid padding.
    if (pending > 7 || suffix) throw ProtocolError("Truncated Huffman input");
    return output;
}
} // namespace d2x::net::protocol
