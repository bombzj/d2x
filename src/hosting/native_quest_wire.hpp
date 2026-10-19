#pragma once
#include "core/bytes.hpp"
#include "core/id.hpp"
#include <optional>
#include <vector>
namespace d2x {
struct ClassicData;
namespace server { struct QuestFact; }
Bytes nativeQuestInitialization(const server::QuestFact &);
std::vector<Bytes> nativeQuestUpdate(const ClassicData &,const server::QuestFact &,std::optional<EntityId> npc = {});
}
