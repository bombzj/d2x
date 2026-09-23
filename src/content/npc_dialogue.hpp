#pragma once
#include "resources/archive.hpp"
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace d2x {
struct NpcSpeech {
    std::string text;
    std::string wave; // Original MPQ reference; playback is not implemented.
    int speed = 0;
};
using NpcDialogues = std::map<std::string, std::vector<NpcSpeech>, std::less<>>;
NpcDialogues loadActOneNpcDialogues(Archives &archives);
const NpcSpeech *introSpeech(const NpcDialogues &dialogues, std::string_view npc);
const NpcSpeech *gossipSpeech(const NpcDialogues &dialogues, std::string_view npc, size_t turn);
} // namespace d2x
