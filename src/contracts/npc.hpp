#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/quest/id.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace d2x {
enum class NpcMenuAction {
    None, Talk, Trade, Identify, Cancel, Gossip, Respec, Imbue, GoEast,
    Gamble, Hire, Introduction, Resurrect, Sail, QuestTopic
};
struct NpcMenuSelection {
    NpcMenuAction action = NpcMenuAction::None;
    std::optional<QuestId> quest;
};
struct NpcMenuEntry {
    std::string label;
    NpcMenuSelection selection;
};
struct NpcTopicView { QuestId quest; std::string title, text; };
struct NpcConversationView {
    uint64_t revision = 0;
    EntityId actor, npc;
    Vec position;
    bool valid = false;
    std::string speaker;
    std::optional<std::string> introduction;
    std::vector<NpcTopicView> topics;
    std::vector<std::string> gossip;
    std::vector<NpcMenuEntry> services, talkEntries;
};
struct NpcPublicView {
    bool questAlert = false;
    int overlayHeight = -1;
};
struct NpcSceneView {
    uint64_t revision = 0;
    std::map<EntityId, NpcPublicView> npcs;
    const NpcPublicView *npc(EntityId id) const {
        const auto found = npcs.find(id);
        return found == npcs.end() ? nullptr : &found->second;
    }
};
} // namespace d2x
