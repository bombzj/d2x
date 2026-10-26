#include "world/preset_identity.hpp"
#include <array>
namespace d2x {
int pd2ObjectClass(const MapObject &source,int version,int act) {
    // Current 17d5 constructors patch Common's Act I ordinal slots 135..149.
    // These are native table identities, independent of Objects parameters.
    constexpr std::array classes{616,617,613,614,615,601,602,603,604,605,606,607,608,609,610};
    if(!source.nativeIdentity && version>5 && act==0 && source.id>=135 && source.id<150)
        return classes[size_t(source.id-135)];
    return originalObjectClass(source,version,act);
}
}
