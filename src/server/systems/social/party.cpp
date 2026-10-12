#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/travel/system.hpp"
#include <algorithm>
#include <chrono>

namespace d2x::server::social {
namespace {
// PlayerList uses real elapsed time, not the pausable 25 Hz simulation clock.
uint64_t hostileClock() {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
uint16_t membership(const State &state,PlayerId id) {
    const auto found=state.parties.find(id);
    return found==state.parties.end()?UINT16_MAX:found->second;
}
void removeMember(State &state,PlayerId id) {
    const auto group=membership(state,id);
    state.parties.erase(id);
    std::erase_if(state.invitations,[&](const auto &invite){return invite.first==id || invite.second==id;});
    if(group==UINT16_MAX) return;
    const auto count=std::count_if(state.parties.begin(),state.parties.end(),[&](const auto &p){return p.second==group;});
    if(count<2) {
        std::set<PlayerId> remaining;
        for(const auto &[member,party]:state.parties) if(party==group) remaining.insert(member);
        std::erase_if(state.invitations,[&](const auto &p){return remaining.contains(p.first) || remaining.contains(p.second);});
        std::erase_if(state.parties,[&](const auto &p){return p.second==group;});
    }
}
}
uint16_t System::partyId(PlayerId id) const {return membership(state_,id);}
bool System::sameParty(PlayerId first,PlayerId second) const {
    return partyId(first)!=UINT16_MAX && partyId(first)==partyId(second);
}
uint16_t System::flags(PlayerId from,PlayerId to) const {
    const auto found=state_.relations.find({from,to});
    return found==state_.relations.end()?0:found->second.flags;
}
bool System::hostilityTravelDelay(PlayerId id,uint64_t milliseconds) const {
    const auto found=state_.lastHostility.find(id);
    return found!=state_.lastHostility.end() && hostileClock()-found->second<milliseconds;
}
uint8_t System::partyStatus(PlayerId observer,PlayerId target) const {
    if(observer==target || sameParty(observer,target)) return 1;
    if(state_.invitations.contains({observer,target})) return 4;
    if(state_.invitations.contains({target,observer})) return 2;
    return partyId(target)==UINT16_MAX?0:1;
}
DomainResult<> System::party(const ActorContext &actor,const Request &request) {
    const auto *player=ports_.players.find(actor.player);
    const auto *target=request.target?ports_.players.find(*request.target):nullptr;
    if(!target || !target->entered || (request.action==Action::LeaveParty?target!=player:target==player))
        return {DomainStatus::InvalidRequest,{}};
    auto next=state_;
    std::vector<DomainFact> notices;
    const auto invite=std::pair{actor.player,target->player};
    switch(request.action) {
    case Action::Invite:
        // PlayerList field_C must be zero: no reciprocal invitation or party.
        if(partyStatus(actor.player,target->player)!=0) return {DomainStatus::Conflict,{}};
        next.invitations.insert(invite);
        notices.emplace_back(PartyNoticeFact{target->player,actor.actor,5});
        break;
    case Action::CancelInvite:
        if(!next.invitations.erase(invite)) return {DomainStatus::Conflict,{}};
        notices.emplace_back(PartyNoticeFact{target->player,actor.actor,6});
        break;
    case Action::Accept: {
        if(partyId(actor.player)!=UINT16_MAX || !next.invitations.contains({target->player,actor.player}))
            return {DomainStatus::Conflict,{}};
        auto group=partyId(target->player);
        if(group==UINT16_MAX) {
            group=next.nextParty;
            for(unsigned attempts=0;attempts<32765;++attempts) {
                if(group<3 || group>32767) group=3;
                if(std::none_of(next.parties.begin(),next.parties.end(),[&](const auto &p){return p.second==group;})) break;
                ++group;
            }
            next.nextParty=group==32767?3:uint16_t(group+1);
            next.parties[target->player]=group;
            // A newly grouped inviter cannot accept an unrelated invitation.
            std::erase_if(next.invitations,[&](const auto &p){return p.second==target->player;});
        }
        for(const auto &[id,party]:next.parties) if(party==group)
            notices.emplace_back(PartyNoticeFact{id,actor.actor,7});
        next.parties[actor.player]=group;
        std::erase_if(next.invitations,[&](const auto &p){return p.second==actor.player;});
        notices.emplace_back(PartyNoticeFact{actor.player,target->actor,8});
        break;
    }
    case Action::LeaveParty: {
        const auto group=partyId(actor.player);
        if(group==UINT16_MAX) return {DomainStatus::Conflict,{}};
        for(const auto &[id,party]:next.parties) if(party==group && id!=actor.player)
            notices.emplace_back(PartyNoticeFact{id,actor.actor,9});
        removeMember(next,actor.player);
        break;
    }
    default:return {DomainStatus::InvalidRequest,{}};
    }
    const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Game,{},actor.area},std::move(notices)});
    if(!sent) return {sent.status,{}};
    std::swap(state_,next);
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::disconnect(PlayerId id,uint64_t tick) {
    const auto *player=ports_.players.find(id);
    auto next=state_;
    std::vector<DomainFact> notices;
    if(player && player->entered) notices.emplace_back(PlayerMessageFact{3,player->persistent.player.name});
    const auto group=partyId(id);
    if(player && group!=UINT16_MAX) for(const auto &[member,party]:next.parties)
        if(party==group && member!=id) notices.emplace_back(PartyNoticeFact{member,player->actor,9});
    removeMember(next,id);
    std::erase_if(next.relations,[&](const auto &p){return p.first.first==id || p.first.second==id;});
    next.hover.erase(id);
    next.lastHostility.erase(id);
    if(!notices.empty()) {
        const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Game,{},player->area},std::move(notices)});
        if(!sent) return {sent.status,{}};
    }
    std::swap(state_,next);
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::hostility(const ActorContext &actor,const Request &request) {
    const auto *player=ports_.players.find(actor.player);
    const auto *target=request.target?ports_.players.find(*request.target):nullptr;
    const auto *area=ports_.areas.find(actor.area);
    if(!player || !target || !target->entered || target==player || !area || !area->definition.town ||
       area->generation!=actor.areaGeneration || player->persistent.player.level<9 || target->persistent.player.level<9)
        return {DomainStatus::InvalidRequest,{}};
    const auto now=hostileClock();
    if(now>UINT64_MAX-60000 || std::any_of(state_.relations.begin(),state_.relations.end(),[](const auto &entry){return entry.second.revision==UINT64_MAX;}))
        return {DomainStatus::Capacity,{}};
    auto next=state_;
    std::vector<DomainFact> facts;
    const auto publishRelation=[&](PlayerId from,PlayerId to,bool enabled) {
        auto &relation=next.relations[{from,to}];
        relation.flags=enabled?uint16_t(relation.flags|8):uint16_t(relation.flags&~8);
        relation.hostile=enabled;++relation.revision;
        const auto *a=ports_.players.find(from),*b=ports_.players.find(to);
        facts.emplace_back(ChatRelationFact{a->actor,b->actor,relation.flags,next.relations[{to,from}].flags,
            uint16_t(a->persistent.player.level),uint16_t(b->persistent.player.level)});
    };
    const bool current=(flags(actor.player,target->player)&8)!=0;
    if(current==request.enabled) {
        publishRelation(actor.player,target->player,current);
        publishRelation(target->player,actor.player,(flags(target->player,actor.player)&8)!=0);
    } else if(!request.enabled) {
        publishRelation(actor.player,target->player,false);
        facts.emplace_back(PartyNoticeFact{target->player,actor.actor,4});
    } else {
        if(next.relations[{actor.player,target->player}].hostileAfter>now) {
            const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},
                {SoundFact{actor.actor,0,actor.area,0x13},PlayerMessageFact{16,{}}}});
            return sent?DomainResult<>{DomainStatus::Applied,std::monostate{}}:DomainResult<>{sent.status,{}};
        }
        next.relations[{actor.player,target->player}].hostileAfter=now+60000;
        next.lastHostility[actor.player]=now;
        const auto group=partyId(target->player);
        for(const auto &[id,peer]:ports_.players.all()) {
            if(!peer.entered || id==actor.player || peer.persistent.player.level<9 ||
               (group==UINT16_MAX?id!=target->player:partyId(id)!=group)) continue;
            publishRelation(actor.player,id,true);publishRelation(id,actor.player,true);
            facts.emplace_back(PartyNoticeFact{id,actor.actor,3});
        }
        if(group!=UINT16_MAX && group==partyId(actor.player)) {
            for(const auto &[id,party]:next.parties) if(party==group && id!=actor.player)
                facts.emplace_back(PartyNoticeFact{id,actor.actor,9});
            removeMember(next,actor.player);
        }
    }
    const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Game,{},actor.area},std::move(facts)});
    if(!sent) return {sent.status,{}};
    std::swap(state_,next);
    if(request.enabled && !current) ports_.travel.closePlayerPortal(actor.player);
    return {DomainStatus::Applied,std::monostate{}};
}
}
