#include "gameplay/skills/spec.hpp"
#include "presentation/scene_assets.hpp"
#include "content/classic_data.hpp"
#include "content/skills/description_data.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <string_view>

namespace d2x {
void SceneAssets::loadProjectileDefinitions(const ClassicData &content) {
    // Client callbacks need display inputs, not the self-hosted combat catalog.
    if (!content.nativeFormulas && !content.skills.descriptions) return;
    for (const auto &[code, item] : content.items.entries())
        if (item.base.projectile) weaponMissiles.emplace(code, item.base.projectile->id);
    missileDefinitions_ = std::make_unique<DataTable>(content.tables.at("missiles"));
    const auto &table = *missileDefinitions_;
    std::map<std::string_view, int> names;
    for (size_t row = 0; row < table.rows().size(); ++row)
        if (const auto id = table.number(row, "Id")) {
            missileRows_.emplace(*id, row); names.emplace(table.value(row, "Missile"), *id);
        }
    const auto linked = [&](size_t row, std::string_view field) {
        const auto found = names.find(table.value(row, field));
        return found == names.end() ? -1 : found->second;
    };
    std::set<int> hammerMissiles;
    if (content.profile=="remote-display-txt-v1") {
        const auto &skills=content.tables.at("skills");
        for (size_t row=0;row<skills.rows().size();++row)
            if (skills.number(row,"cltdofunc")==35)
                if (const auto missile=names.find(skills.value(row,"cltmissilea"));missile!=names.end()) hammerMissiles.insert(missile->second);
    }
    for (const auto &[id, row] : missileRows_) {
        const auto callback = [&](std::string_view field) {
            if (content.nativeFormulas) return content.nativeFormulas->missiles.at(id).number(field);
            auto name = std::string(table.value(row, "Missile"));
            for (auto &letter : name) letter = char(std::tolower(static_cast<unsigned char>(letter)));
            return content.skills.descriptions->missiles.at(name).number(field).value_or(0);
        };
        auto number = [&](std::string_view field) { return table.number(row, field).value_or(0); };
        ProjectileVisual visual;
        visual.fps = float(table.number(row, "animrate").value_or(1024)) * 25.f / 1024.f;
        visual.loop = number("LoopAnim") != 0; visual.frames = number("AnimLen");
        if (number("SubLoop")) { visual.loopStart = number("SubStart"); visual.loopEnd = number("SubStop"); }
        visual.lifetime = float(number("Range")) / 25.f;
        visual.initSteps = number("InitSteps"); visual.trans = number("Trans");
        visual.lightRadius = number("Light"); visual.lightFlicker = number("Flicker") != 0;
        visual.lightColor = {uint8_t(number("Red")), uint8_t(number("Green")), uint8_t(number("Blue")), 255};
        if (visual.lifetime <= 0 && number("Explosion") && !visual.loop && visual.fps > 0)
            visual.lifetime = float(visual.frames) / visual.fps;
        if (visual.trans) translucentProjectiles.insert(id);
        projectileVisuals.emplace(id, visual);
        if (!content.missileCollisions.contains(id)) continue;
        ClientMissileProgram program;
        if (content.profile=="remote-display-txt-v1") program.rules=ClientMissileProgram::Rules::Pd2Cn;
        program.animation={number("animrate"),number("AnimLen"),number("SubStart"),number("SubStop"),number("LoopAnim")!=0,number("SubLoop")!=0};
        program.function = callback("pcltdofunc");
        if(content.nativeFormulas) {
            const auto skill=content.nativeFormulas->missiles.at(id).reference("skill");
            if(!skill.empty()) program.linkedSkill=content.nativeFormulas->skillIds.at(std::string(skill));
        }
        else {
            auto skillName=std::string(table.value(row,"Skill"));
            for(auto &letter:skillName) letter=char(std::tolower(static_cast<unsigned char>(letter)));
            const auto skill=content.skills.descriptions->skillIds.find(skillName);
            if(skill!=content.skills.descriptions->skillIds.end()) program.linkedSkill=skill->second;
        }
        // Skill CltDo35 supplies the hammer path even without host rules.
        program.blessedHammer=hammerMissiles.contains(id);
        program.localBlood = number("LocalBlood") != 0;
        program.clientSend=number("ClientSend")!=0;
        program.activationDelay=number("Activate");
        program.deathFlame=table.value(row,"Missile")=="andycolumnfire";
        const auto serverHitFunction=table.value(row,"pSrvHitFunc");
        program.holyBolt=!serverHitFunction.starts_with('*') && number("pSrvHitFunc")==7;
        program.poisonVelocity={number("Param1"),number("Param2")};
        if(number("SubLoop")) program.loopFrames=number("SubStop")-number("SubStart");
        if(program.function==7) program.guidedRadius=number("Param2");
        for(const auto &[skillId,record]:content.skills.skills) {
            (void)skillId;
            if(!record.spell || record.spell->missileId!=id) continue;
            if(record.spell->weapon && record.spell->weapon->bow && record.spell->weapon->bow->immolation) program.immolationRadius=record.spell->weapon->bow->fireRadius;
            if(record.spell->weapon && record.spell->weapon->spear && record.spell->weapon->spear->kind==SpearSkillSpec::Kind::Fury) {
                const auto &f=*record.spell->weapon->spear;program.targetBurst=MissileTargetBurst{f.countBase,f.countPerLevel,f.targetRadius};
            }
            if(record.spell->blizzard) program.blizzard=record.spell->blizzard;
            if(record.spell->effect==SkillBehavior::BlessedHammer) program.blessedHammer=true;
            if(record.spell->heaven) {
                const auto &h=*record.spell->heaven;program.targetBurst=MissileTargetBurst{h.limit,h.limitPerLevel,h.radius};
            }
            if(record.spell->effect==SkillBehavior::ChainLightning) {program.chain=record.spell->arc;program.chainCountDivisor=5;}
            if(record.spell->weapon && record.spell->weapon->spear && record.spell->weapon->spear->kind==SpearSkillSpec::Kind::Strike) program.chain=record.spell->arc;
        }
        // Retail *16 is WORD 64952, outside the callback table; it is not 16.
        program.hitFunction = callback("pclthitfunc");
        program.velocity = number("Vel"); program.velocityPerLevel = number("VelLev");
        program.acceleration = number("Accel"); program.maximumVelocity = number("MaxVel") << 8;
        program.frames = number("Range"); program.framesPerLevel = number("LevRange");
        program.sourceTownFrames = number("CltSrcTown");
        if (const auto collision = content.missileCollisions.find(id); collision != content.missileCollisions.end())
            program.collision = collision->second;
        // CN CltDo72 -> D2Common ordinal11164 sets path +0x50 to 0x100:
        // the move-test collision mask, independently of the hammer path.
        if (program.rules==ClientMissileProgram::Rules::Pd2Cn && program.function==72) program.collision.mask=0x100;
        program.collide = number("ClientCol") != 0;program.returnFire=number("ReturnFire")!=0;
        program.canSlow=number("CanSlow")!=0;
        // CltDo2 creates throwing potions with the original distance-limited flag.
        program.groundThrow=number("CollideType")==6 && (program.hitFunction==2 || program.hitFunction==3 || program.hitFunction==44);
        program.killOnContact = number("CollideKill") != 0;
        program.explodeOnExpiry = number("AlwaysExplode") != 0;
        program.explosion = linked(row, "ExplosionMissile");
        for (size_t i = 0; i < program.parameters.size(); ++i) {
            program.parameters[i] = number("CltParam" + std::to_string(i + 1));
            program.serverParameters[i] = number("Param" + std::to_string(i + 1));
        }
        for (size_t i = 0; i < program.hitParameters.size(); ++i) {
            program.hitParameters[i] = number("cHitPar" + std::to_string(i + 1));
            program.serverHitParameters[i] = number("sHitPar" + std::to_string(i + 1));
            program.children[i] = linked(row, "CltSubMissile" + std::to_string(i + 1));
        }
        for (size_t i = 0; i < program.hitChildren.size(); ++i)
            program.hitChildren[i] = linked(row, "CltHitSubMissile" + std::to_string(i + 1));
        // D2Client BB070 reads Missiles +0x88 (SHitCalc1), then falls back
        // to the parent range. PD2 Blaze supplies a constant fire duration.
        if(program.rules==ClientMissileProgram::Rules::Pd2Cn && program.hitFunction==12)
            program.hitDuration=table.number(row,"SHitCalc1");
        if(program.rules==ClientMissileProgram::Rules::Pd2Cn && program.hitFunction==67)
            program.hitHeight=table.number(row,"CHitCalc1");
        // Retail CltHit46 deliberately reads the server HitSubMissile slot.
        if(program.hitFunction==46) program.hitChildren[0]=linked(row,"HitSubMissile1");
        if(program.hitFunction==36) {
            program.hellMeteorRadius=number("Param1");
            // The native callback chooses from this three-class table.
            constexpr std::array<std::string_view,3> fire{"firewall","firesmall","firemedium"};
            for(size_t index=0;index<fire.size();++index) {
                const auto found=names.find(fire[index]);
                program.hitChildren[index]=found==names.end()?-1:found->second;
            }
        }
        if(program.hitFunction==50 || program.hitFunction==52) {
            const auto rocks=names.find("moltenboulder-flyingrocks");
            program.boulderRocks=rocks==names.end()?-1:rocks->second;
        }
        if(program.hitFunction==48) {
            constexpr std::array<std::string_view,3> cold{"freezingarrowexp1","freezingarrowexp2","catapult cold explosion"};
            for(size_t index=0;index<cold.size();++index) {
                const auto found=names.find(cold[index]);
                program.hitChildren[index]=found==names.end()?-1:found->second;
            }
        }
        if (const auto child=missileRows_.find(program.children[0]); child!=missileRows_.end())
            program.childServerSent=table.number(child->second,"ClientSend").value_or(0)!=0;
        if (program.function==9 && program.hitFunction==18) {
            const auto light=missileRows_.find(program.hitChildren[1]);
            meteorVisuals.emplace(id,MeteorVisual{light==missileRows_.end()?0:table.number(light->second,"Range").value_or(0),
                program.hitChildren[0],program.hitParameters[0],program.hitChildren[1],program.hitChildren[2],program.hitChildren[3],
                program.hitParameters[1],program.hitParameters[2]});
        }
        if(program.function==55) if(const auto cloud=names.find("ancient death cloud");cloud!=names.end()) program.children[0]=cloud->second;
        clientMissilePrograms.emplace(id, program);
        if (program.hitFunction == 3 && (program.hitChildren[1] >= 0 || program.hitChildren[2] >= 0))
            projectileImpactVariants.emplace(id, std::array{program.hitChildren[1], program.hitChildren[2]});
        if (program.hitFunction == 14 && program.hitChildren[1] >= 0)
            projectileFreezingEjecta.emplace(id, program.hitChildren[1]);
        if(program.function==1 && program.hitFunction==19 && program.parameters[0]>0 && program.parameters[1]>0)
            blizzardFalls.emplace(id,BlizzardVisual{program.parameters[0],program.parameters[1],program.hitChildren[0],
                program.hitChildren[0]>=0?table.number(missileRows_.at(program.hitChildren[0]),"Range").value_or(0):0});
    }
}
} // namespace d2x
