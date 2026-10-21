#include "core/sha256.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace d2x {
namespace {
// FIPS 180-4, sections 4.2.2, 5.1.1 and 6.2.2. No platform crypto dependency.
constexpr std::array<uint32_t, 64> constants{
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
void wipe(void *memory, size_t size) {
    auto bytes = static_cast<volatile uint8_t *>(memory);
    while (size--) *bytes++ = 0;
}
}
Sha256::~Sha256() { wipe(this, sizeof(*this)); }
void Sha256::compress(std::span<const uint8_t, 64> block) {
    std::array<uint32_t, 64> schedule{};
    for (size_t i = 0; i < 16; ++i)
        schedule[i] = uint32_t(block[i*4]) << 24 | uint32_t(block[i*4+1]) << 16 |
                      uint32_t(block[i*4+2]) << 8 | block[i*4+3];
    for (size_t i = 16; i < schedule.size(); ++i) {
        const auto x = schedule[i-15], y = schedule[i-2];
        schedule[i] = schedule[i-16] + (std::rotr(x,7)^std::rotr(x,18)^(x>>3)) +
                      schedule[i-7] + (std::rotr(y,17)^std::rotr(y,19)^(y>>10));
    }
    auto a=state_[0], b=state_[1], c=state_[2], d=state_[3];
    auto e=state_[4], f=state_[5], g=state_[6], h=state_[7];
    for (size_t i=0; i<schedule.size(); ++i) {
        const auto first = h + (std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25)) +
                           ((e&f)^(~e&g)) + constants[i] + schedule[i];
        const auto second = (std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22)) +
                            ((a&b)^(a&c)^(b&c));
        h=g; g=f; f=e; e=d+first; d=c; c=b; b=a; a=first+second;
    }
    state_[0]+=a; state_[1]+=b; state_[2]+=c; state_[3]+=d;
    state_[4]+=e; state_[5]+=f; state_[6]+=g; state_[7]+=h;
    wipe(schedule.data(), sizeof(schedule));
}
void Sha256::append(std::span<const uint8_t> input) {
    if (input.size() > std::numeric_limits<uint64_t>::max()/8 - bytes_)
        throw std::length_error("SHA-256 input exceeds its bit-length range");
    bytes_ += input.size();
    while (!input.empty()) {
        const auto count = std::min(block_.size()-buffered_, input.size());
        std::copy_n(input.begin(), count, block_.begin()+buffered_);
        buffered_ += count;
        input = input.subspan(count);
        if (buffered_ == block_.size()) { compress(block_); buffered_ = 0; }
    }
}
Sha256Digest Sha256::finish() const {
    if(buffered_>=block_.size()) throw std::logic_error("Invalid SHA-256 buffered state");
    auto copy = *this;
    std::array<uint8_t, 128> padded{};
    std::copy_n(block_.begin(), buffered_, padded.begin());
    padded[buffered_] = 0x80;
    const size_t size = buffered_ < 56 ? 64 : 128;
    const auto bits = bytes_*8;
    for (size_t i=0; i<8; ++i) padded[size-1-i] = uint8_t(bits>>(i*8));
    copy.compress(std::span<const uint8_t,64>{padded.data(),64});
    if (size == 128) copy.compress(std::span<const uint8_t,64>{padded.data()+64,64});
    Sha256Digest digest{};
    for (size_t i=0; i<copy.state_.size(); ++i)
        for (size_t j=0; j<4; ++j) digest[i*4+j] = uint8_t(copy.state_[i]>>(24-j*8));
    wipe(padded.data(), padded.size());
    return digest;
}
std::string hexDigest(const Sha256Digest &digest) {
    constexpr char digits[]="0123456789abcdef";
    std::string result(digest.size()*2, '0');
    for (size_t i=0; i<digest.size(); ++i) {
        result[i*2]=digits[digest[i]>>4]; result[i*2+1]=digits[digest[i]&15];
    }
    return result;
}
std::string sha256Hex(std::span<const uint8_t> input) {
    Sha256 hash; hash.append(input); auto digest=hash.finish();
    auto result=hexDigest(digest); wipe(digest.data(), digest.size()); return result;
}
} // namespace d2x
