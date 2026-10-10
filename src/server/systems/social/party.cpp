#include "system.hpp"
#include "server/player_store.hpp"
#include <algorithm>

namespace d2x::server::social {
namespace {
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
    const auto group=partyId(id);
    if(player && group!=UINT16_MAX) for(const auto &[member,party]:next.parties)
        if(party==group && member!=id) notices.emplace_back(PartyNoticeFact{member,player->actor,9});
    removeMember(next,id);
    std::erase_if(next.relations,[&](const auto &p){return p.first.first==id || p.first.second==id;});
    next.hover.erase(id);
    if(!notices.empty()) {
        const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Game,{},player->area},std::move(notices)});
        if(!sent) return {sent.status,{}};
    }
    std::swap(state_,next);
    return {DomainStatus::Applied,std::monostate{}};
}
}
