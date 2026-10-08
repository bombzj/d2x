#include "monster_effects.hpp"
#include <algorithm>
#include <utility>

namespace d2x {
void RemoteMonsterEffects::observe(const OnlineUnit &unit,Definition definition,
    const OnlineCombatEvent &event,float now,float age,float fps) {
    auto &actor=actors_[unit.key];
    if(actor.assignment!=unit.assignmentRevision) {actor={};actor.assignment=unit.assignmentRevision;}
    // Reposition/skill packets can end GH without another Action event.
    // Do not keep treating later non-GH life updates as a recovery callback.
    if(actor.mode==3 && actor.lightning<0 && unit.mode && unit.mode!=3) actor.mode=*unit.mode;
    if(event.kind==OnlineCombatEvent::Kind::Hit && event.packet==0x0C && event.flags==19 && event.life) {
        actor.lightningReady=(*event.life&0x80)!=0;
        // Retail RVA 20BC0: hit callback emits outside GH; 20340 gates it
        // with nLastAnimMode bit 0, populated from the life byte at 4E0A9.
        if(definition.unique && definition.lightning && actor.lightningReady && actor.mode!=3)
            releases_.push_back({unit.key,195});
    }
    if(event.kind!=OnlineCombatEvent::Kind::Action || !event.action) return;
    if(*event.action==6) {
        actor.mode=3;
        if(event.life) actor.lightningReady=(*event.life&0x80)!=0;
        // Retail RVA 20BF0: GH's animation-frame-2 callback.
        actor.lightning=definition.unique && definition.lightning && fps>0?now-age+2.f/fps:-1;
    } else {
        actor.mode=*event.action==8?0:*event.action==9?12:1;
        actor.lightning=-1;
    }
    if(*event.action==8 && definition.unique && definition.cold && fps>0)
        actor.cold=now-age+4.f/fps; // Retail RVA 20C40: death animation frame 4.
    else if(actor.mode!=0 && actor.mode!=12) actor.cold=-1;
}
std::vector<RemoteMonsterEffects::Release> RemoteMonsterEffects::advance(const OnlineWorldView &world,float now) {
    std::erase_if(actors_,[&](const auto &entry){
        const auto unit=world.units.find(entry.first);
        return unit==world.units.end() || unit->second.assignmentRevision!=entry.second.assignment;
    });
    for(auto &[key,actor]:actors_) {
        const auto &unit=world.units.at(key);
        if(actor.mode==3 && unit.mode && unit.mode!=3) {actor.mode=*unit.mode;actor.lightning=-1;}
        if(actor.lightning>=0 && now>=actor.lightning) {
            if(actor.lightningReady) {releases_.push_back({key,195});actor.lightning=-1;}
        }
        if(actor.cold>=0 && now>=actor.cold) {releases_.push_back({key,194});actor.cold=-1;}
    }
    return std::exchange(releases_,{});
}
}
