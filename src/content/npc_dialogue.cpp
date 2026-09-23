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
    std::string name;
    NpcSpeech speech;
    bool readingQuote = false;
    auto finish = [&] {
        if (!name.empty() && !speech.text.empty())
            result[name].push_back(std::move(speech));
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

const NpcSpeech *introSpeech(const NpcDialogues &dialogues, std::string_view npc) {
    const auto key = npc == "Deckard Cain" ? "Cain" : std::string(npc);
    auto intro = dialogues.find(key + "Intro");
    if (intro != dialogues.end() && !intro->second.empty())
        return &intro->second.front();
    auto generic = dialogues.find(key);
    return generic != dialogues.end() && !generic->second.empty() ? &generic->second.front() : nullptr;
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
} // namespace d2x
