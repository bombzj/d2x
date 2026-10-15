#include "activation_program.hpp"
#include "cast_spec.hpp"
#include "behavior.hpp"
#include "bone_spec.hpp"

namespace d2x {
SkillActivationProgram skillActivationProgram(const SkillCastSpec &skill) {
    using Program = SkillActivationProgram;
    switch (skill.effect) {
    case SkillBehavior::ItemSkill: return Program::Item;
    case SkillBehavior::Unsummon: return Program::Unsummon;
    case SkillBehavior::Kick: return Program::Kick;
    case SkillBehavior::Assassin: return Program::Assassin;
    default: break;
    }
    if (skill.summon || (skill.bone && skill.bone->barrier)) return Program::AmazonSummon;
    if (skill.amazonMagic) return Program::AmazonMagic;
    switch (skill.effect) {
    case SkillBehavior::Teleport: return Program::Teleport;
    case SkillBehavior::Leap: return Program::Leap;
    case SkillBehavior::Hydra: return Program::Hydra;
    default: break;
    }
    if (skill.warcry) return Program::Warcry;
    if (skill.grimWard) return Program::GrimWard;
    if (skill.appliedEffect) return Program::Effect;
    if (skill.effect == SkillBehavior::Howl || skill.effect == SkillBehavior::BattleCry) return Program::Missile;
    if (skill.curse) return Program::Curse;
    if (skill.bone && skill.bone->corpse) return Program::Corpse;
    if (skill.corpseSearch) return Program::CorpseSearch;
    switch (skill.effect) {
    case SkillBehavior::StaticField: return Program::StaticField;
    case SkillBehavior::Telekinesis: return Program::Telekinesis;
    case SkillBehavior::WeaponProjectile:
    case SkillBehavior::HolyBolt:
    case SkillBehavior::BlessedHammer: case SkillBehavior::FistOfTheHeavens:
    case SkillBehavior::FireBolt: case SkillBehavior::Fireball:
    case SkillBehavior::Teeth:
    case SkillBehavior::BoneSpear: case SkillBehavior::BoneSpirit:
    case SkillBehavior::IceBolt: case SkillBehavior::IceBlast:
    case SkillBehavior::Inferno: case SkillBehavior::ChillingArmor:
    case SkillBehavior::ChargedBolt: case SkillBehavior::FrostNova:
    case SkillBehavior::Nova: case SkillBehavior::PoisonNova: case SkillBehavior::GlacialSpike:
    case SkillBehavior::WarCry:
    case SkillBehavior::FrozenOrb: case SkillBehavior::Blizzard:
    case SkillBehavior::Lightning: case SkillBehavior::ChainLightning:
    case SkillBehavior::FireWall: case SkillBehavior::Blaze:
    case SkillBehavior::Meteor: return Program::Missile;
    default: return Program::Unsupported;
    }
}
}
