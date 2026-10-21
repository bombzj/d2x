#include "network/protocol/native_auth_resource.hpp"
#include "network/protocol/auth.hpp"
#include "network/protocol/wire.hpp"
#include <algorithm>
#include <array>
#include <span>

namespace d2x::net::protocol {
namespace {
template<class T> class EraseOnExit {
    T &value_;
  public:
    explicit EraseOnExit(T &value) : value_(value) {}
    ~EraseOnExit() {
        auto *bytes = reinterpret_cast<volatile uint8_t *>(value_.data());
        for (size_t i=0; i<value_.size()*sizeof(value_[0]); ++i) bytes[i]=0;
    }
};
uint16_t word(const uint8_t *bytes) { return uint16_t(bytes[0] | uint16_t(bytes[1])<<8); }
uint32_t dword(const uint8_t *bytes) { return uint32_t(word(bytes)) | uint32_t(word(bytes+2))<<16; }
using State = std::array<uint32_t,5>;
constexpr State initial{0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0};
uint32_t signedRotate(uint32_t value, unsigned left) {
    // Bnclient 0x16810 uses SAR, not SHA-1's logical rotate; retain its x86 bits.
    const auto right=32-left;
    const auto high=(value&0x80000000u) ? (~uint32_t{} << left) : 0;
    return (value<<left) | (value>>right) | high;
}
void transform(State &state, std::span<const uint8_t,64> block) {
    std::array<uint32_t,80> words{}; EraseOnExit erase(words);
    for(size_t i=0;i<16;++i) words[i]=dword(block.data()+i*4);
    for(size_t i=16;i<80;++i) words[i]=words[i-16]^words[i-14]^words[i-8]^words[i-3];
    auto a=state[0],b=state[1],c=state[2],d=state[3],e=state[4];
    for(size_t i=0;i<80;++i) {
        const uint32_t f=i<20 ? ((~b&d)|(c&b)) : i<40 || i>=60 ? (b^c^d) : ((b|c)&d)|(b&c);
        const uint32_t k=i<20 ? 0x5a827999 : i<40 ? 0x6ed9eba1 : i<60 ? 0x8f1bbcdc : 0xca62c1d6;
        const auto next=signedRotate(a,5)+f+words[i]+k+e;
        e=d;d=c;c=signedRotate(b,30);b=a;a=next;
    }
    state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;
}
uint8_t nativeRandom(uint32_t &seed) {
    seed=seed*214013u+2531011u;
    return uint8_t((seed>>16)&0x7fff);
}
uint16_t multiply(uint16_t a,uint16_t b) {
    const auto product=uint64_t(a ? a : 65536u)*(b ? b : 65536u);
    return uint16_t(product%65537u);
}
uint16_t inverse(uint16_t value) {
    uint16_t result=1,base=value;
    for(uint32_t power=65535;power;power>>=1) {
        if(power&1) result=multiply(result,base);
        base=multiply(base,base);
    }
    return result;
}
using Schedule=std::array<uint16_t,52>;
Schedule decryptSchedule(std::span<const uint8_t,16> key) {
    Schedule enc{},dec{}; EraseOnExit erase(enc);
    for(size_t i=0;i<8;++i) enc[i]=word(key.data()+i*2);
    size_t base=0,index=0;
    for(size_t i=8;i<52;++i) {
        enc[i]=uint16_t((enc[base+((index+2)&7)]>>7)|(enc[base+((index+1)&7)]<<9));
        if(++index==8){index=0;base+=8;}
    }
    dec[48]=inverse(enc[0]);dec[49]=uint16_t(-enc[1]);
    dec[50]=uint16_t(-enc[2]);dec[51]=inverse(enc[3]);
    for(size_t round=0;round<7;++round) {
        const auto source=4+round*6,target=42-round*6;
        dec[target+4]=enc[source];dec[target+5]=enc[source+1];
        dec[target]=inverse(enc[source+2]);dec[target+1]=uint16_t(-enc[source+4]);
        dec[target+2]=uint16_t(-enc[source+3]);dec[target+3]=inverse(enc[source+5]);
    }
    dec[4]=enc[46];dec[5]=enc[47];dec[0]=inverse(enc[48]);
    dec[1]=uint16_t(-enc[49]);dec[2]=uint16_t(-enc[50]);dec[3]=inverse(enc[51]);
    return dec;
}
void decryptBlock(const uint8_t *source,uint8_t *destination,const Schedule &key) {
    auto a=word(source),b=word(source+2),c=word(source+4),d=word(source+6);
    for(size_t round=0;round<8;++round) {
        const auto k=round*6;
        a=multiply(a,key[k]);b=uint16_t(b+key[k+1]);c=uint16_t(c+key[k+2]);d=multiply(d,key[k+3]);
        const auto first=multiply(uint16_t(c^a),key[k+4]);
        const auto second=multiply(uint16_t((d^b)+first),key[k+5]);
        const auto nextB=uint16_t(second^c);
        c=uint16_t((first+second)^b);b=nextB;
        a^=second;d^=uint16_t(first+second);
    }
    const std::array<uint16_t,4> result{multiply(a,key[48]),uint16_t(c+key[49]),
                                       uint16_t(b+key[50]),multiply(d,key[51])};
    for(size_t i=0;i<4;++i){destination[i*2]=uint8_t(result[i]);destination[i*2+1]=uint8_t(result[i]>>8);}
}
}
std::string decode_native_auth_resource(Bytes &bytes) {
    struct InputErase { Bytes &bytes; ~InputErase(){erase_secret(bytes);} } inputErase{bytes};
    // Bnclient 0x15E70/0x16070: whole 64-byte blocks, eight-byte trailer,
    // plaintext C string bounded to 128 bytes. Only key/owner resources here.
    if(bytes.size()<72 || bytes.size()>1024 || (bytes.size()-8)%64)
        throw ProtocolError("Unsupported native authentication resource envelope");
    std::array<uint8_t,19> password{}; EraseOnExit erasePassword(password);
    uint32_t seed=0x150b;
    for(size_t i=0;i<password.size();) if(const auto byte=nativeRandom(seed)) password[i++]=byte;
    std::array<uint8_t,64> repeated{}; EraseOnExit eraseRepeated(repeated);
    for(size_t i=0;i<repeated.size();++i) repeated[i]=password[i%password.size()];
    auto digest=initial; EraseOnExit eraseDigest(digest); transform(digest,repeated);
    std::array<uint8_t,112> material{}; EraseOnExit eraseMaterial(material);
    seed=0x4fa7;
    for(size_t i=0;i<material.size();++i)
        material[i]=nativeRandom(seed)^uint8_t(digest[(i%20)/4]>>((i%4)*8));
    auto schedule=decryptSchedule(std::span<const uint8_t,16>{material.data(),16});
    EraseOnExit eraseSchedule(schedule);
    auto state=initial; EraseOnExit eraseState(state);
    transform(state,std::span<const uint8_t,64>{material.data()+48,64});
    const auto blocks=(bytes.size()-8)/64;
    std::array<uint8_t,64> block{}; EraseOnExit eraseBlock(block);
    for(size_t index=0;index<blocks;++index) {
        auto *source=bytes.data()+index*64;
        if(index%8==0) for(size_t offset=0;offset<64;offset+=8) decryptBlock(source+offset,block.data()+offset,schedule);
        else std::copy_n(source,64,block.data());
        for(size_t i=0;i<64;++i) {
            const auto digestByte=(63-i)%20;
            block[i]=uint8_t(block[i]-uint8_t(state[digestByte/4]>>((digestByte%4)*8)));
        }
        if(index%16==0) transform(state,block);
        std::copy(block.begin(),block.end(),source);
    }
    const auto *trailer=bytes.data()+blocks*64;
    if(trailer[4] || trailer[5]>64 || dword(trailer)!=state[0])
        throw ProtocolError("Native authentication resource checksum failed");
    const auto length=bytes.size()-72+trailer[5];
    if(!length || length>=bytes.size()) throw ProtocolError("Invalid native authentication resource length");
    bytes[length]=0;
    const auto end=std::find(bytes.begin(),bytes.begin()+std::min(length+1,size_t(128)),uint8_t{});
    if(end==bytes.begin()+std::min(length+1,size_t(128)))
        throw ProtocolError("Native authentication resource string exceeds its limit");
    return std::string(bytes.begin(),end);
}
}
