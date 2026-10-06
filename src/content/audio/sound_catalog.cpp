#include "sound_catalog.hpp"
#include "content/character/character_attributes.hpp"
#include <algorithm>
#include <cctype>

namespace d2x {
namespace {
std::string lower(std::string value) {
    for (auto &ch : value) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}
SoundRule rule(const DataTable &table, size_t row, std::string_view name,
               std::string_view delay = {}, std::string_view probability = {},
               std::string_view volume = {}, bool interruptible = false) {
    SoundRule result;
    result.sound = table.value(row, name);
    result.delay = delay.empty() ? 0 : std::max(0, table.number(row, delay).value_or(0)) / 25.f;
    result.probability = probability.empty() ? 100 : std::clamp(table.number(row, probability).value_or(0), 0, 100);
    result.volume = volume.empty() ? -1 : std::clamp(table.number(row, volume).value_or(0), 0, 255) / 255.f;
    result.interruptible = interruptible;
    return result;
}
}
SoundCatalog::SoundCatalog(Archives &archives) : sounds_(archives.read("data/global/excel/sounds.txt")) {
    for (size_t row = 0; row < sounds_.rows().size(); ++row)
        soundRows_.emplace(sounds_.value(row, "Sound"), row);
    const DataTable voices(archives.read("data/global/excel/monsounds.txt"));
    std::map<std::string, MonsterSoundDefinition, std::less<>> profiles;
    for (size_t row = 0; row < voices.rows().size(); ++row) {
        const auto id = voices.value(row, "Id");
        if (id.empty()) continue;
        MonsterSoundDefinition profile;
        profile.attacks = {rule(voices,row,"Attack1","Att1Del","Att1Prb",{},true),
                           rule(voices,row,"Attack2","Att2Del","Att2Prb",{},true)};
        profile.weapons = {rule(voices,row,"Weapon1","Wea1Del",{},"Wea1Vol",true),
                           rule(voices,row,"Weapon2","Wea2Del",{},"Wea2Vol",true)};
        for (size_t i = 0; i < profile.skills.size(); ++i)
            profile.skills[i] = rule(voices,row,"Skill" + std::to_string(i + 1),{},{},{},true);
        profile.hit = rule(voices,row,"HitSound","HitDelay");
        profile.death = rule(voices,row,"DeathSound","DeaDelay");
        profile.footstep = rule(voices,row,"Footstep",{},"FsPrb");
        profile.footstepLayer = rule(voices,row,"FootstepLayer",{},"FsPrb");
        profile.footstepCount = std::max(0, voices.number(row,"FsCnt").value_or(0));
        profile.footstepOffset = voices.number(row,"FsOff").value_or(0);
        profile.neutral = rule(voices,row,"Neutral");
        profile.neutralInterval = std::max(0, voices.number(row,"NeuTime").value_or(0)) / 25.f;
        profiles.emplace(id, std::move(profile));
    }
    const DataTable monsters(archives.read("data/global/excel/monstats.txt"));
    for (size_t row = 0; row < monsters.rows().size(); ++row) {
        const auto id = monsters.number(row,"hcIdx");
        if (!id) continue;
        const auto voice = monsters.value(row,"MonSound");
        if (voice.empty()) monsters_.emplace(*id, MonsterSoundDefinition{}); // Intentional silent actor.
        else if (const auto found = profiles.find(voice); found != profiles.end()) monsters_.emplace(*id, found->second);
    }
    const DataTable characters(archives.read("data/global/excel/charstats.txt"));
    int identity = 0;
    for (const auto &character : loadCharacterDefinitions(characters)) {
        const auto name = lower(std::string(characters.value(character.sourceRow,"class")));
        players_.emplace(identity++, PlayerSoundDefinition{SoundRule{name + "_hit_1"}, SoundRule{name + "_death_1"}});
    }
    const DataTable skills(archives.read("data/global/excel/skills.txt"));
    for (size_t row = 0; row < skills.rows().size(); ++row)
        if (const auto id = skills.number(row,"Id"))
            skills_.emplace(*id, SkillSoundDefinition{rule(skills,row,"stsound","stsounddelay",{},{},true),
                rule(skills,row,"dosound","dosounddelay",{},{},true)});
}
std::optional<size_t> SoundCatalog::soundRow(std::string_view id) const {
    const auto found = soundRows_.find(id);
    return found == soundRows_.end() ? std::nullopt : std::optional{found->second};
}
const MonsterSoundDefinition *SoundCatalog::monster(int id) const {
    const auto found = monsters_.find(id); return found == monsters_.end() ? nullptr : &found->second;
}
const PlayerSoundDefinition *SoundCatalog::player(int id) const {
    const auto found = players_.find(id); return found == players_.end() ? nullptr : &found->second;
}
const SkillSoundDefinition *SoundCatalog::skill(int id) const {
    const auto found = skills_.find(id); return found == skills_.end() ? nullptr : &found->second;
}
} // namespace d2x
