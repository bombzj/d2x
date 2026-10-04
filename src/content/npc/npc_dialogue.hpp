#pragma once
#include "resources/archive.hpp"
#include "resources/data_table.hpp"
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace d2x {
struct NpcSpeech {
    std::string text;
    std::string wave; // Original MPQ reference; playback is not implemented.
    std::string quest, state; // SECTION:QUEST / QUEST / STATE from aNnpc.txt.
    int speed = 0;
    int act = 0;
    std::string speaker, introClass;
    std::string soundId;
    bool introduction = false, gossip = false, arrival = false;
};
struct NpcDialogues : std::map<std::string, std::vector<NpcSpeech>, std::less<>> {
    std::map<std::string, std::string, std::less<>> speakers;
    std::map<unsigned, std::string> introductionKeys;
};
NpcDialogues loadNpcDialogues(Archives &archives, const DataTable &monsters, const DataTable &presets,
                             const std::map<std::string, std::string, std::less<>> &strings);
std::string npcIntroductionKey(std::string_view npc, int act);
const NpcSpeech *introSpeech(const NpcDialogues &dialogues, std::string_view npc,
                             std::string_view characterClass = {}, int act = 0);
const NpcSpeech *gossipSpeech(const NpcDialogues &dialogues, std::string_view npc, size_t turn, int act = 0);
const NpcSpeech *questSpeech(const NpcDialogues &dialogues, std::string_view quest,
                             std::string_view state, std::string_view npc);
const NpcSpeech *arrivalSpeech(const NpcDialogues &dialogues, std::string_view npc, int act);
} // namespace d2x
