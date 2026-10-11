#include "table_values.hpp"
#include "barbarian_data.hpp"
#include "content/classic_data.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/barbarian_passive_spec.hpp"
#include "resources/archive.hpp"
#include "content/skills/missile_effects.hpp"
#include "gameplay/skills/corpse_search_spec.hpp"
#include "gameplay/skills/leap_spec.hpp"
#include "gameplay/skills/grim_ward_spec.hpp"
#include <stdexcept>
#include <string>

namespace d2x {
namespace {
size_t named(const DataTable &table, std::string_view name) {
    return findSkillTableRow(table,"skill",name,SkillIdentityPolicy::AllowEmpty);
}
int required(const DataTable &table, size_t row, std::string_view column) {
    return skillTableNumber(table,row,column);
}
void formula(const DataTable &table, size_t row, std::string_view column, std::string_view expected) {
    requireSkillTableText(table,row,column,expected,SkillTextPolicy::Formula);
}
size_t missileNamed(const DataTable &table,std::string_view name) {
    return findSkillTableRow(table,"Missile",name,SkillIdentityPolicy::AllowEmpty);
}
SkillSpec base(const DataTable &table, size_t row) {
    SkillSpec spec;
    spec.sourceId = required(table, row, "Id");
    spec.mana = required(table, row, "mana");
    spec.minimumMana = required(table, row, "minmana");
    spec.manaPerLevel = required(table, row, "lvlmana");
    spec.manaShift = required(table, row, "manashift");
    spec.hitClass = table.number(row, "HitClass").value_or(0);
    return spec;
}
void mastery(ClassicData &data, const DataTable &skills, std::string_view name, bool thrown=false) {
    const auto row = named(skills, name);
    formula(skills, row, "passivecalc1", "ln12");
    formula(skills, row, "passivecalc2", "ln34");
    formula(skills, row, "passivecalc3", "dm56");
    formula(skills, row, "passivestat1", thrown?"passive_mastery_throw_th":"passive_mastery_melee_th");
    formula(skills, row, "passivestat2", thrown?"passive_mastery_throw_dmg":"passive_mastery_melee_dmg");
    formula(skills, row, "passivestat3", thrown?"passive_mastery_throw_crit":"passive_mastery_melee_crit");
    BarbarianPassiveSpec spec;
    spec.thrown=thrown;
    spec.weaponType = skills.value(row, "passiveitype");
    if (spec.weaponType.empty()) throw std::runtime_error("Missing mastery weapon type");
    for (size_t i = 0; i < spec.parameters.size(); ++i)
        spec.parameters[i] = required(skills, row, "Param" + std::to_string(i + 1));
    data.skills.skills.at(required(skills, row, "Id")).passiveContribution.barbarian =
        std::make_shared<const BarbarianPassiveSpec>(std::move(spec));
}
void linearPassive(ClassicData &data,const DataTable &skills,std::string_view name,BarbarianPassiveSpec::Kind kind,std::string_view stat) {
    const auto row=named(skills,name);formula(skills,row,"passivestat1",stat);formula(skills,row,"passivecalc1","ln12");
    BarbarianPassiveSpec spec;spec.kind=kind;spec.parameters[0]=required(skills,row,"Param1");spec.parameters[1]=required(skills,row,"Param2");
    data.skills.skills.at(required(skills,row,"Id")).passiveContribution.barbarian=std::make_shared<const BarbarianPassiveSpec>(std::move(spec));
}
}
void loadBarbarianSkills(ClassicData &data, Archives &archives) {
    (void)archives;
    const auto &skills = data.tables.at("skills");
    const auto row = named(skills, "Bash");
    if (required(skills, row, "srvstfunc") != 32 || required(skills, row, "srvdofunc") != 2 ||
        required(skills, row, "SrcDam") != 128 || required(skills, row, "ResultFlags") != 8)
        throw std::runtime_error("Unsupported original Bash program");
    formula(skills, row, "calc1", "ln12+skill('Stun'.blvl)*par8");
    formula(skills, row, "calc2", "ln34");
    formula(skills, row, "ToHitCalc", "15+lvl*5+skill('Concentrate'.blvl)*par7");
    auto spec = base(skills, row);
    spec.effect = SkillBehavior::WeaponProjectile;
    spec.weapon = WeaponSkillSpec{};
    auto &weapon = *spec.weapon;
    weapon.requiredType = skills.value(row, "itypea1");
    weapon.damagePercent = required(skills, row, "Param1");
    weapon.damagePerLevel = required(skills, row, "Param2");
    weapon.physicalFlat = required(skills, row, "Param3");
    weapon.physicalFlatPerLevel = required(skills, row, "Param4");
    // ToHitCalc's 15 + lvl*5 constants are native formula constants.
    weapon.attackRating = 20;
    weapon.attackRatingPerLevel = 5;
    weapon.knockback = true;
    weapon.damageSynergies.emplace(required(skills, named(skills, "Stun"), "Id"), required(skills, row, "Param8"));
    weapon.attackRatingSynergies.emplace(required(skills, named(skills, "Concentrate"), "Id"), required(skills, row, "Param7"));
    data.skills.skills.at(spec.sourceId).spell = std::make_shared<const SkillSpec>(std::move(spec));
    mastery(data, skills, "Sword Mastery");
    mastery(data, skills, "Axe Mastery");
    mastery(data, skills, "Mace Mastery");
    const auto howlRow = named(skills, "Howl");
    formula(skills, howlRow, "calc1", "par1 * (lvl-1)");
    auto howl = base(skills, howlRow);
    howl.effect = SkillBehavior::Howl;
    const auto &missiles = data.tables.at("missiles");
    size_t missileRow = 0;
    while (missileRow < missiles.rows().size() && missiles.value(missileRow, "Missile") != skills.value(howlRow, "srvmissilea")) ++missileRow;
    if (missileRow == missiles.rows().size() || required(missiles, missileRow, "pSrvHitFunc") != 17)
        throw std::runtime_error("Unsupported original Howl missile");
    const auto resource = loadProjectileResource(missiles, missileRow, archives);
    howl.missileId = resource.id; howl.missileArt = resource.art; howl.missileLifetime = resource.lifetime;
    howl.missileVelocity = float(required(missiles, missileRow, "Vel"));
    howl.missileVelocityPerLevel = required(skills, howlRow, "Param1");
    // Nova's Calc1 is (lvl-1), whereas missile VelLev normally multiplies lvl.
    howl.missileVelocity -= float(howl.missileVelocityPerLevel);
    howl.missileAcceleration = required(missiles, missileRow, "Accel");
    howl.missileMaxVelocity = required(missiles, missileRow, "MaxVel");
    CurseSpec fear;
    fear.state = data.states.at(std::string(skills.value(howlRow, "auratargetstate"))).definition;
    fear.targetFilter = 2; fear.ai = CurseAi::Terror;
    fear.singleTarget = true; fear.skipActive = true; fear.ignoreDifficultyDivisor = true;
    fear.targetLevelBonus = required(skills, howlRow, "Param2");
    fear.frames = required(skills, howlRow, "Param5"); fear.framesPerLevel = required(skills, howlRow, "Param6");
    howl.curse = std::move(fear);
    data.skills.skills.at(howl.sourceId).spell = std::make_shared<const SkillSpec>(std::move(howl));
    const auto potionRow=named(skills,"Find Potion");
    formula(skills,potionRow,"calc1","dm12");
    auto potion=base(skills,potionRow);potion.effect=SkillBehavior::FindPotion;
    CorpseSearchSpec search;
    search.minimumChance=required(skills,potionRow,"Param1");search.maximumChance=required(skills,potionRow,"Param2");
    search.manaPotionChance=required(skills,potionRow,"Param3");search.rejuvenationChance=required(skills,potionRow,"Param4");
    search.noSelectState=data.states.at("corpse_noselect").definition.id;
    for(const auto &group:search.potions) for(const auto *code:group)
        if(!data.items.find(code)) throw std::runtime_error("Missing original Find Potion item");
    potion.corpseSearch=std::make_shared<const CorpseSearchSpec>(std::move(search));
    data.skills.skills.at(potion.sourceId).spell=std::make_shared<const SkillSpec>(std::move(potion));
    const auto leapRow=named(skills,"Leap");
    formula(skills,leapRow,"aurarangecalc","dm12");formula(skills,leapRow,"calc1","ln34");
    auto jump=base(skills,leapRow);jump.effect=SkillBehavior::Leap;
    LeapSpec leap;leap.rangeMinimum=required(skills,leapRow,"Param1");leap.rangeMaximum=required(skills,leapRow,"Param2");
    leap.knockbackRadius=required(skills,leapRow,"Param3");leap.knockbackPerLevel=required(skills,leapRow,"Param4");
    leap.moving=data.states.at("skill_move").definition;
    jump.leap=std::make_shared<const LeapSpec>(std::move(leap));
    data.skills.skills.at(jump.sourceId).spell=std::make_shared<const SkillSpec>(std::move(jump));
    const auto swingRow=named(skills,"Double Swing");
    formula(skills,swingRow,"calc1","skill('Bash'.blvl)*par8");formula(skills,swingRow,"calc3","par5");
    auto swing=base(skills,swingRow);swing.effect=SkillBehavior::WeaponProjectile;swing.weapon=WeaponSkillSpec{};
    auto &doubleWeapon=*swing.weapon;doubleWeapon.requiredType=skills.value(swingRow,"itypea1");
    doubleWeapon.dualWield=true;doubleWeapon.sequence=required(skills,swingRow,"seqnum");
    doubleWeapon.attackRateBonus=required(skills,swingRow,"Param5");
    doubleWeapon.attackRating=required(skills,swingRow,"ToHit");doubleWeapon.attackRatingPerLevel=required(skills,swingRow,"LevToHit");
    doubleWeapon.damageSynergies.emplace(required(skills,named(skills,"Bash"),"Id"),required(skills,swingRow,"Param8"));
    data.skills.skills.at(swing.sourceId).spell=std::make_shared<const SkillSpec>(std::move(swing));
    mastery(data,skills,"Pole Arm Mastery");
    mastery(data,skills,"Throwing Mastery",true);
    mastery(data,skills,"Spear Mastery");
    const auto tauntRow=named(skills,"Taunt");
    formula(skills,tauntRow,"aurastat1","item_tohit_percent");formula(skills,tauntRow,"aurastatcalc1","ln12");
    formula(skills,tauntRow,"aurastat2","damagepercent");formula(skills,tauntRow,"aurastatcalc2","ln34");
    auto taunt=base(skills,tauntRow);taunt.effect=SkillBehavior::Taunt;
    CurseSpec challenge;challenge.state=data.states.at(std::string(skills.value(tauntRow,"auratargetstate"))).definition;
    challenge.targetFilter=required(skills,tauntRow,"aurafilter");challenge.singleTarget=true;challenge.ai=CurseAi::Taunt;challenge.permanent=true;
    challenge.modifiers.combat.attackRatingPercent=required(skills,tauntRow,"Param1");challenge.attackRatingPerLevel=required(skills,tauntRow,"Param2");
    challenge.modifiers.combat.damagePercent=required(skills,tauntRow,"Param3");challenge.damagePerLevel=required(skills,tauntRow,"Param4");
    taunt.curse=std::move(challenge);
    data.skills.skills.at(taunt.sourceId).spell=std::make_shared<const SkillSpec>(std::move(taunt));
    const auto shoutRow=named(skills,"Shout");
    formula(skills,shoutRow,"aurastat1","skill_armor_percent");formula(skills,shoutRow,"aurastatcalc1","ln12");
    formula(skills,shoutRow,"auralencalc","ln34+(skill('Battle Orders'.blvl)+skill('Battle Command'.blvl))*par8");
    auto shout=base(skills,shoutRow);shout.effect=SkillBehavior::Shout;shout.warcry=true;
    shout.state=data.states.at(std::string(skills.value(shoutRow,"aurastate"))).definition;
    for(const int index:{0,1,2,3,7}) shout.armorParameters[size_t(index)]=required(skills,shoutRow,"Param"+std::to_string(index+1));
    for(const auto name:{"Battle Orders","Battle Command"}) shout.armorSynergySkills.push_back(required(skills,named(skills,name),"Id"));
    size_t shoutMissile=0;
    while(shoutMissile<missiles.rows().size() && missiles.value(shoutMissile,"Missile")!=skills.value(shoutRow,"srvmissilea")) ++shoutMissile;
    if(shoutMissile==missiles.rows().size() || required(missiles,shoutMissile,"pSrvHitFunc")!=18) throw std::runtime_error("Unsupported original Shout missile");
    const auto shoutArt=loadProjectileResource(missiles,shoutMissile,archives);
    shout.missileId=shoutArt.id;shout.missileArt=shoutArt.art;shout.missileLifetime=shoutArt.lifetime;
    shout.missileVelocity=float(required(missiles,shoutMissile,"Vel"));shout.missileAcceleration=required(missiles,shoutMissile,"Accel");shout.missileMaxVelocity=required(missiles,shoutMissile,"MaxVel");
    data.skills.skills.at(shout.sourceId).spell=std::make_shared<const SkillSpec>(std::move(shout));
    const auto stunRow=named(skills,"Stun");
    formula(skills,stunRow,"calc1","skill('Bash'.blvl)*par8");formula(skills,stunRow,"ToHitCalc","10+lvl*5+skill('Concentrate'.blvl)*par7");
    formula(skills,stunRow,"ELenSymPerCalc","skill('War Cry'.blvl)*par6");
    auto stun=base(skills,stunRow);stun.effect=SkillBehavior::WeaponProjectile;stun.weapon=WeaponSkillSpec{};
    auto &stunWeapon=*stun.weapon;stunWeapon.requiredType=skills.value(stunRow,"itypea1");
    stunWeapon.attackRating=15;stunWeapon.attackRatingPerLevel=5;
    stunWeapon.damageSynergies.emplace(required(skills,named(skills,"Bash"),"Id"),required(skills,stunRow,"Param8"));
    stunWeapon.attackRatingSynergies.emplace(required(skills,named(skills,"Concentrate"),"Id"),required(skills,stunRow,"Param7"));
    stunWeapon.stunFrames=required(skills,stunRow,"ELen");stunWeapon.stunCurve=std::array<int,3>{};
    for(size_t i=0;i<3;++i) stunWeapon.stunCurve->at(i)=required(skills,stunRow,"ELevLen"+std::to_string(i+1));
    stunWeapon.stunSynergies.emplace(required(skills,named(skills,"War Cry"),"Id"),required(skills,stunRow,"Param6"));
    data.skills.skills.at(stun.sourceId).spell=std::make_shared<const SkillSpec>(std::move(stun));
    const auto throwRow=named(skills,"Double Throw");formula(skills,throwRow,"calc1","skill('Double Swing'.blvl)*par8");
    auto throwing=base(skills,throwRow);throwing.effect=SkillBehavior::WeaponProjectile;throwing.weapon=WeaponSkillSpec{};
    auto &doubleThrow=*throwing.weapon;doubleThrow.requiredType=skills.value(throwRow,"itypea1");doubleThrow.dualWield=true;doubleThrow.thrown=true;
    doubleThrow.sequence=required(skills,throwRow,"seqnum");doubleThrow.attackRating=required(skills,throwRow,"ToHit");doubleThrow.attackRatingPerLevel=required(skills,throwRow,"LevToHit");
    doubleThrow.damageSynergies.emplace(required(skills,named(skills,"Double Swing"),"Id"),required(skills,throwRow,"Param8"));
    data.skills.skills.at(throwing.sourceId).spell=std::make_shared<const SkillSpec>(std::move(throwing));
    linearPassive(data,skills,"Increased Stamina",BarbarianPassiveSpec::Kind::Stamina,"skill_passive_staminapercent");
    const auto itemRow=named(skills,"Find Item");formula(skills,itemRow,"calc1","dm12");
    auto finding=base(skills,itemRow);finding.effect=SkillBehavior::FindItem;
    CorpseSearchSpec itemSearch;itemSearch.potion=false;itemSearch.minimumChance=required(skills,itemRow,"Param1");itemSearch.maximumChance=required(skills,itemRow,"Param2");
    itemSearch.noSelectState=data.states.at("corpse_noselect").definition.id;
    finding.corpseSearch=std::make_shared<const CorpseSearchSpec>(std::move(itemSearch));
    data.skills.skills.at(finding.sourceId).spell=std::make_shared<const SkillSpec>(std::move(finding));
    const auto attackRow=named(skills,"Leap Attack");formula(skills,attackRow,"calc1","ln34+skill('Leap'.blvl)*par8");
    auto jumpingAttack=base(skills,attackRow);jumpingAttack.effect=SkillBehavior::Leap;LeapSpec leapAttack;
    leapAttack.attack=true;leapAttack.moving=data.states.at("skill_move").definition;
    jumpingAttack.leap=std::make_shared<const LeapSpec>(leapAttack);jumpingAttack.weapon=WeaponSkillSpec{};
    auto &leapWeapon=*jumpingAttack.weapon;leapWeapon.requiredType=skills.value(attackRow,"itypea1");leapWeapon.knockback=true;leapWeapon.interruptible=false;
    leapWeapon.clearTargetStun=true;
    leapWeapon.attackRating=required(skills,attackRow,"ToHit");leapWeapon.attackRatingPerLevel=required(skills,attackRow,"LevToHit");
    leapWeapon.damagePercent=required(skills,attackRow,"Param3");leapWeapon.damagePerLevel=required(skills,attackRow,"Param4");
    leapWeapon.damageSynergies.emplace(required(skills,named(skills,"Leap"),"Id"),required(skills,attackRow,"Param8"));
    leapWeapon.sequence=required(skills,attackRow,"seqnum");
    data.skills.skills.at(jumpingAttack.sourceId).spell=std::make_shared<const SkillSpec>(std::move(jumpingAttack));
    const auto concentrateRow=named(skills,"Concentrate");
    formula(skills,concentrateRow,"calc1","ln12+skill('Bash'.blvl)*par8+skill('Battle Orders'.blvl)*par7");
    formula(skills,concentrateRow,"calc4","skill('Berserk'.blvl)");formula(skills,concentrateRow,"aurastatcalc1","ln34");
    auto concentrate=base(skills,concentrateRow);concentrate.effect=SkillBehavior::WeaponProjectile;concentrate.weapon=WeaponSkillSpec{};
    auto &concentrated=*concentrate.weapon;concentrated.requiredType=skills.value(concentrateRow,"itypea1");concentrated.interruptible=false;
    concentrated.attackRating=required(skills,concentrateRow,"ToHit");concentrated.attackRatingPerLevel=required(skills,concentrateRow,"LevToHit");
    concentrated.damagePercent=required(skills,concentrateRow,"Param1");concentrated.damagePerLevel=required(skills,concentrateRow,"Param2");
    concentrated.damageSynergies.emplace(required(skills,named(skills,"Bash"),"Id"),required(skills,concentrateRow,"Param8"));
    concentrated.damageSynergies.emplace(required(skills,named(skills,"Battle Orders"),"Id"),required(skills,concentrateRow,"Param7"));
    concentrated.magicConversionSynergies.emplace(required(skills,named(skills,"Berserk"),"Id"),1);
    concentrated.attackState=data.states.at(std::string(skills.value(concentrateRow,"aurastate"))).definition;
    concentrated.attackDefense=required(skills,concentrateRow,"Param3");concentrated.attackDefensePerLevel=required(skills,concentrateRow,"Param4");
    data.skills.skills.at(concentrate.sourceId).spell=std::make_shared<const SkillSpec>(std::move(concentrate));
    linearPassive(data,skills,"Iron Skin",BarbarianPassiveSpec::Kind::Defense,"skill_armor_percent");
    const auto cryRow=named(skills,"Battle Cry");
    formula(skills,cryRow,"auralencalc","ln12");formula(skills,cryRow,"aurastatcalc1","ln34");formula(skills,cryRow,"aurastatcalc2","ln56");
    auto cry=base(skills,cryRow);cry.effect=SkillBehavior::BattleCry;
    CurseSpec weakened;weakened.state=data.states.at(std::string(skills.value(cryRow,"auratargetstate"))).definition;
    weakened.targetFilter=42883;weakened.singleTarget=true;
    weakened.frames=required(skills,cryRow,"Param1");weakened.framesPerLevel=required(skills,cryRow,"Param2");
    weakened.modifiers.combat.defensePercent=required(skills,cryRow,"Param3");weakened.defensePerLevel=required(skills,cryRow,"Param4");
    weakened.modifiers.combat.damagePercent=required(skills,cryRow,"Param5");weakened.damagePerLevel=required(skills,cryRow,"Param6");cry.curse=std::move(weakened);
    size_t cryMissile=0;while(cryMissile<missiles.rows().size() && missiles.value(cryMissile,"Missile")!=skills.value(cryRow,"srvmissilea")) ++cryMissile;
    if(cryMissile==missiles.rows().size() || required(missiles,cryMissile,"pSrvHitFunc")!=21) throw std::runtime_error("Unsupported original Battle Cry missile");
    const auto cryArt=loadProjectileResource(missiles,cryMissile,archives);cry.missileId=cryArt.id;cry.missileArt=cryArt.art;cry.missileLifetime=cryArt.lifetime;
    cry.missileVelocity=float(required(missiles,cryMissile,"Vel"));cry.missileAcceleration=required(missiles,cryMissile,"Accel");cry.missileMaxVelocity=required(missiles,cryMissile,"MaxVel");
    data.skills.skills.at(cry.sourceId).spell=std::make_shared<const SkillSpec>(std::move(cry));
    const auto frenzyRow=named(skills,"Frenzy");
    formula(skills,frenzyRow,"calc1","ln12+(skill('Double Swing'.blvl) + skill('Taunt'.blvl))*par8");
    formula(skills,frenzyRow,"calc4","skill('Berserk'.blvl)");formula(skills,frenzyRow,"aurastatcalc1","dm34");formula(skills,frenzyRow,"aurastatcalc2","dm56");
    auto frenzy=base(skills,frenzyRow);frenzy.effect=SkillBehavior::WeaponProjectile;frenzy.weapon=WeaponSkillSpec{};
    auto &furious=*frenzy.weapon;furious.requiredType=skills.value(frenzyRow,"itypea1");furious.dualWield=true;furious.interruptible=false;
    furious.sequence=required(skills,frenzyRow,"seqnum");furious.frenzy=true;furious.frenzyState=data.states.at(std::string(skills.value(frenzyRow,"aurastate"))).definition;
    for(size_t i=0;i<4;++i) furious.frenzyParameters[i]=required(skills,frenzyRow,"Param"+std::to_string(i+3));
    furious.frenzyFrames=required(skills,frenzyRow,"Param7");furious.damagePercent=required(skills,frenzyRow,"Param1");furious.damagePerLevel=required(skills,frenzyRow,"Param2");
    furious.attackRating=required(skills,frenzyRow,"ToHit");furious.attackRatingPerLevel=required(skills,frenzyRow,"LevToHit");
    for(const auto name:{"Double Swing","Taunt"}) furious.damageSynergies.emplace(required(skills,named(skills,name),"Id"),required(skills,frenzyRow,"Param8"));
    furious.magicConversionSynergies.emplace(required(skills,named(skills,"Berserk"),"Id"),1);
    data.skills.skills.at(frenzy.sourceId).spell=std::make_shared<const SkillSpec>(std::move(frenzy));
    const auto speedRow=named(skills,"Increased Speed");formula(skills,speedRow,"passivecalc1","dm12");
    BarbarianPassiveSpec speed;speed.kind=BarbarianPassiveSpec::Kind::Speed;speed.parameters[0]=required(skills,speedRow,"Param1");speed.parameters[1]=required(skills,speedRow,"Param2");
    data.skills.skills.at(required(skills,speedRow,"Id")).passiveContribution.barbarian=std::make_shared<const BarbarianPassiveSpec>(std::move(speed));
    const auto ordersRow=named(skills,"Battle Orders");formula(skills,ordersRow,"auralencalc","ln12+(skill('Shout'.blvl)+skill('Battle Command'.blvl))*par8");
    auto orders=base(skills,ordersRow);orders.effect=SkillBehavior::BattleOrders;orders.warcry=true;
    orders.state=data.states.at(std::string(skills.value(ordersRow,"aurastate"))).definition;
    for(const int index:{0,1,2,3,7}) orders.armorParameters[size_t(index)]=required(skills,ordersRow,"Param"+std::to_string(index+1));
    for(const auto name:{"Shout","Battle Command"}) orders.armorSynergySkills.push_back(required(skills,named(skills,name),"Id"));
    size_t ordersMissile=0;while(ordersMissile<missiles.rows().size() && missiles.value(ordersMissile,"Missile")!=skills.value(ordersRow,"srvmissilea")) ++ordersMissile;
    if(ordersMissile==missiles.rows().size() || required(missiles,ordersMissile,"pSrvHitFunc")!=18) throw std::runtime_error("Unsupported original Battle Orders missile");
    const auto ordersArt=loadProjectileResource(missiles,ordersMissile,archives);orders.missileId=ordersArt.id;orders.missileArt=ordersArt.art;orders.missileLifetime=ordersArt.lifetime;
    orders.missileVelocity=float(required(missiles,ordersMissile,"Vel"));orders.missileAcceleration=required(missiles,ordersMissile,"Accel");orders.missileMaxVelocity=required(missiles,ordersMissile,"MaxVel");
    data.skills.skills.at(orders.sourceId).spell=std::make_shared<const SkillSpec>(std::move(orders));
    const auto wardRow=named(skills,"Grim Ward");formula(skills,wardRow,"aurarangecalc","ln12");formula(skills,wardRow,"calc1","ln34");formula(skills,wardRow,"auralencalc","par6");
    auto ward=base(skills,wardRow);ward.effect=SkillBehavior::GrimWard;GrimWardSpec totem;
    totem.radius=required(skills,wardRow,"Param1");totem.radiusPerLevel=required(skills,wardRow,"Param2");
    totem.duration=required(skills,wardRow,"Param3");totem.durationPerLevel=required(skills,wardRow,"Param4");
    totem.noSelectState=data.states.at("corpse_noselect").definition.id;totem.noDrawState=data.states.at("corpse_nodraw").definition.id;
    const std::array<std::string_view,3> columns{"srvmissileb","srvmissilea","srvmissilec"};
    for(size_t i=0;i<3;++i) {
        const auto start=missileNamed(missiles,skills.value(wardRow,columns[i]));
        if(required(missiles,start,"pSrvHitFunc")!=26) throw std::runtime_error("Unsupported Grim Ward start");
        const auto active=missileNamed(missiles,missiles.value(start,"HitSubMissile1"));
        if(required(missiles,active,"pSrvDoFunc")!=14 || required(missiles,active,"Param2")!=30) throw std::runtime_error("Unsupported Grim Ward pulse");
        const auto stop=missileNamed(missiles,missiles.value(active,"CltHitSubMissile1"));
        const auto startArt=loadProjectileResource(missiles,start,archives),activeArt=loadProjectileResource(missiles,active,archives),stopArt=loadProjectileResource(missiles,stop,archives);
        totem.forms[i]={startArt.id,required(missiles,start,"Range"),activeArt.id,stopArt.id,required(missiles,stop,"Range"),required(missiles,active,"Param1")};
    }
    CurseSpec dread;dread.state=data.states.at(std::string(skills.value(wardRow,"auratargetstate"))).definition;dread.targetFilter=2;
    dread.singleTarget=true;dread.ai=CurseAi::Terror;dread.ignoreDifficultyDivisor=true;dread.frames=required(skills,wardRow,"Param6");
    ward.curse=std::move(dread);ward.grimWard=std::make_shared<const GrimWardSpec>(std::move(totem));
    data.skills.skills.at(ward.sourceId).spell=std::make_shared<const SkillSpec>(std::move(ward));
    const auto whirlwindRow=named(skills,"Whirlwind");formula(skills,whirlwindRow,"calc1","ln12");
    auto whirlwind=base(skills,whirlwindRow);whirlwind.effect=SkillBehavior::WeaponProjectile;whirlwind.weapon=WeaponSkillSpec{};
    auto &spinning=*whirlwind.weapon;spinning.requiredType=skills.value(whirlwindRow,"itypea1");spinning.whirlwind=true;spinning.interruptible=false;
    spinning.sequence=required(skills,whirlwindRow,"seqnum");spinning.attackRatingPerLevel=required(skills,whirlwindRow,"LevToHit");spinning.attackRating=spinning.attackRatingPerLevel;
    spinning.damagePercent=required(skills,whirlwindRow,"Param1");spinning.damagePerLevel=required(skills,whirlwindRow,"Param2");
    spinning.attackState=data.states.at(std::string(skills.value(whirlwindRow,"aurastate"))).definition;spinning.movingState=data.states.at("skill_move").definition;
    data.skills.skills.at(whirlwind.sourceId).spell=std::make_shared<const SkillSpec>(std::move(whirlwind));
    const auto berserkRow=named(skills,"Berserk");formula(skills,berserkRow,"calc1","ln12+(skill('Howl'.blvl) + skill('Shout'.blvl))*par8");
    formula(skills,berserkRow,"calc2","par4-min(((110*lvl)/(lvl+6)*(par4-par3)/100),(par4-par3))");formula(skills,berserkRow,"calc4","100");formula(skills,berserkRow,"aurastatcalc2","-100");
    auto berserk=base(skills,berserkRow);berserk.effect=SkillBehavior::WeaponProjectile;berserk.weapon=WeaponSkillSpec{};
    auto &raging=*berserk.weapon;raging.requiredType=skills.value(berserkRow,"itypea1");raging.interruptible=false;raging.magicConversionPercent=100;
    raging.damagePercent=required(skills,berserkRow,"Param1");raging.damagePerLevel=required(skills,berserkRow,"Param2");
    raging.attackRating=required(skills,berserkRow,"ToHit");raging.attackRatingPerLevel=required(skills,berserkRow,"LevToHit");
    for(const auto name:{"Howl","Shout"}) raging.damageSynergies.emplace(required(skills,named(skills,name),"Id"),required(skills,berserkRow,"Param8"));
    raging.attackState=data.states.at(std::string(skills.value(berserkRow,"aurastate"))).definition;
    raging.attackDurationMinimum=required(skills,berserkRow,"Param3");raging.attackDurationMaximum=required(skills,berserkRow,"Param4");
    raging.attackPhysicalResist=required(skills,berserkRow,"Param5");raging.armorOverridePercent=-100;
    data.skills.skills.at(berserk.sourceId).spell=std::make_shared<const SkillSpec>(std::move(berserk));
    const auto resistanceRow=named(skills,"Natural Resistance");
    const std::array<std::string_view,4> resistanceStats{"fireresist","lightresist","coldresist","poisonresist"};
    for(size_t i=0;i<4;++i) {formula(skills,resistanceRow,"passivestat"+std::to_string(i+1),resistanceStats[i]);formula(skills,resistanceRow,"passivecalc"+std::to_string(i+1),"dm12");}
    BarbarianPassiveSpec resistance;resistance.kind=BarbarianPassiveSpec::Kind::Resistance;
    resistance.parameters[0]=required(skills,resistanceRow,"Param1");resistance.parameters[1]=required(skills,resistanceRow,"Param2");
    data.skills.skills.at(required(skills,resistanceRow,"Id")).passiveContribution.barbarian=std::make_shared<const BarbarianPassiveSpec>(std::move(resistance));
    const auto warRow=named(skills,"War Cry");formula(skills,warRow,"DmgSymPerCalc","(skill('Howl'.blvl)+skill('Taunt'.blvl)+skill('Battle Cry'.blvl))*par8");
    auto war=base(skills,warRow);war.effect=SkillBehavior::WarCry;war.minimumDamage=required(skills,warRow,"MinDam");war.maximumDamage=required(skills,warRow,"MaxDam");war.hitShift=required(skills,warRow,"HitShift");
    for(size_t i=0;i<5;++i) {war.minimumPerLevel[i]=required(skills,warRow,"MinLevDam"+std::to_string(i+1));war.maximumPerLevel[i]=required(skills,warRow,"MaxLevDam"+std::to_string(i+1));}
    war.synergyPercent=required(skills,warRow,"Param8");for(const auto name:{"Howl","Taunt","Battle Cry"}) war.synergySkills.push_back(required(skills,named(skills,name),"Id"));
    war.stunFrames=required(skills,warRow,"Param1");war.stunFramesPerLevel=required(skills,warRow,"Param2");war.hitClass=0x60;
    const auto warMissile=missileNamed(missiles,skills.value(warRow,"srvmissilea"));
    if(required(missiles,warMissile,"pSrvDmgFunc")!=7) throw std::runtime_error("Unsupported War Cry damage");
    const auto warArt=loadProjectileResource(missiles,warMissile,archives);war.missileId=warArt.id;war.missileArt=warArt.art;war.missileLifetime=warArt.lifetime;
    war.missileVelocity=float(required(missiles,warMissile,"Vel"));war.missileAcceleration=required(missiles,warMissile,"Accel");war.missileMaxVelocity=required(missiles,warMissile,"MaxVel");
    data.skills.skills.at(war.sourceId).spell=std::make_shared<const SkillSpec>(std::move(war));
    const auto commandRow=named(skills,"Battle Command");formula(skills,commandRow,"auralencalc","ln12+(skill('Shout'.blvl)+skill('Battle Orders'.blvl))*par8");formula(skills,commandRow,"aurastatcalc1","1");
    auto command=base(skills,commandRow);command.effect=SkillBehavior::BattleCommand;command.warcry=true;
    command.state=data.states.at(std::string(skills.value(commandRow,"aurastate"))).definition;
    for(const int index:{0,1,7}) command.armorParameters[size_t(index)]=required(skills,commandRow,"Param"+std::to_string(index+1));
    for(const auto name:{"Shout","Battle Orders"}) command.armorSynergySkills.push_back(required(skills,named(skills,name),"Id"));
    const auto commandMissile=missileNamed(missiles,skills.value(commandRow,"srvmissilea"));
    if(required(missiles,commandMissile,"pSrvHitFunc")!=18) throw std::runtime_error("Unsupported Battle Command missile");
    const auto commandArt=loadProjectileResource(missiles,commandMissile,archives);command.missileId=commandArt.id;command.missileArt=commandArt.art;command.missileLifetime=commandArt.lifetime;
    command.missileVelocity=float(required(missiles,commandMissile,"Vel"));command.missileAcceleration=required(missiles,commandMissile,"Accel");command.missileMaxVelocity=required(missiles,commandMissile,"MaxVel");
    data.skills.skills.at(command.sourceId).spell=std::make_shared<const SkillSpec>(std::move(command));
    // Import missile contact cooldowns after each skill's native program is prepared.
    for(auto &[id,record]:data.skills.skills) if(id>=126 && id<=155 && record.spell && record.spell->missileId>=0) {
        auto prepared=std::make_shared<SkillSpec>(*record.spell);
        for(size_t row=0;row<missiles.rows().size();++row) if(missiles.number(row,"Id")==prepared->missileId && missiles.number(row,"NextHit").value_or(0)) prepared->missileNextDelay=missiles.number(row,"NextDelay").value_or(0);
        record.spell=std::move(prepared);
    }
}
}
