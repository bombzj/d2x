#include "resources/archive.hpp"
#include "npc_dialogue.hpp"
#include "resources/text.hpp"
#include <charconv>
#include <algorithm>
#include <cctype>
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

namespace {
NpcDialogues loadNpcDialogueFile(Archives &archives, int act) {
    const auto path = "data/local/docs/eng/a" + std::to_string(act + 1) + "npc.txt";
    if (!archives.contains(path))
        throw std::runtime_error("Original NPC dialogue is missing: " + path);
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
            // Expansion a5npc also contains the authored A4Q2 ending.
            speech.act = speech.quest.size() >= 3 && speech.quest[0] == 'A' &&
                speech.quest[1] >= '1' && speech.quest[1] <= '5' && speech.quest[2] == 'Q'
                ? speech.quest[1] - '1' : act;
            const auto arrival = name.find("ActIntro");
            speech.arrival = speech.quest.empty() && arrival != std::string::npos &&
                (arrival + 8 == name.size() || arrival + 11 == name.size());
            const auto group = speech.act == 0 ? name : "act" + std::to_string(speech.act + 1) + ":" + name;
            result[group].push_back(std::move(speech));
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
        const auto colon = line.find(':');
        std::string tag(colon == std::string_view::npos ? std::string_view{} : line.substr(0, colon));
        std::transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char c) { return char(std::toupper(c)); });
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
        } else if (tag == "SECTION") {
            finish();
            section = trim(line.substr(8));
            std::transform(section.begin(), section.end(), section.begin(), [](unsigned char c) { return char(std::toupper(c)); });
            quest.clear();
            state.clear();
            name.clear();
        } else if (tag == "QUEST") {
            finish();
            quest = trim(line.substr(6));
            state.clear();
            name.clear();
        } else if (tag == "STATE") {
            finish();
            state = trim(line.substr(6));
            name.clear();
        } else if (tag == "NAME") {
            finish();
            name = trim(line.substr(5));
        } else if (tag == "SPEED") {
            auto number = trim(line.substr(6));
            int speed = 0;
            auto [last, error] = std::from_chars(number.data(), number.data() + number.size(), speed);
            if (error == std::errc{} && last == number.data() + number.size())
                speech.speed = speed;
        } else if (tag == "WAVE") {
            speech.wave = trim(line.substr(5));
        } else if (!name.empty() && line.starts_with("//") && normalize(std::string(trim(line.substr(2)))).ends_with(".wav")) {
            // a4npc records authored sound paths as comments instead of WAVE tags.
            speech.wave = trim(line.substr(2));
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
std::string speakerId(std::string_view value) {
    std::string result(value);
    while (!result.empty() && std::isdigit(static_cast<unsigned char>(result.back()))) result.pop_back();
    return result;
}
std::string identityName(std::string_view value) {
    std::string result;
    for (unsigned char letter : value)
        if (std::isalnum(letter)) result += char(std::tolower(letter));
    return result;
}
bool matchesSpeaker(std::string_view id, std::string_view name, std::string_view speaker) {
    const auto identity = identityName(speaker);
    const auto monster = identityName(speakerId(id));
    const auto display = identityName(name);
    if (monster == identity || display == identity) return true;
    if (monster.starts_with("cain") && identity == "cain" && display.ends_with("cain")) return true;
    // A5Q3 calls Anya MONSTER_DREHYA in the original engine.
    return monster == "drehya" && identity == "anya";
}
}
std::string npcIntroductionKey(std::string_view npc, int act) {
    return act == 0 ? std::string(npc) : "act" + std::to_string(act + 1) + ":" + std::string(npc);
}
NpcDialogues loadNpcDialogues(Archives &archives, const DataTable &monsters, const DataTable &presets,
                             const std::map<std::string, std::string, std::less<>> &strings) {
    NpcDialogues result;
    for (int act = 0; act < 5; ++act) {
        auto source = loadNpcDialogueFile(archives, act);
        for (auto &[group, speeches] : source) {
            auto &target = result[group];
            for (auto &speech : speeches) target.push_back(std::move(speech));
        }
    }
    DataTable sounds(archives.read("data/global/excel/sounds.txt"));
    std::map<std::string, std::string> soundNames;
    for (size_t row = 0; row < sounds.rows().size(); ++row)
        soundNames.try_emplace(normalize(std::string(sounds.value(row, "FileName"))), sounds.value(row, "Sound"));
    for (auto &[group, speeches] : result)
        for (auto &speech : speeches) {
            auto sound = soundNames.find(normalize(speech.wave));
            if (sound == soundNames.end()) continue;
            const auto &name = sound->second;
            speech.soundId = name;
            const auto actMarker = name.find("_act");
            speech.speaker = name.substr(0, actMarker == std::string::npos ? name.find('_') : actMarker);
            auto intro = name.find("_intro");
            speech.introduction = speech.quest.empty() && !speech.arrival && intro != std::string::npos;
            if ((speech.introduction || speech.arrival) && intro != std::string::npos && intro + 6 < name.size())
                speech.introClass = name.substr(intro + 7);
            const auto gossip = name.find("_gossip_");
            speech.gossip = speech.quest.empty() && gossip != std::string::npos &&
                gossip + 8 < name.size() && std::isdigit(static_cast<unsigned char>(name[gossip + 8]));
            for (size_t row = 0; row < monsters.rows().size(); ++row) {
                if (!monsters.number(row, "interact").value_or(0)) continue;
                auto nameKey = std::string(monsters.value(row, "NameStr"));
                auto display = strings.find(nameKey);
                const auto displayName = display == strings.end() ? nameKey : display->second;
                if (!matchesSpeaker(monsters.value(row, "Id"), displayName, speech.speaker)) continue;
                result.speakers[npcIntroductionKey(displayName, speech.act)] = speech.speaker;
            }
        }
    constexpr unsigned nativeNpcClasses[]{147,148,150,155,154,265,175,176,177,178,202,200,210,201,198,199,244,
        245,246,251,252,253,254,255,256,257,264,297,511,512,513,514,515,520};
    std::map<std::string, int, std::less<>> presetActs;
    for (size_t row = 0; row < presets.rows().size(); ++row)
        if (auto act = presets.number(row, "Act"); act && *act >= 1 && *act <= 5)
            presetActs.emplace(presets.value(row, "Place"), *act - 1);
    for (unsigned index = 0; index < std::size(nativeNpcClasses); ++index)
        for (size_t row = 0; row < monsters.rows().size(); ++row)
            if (monsters.number(row, "hcIdx") == int(nativeNpcClasses[index])) {
                auto nameKey = std::string(monsters.value(row, "NameStr"));
                auto display = strings.find(nameKey);
                const auto id = monsters.value(row, "Id");
                // PlrIntro assigns Cain in Act I and Tyrael in Act IV to these bits;
                // Cain5 has no MonPreset, while Tyrael1 also appears in Act II.
                const int act = nativeNpcClasses[index] == 265 ? 0 : nativeNpcClasses[index] == 251 ? 3
                    : presetActs.at(std::string(id));
                result.introductionKeys.emplace(index + 1,
                    npcIntroductionKey(display == strings.end() ? nameKey : display->second, act));
                break;
            }
    return result;
}

const NpcSpeech *introSpeech(const NpcDialogues &dialogues, std::string_view npc,
                             std::string_view characterClass, int act) {
    auto speaker = dialogues.speakers.find(npcIntroductionKey(npc, act));
    if (speaker == dialogues.speakers.end()) return nullptr;
    std::string suffix(characterClass.substr(0, 3));
    std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char letter) { return char(std::tolower(letter)); });
    const NpcSpeech *generic = nullptr;
    for (const auto &[group, speeches] : dialogues)
        for (const auto &speech : speeches)
            if (speech.act == act && speech.speaker == speaker->second && speech.introduction) {
                if (!suffix.empty() && speech.introClass == suffix) return &speech;
                if (speech.introClass.empty() && !generic) generic = &speech;
            }
    return generic;
}
const NpcSpeech *gossipSpeech(const NpcDialogues &dialogues, std::string_view npc, size_t turn, int act) {
    auto speaker = dialogues.speakers.find(npcIntroductionKey(npc, act));
    if (speaker == dialogues.speakers.end()) return nullptr;
    std::vector<const NpcSpeech *> generic;
    for (const auto &[group, speeches] : dialogues)
        for (const auto &speech : speeches)
            if (speech.act == act && speech.speaker == speaker->second && speech.gossip)
                generic.push_back(&speech);
    if (generic.empty())
        return nullptr;
    return generic[turn % generic.size()];
}
const NpcSpeech *questSpeech(const NpcDialogues &dialogues, std::string_view quest,
                             std::string_view state, std::string_view npc) {
    const int act = quest.size() > 2 && quest[0] == 'A' && quest[1] >= '1' && quest[1] <= '5'
        ? quest[1] - '1' : 0;
    const auto speaker = dialogues.speakers.find(npcIntroductionKey(npc, act));
    if (speaker == dialogues.speakers.end()) {
        // Authored quest text on a book has no NPC voice/Sounds identity.
        auto group = dialogues.find(act == 0 ? std::string(npc) : "act" + std::to_string(act + 1) + ":" + std::string(npc));
        if (group != dialogues.end())
            for (const auto &speech : group->second)
                if (speech.quest == quest && speech.state == state)
                    return &speech;
        return nullptr;
    }
    for (const auto &[group, speeches] : dialogues)
        for (const auto &speech : speeches)
            if (speech.act == act && speech.speaker == speaker->second &&
                speech.quest == quest && speech.state == state)
                return &speech;
    return nullptr;
}
const NpcSpeech *arrivalSpeech(const NpcDialogues &dialogues, std::string_view npc, int act, std::string_view characterClass) {
    const auto identity = dialogues.speakers.find(npcIntroductionKey(npc, act));
    if (identity == dialogues.speakers.end()) return nullptr;
    std::string suffix(characterClass.substr(0, 3));
    std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    const NpcSpeech *generic = nullptr;
    for (const auto &[group, speeches] : dialogues)
        for (const auto &speech : speeches)
            if (speech.act == act && speech.speaker == identity->second && speech.arrival) {
                if (!suffix.empty() && speech.introClass == suffix) return &speech;
                if (speech.introClass.empty() && !generic) generic = &speech;
            }
    return generic;
}
} // namespace d2x
