#pragma once
#include "region.hpp"

namespace d2x {
void populateAct1WorldObjects(Region &region, EntityIds &ids, const WorldCatalog &catalog,
                              const Table &objectRows, const Table &groupRows, uint32_t worldSeed,
                              std::vector<int> *deferred = nullptr);
} // namespace d2x
