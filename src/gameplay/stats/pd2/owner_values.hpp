#pragma once
#include "gameplay/stats/stat_list.hpp"

namespace d2x {
struct Pd2OwnerStatValues {
    std::shared_ptr<const StatList> sources;
    StatSnapshot full,values;
};
// Current core 0x2FAA20 queries the actual owner's life and equipped ethereals.
// Keep these values outside the saved item properties and cached coefficients.
Pd2OwnerStatValues samplePd2OwnerStats(const StatRuleCatalog &,const StatList &,
    const StatSnapshot &full,int32_t life,int ethereals,StatQueryContext);
}
