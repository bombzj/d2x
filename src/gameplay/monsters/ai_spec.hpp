#pragma once
#include <array>
#include <optional>
#include <cstdint>

namespace d2x {
enum class MonsterAiKind { Skeleton, Brute, Zombie, Fallen, CorruptRogue, Goatman, QuillRat,
                           Wraith, CorruptLancer, CorruptArcher, SkeletonBow, Bighead,
                           SkeletonMage, Fetish, Vampire, FallenShaman, FoulCrowNest, BloodHawk,
                           Arach, Smith, Griswold, BloodRaven, Countess, Andariel, GargoyleTrap, FlyingScimitar,
                           SandRaider, SandMaggot, SandMaggotQueen, MaggotEgg, MaggotLarva,
                           ClawViper, SandLeaper, Swarm, Scarab, GreaterMummy, Mummy,
                           PantherWoman, PantherJavelin, Baboon, Vulture, BatDemon, PinHead,
                           Summoner, Duriel, Sarcophagus, ArcaneTower, GoodNpcRanged, ThornHulk, FetishBlowgun, FetishShaman, Mosquito, FrogDemon, ClawViperEx, Willowisp, ZakarumZealot, ZakarumPriest, HighPriest, Hydra, VileMother, VileDog, FingerMage, Regurgitator, DoomKnight, AbyssKnight, OblivionKnight, Megademon, ReanimatedHorde, FrozenHorror, BloodLord, DeathMauler, BaalMinion, PutridDefiler, SiegeBeast, Imp, GenericSpawner, Minion, SuicideMinion, Overseer, MinionSpawner, Succubus, SuccubusWitch, Izual, UberIzual, Mephisto, Idle, Diablo, Nihlathak, AncientMadawc, AncientTalic, AncientKorlic, BaalTentacle, Baal, BaalClone, BaalToStairs, BaalThrone, BaalTaunt, HellMeteor, TrappedSoul, Catapult, CatapultSpotter, Tentacle, TentacleHead, EvilHole, NpcBarb, ScriptedNpc, Npc, Towner, NpcStationary, JarJar, NpcOutOfTown, RetailEmpty, CagedWussie };
constexpr bool townNpcAi(MonsterAiKind kind) {
    return kind==MonsterAiKind::Npc || kind==MonsterAiKind::Towner || kind==MonsterAiKind::NpcStationary || kind==MonsterAiKind::JarJar || kind==MonsterAiKind::NpcOutOfTown;
}
struct MonsterAiProfile {
    struct Pd2ArcherCycle {
        int wanderRadius{},wanderChance{},attackEnd{},cycleEnd{},state{-1};
        std::array<int,6> restingResistances{}; // Dm, Ma, Fi, Li, Co, Po, fixed H columns.
    };
    MonsterAiKind kind;
    std::array<int, 8> params{};
    // PD2 wrappers also read fixed columns independently of game difficulty.
    // Preserve native WORD inputs; each callback chooses signedness.
    std::array<std::array<int,3>,8> pd2Parameters{};
    int pd2FixedIdle{};
    std::array<uint8_t,3> pd2Delays{};
    std::array<uint8_t,3> pd2Distances{};
    std::array<uint8_t,8> pd2SkillRanks{};
    std::array<uint16_t,6> pd2RestingResistances{}; // Fi, Li, Co, Po, Ma, Dm; fixed H columns.
    std::optional<Pd2ArcherCycle> pd2ArcherCycle;
    int meleeRange = 0; // Resolved MonStats2.MeleeRng, including the 255 weapon-class sentinel.
    int retreatVelocityBonus = 0;
    int searchDistance = 35; // AiUtil::sub_6FCF2110: zero AiDist uses 35.
    bool preserveHomeOnAiSwitch{};
};
} // namespace d2x
