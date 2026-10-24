#pragma once
#include <array>

namespace d2x {
enum class MonsterAiKind { Skeleton, Brute, Zombie, Fallen, CorruptRogue, Goatman, QuillRat,
                           Wraith, CorruptLancer, CorruptArcher, SkeletonBow, Bighead,
                           SkeletonMage, Fetish, Vampire, FallenShaman, FoulCrowNest, BloodHawk,
                           Arach, Smith, Griswold, BloodRaven, Countess, Andariel, GargoyleTrap, FlyingScimitar,
                           SandRaider, SandMaggot, SandMaggotQueen, MaggotEgg, MaggotLarva,
                           ClawViper, SandLeaper, Swarm, Scarab, GreaterMummy, Mummy,
                           PantherWoman, PantherJavelin, Baboon, Vulture, BatDemon, PinHead,
                           Summoner, Duriel, Sarcophagus, ArcaneTower, GoodNpcRanged, ThornHulk, FetishBlowgun, FetishShaman, Mosquito, FrogDemon, ClawViperEx, Willowisp, ZakarumZealot, ZakarumPriest, HighPriest, Hydra, VileMother, VileDog, FingerMage, Regurgitator, DoomKnight, AbyssKnight, OblivionKnight, Megademon, ReanimatedHorde, FrozenHorror, BloodLord, DeathMauler, BaalMinion, PutridDefiler, SiegeBeast, Imp, GenericSpawner, Minion, SuicideMinion, Overseer, MinionSpawner, Succubus, SuccubusWitch, Izual, UberIzual, Mephisto, Idle, Diablo, Nihlathak, AncientMadawc, AncientTalic, AncientKorlic, BaalTentacle, Baal, BaalClone, BaalToStairs, BaalThrone, BaalTaunt, HellMeteor, TrappedSoul, Catapult, CatapultSpotter, Tentacle, TentacleHead, EvilHole, NpcBarb };
struct MonsterAiProfile {
    MonsterAiKind kind;
    std::array<int, 8> params{};
    int meleeRange = 0; // Resolved MonStats2.MeleeRng, including the 255 weapon-class sentinel.
    int retreatVelocityBonus = 0;
    int searchDistance = 35; // AiUtil::sub_6FCF2110: zero AiDist uses 35.
};
} // namespace d2x
