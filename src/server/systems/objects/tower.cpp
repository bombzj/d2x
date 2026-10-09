#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/loot/system.hpp"
#include "core/random.hpp"
namespace d2x::server::objects {
void System::towerStep(TickContext tick) {
    if(!ports_.quests.read().objectives.at(questIndex(QuestId::ForgottenTower))) return;
    for(auto &[id,object]:state_.objects) {
        if(!object.rule.towerReward || int(object.area)!=25) continue;
        const auto &rule=*object.rule.towerReward;
        auto [at,fresh]=towerClocks_.try_emplace(id,TowerClock{rule.lifetimeFrames,false,initialRandom(id.value)});
        auto &clock=at->second;if(clock.remaining<=0) continue;
        (void)fresh;
        PlayerId beneficiary{};
        for(const auto &[key,p]:ports_.players.all()) if(p.entered && p.area==object.area && p.persistent.player.hp>0) {beneficiary=key;break;}
        if(!beneficiary.value) continue;
        LootRequest source;source.region=object.area;source.difficulty=ports_.settings.difficulty;
        if(clock.remaining==rule.lifetimeFrames-rule.openingFrame && !clock.opened) {
            source.source=id;
            const auto result=ports_.loot.queue({tick.tick,source,beneficiary,object.position,loot::ObjectSource{object.definition,1,object.rule.chest,{}},{}});
            if(!result) continue;
            object.pending=true;clock.opened=true;
        }
        if(clock.opened && clock.remaining%rule.goldInterval==0) {
            auto random=clock.random;const auto width=unsigned(2*rule.radius+1);
            const Vec candidate=object.position+Vec{float(int(limitedRandom(random,width))-rule.radius),float(int(limitedRandom(random,width))-rule.radius)};
            const auto &grid=ports_.areas.at(object.area).definition.collision;
            const auto position=grid.nearest(candidate,playerMovement);
            if(grid.walkable(position,playerMovement) && (position-candidate).length()<=4) {
                if(ports_.loot.read().pending.size()>=256) continue;
                source.source=ports_.ids.allocate();
                const auto result=ports_.loot.queue({tick.tick,source,beneficiary,position,{},{},true});
                if(!result) continue;
            }
            clock.random=random;
        }
        if(clock.remaining==1) {
            const auto result=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},object.area},{SoundFact{id,2,object.area,0x5c}}});
            if(!result) continue;
        }
        --clock.remaining;
    }
}
}
