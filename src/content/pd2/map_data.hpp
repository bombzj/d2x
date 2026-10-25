#pragma once
#include "gameplay/maps/pd2/definition.hpp"
#include <memory>

namespace d2x {
struct ClassicData;
namespace pd2 {
std::shared_ptr<const MapCatalog> prepareMapCatalog(const ClassicData &);
}
}
