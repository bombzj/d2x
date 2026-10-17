#pragma once
#include <memory>
#include "system.hpp"
#include "server/systems/monsters/system.hpp"
namespace d2x::server::effects {struct AuraUnitPublication;}

namespace d2x::server::companions {
// Prepared world half of both loaded and replacement hireling transactions.
struct System::HirelingBirthPublication {
    monsters::NativeBirthBatch components;
    std::shared_ptr<effects::AuraUnitPublication> units;
    std::map<EntityId,Companion> companions;
    std::vector<std::shared_ptr<const RoomRandomLoan>> rooms;
    uint64_t expectedRandom{},nextRandom{};
};
}
