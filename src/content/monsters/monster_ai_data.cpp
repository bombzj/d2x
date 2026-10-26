#include "monster_ai_data.hpp"
#include <string>
#include <algorithm>
#include <vector>

namespace d2x {
std::optional<MonsterAiProfile> loadMonsterAiProfile(const DataTable &stats, size_t row,
                                                     std::string_view ai, int difficulty) {
    if (difficulty < 0 || difficulty > 2) return std::nullopt;
    MonsterAiProfile profile;
    if (ai == "Skeleton") profile.kind = MonsterAiKind::Skeleton;
    else if (ai == "Idle" && (stats.value(row,"Id").starts_with("boneprison") || stats.value(row,"Id")=="prisondoor" || stats.value(row,"Id")=="window1" || stats.value(row,"Id")=="window2")) profile.kind = MonsterAiKind::Idle;
    else if (ai == "Brute") profile.kind = MonsterAiKind::Brute;
    else if (ai == "Zombie") profile.kind = MonsterAiKind::Zombie;
    else if (ai == "Fallen") profile.kind = MonsterAiKind::Fallen;
    else if (ai == "CorruptRogue") profile.kind = MonsterAiKind::CorruptRogue;
    else if (ai == "Goatman") profile.kind = MonsterAiKind::Goatman;
    else if (ai == "QuillRat") profile.kind = MonsterAiKind::QuillRat;
    else if (ai == "Wraith") profile.kind = MonsterAiKind::Wraith;
    else if (ai == "CorruptLancer") profile.kind = MonsterAiKind::CorruptLancer;
    else if (ai == "CorruptArcher") profile.kind = MonsterAiKind::CorruptArcher;
    else if (ai == "SkeletonBow") profile.kind = MonsterAiKind::SkeletonBow;
    else if (ai == "Bighead") profile.kind = MonsterAiKind::Bighead;
    else if (ai == "SkeletonMage") profile.kind = MonsterAiKind::SkeletonMage;
    else if (ai == "Fetish") profile.kind = MonsterAiKind::Fetish;
    else if (ai == "Vampire") profile.kind = MonsterAiKind::Vampire;
    else if (ai == "FallenShaman") profile.kind = MonsterAiKind::FallenShaman;
    else if (ai == "FoulCrowNest") profile.kind = MonsterAiKind::FoulCrowNest;
    else if (ai == "BloodHawk") profile.kind = MonsterAiKind::BloodHawk;
    else if (ai == "Arach") profile.kind = MonsterAiKind::Arach;
    else if (ai == "Smith") profile.kind = MonsterAiKind::Smith;
    else if (ai == "Griswold") profile.kind = MonsterAiKind::Griswold;
    else if (ai == "BloodRaven") profile.kind = MonsterAiKind::BloodRaven;
    else if (ai == "Andariel") profile.kind = MonsterAiKind::Andariel;
    else if (ai == "GargoyleTrap") profile.kind = MonsterAiKind::GargoyleTrap;
    else if (ai == "FlyingScimitar") profile.kind = MonsterAiKind::FlyingScimitar;
    else if (ai == "SandRaider") profile.kind = MonsterAiKind::SandRaider;
    else if (ai == "SandMaggot") profile.kind = MonsterAiKind::SandMaggot;
    else if (ai == "SandMaggotQueen") profile.kind = MonsterAiKind::SandMaggotQueen;
    else if (ai == "MaggotEgg") profile.kind = MonsterAiKind::MaggotEgg;
    else if (ai == "MaggotLarva") profile.kind = MonsterAiKind::MaggotLarva;
    else if (ai == "ClawViper") profile.kind = MonsterAiKind::ClawViper;
    else if (ai == "ClawViperEx") profile.kind = MonsterAiKind::ClawViperEx;
    else if (ai == "WillOWisp") profile.kind = MonsterAiKind::Willowisp;
    else if (ai == "ZakarumZealot") profile.kind = MonsterAiKind::ZakarumZealot;
    else if (ai == "ZakarumPriest") profile.kind = MonsterAiKind::ZakarumPriest;
    else if (ai == "HighPriest") profile.kind = MonsterAiKind::HighPriest;
    else if (ai == "Hydra") profile.kind = MonsterAiKind::Hydra;
    else if (ai == "VileMother") profile.kind = MonsterAiKind::VileMother;
    else if (ai == "VileDog") profile.kind = MonsterAiKind::VileDog;
    else if (ai == "FingerMage") profile.kind = MonsterAiKind::FingerMage;
    else if (ai == "Regurgitator") profile.kind = MonsterAiKind::Regurgitator;
    else if (ai == "DoomKnight") profile.kind = MonsterAiKind::DoomKnight;
    else if (ai == "AbyssKnight") profile.kind = MonsterAiKind::AbyssKnight;
    else if (ai == "OblivionKnight") profile.kind = MonsterAiKind::OblivionKnight;
    else if (ai == "SuicideMinion") profile.kind = MonsterAiKind::SuicideMinion;
    else if (ai == "Minion") profile.kind = MonsterAiKind::Minion;
    else if (ai == "Overseer") profile.kind = MonsterAiKind::Overseer;
    else if (ai == "MinionSpawner") profile.kind = MonsterAiKind::MinionSpawner;
    else if (ai == "Mephisto") profile.kind = MonsterAiKind::Mephisto;
    else if (ai == "Diablo") profile.kind = MonsterAiKind::Diablo;
    else if (ai == "Ancient" && stats.value(row,"Id")=="ancientbarb1") profile.kind = MonsterAiKind::AncientTalic;
    else if (ai == "Ancient" && stats.value(row,"Id")=="ancientbarb2") profile.kind = MonsterAiKind::AncientMadawc;
    else if (ai == "Ancient" && stats.value(row,"Id")=="ancientbarb3") profile.kind = MonsterAiKind::AncientKorlic;
    else if (ai == "Nihlathak") profile.kind = MonsterAiKind::Nihlathak;
    else if (ai == "UberIzual") profile.kind = MonsterAiKind::UberIzual;
    else if (ai == "Izual") profile.kind = MonsterAiKind::Izual;
    else if (ai == "SuccubusWitch") profile.kind = MonsterAiKind::SuccubusWitch;
    else if (ai == "Succubus") profile.kind = MonsterAiKind::Succubus;
    else if (ai == "GenericSpawner") profile.kind = MonsterAiKind::GenericSpawner;
    else if (ai == "Imp") profile.kind = MonsterAiKind::Imp;
    else if (ai == "SiegeBeast") profile.kind = MonsterAiKind::SiegeBeast;
    else if (ai == "PutridDefiler") profile.kind = MonsterAiKind::PutridDefiler;
    else if (ai == "BaalMinion") profile.kind = MonsterAiKind::BaalMinion;
    else if (ai == "EvilHole") profile.kind = MonsterAiKind::EvilHole;
    else if (ai == "Tentacle") profile.kind = MonsterAiKind::Tentacle;
    else if (ai == "TentacleHead") profile.kind = MonsterAiKind::TentacleHead;
    else if (ai == "BaalTentacle") profile.kind = MonsterAiKind::BaalTentacle;
    else if (ai == "BaalCrab") profile.kind = MonsterAiKind::Baal;
    else if (ai == "BaalCrabClone") profile.kind = MonsterAiKind::BaalClone;
    else if (ai == "BaalToStairs") profile.kind = MonsterAiKind::BaalToStairs;
    else if (ai == "BaalThrone") profile.kind = MonsterAiKind::BaalThrone;
    else if (ai == "BaalTaunt") profile.kind = MonsterAiKind::BaalTaunt;
    else if (ai == "HellMeteor") profile.kind = MonsterAiKind::HellMeteor;
    else if (ai == "TrappedSoul") profile.kind = MonsterAiKind::TrappedSoul;
    else if (ai == "Catapult") profile.kind = MonsterAiKind::Catapult;
    else if (ai == "CatapultSpotter") profile.kind = MonsterAiKind::CatapultSpotter;
    else if (ai == "NpcBarb") profile.kind = MonsterAiKind::NpcBarb;
    else if (ai == "Ancient") profile.kind = MonsterAiKind::ScriptedNpc;
    else if (ai == "DeathMauler") profile.kind = MonsterAiKind::DeathMauler;
    else if (ai == "BloodLord") profile.kind = MonsterAiKind::BloodLord;
    else if (ai == "FrozenHorror") profile.kind = MonsterAiKind::FrozenHorror;
    else if (ai == "ReanimatedHorde") profile.kind = MonsterAiKind::ReanimatedHorde;
    else if (ai == "Megademon") profile.kind = MonsterAiKind::Megademon;
    else if (ai == "SandLeaper") profile.kind = MonsterAiKind::SandLeaper;
    else if (ai == "Swarm") profile.kind = MonsterAiKind::Swarm;
    else if (ai == "Scarab") profile.kind = MonsterAiKind::Scarab;
    else if (ai == "GreaterMummy") profile.kind = MonsterAiKind::GreaterMummy;
    else if (ai == "Mummy") profile.kind = MonsterAiKind::Mummy;
    else if (ai == "PantherWoman") profile.kind = MonsterAiKind::PantherWoman;
    else if (ai == "PantherJavelin") profile.kind = MonsterAiKind::PantherJavelin;
    else if (ai == "Baboon") profile.kind = MonsterAiKind::Baboon;
    else if (ai == "Vulture") profile.kind = MonsterAiKind::Vulture;
    else if (ai == "BatDemon") profile.kind = MonsterAiKind::BatDemon;
    else if (ai == "PinHead") profile.kind = MonsterAiKind::PinHead;
    else if (ai == "Summoner") profile.kind = MonsterAiKind::Summoner;
    else if (ai == "Duriel") profile.kind = MonsterAiKind::Duriel;
    else if (ai == "Sarcophagus") profile.kind = MonsterAiKind::Sarcophagus;
    else if (ai == "ArcaneTower") profile.kind = MonsterAiKind::ArcaneTower;
    else if (ai == "GoodNpcRanged") profile.kind = MonsterAiKind::GoodNpcRanged;
    else if (ai == "ThornHulk") profile.kind = MonsterAiKind::ThornHulk;
    else if (ai == "FetishBlowgun") profile.kind = MonsterAiKind::FetishBlowgun;
    else if (ai == "FetishShaman") profile.kind = MonsterAiKind::FetishShaman;
    else if (ai == "Mosquito") profile.kind = MonsterAiKind::Mosquito;
    else if (ai == "FrogDemon") profile.kind = MonsterAiKind::FrogDemon;
    else return std::nullopt;
    if (profile.kind == MonsterAiKind::Vampire || profile.kind == MonsterAiKind::Baboon || profile.kind == MonsterAiKind::ZakarumZealot || profile.kind == MonsterAiKind::FingerMage) {
        const int velocity = stats.number(row, "Velocity").value_or(0);
        const int run = stats.number(row, "Run").value_or(0);
        if (velocity > 0) profile.retreatVelocityBonus = std::clamp(100 * run / velocity - 100, 0, 120);
    }
    const std::string suffix = difficulty == 0 ? "" : difficulty == 1 ? "(N)" : "(H)";
    const int sight = stats.number(row, "aidist" + suffix).value_or(0);
    if (sight < 0 || sight > 255) return std::nullopt;
    profile.searchDistance = sight ? sight : 35;
    for (int index = 0; index < 8; ++index) {
        auto value = stats.number(row, "aip" + std::to_string(index + 1) + suffix);
        if (value && (*value < 0 || *value > 65535)) return std::nullopt;
        profile.params[index] = value.value_or(0);
    }
    auto percentage = [&](int index) { return profile.params[index] <= 100; };
    if(profile.kind==MonsterAiKind::HighPriest && (!percentage(0) || !percentage(1) || !percentage(3) || !percentage(4) || !percentage(5) || !percentage(6))) return std::nullopt;
    if(profile.kind==MonsterAiKind::GargoyleTrap && !percentage(1)) return std::nullopt;
    if (profile.kind == MonsterAiKind::Andariel &&
        (!percentage(0) || !percentage(1) || !percentage(2) || !percentage(3))) return std::nullopt;
    if (profile.kind == MonsterAiKind::Skeleton &&
        (!percentage(0) || !percentage(2) || !percentage(3))) return std::nullopt;
    if (profile.kind == MonsterAiKind::Brute &&
        (!percentage(2) || !percentage(3))) return std::nullopt;
    if (profile.kind == MonsterAiKind::Zombie &&
        (!percentage(0) || !percentage(3))) return std::nullopt;
    if (profile.kind == MonsterAiKind::Fallen &&
        (!percentage(0) || !percentage(2) || !percentage(3))) return std::nullopt;
    if (profile.kind == MonsterAiKind::CorruptRogue &&
        (!percentage(0) || !percentage(2) || !percentage(4) || profile.params[3] > 126))
        return std::nullopt;
    if (profile.kind == MonsterAiKind::Goatman &&
        (!percentage(0) || !percentage(2))) return std::nullopt;
    if (profile.kind == MonsterAiKind::QuillRat &&
        (!percentage(1) || profile.params[0] > 255 || profile.params[3] > 255))
        return std::nullopt;
    if (profile.kind == MonsterAiKind::Wraith &&
        (!percentage(0) || !percentage(2))) return std::nullopt;
    if (profile.kind == MonsterAiKind::CorruptLancer &&
        (!percentage(0) || !percentage(1) || !percentage(3) ||
         !percentage(5) || !percentage(6) || !percentage(7) || profile.params[4] > 255))
        return std::nullopt;
    if (profile.kind == MonsterAiKind::CorruptArcher &&
        (!percentage(0) || !percentage(1) || !percentage(3) ||
         !percentage(5) || !percentage(6) || profile.params[4] > 255 ||
         profile.params[7] > 255)) return std::nullopt;
    if (profile.kind == MonsterAiKind::SkeletonBow &&
        (!percentage(0) || !percentage(2) || profile.params[1] > 255 ||
         profile.params[3] > 255 || profile.params[4] > 255)) return std::nullopt;
    if (profile.kind == MonsterAiKind::Bighead &&
        (!percentage(0) || !percentage(1) || !percentage(2) || !percentage(3)))
        return std::nullopt;
    if (profile.kind == MonsterAiKind::SkeletonMage &&
        (!percentage(0) || !percentage(2) || !percentage(4) || !percentage(6) ||
         profile.params[1] > 255 || profile.params[3] > 255 ||
         profile.params[5] > 255 || profile.params[7] > 255)) return std::nullopt;
    if (profile.kind == MonsterAiKind::Fetish &&
        (!percentage(0) || !percentage(3) || profile.params[1] > 255 ||
         profile.params[2] > 255)) return std::nullopt;
    if (profile.kind == MonsterAiKind::Vampire &&
        (!percentage(0) || !percentage(1) || !percentage(3) ||
         profile.params[2] > 255 || profile.params[4] > 7)) return std::nullopt;
    if (profile.kind == MonsterAiKind::FallenShaman &&
        (!percentage(0) || !percentage(1) || !percentage(2) ||
         profile.params[3] > 255 || profile.params[4] > 255)) return std::nullopt;
    if (profile.kind == MonsterAiKind::FoulCrowNest &&
        (profile.params[0] > 2500 || profile.params[2] > 100)) return std::nullopt;
    if (profile.kind == MonsterAiKind::BloodHawk &&
        (!percentage(0) || !percentage(1) || !percentage(2) ||
         profile.params[3] > 255 || profile.params[4] > 255)) return std::nullopt;
    if (profile.kind == MonsterAiKind::Arach &&
        (!percentage(0) || !percentage(1) || !percentage(2) ||
         !percentage(4) || profile.params[3] > 255)) return std::nullopt;
    // The family-specific percentage columns must not silently become always
    // true when malformed content reaches the pure decision programs.
    std::vector<int> percentages;
    switch(profile.kind) {
    case MonsterAiKind::FlyingScimitar: percentages={0,1,3};break;
    case MonsterAiKind::NpcBarb: percentages={1};break;
    case MonsterAiKind::SandRaider: percentages={0,1,2,3,6};break;
    case MonsterAiKind::SandMaggot: percentages={0,1,3};break;
    case MonsterAiKind::ClawViper: percentages={0,2,3};break;
    case MonsterAiKind::ClawViperEx: percentages={0,2,3};break;
    case MonsterAiKind::Willowisp: percentages={0,1,2};break;
    case MonsterAiKind::ZakarumZealot: percentages={0,1,2,3};break;
    case MonsterAiKind::ZakarumPriest: percentages={0,1,2,3};break;
    case MonsterAiKind::VileMother: percentages={2,3,4,5};break;
    case MonsterAiKind::VileDog: percentages={0,2};break;
    case MonsterAiKind::FingerMage: percentages={0,1,2,3};break;
    case MonsterAiKind::Regurgitator: percentages={0,1,2,3,4};break;
    case MonsterAiKind::DoomKnight: percentages={0,2};break;
    case MonsterAiKind::AbyssKnight: percentages={0,1,2,6};break;
    case MonsterAiKind::OblivionKnight: percentages={3,4,5,6};break;
    case MonsterAiKind::SiegeBeast: percentages={1,2,4,5};break;
    case MonsterAiKind::Minion: percentages={0,1,2};break;
    case MonsterAiKind::SuicideMinion: percentages={2};break;
    case MonsterAiKind::Overseer: percentages={1,2,5,6};break;
    case MonsterAiKind::Succubus: percentages={0,1,2,6,7};break;
    case MonsterAiKind::SuccubusWitch: percentages={0,1,2,4,6,7};break;
    case MonsterAiKind::Mephisto: percentages={0,1,2};break;
    case MonsterAiKind::Nihlathak: percentages={0,2,3};break;
    case MonsterAiKind::AncientMadawc: percentages={1,2,3};break;
    case MonsterAiKind::AncientTalic: percentages={1,2};break;
    case MonsterAiKind::AncientKorlic: percentages={1,2};break;
    case MonsterAiKind::Tentacle:
    case MonsterAiKind::TentacleHead: percentages={0,1};break;
    case MonsterAiKind::BaalTentacle: percentages={0};break;
    case MonsterAiKind::UberIzual:
    case MonsterAiKind::Izual: percentages={0,1,2,3};break;
    case MonsterAiKind::BaalMinion: percentages={0,1,2};break;
    case MonsterAiKind::DeathMauler: percentages={0,1,3};break;
    case MonsterAiKind::BloodLord: percentages={0,1,2};break;
    case MonsterAiKind::FrozenHorror: percentages={0,1,2};break;
    case MonsterAiKind::ReanimatedHorde: percentages={0,3,4,5,7};break;
    case MonsterAiKind::Megademon: percentages={0,1,2,3,4};break;
    case MonsterAiKind::SandLeaper: percentages={0,1,2,3};break;
    case MonsterAiKind::Swarm: percentages={0,2};break;
    case MonsterAiKind::Scarab: percentages={0,1,3,4};break;
    case MonsterAiKind::GreaterMummy: percentages={0,1,2,3};break;
    case MonsterAiKind::Mummy: percentages={1,2,3};break;
    case MonsterAiKind::PantherWoman: percentages={0,1};break;
    case MonsterAiKind::PantherJavelin: percentages={0,1,3};break;
    case MonsterAiKind::Baboon: percentages={0,1,2,3};break;
    case MonsterAiKind::Vulture: percentages={0,2,3,4};break;
    case MonsterAiKind::BatDemon: percentages={0,1,2,3};break;
    case MonsterAiKind::PinHead: percentages={0,2,4};break;
    case MonsterAiKind::Summoner: percentages={0,1,2,5};break;
    case MonsterAiKind::Duriel: percentages={1,2,3,4};break;
    case MonsterAiKind::MaggotEgg: percentages={1};break;
    case MonsterAiKind::ThornHulk: percentages={0,1,2,3};break;
    case MonsterAiKind::FetishBlowgun: percentages={1};break;
    case MonsterAiKind::FetishShaman: percentages={0,3};break;
    case MonsterAiKind::Mosquito: percentages={2,3};break;
    case MonsterAiKind::FrogDemon: percentages={0,1,2,3,4};break;
    default:break;
    }
    for(int index:percentages) if(!percentage(index)) return std::nullopt;
    return profile;
}
} // namespace d2x
