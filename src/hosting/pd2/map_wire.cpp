#include "map_wire.hpp"
#include "server/runtime/events.hpp"
#include <algorithm>

namespace d2x::hosting {
Bytes pd2MapAttributes(const server::Pd2MapAttributesFact &fact) {
    net::protocol::Writer out;out.u8(0x55);
    out.u32(uint32_t(fact.portal.value));out.u32(2);
    const auto count=std::min(size_t(24),fact.attributes.size());
    for(size_t slot=0;slot<24;++slot) {
        const auto value=slot<count?fact.attributes[slot]:StatValue{};
        out.u16(value.key.layer);out.u16(value.key.id);out.u32(uint32_t(value.value));
    }
    out.u16(uint16_t(count));out.u16(uint16_t(fact.area));out.u8(fact.quality);out.u32(fact.itemIndex);
    return out.release();
}
}
