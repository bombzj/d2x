#include "resources/archive.hpp"
#include "npc_dialogue.hpp"
#include "resources/text.hpp"
#include <charconv>
#include <stdexcept>

namespace d2x {
namespace {
std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
        value.remove_suffix(1);
    return value;
}
} // namespace

NpcDialogues loadActOneNpcDialogues(Archives &archives) {
    constexpr auto path = "data/local/docs/eng/a1npc.txt";
    if (!archives.contains(path))
        throw std::runtime_error("Original Act I NPC dialogue is missing");
    auto source = decodeText(archives.read(path));
    NpcDialogues result;
    std::string name, section, quest, state;
    NpcSpeech speech;
    bool readingQuote = false;
    auto finish = [&] {
        if (!name.empty() && !speech.text.empty()) {
            if (section == "QUEST") {
                speech.quest = quest;
                speech.state = state;
            }
            result[name].push_back(std::move(speech));
        }
        speech = {};
    };
    size_t begin = 0;
    while (begin < source.size()) {
        auto end = source.find('\n', begin);
        if (end == std::string::npos)
            end = source.size();
        auto line = trim(std::string_view(source).substr(begin, end - begin));
        begin = end + 1;
        if (readingQuote) {
            if (!speech.text.empty())
                speech.text += '\n';
            const bool closed = !line.empty() && line.back() == '"';
            if (closed)
                line.remove_suffix(1);
            speech.text += line;
            if (closed) {
                readingQuote = false;
                finish();
            }
        } else if (line.starts_with("SECTION:")) {
            finish();
            section = trim(line.substr(8));
            quest.clear();
            state.clear();
            name.clear();
        } else if (line.starts_with("QUEST:")) {
            finish();
            quest = trim(line.substr(6));
            state.clear();
            name.clear();
        } else if (line.starts_with("STATE:")) {
            finish();
            state = trim(line.substr(6));
            name.clear();
        } else if (line.starts_with("NAME:")) {
            finish();
            name = trim(line.substr(5));
        } else if (line.starts_with("SPEED:")) {
            auto number = trim(line.substr(6));
            int speed = 0;
            auto [last, error] = std::from_chars(number.data(), number.data() + number.size(), speed);
            if (error == std::errc{} && last == number.data() + number.size())
                speech.speed = speed;
        } else if (line.starts_with("wave:")) {
            speech.wave = trim(line.substr(5));
        } else if (!name.empty() && line.starts_with("\"")) {
            line.remove_prefix(1);
            const bool closed = !line.empty() && line.back() == '"';
            if (closed)
                line.remove_suffix(1);
            speech.text = line;
            readingQuote = !closed;
            if (closed)
                finish();
        }
    }
    if (readingQuote)
        throw std::runtime_error("Unterminated original NPC dialogue");
    finish();
    return result;
}

const NpcSpeech *introSpeech(const NpcDialogues &dialogues, std::string_view npc,
                             std::string_view characterClass) {
    const auto key = npc == "Deckard Cain" ? "Cain" : std::string(npc);
    const auto introKey = npc == "Warriv" ? "WarrivAct1Intro" : key + "Intro";
    // ACT1Intro_Callback00_NpcActivate / ACT1Q0: class-specific greetings.
    std::string suffix;
    if (npc == "Akara" && characterClass == "Sorceress") suffix = "Sor";
    if (npc == "Kashya" && characterClass == "Amazon") suffix = "Ama";
    if (npc == "Charsi" && characterClass == "Barbarian") suffix = "Bar";
    if (npc == "Gheed" && characterClass == "Necromancer") suffix = "Nec";
    if (npc == "Warriv" && characterClass == "Paladin") suffix = "Pal";
    if (!suffix.empty())
        if (auto special = dialogues.find(introKey + suffix); special != dialogues.end())
            for (const auto &speech : special->second)
                if (speech.quest.empty()) return &speech;
    auto intro = dialogues.find(introKey);
    if (intro != dialogues.end())
        for (const auto &speech : intro->second)
            if (speech.quest.empty()) return &speech;
    // Cain's first Act I speech is selected by his rescue quest, not generic gossip.
    if (npc == "Deckard Cain") return nullptr;
    auto generic = dialogues.find(key);
    if (generic != dialogues.end())
        for (const auto &speech : generic->second)
            if (speech.quest.empty()) return &speech;
    return nullptr;
}
const NpcSpeech *gossipSpeech(const NpcDialogues &dialogues, std::string_view npc, size_t turn) {
    const auto key = npc == "Deckard Cain" ? "Cain" : std::string(npc);
    auto group = dialogues.find(key);
    if (group == dialogues.end())
        return nullptr;
    std::vector<const NpcSpeech *> generic;
    for (const auto &speech : group->second) {
        auto marker = speech.wave.find("_gossip_");
        if (marker != std::string::npos && marker + 8 < speech.wave.size() &&
            speech.wave[marker + 8] >= '0' && speech.wave[marker + 8] <= '9')
            generic.push_back(&speech);
    }
    if (generic.empty())
        return nullptr;
    return generic[turn % generic.size()];
}
const NpcSpeech *questSpeech(const NpcDialogues &dialogues, std::string_view quest,
                             std::string_view state, std::string_view npc) {
    auto key = npc == "Deckard Cain" ? "Cain" : std::string(npc);
    if (key == "Charsi") key = "CharsiMain";
    auto group = dialogues.find(key);
    if (group == dialogues.end()) return nullptr;
    for (const auto &speech : group->second)
        if (speech.quest == quest && speech.state == state)
            return &speech;
    return nullptr;
}
} // namespace d2x
