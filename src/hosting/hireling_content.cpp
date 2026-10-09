#include "companion_content.hpp"
#include "loot_content.hpp"
#include "monster_actions_content.hpp"
#include "content/classic_data.hpp"
#include "content/monsters/monster_animation.hpp"
#include "gameplay/skills/resolve.hpp"
#include "gameplay/skills/spec.hpp"
#include "resources/anim_data.hpp"
#include "resources/archive.hpp"
#include "content/string_table.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
void preparePendingHirelings(GameHost &host,GameHandle game,Archives &archives,const ClassicData &data,LootContent &cache) {
    const auto lists=host.pendingHirelingLists(game);
    if(!lists.empty()) {
        ClassicStrings strings(archives);
        for(const auto &source:lists) {
            server::companions::PreparedHirelingList prepared;prepared.source=source;
            try {
                auto random=source.seed;
                for(const auto &offer:planHirelingOffers(data.hirelings,source.seller,source.difficulty,source.level,random)) {
                    const auto definition=std::find_if(data.hirelings.begin(),data.hirelings.end(),[&](const auto &entry){return entry.sourceRow==offer.sourceRow;});
                    const int name=strings.index(offer.nameKey);
                    if(definition==data.hirelings.end() || definition->act!=1 || name<0 || name>UINT16_MAX) throw std::runtime_error("Unsupported original Rogue candidate");
                    prepared.offers.push_back({uint16_t(name),{offer.sourceRow,definition->classId,offer.nameKey,offer.level,float(offer.stats.life),offer.stats.experience,offer.seed},offer.stats.price});
                }
            } catch(const std::exception &error) {prepared.deferred=error.what();prepared.offers.clear();}
            host.installHirelingList(game,std::move(prepared));
        }
    }
    const auto pending=host.pendingHirelings(game);if(pending.empty()) return;
    if(!cache.monsters) cache.monsters=std::make_shared<const MonsterCatalog>(archives,data.tables.at("monstats"));
    for(const auto &source:pending) {
        server::companions::PreparedHireling result;result.source=source;
        try {
            auto row=std::find_if(data.hirelings.begin(),data.hirelings.end(),[&](const auto &d){return d.sourceRow==source.record.sourceRow;});
            if(row==data.hirelings.end() || row->act!=1 || row->classId!=source.record.classId) throw std::runtime_error("Only the original Act I Rogue reward is prepared");
            const int identity=row->id;
            for(auto candidate=data.hirelings.begin();candidate!=data.hirelings.end();++candidate)
                if(candidate->id==identity && candidate->level<=source.record.level && candidate->level>row->level) row=candidate;
            const MonsterRecord *monster=nullptr;for(const auto &[id,m]:cache.monsters->monsters()) {(void)id;if(m.index==row->classId) {monster=&m;break;}}
            if(!monster || !monster->walkVelocity) throw std::runtime_error("Missing original Rogue MonStats");
            const auto stats=deriveHirelingStats(*row,source.record.level);
            result.strength=stats.strength;result.dexterity=stats.dexterity;result.weaponType=row->weaponType1;
            result.baseExperience=stats.experience;result.nextExperience=stats.nextExperience;
            auto &rule=result.rule;result.code=monster->id;rule.nativeClass=monster->index;rule.level=source.record.level;rule.difficulty=source.difficulty;
            rule.minimumLife=rule.maximumLife=stats.life;rule.defense=stats.defense;rule.attackRating=stats.attackRating;rule.minimumDamage=stats.damageMin;rule.maximumDamage=stats.damageMax;
            rule.resistances={0,0,stats.resist,stats.resist,stats.resist,stats.resist};rule.nativeVelocity=*monster->walkVelocity;rule.size=monster->collisionSize;
            rule.collision=monster->movementRule();rule.spawnCollision=monster->spawnRule();
            rule.coldEffect=monster->coldEffect.at(size_t(source.difficulty));rule.coldDivisor=data.monsterColdDivisor.at(size_t(source.difficulty));
            const auto *timing=cache.monsters->hirelingAttackTiming(row->classId);
            const auto *death=cache.monsters->hirelingMotion(row->classId,"dt");
            if(!timing || !death) throw std::runtime_error("Missing original Rogue AnimData");
            rule.attackTicks=std::max(1,int(std::ceil(timing->duration*25)));rule.impactTick=std::max(1,int(std::ceil(timing->impact*25)));rule.deathTicks=std::max(1,int(std::ceil(death->duration*25)));
            AnimDataTable animations(archives.read("data/global/animdata.d2"));
            if(!prepareMonsterLifecycle(archives,data,animations,*monster,rule)) throw std::runtime_error("Missing Rogue components/lifecycle");
            // Fn061 chooses a target only at distance <25. Hireable's empty
            // aidist is not an omitted table value or a reason to invent Sight.
            const auto &monstats=data.tables.at("monstats");result.vision=25;
            rule.damageRegen=monstats.number(monster->sourceRow,"DamageRegen").value_or(0);
            result.follow=24;result.warp=100;result.think=5;rule.decisionTicks=5;
            result.defaultChance=row->defaultChance;const int delta=std::max(0,source.record.level-row->level);
            const auto &skills=data.tables.at("skills"),&missiles=data.tables.at("missiles");
            std::map<int,int> ranks;
            for(const auto &s:row->skills) if(source.record.level>=s.requiredLevel) ranks[s.id]=std::clamp(s.level+((delta*s.levelPerLevel)>>5),0,32);
            const auto prepareAttack=[&](int id,int rank)->server::MonsterAttackRule {
                size_t skill=0;while(skill<skills.rows().size() && skills.number(skill,"Id")!=id) ++skill;
                if(skill==skills.rows().size()) throw std::runtime_error("Missing Rogue skill row");
                std::string missile(skills.value(skill,"srvmissile"));if(missile.empty()) missile=skills.value(skill,"srvmissilea");
                size_t m=0;while(m<missiles.rows().size() && missiles.value(m,"Missile")!=missile) ++m;
                if(m==missiles.rows().size()) throw std::runtime_error("Missing Rogue missile row");
                auto projectile=prepareMonsterMissile(data,missiles.number(m,"Id").value(),rank);
                if(!projectile) throw std::runtime_error("Unsupported Rogue projectile");
                std::optional<SkillCastSpec> weaponSkill;
                if(const auto *s=data.skills.find(id);s && s->spell) {
                    const auto cast=resolveSkill(*s->spell,{rank,ranks});
                    if(cast.weapon) weaponSkill=cast;
                    else if(cast.minimumDamage>0 || cast.maximumDamage>0) {
                        projectile->elementalMinimum=int(cast.minimumDamage*256.f);projectile->elementalMaximum=int(cast.maximumDamage*256.f);
                        projectile->coldFrames=uint64_t(std::max(0.f,cast.coldDuration)*25.f);
                    }
                }
                server::MonsterAttackRule attack{stats.damageMin,stats.damageMax,stats.attackRating,rule.attackTicks,rule.impactTick,{}};
                attack.missile=*projectile;attack.rank=uint8_t(rank);attack.weaponSkill=std::move(weaponSkill);return attack;
            };
            const auto defaultName=monstats.value(monster->sourceRow,"Skill1");size_t defaultRow=0;
            while(defaultRow<skills.rows().size() && skills.value(defaultRow,"skill")!=defaultName) ++defaultRow;
            if(defaultRow==skills.rows().size()) throw std::runtime_error("Missing original RogueMissile");
            const int defaultId=skills.number(defaultRow,"Id").value();rule.skillIds[0]=uint16_t(defaultId);rule.skillActions.emplace(uint16_t(defaultId),prepareAttack(defaultId,1));
            for(const auto &s:row->skills) {
                if(!ranks.contains(s.id) || ranks.at(s.id)<=0) continue;
                const int rank=ranks.at(s.id);server::companions::HirelingAction action{s.id,rank,s.chance+delta*s.chancePerLevel/4,s.mode,{}};
                if(const auto *entry=data.skills.find(s.id);entry && entry->spell && entry->spell->amazonMagic) {
                    action.magic=resolveSkill(*entry->spell,{rank,ranks});action.magic->manaCost=0;
                } else rule.skillActions.emplace(uint16_t(s.id),prepareAttack(s.id,rank));
                result.actions.push_back(std::move(action));
            }
        } catch(const std::exception &error) {result.deferred=error.what();}
        host.installHireling(game,std::move(result));
    }
}
}
