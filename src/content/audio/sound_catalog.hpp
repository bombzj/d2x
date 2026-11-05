#pragma once
#include "resources/archive.hpp"
#include "resources/data_table.hpp"
#include "gameplay/items/quality.hpp"
#include <array>
#include <map>
#include <optional>
#include <string>

namespace d2x {
struct SoundRule {
    std::string sound;
    float delay = 0, volume = -1; // -1 uses Sounds.Volume; MonSounds weapon volume overrides it.
    int probability = 100;
    bool interruptible = false;
};
struct MonsterSoundDefinition {
    std::array<SoundRule, 2> attacks, weapons;
    std::array<SoundRule, 4> skills;
    SoundRule hit, death, footstep, footstepLayer, neutral;
    int footstepCount = 0, footstepOffset = 0;
    float neutralInterval = 0;
};
struct PlayerSoundDefinition { SoundRule hit, death, needKey,denComplete,nihlathakComplete; std::array<SoundRule,8> chat; };
struct SkillSoundDefinition { SoundRule start, active; };
struct ItemSoundDefinition { std::string code, drop; int dropFrame = -1; std::string use; };
// The only MPQ configuration loader for sound choices and timing.
// Values have no authority, device, audio handle or network dependency.
class SoundCatalog {
    DataTable sounds_;
    std::map<std::string, size_t, std::less<>> soundRows_;
    std::map<int, MonsterSoundDefinition> monsters_;
    std::map<int, PlayerSoundDefinition> players_;
    std::map<int, std::array<std::string, 51>> playerNarratives_;
    std::map<int, SkillSoundDefinition> skills_;
    std::map<int, SoundRule> npcReturnGreetings_;
    std::map<int, std::array<std::string, 5>> npcGreetings_;
    std::map<std::pair<int, int>, SoundRule> objects_;
    std::map<std::string, ItemSoundDefinition, std::less<>> items_;
    std::map<int, ItemSoundDefinition> uniqueItems_, setItems_;
    std::map<int, int> groundMaterials_;
  public:
    explicit SoundCatalog(Archives &);
    const DataTable &sounds() const { return sounds_; }
    std::optional<size_t> soundRow(std::string_view) const;
    const MonsterSoundDefinition *monster(int identity) const;
    const PlayerSoundDefinition *player(int identity) const;
    std::string playerNarrative(int identity, int event) const;
    const SoundRule *npcReturnGreeting(int identity) const;
    std::string npcGreeting(int identity, int mode, int period, uint64_t &random) const;
    const SkillSoundDefinition *skill(int identity) const;
    const SoundRule *object(int identity, int mode) const;
    std::optional<ItemSoundDefinition> item(std::string_view code, ItemQuality, int specialRow = -1) const;
    std::string playerFootstep(int identity, bool running, int material, int level) const;
};
} // namespace d2x
