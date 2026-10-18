#include "gameplay/monsters/implementation.hpp"

namespace d2x {
MonsterImplementation monsterImplementation(const std::string &code) {
    if(code=="cantor1" || code=="cantor2" || code=="cantor3" || code=="cantor4" || code=="cantor5" || code=="cantor6") return {MonsterKind::ZakarumPriest,false};
    if(code=="zealot1" || code=="zealot2" || code=="zealot3" || code=="zealot4" || code=="zealot5") return {MonsterKind::ZakarumZealot,false};
    if(code=="willowisp1" || code=="willowisp2" || code=="willowisp3" || code=="willowisp4" || code=="willowisp5" || code=="willowisp6" || code=="willowisp7" || code=="willowisp8") return {MonsterKind::Willowisp,false};
    if(code=="frogdemon1" || code=="frogdemon2" || code=="frogdemon3") return {MonsterKind::FrogDemon,false};
    if(code=="mosquito1" || code=="mosquito2" || code=="mosquito3" || code=="mosquito4") return {MonsterKind::Mosquito,false};
    if(code=="bonefetish1" || code=="bonefetish2" || code=="bonefetish3" ||
       code=="bonefetish4" || code=="bonefetish5" || code=="bonefetish6" || code=="bonefetish7") return {MonsterKind::Fetish,false};
    if (code == "fetishshaman1" || code == "fetishshaman2" || code == "fetishshaman3" ||
        code == "fetishshaman4" || code == "fetishshaman5" || code == "fetishshaman6" ||
        code == "fetishshaman7" || code == "fetishshaman8") return {MonsterKind::FetishShaman, false};
    if (code == "fetishblow1" || code == "fetishblow2" || code == "fetishblow3" ||
        code == "fetishblow4" || code == "fetishblow5" || code == "fetishblow6" ||
        code == "fetishblow7" || code == "fetishblow8") return {MonsterKind::FetishBlowgun, false};
    if (code == "thornhulk1" || code == "thornhulk2" || code == "thornhulk3" ||
        code == "thornhulk4" || code == "thornhulk5") return {MonsterKind::ThornHulk, false};
    if (code == "lightningspire") return {MonsterKind::ArcaneTower, false};
    if (code == "sandraider1" || code == "sandraider2" || code == "sandraider3" || code == "sandraider4" || code == "sandraider5" ||
        code == "sandraider6" || code == "sandraider7" || code == "sandraider8" || code == "sandraider9" || code == "sandraider10") return {MonsterKind::SandRaider, false};
    if (code == "sandmaggot1" || code == "sandmaggot2" || code == "sandmaggot3" ||
        code == "sandmaggot4" || code == "sandmaggot5" || code == "sandmaggot6" || code == "sandmaggot7") return {MonsterKind::SandMaggot, false};
    if (code == "maggotqueen1") return {MonsterKind::SandMaggotQueen, false};
    if (code == "maggotegg1" || code == "maggotegg2" || code == "maggotegg3" ||
        code == "maggotegg4" || code == "maggotegg5" || code == "maggotegg6") return {MonsterKind::MaggotEgg, false};
    if (code == "maggotbaby1" || code == "maggotbaby2" || code == "maggotbaby3" ||
        code == "maggotbaby4" || code == "maggotbaby5" || code == "maggotbaby6") return {MonsterKind::MaggotLarva, false};
    if (code == "clawviper1" || code == "clawviper2" || code == "clawviper3" || code == "clawviper4" || code == "clawviper5") return {MonsterKind::ClawViper, false};
    if (code == "clawviper6" || code == "clawviper7" || code == "clawviper8" || code == "clawviper9" || code == "clawviper10") return {MonsterKind::ClawViper, false};
    if (code == "sandleaper1" || code == "sandleaper2" || code == "sandleaper3" ||
        code == "sandleaper4" || code == "sandleaper5" || code == "sandleaper6" || code == "sandleaper7") return {MonsterKind::SandLeaper, false};
    if (code == "swarm1" || code == "swarm2" || code == "swarm3" || code == "swarm4" || code == "swarm5") return {MonsterKind::Swarm, false};
    if (code == "scarab1" || code == "scarab2" || code == "scarab3" || code == "scarab4" || code == "scarab5" || code == "scarab6" || code == "scarab7" || code == "scarab8") return {MonsterKind::Scarab, false};
    if (code == "unraveler1" || code == "unraveler2" || code == "unraveler3" || code == "unraveler4" || code == "unraveler5" ||
        code == "unraveler6" || code == "unraveler7" || code == "unraveler8" || code == "unraveler9" || code == "radament") return {MonsterKind::GreaterMummy, false};
    if (code == "mummy1" || code == "mummy2" || code == "mummy3" || code == "mummy4" || code == "mummy5" || code == "mummy6") return {MonsterKind::Mummy, false};
    if (code == "pantherwoman1" || code == "pantherwoman2" || code == "pantherwoman3" || code == "pantherwoman4" || code == "pantherwoman5" || code == "pantherwoman6") return {MonsterKind::PantherWoman, false};
    if (code == "slinger1" || code == "slinger2" || code == "slinger3" || code == "slinger4" || code == "slinger5" || code == "slinger6" || code == "slinger7" || code == "slinger8" || code == "slinger9") return {MonsterKind::PantherJavelin, false};
    if (code == "baboon1" || code == "baboon2" || code == "baboon3" || code == "baboon4" ||
        code == "baboon5" || code == "baboon6" || code == "baboon7" || code == "baboon8") return {MonsterKind::Baboon, false};
    if (code == "vulture1" || code == "vulture2" || code == "vulture3" || code == "vulture4" || code == "vulture5") return {MonsterKind::Vulture, false};
    if (code == "batdemon1" || code == "batdemon2" || code == "batdemon3" ||
        code == "batdemon4" || code == "batdemon5" || code == "batdemon6" || code == "batdemon7") return {MonsterKind::BatDemon, false};
    if (code == "blunderbore1" || code == "blunderbore2" || code == "blunderbore3" ||
        code == "blunderbore4" || code == "blunderbore5" || code == "blunderbore6") return {MonsterKind::PinHead, false};
    if (code == "summoner") return {MonsterKind::Summoner, false};
    if (code == "duriel") return {MonsterKind::Duriel, false};
    if (code == "sarcophagus") return {MonsterKind::Sarcophagus, false};
    if (code == "dopplezon" || code == "valkyrie") return {MonsterKind::AmazonPet, false};
    if (code == "claygolem") return {MonsterKind::ClayGolem, false};
    if (code == "bloodgolem") return {MonsterKind::BloodGolem, false};
    if (code == "irongolem") return {MonsterKind::IronGolem, false};
    if (code == "firegolem") return {MonsterKind::FireGolem, false};
    if (code == "necromage") return {MonsterKind::NecroMage, false};
    if (code == "bonewall") return {MonsterKind::BoneWall, false};
    if (code == "prisondoor") return {MonsterKind::PrisonDoor, false};
    if (code == "hydra1") return {MonsterKind::Hydra1, false};
    if (code == "hydra2") return {MonsterKind::Hydra2, false};
    if (code == "hydra3") return {MonsterKind::Hydra3, false};
    if (code == "councilmember1" || code == "councilmember2" || code == "councilmember3" || code == "baalhighpriest") return {MonsterKind::HighPriest, false};
    if (code == "fingermage1" || code == "fingermage2" || code == "fingermage3" || code == "fingermage4" || code == "fingermage5") return {MonsterKind::FingerMage, false};
    if (code == "regurgitator1" || code == "regurgitator2" || code == "regurgitator3" || code == "regurgitator4") return {MonsterKind::Regurgitator, false};
    if (code == "doomknight1" || code == "dkfig1" || code == "dkfig2") return {MonsterKind::DoomKnight, false};
    if (code == "doomknight2") return {MonsterKind::AbyssKnight, false};
    if (code == "doomknight3" || code == "dkmag1" || code == "dkmag2" || code == "dkmag3") return {MonsterKind::OblivionKnight, false};
    if (code == "mephisto") return {MonsterKind::Mephisto, false};
    if (code == "uberizual") return {MonsterKind::UberIzual, false};
    if (code == "izual") return {MonsterKind::Izual, false};
    if (code == "succubuswitch1" || code == "succubuswitch2" || code == "succubuswitch3" || code == "succubuswitch4" || code == "succubuswitch5" || code == "succubuswitch6" || code == "succubuswitch7" || code == "succubuswitch8" || code == "succubuswitch9") return {MonsterKind::SuccubusWitch, false};
    if (code == "succubus1" || code == "succubus2" || code == "succubus3" || code == "succubus4" || code == "succubus5" || code == "succubus6" || code == "succubus7" || code == "succubus8") return {MonsterKind::Succubus, false};
    if (code == "minion1" || code == "minion2" || code == "minion3" || code == "minion4" || code == "minion5" || code == "minion6" || code == "minion7" || code == "minion8" || code == "minion9" || code == "minion10" || code == "minion11") return {MonsterKind::Minion, false};
    if (code == "suicideminion1" || code == "suicideminion2" || code == "suicideminion3" || code == "suicideminion4" || code == "suicideminion5" || code == "suicideminion6" || code == "suicideminion7" || code == "suicideminion8") return {MonsterKind::SuicideMinion, false};
    if (code == "overseer1" || code == "overseer2" || code == "overseer3" || code == "overseer4" || code == "overseer5" || code == "overseer6") return {MonsterKind::Overseer, false};
    if (code == "evilhut") return {MonsterKind::GenericSpawner, false};
    if (code == "minionspawner1" || code == "minionspawner2" || code == "minionspawner3" || code == "minionspawner4" || code == "minionspawner5" || code == "minionspawner6" || code == "minionspawner7" || code == "minionspawner8" || code == "demonspawner") return {MonsterKind::MinionSpawner, false};
    if (code == "imp1" || code == "imp2" || code == "imp3" || code == "imp4" || code == "imp5" || code == "imp6" || code == "imp7" || code == "imp8") return {MonsterKind::Imp, false};
    if (code == "siegebeast1" || code == "siegebeast2" || code == "siegebeast3" || code == "siegebeast4" || code == "siegebeast5") return {MonsterKind::SiegeBeast, false};
    if (code == "putriddefiler1" || code == "putriddefiler2" || code == "putriddefiler3" || code == "putriddefiler4" || code == "putriddefiler5") return {MonsterKind::PutridDefiler, false};
    if (code == "baalminion1" || code == "baalminion2" || code == "baalminion3") return {MonsterKind::BaalMinion, false};
    if (code == "deathmauler1" || code == "deathmauler2" || code == "deathmauler3" || code == "deathmauler4" || code == "deathmauler5" || code == "deathmauler6") return {MonsterKind::DeathMauler, false};
    if (code == "bloodlord1" || code == "bloodlord2" || code == "bloodlord3" || code == "bloodlord4" || code == "bloodlord5" || code == "bloodlord6" || code == "bloodlord7") return {MonsterKind::BloodLord, false};
    if (code == "frozenhorror1" || code == "frozenhorror2" || code == "frozenhorror3" || code == "frozenhorror4" || code == "frozenhorror5") return {MonsterKind::FrozenHorror, false};
    if (code == "reanimatedhorde1" || code == "reanimatedhorde2" || code == "reanimatedhorde3" || code == "reanimatedhorde4" || code == "reanimatedhorde5" || code == "reanimatedhorde6") return {MonsterKind::ReanimatedHorde, false};
    if (code == "megademon1" || code == "megademon2" || code == "megademon3" || code == "megademon4" || code == "megademon5" || code == "megademon6" || code == "venomlord") return {MonsterKind::Megademon, false};
    if (code == "vilemother1" || code == "vilemother2" || code == "vilemother3" || code == "vilemother4" || code == "vilemother5") return {MonsterKind::VileMother, false};
    if (code == "vilechild1" || code == "vilechild2" || code == "vilechild3" || code == "vilechild4" || code == "vilechild5" ||
        code == "painworm1" || code == "painworm2" || code == "painworm3" || code == "painworm4" || code == "painworm5") return {MonsterKind::VileDog, false};
    if (code == "andariel") return {MonsterKind::Andariel, false};
    if (code == "smith") return {MonsterKind::Smith, false};
    if (code == "hephasto") return {MonsterKind::Smith, false};
    if (code == "griswold") return {MonsterKind::Griswold, false};
    if (code == "bloodraven") return {MonsterKind::BloodRaven, false};
    if (code == "necroskeleton") return {MonsterKind::NecroSkeleton, false};
    if (code == "fallen1" || code == "fallen2" || code == "fallen3" || code == "fallen4" ||
        code == "fallen5" || code == "fallen6" || code == "fallen7" || code == "fallen8")
        return {MonsterKind::Fallen, false};
    if (code == "zombie1" || code == "zombie2" || code == "zombie3" || code == "zombie4" ||
        code == "zombie5")
        return {MonsterKind::Zombie, false};
    if (code == "skeleton1" || code == "skeleton2" || code == "skeleton3" ||
        code == "skeleton4" || code == "skeleton5" || code == "skeleton6" || code == "skeleton7" || code == "skeleton8")
        return {MonsterKind::Skeleton, false};
    if (code == "corruptrogue1" || code == "corruptrogue2" || code == "corruptrogue3" ||
        code == "corruptrogue4" || code == "corruptrogue5" || code == "corruptrogue6")
        return {MonsterKind::CorruptRogue, false};
    if (code == "brute1" || code == "brute2" || code == "brute3" || code == "brute4" ||
        code == "brute5" || code == "snowyeti1" || code == "snowyeti2" || code == "snowyeti3" || code == "snowyeti4")
        return {MonsterKind::Brute, false};
    if (code == "goatman1" || code == "goatman2" || code == "goatman3" || code == "goatman4" ||
        code == "goatman5" || code == "goatman6" || code == "goatman7" || code == "goatman8" || code == "goatman9" || code == "goatman10")
        return {MonsterKind::Goatman, false};
    if (code == "quillrat1" || code == "quillrat2" || code == "quillrat3" ||
        code == "quillrat4" || code == "quillrat5" || code == "quillrat6" || code == "quillrat7" || code == "quillrat8")
        return {MonsterKind::QuillRat, false};
    if (code == "wraith1" || code == "wraith2" || code == "wraith3" || code == "wraith4" ||
        code == "wraith5" || code == "wraith6" || code == "wraith7" || code == "wraith8" || code == "wraith9")
        return {MonsterKind::Wraith, false};
    if (code == "cr_lancer1" || code == "cr_lancer2" || code == "cr_lancer3" || code == "cr_lancer4" ||
        code == "cr_lancer5" || code == "cr_lancer6" || code == "cr_lancer7" || code == "cr_lancer8")
        return {MonsterKind::CorruptLancer, false};
    if (code == "cr_archer1" || code == "cr_archer2" || code == "cr_archer3" ||
        code == "cr_archer4" || code == "cr_archer5" || code == "cr_archer6" || code == "cr_archer7" || code == "cr_archer8")
        return {MonsterKind::CorruptArcher, false};
    if (code == "sk_archer1" || code == "sk_archer2" || code == "sk_archer3" || code == "sk_archer4" || code == "sk_archer5" ||
        code == "sk_archer6" || code == "sk_archer7" || code == "sk_archer8" || code == "sk_archer9" || code == "sk_archer10" || code == "sk_archer11")
        return {MonsterKind::SkeletonBow, false};
    if (code == "bighead1" || code == "bighead2" || code == "bighead3" ||
        code == "bighead4" || code == "bighead5" || code == "bighead6" || code == "bighead7" || code == "bighead8" || code == "bighead9" || code == "bighead10")
        return {MonsterKind::Bighead, false};
    if (code == "hellbovine")
        return {MonsterKind::HellBovine, false};
    if (code == "skmage_pois1" || code == "skmage_pois2" || code == "skmage_pois3" ||
        code == "skmage_pois4" || code == "skmage_cold1" || code == "skmage_cold2" ||
        code == "skmage_cold3" || code == "skmage_cold4" || code == "skmage_fire1" ||
        code == "skmage_fire2" || code == "skmage_fire3" || code == "skmage_fire4" ||
        code == "skmage_ltng1" || code == "skmage_ltng2" || code == "skmage_ltng3" ||
        code == "skmage_ltng4" || code == "skmage_fire5" || code == "skmage_fire6" ||
        code == "skmage_ltng5" || code == "skmage_ltng6" || code == "skmage_cold5" ||
        code == "skmage_pois5" || code == "skmage_pois6" || code == "skmage_fire7" ||
        code == "skmage_ltng7" || code == "skmage_cold6" || code == "skmage_pois7")
        return {MonsterKind::SkeletonMage, false};
    if (code == "fetish1" || code == "fetish2" || code == "fetish3" ||
        code == "fetish4" || code == "fetish5" || code == "fetish6" ||
        code == "fetish7" || code == "fetish8" || code == "fetish11")
        return {MonsterKind::Fetish, false};
    if (code == "vampire1" || code == "vampire2" || code == "vampire3" ||
        code == "vampire4" || code == "vampire5" || code == "vampire6" ||
        code == "vampire7" || code == "vampire8" || code == "vampire9")
        return {MonsterKind::Vampire, false};
    if (code == "fallenshaman1" || code == "fallenshaman2" ||
        code == "fallenshaman3" || code == "fallenshaman4" || code == "fallenshaman5" || code == "fallenshaman6" || code == "fallenshaman7" || code == "fallenshaman8")
        return {MonsterKind::FallenShaman, false};
    if (code == "crownest1" || code == "crownest2" || code == "crownest3" || code == "crownest4")
        return {MonsterKind::FoulCrowNest, false};
    if (code == "foulcrow1" || code == "foulcrow2" || code == "foulcrow3" || code == "foulcrow4" ||
        code == "foulcrow5" || code == "foulcrow6" || code == "foulcrow7" || code == "foulcrow8")
        return {MonsterKind::BloodHawk, false};
    if (code == "arach1" || code == "arach2" || code == "arach3" ||
        code == "arach4" || code == "arach5" || code == "arach6" || code == "arach7")
        return {MonsterKind::Arach, false};
    return {MonsterKind::Fallen, true};
}
} // namespace d2x
