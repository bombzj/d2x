#include "world/preset_identity.hpp"
#include <array>
#include <cstdint>
namespace d2x {
int pd2ObjectClass(const MapObject &source,int version,int act) {
    // Current 17d5 constructors patch Common's Act I ordinal slots 135..149.
    // These are native table identities, independent of Objects parameters.
    constexpr std::array classes{616,617,613,614,615,601,602,603,604,605,606,607,608,609,610};
    // Act II's negative ordinals address the patched end of Act I's table.
    // ZharLibrary's -10..-7 therefore resolve to 601..604, just as Common's
    // signed linear indexing does; no level or coordinate substitution.
    const int64_t index=int64_t(act)*150+source.id;
    if(!source.nativeIdentity && version>5 && act>=0 && act<5 && source.id<150 && index>=135 && index<150)
        return classes[size_t(index-135)];
    return originalObjectClass(source,version,act);
}
}
