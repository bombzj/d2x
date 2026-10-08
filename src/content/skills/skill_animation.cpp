#include "resources/archive.hpp"
#include "skill_animation.hpp"
#include "resources/anim_data.hpp"
#include "content/monsters/monster_animation.hpp"
#include "gameplay/skills/cast_timing.hpp"
#include <cmath>
#include <stdexcept>
#include <cctype>
#include <set>

namespace d2x {
void loadSkillAnimations(SkillCatalog &catalog, const DataTable &skills, const DataTable &weapons, Archives &archives) {
    const AnimDataTable animations(archives.read("data/global/animdata.d2"));
    std::set<std::string> classes{"hth", "1js", "1jt", "1ss", "1st", "ht2"};
    for (size_t row = 0; row < weapons.rows().size(); ++row)
        for (auto field : {"wclass", "2handedwclass"}) {
            std::string value(weapons.value(row, field));
            for (auto &letter : value) letter = char(std::tolower(static_cast<unsigned char>(letter)));
            if (!value.empty()) classes.insert(value);
        }
    std::set<std::string> modes{"sc", "s1", "bl", "a2", "kk"};
    for (const auto &[id, skill] : catalog.skills)
        if (skill.basicAction != BasicSkillAction::None) modes.insert(skill.animationMode);
    for (const auto &tree : catalog.classes)
        for (const auto &weapon : classes)
            for (const auto &mode : modes) {
                if (mode == "s1" && tree.classCode != "pal" && tree.classCode != "ama") continue;
                const auto key = tree.iconToken + mode + weapon;
                auto upper = key;
                for (auto &letter : upper) letter = char(std::toupper(static_cast<unsigned char>(letter)));
                const auto *anim = animations.find(upper);
                if (!anim || anim->frames <= 1 || anim->frames > 144 || anim->speed <= 0 ||
                    !archives.contains("data/global/chars/" + tree.iconToken + "/cof/" + key + ".cof")) continue;
                // Native frame events: 1 = melee/skill, 2 = missile release.
                if (mode == "bl" || (mode == "s1" && tree.classCode == "ama")) {
                    catalog.attackTimings.emplace(key, SkillCatalog::CastTiming{int(anim->frames), anim->speed, 0});
                    continue;
                }
                if (const auto timing=prepareCastAnimationTiming(int(anim->frames),anim->speed,anim->frameFlags)) {
                    auto &target = mode == "sc" ? catalog.castTimings : catalog.attackTimings;
                    target.emplace(key,SkillCatalog::CastTiming{timing->frames,timing->speed,timing->actionFrame});
                }
            }
    const DataTable monsters(archives.read("data/global/excel/monstats.txt"));
    const DataTable extras(archives.read("data/global/excel/monstats2.txt"));
    HydraSpec hydra;
    for(size_t index=0;index<hydra.heads.size();++index) {
        auto &head=hydra.heads[index];head.code="hydra"+std::to_string(index+1);
        bool found=false;
        for(size_t row=0;row<monsters.rows().size();++row) {
            if(monsters.value(row,"Id")!=head.code) continue;
            const auto token=monsters.value(row,"Code");
            std::string_view weapon;
            for(size_t extra=0;extra<extras.rows().size();++extra) if(extras.value(extra,"Id")==monsters.value(row,"MonStatsEx")) {weapon=extras.value(extra,"BaseW");break;}
            const auto attack=loadMonsterAttackTiming(animations,token,1,monsterModeWeapon(archives,token,"a1",weapon),2);
            const auto rise=loadMonsterMotionTiming(animations,token,"s2",monsterModeWeapon(archives,token,"s2",weapon));
            const auto death=loadMonsterMotionTiming(animations,token,"dt",monsterModeWeapon(archives,token,"dt",weapon));
            if(!attack || !rise || !death || monsters.value(row,"AI")!="Hydra" || monsters.value(row,"Sk1mode")!="A1")
                throw std::runtime_error("Missing original Hydra lifecycle animation");
            head.nativeClass=monsters.number(row,"hcIdx").value();
            head.attackTicks=std::max(1,int(std::ceil(attack->duration*25.f)));
            head.impactTick=std::max(1,int(std::ceil(attack->impact*25.f)));
            head.riseTicks=std::max(1,int(std::ceil(rise->duration*25.f)));
            head.deathTicks=std::max(1,int(std::ceil(death->duration*25.f)));
            for(size_t skill=0;skill<skills.rows().size();++skill) if(skills.value(skill,"skill")==monsters.value(row,"Skill1")) head.attackSkill=skills.number(skill,"Id").value_or(-1);
            if(head.attackSkill<=0) throw std::runtime_error("Missing Hydra missile skill identity");
            found=true;break;
        }
        if(!found) throw std::runtime_error("Missing original Hydra head");
    }
    catalog.hydra=std::move(hydra);

}
} // namespace d2x
