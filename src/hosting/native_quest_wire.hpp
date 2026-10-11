#pragma once
#include "server/runtime/events.hpp"
#include "core/bytes.hpp"
#include <optional>
namespace d2x { struct ClassicData; Bytes nativeQuestInitialization(const server::QuestFact &); std::vector<Bytes> nativeQuestUpdate(const ClassicData &,const server::QuestFact &,std::optional<EntityId> npc = {}); }
