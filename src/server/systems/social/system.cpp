#include "system.hpp"
#include "server/player_store.hpp"
#include "server/systems/trade/system.hpp"
#include <algorithm>
namespace d2x::server::social {
DomainResult<> System::execute(const ActorContext &actor, const Request &request) {
    const auto *player = ports_.players.find(actor.player);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area) return {DomainStatus::InvalidActor, {}};
    if(request.action==Action::Invite || request.action==Action::CancelInvite || request.action==Action::Accept || request.action==Action::LeaveParty)
        return party(actor,request);
    const auto flags=[&](PlayerId from,PlayerId to)->uint16_t {
        const auto relation=state_.relations.find({from,to}); return relation==state_.relations.end()?0:relation->second.flags;
    };
    const auto blocked=[&](PlayerId receiver) {return bool((flags(receiver,actor.player)&4) || (flags(actor.player,receiver)&2));};
    if(request.action==Action::Ignore || request.action==Action::Squelch) {
        const auto *peer=request.target?ports_.players.find(*request.target):nullptr;
        if(!peer || !peer->entered || peer==player) return {DomainStatus::InvalidRequest,{}};
        auto relations=state_.relations;
        std::erase_if(relations,[&](const auto &entry){return !ports_.players.find(entry.first.first) || !ports_.players.find(entry.first.second);});
        auto &relation=relations[{actor.player,peer->player}]; const uint16_t bit=request.action==Action::Ignore?2:4;
        if(relation.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
        relation.flags=request.enabled?uint16_t(relation.flags|bit):uint16_t(relation.flags&~bit);++relation.revision;
        const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Game,{},actor.area},
            {ChatRelationFact{actor.actor,peer->actor,relation.flags,flags(peer->player,actor.player),uint16_t(player->persistent.player.level),uint16_t(peer->persistent.player.level)}}});
        if(!sent) return {sent.status,{}};
        state_.relations.swap(relations); return {DomainStatus::Applied,std::monostate{}};
    }
    if(request.action!=Action::Chat) return {};
    const auto ascii=[](std::string_view value) { return std::all_of(value.begin(),value.end(),[](unsigned char c){return c>=32 && c<127;}); };
    if (request.target || request.language || request.text.empty() || request.text.size() > 255 || !ascii(request.text) ||
        request.receiver.size()>15 || !ascii(request.receiver) || (request.overhead && !request.receiver.empty()) ||
        std::all_of(request.text.begin(),request.text.end(),[](char c){return c==' ';})) return {DomainStatus::InvalidRequest, {}};
    if(request.overhead) {
        if(state_.nextHover==UINT64_MAX) return {DomainStatus::Capacity,{}};
        auto next=state_.hover;
        const auto length=std::min<size_t>(request.text.size(),254);
        next[actor.player]={request.text.substr(0,length),actor.tick+125+8*length,state_.nextHover};
        // Replication emits the current hover to visible recipients and late arrivals.
        const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Game,{},actor.area},
            {ChatFact{actor.actor,{},request.text,{},5,0,0,0}}});
        if(!sent) return {sent.status,{}};
        state_.hover.swap(next);++state_.nextHover;return {DomainStatus::Applied,std::monostate{}};
    }
    ChatFact fact{{},player->persistent.player.name,request.text,{}};
    fact.nameColor=uint8_t(player->persistent.player.level);
    std::vector<DomainFact> facts;
    if (!request.receiver.empty()) {
        const auto equal=[](std::string_view a,std::string_view b) {
            const auto lower=[](unsigned char c){return c>='A' && c<='Z'?c+('a'-'A'):c;};
            return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[&](auto x,auto y){return lower(x)==lower(y);});
        };
        for(const auto &[id,peer]:ports_.players.all()) if(peer.entered && equal(peer.persistent.player.name,request.receiver)) fact.recipients.push_back(id);
        if(fact.recipients.empty()) facts.emplace_back(PlayerMessageFact{4,request.receiver});
        else if(blocked(fact.recipients.front())) facts.emplace_back(PlayerMessageFact{13,request.receiver});
        else {
            fact.type=2; facts.emplace_back(fact);
            fact.type=6; fact.name=request.receiver; fact.nameColor=0; fact.recipients={actor.player}; facts.emplace_back(std::move(fact));
        }
    } else {
        const auto *exchange=ports_.trade.find(actor.player);
        if(request.overhead) {fact.actor=actor.actor;fact.type=5;fact.unitType=0;fact.name.clear();fact.nameColor=0;}
        for(const auto &[id,peer]:ports_.players.all()) if(peer.entered) {
            if(blocked(id)) continue;
            if(request.overhead && peer.area!=actor.area) continue;
            const auto *peerExchange=ports_.trade.find(id);
            // Native PlrMsg limits broadcasts to the two trading participants.
            if(!request.overhead && ((exchange && exchange!=peerExchange) || (peerExchange && exchange!=peerExchange))) continue;
            fact.recipients.push_back(id);
        }
        facts.emplace_back(std::move(fact));
    }
    const auto audience=std::holds_alternative<PlayerMessageFact>(facts.front())?Audience{AudienceKind::Player,actor.player,actor.area}:Audience{AudienceKind::Game,{},actor.area};
    const auto published = ports_.events.publish({0, actor.tick, {}, audience, std::move(facts)});
    return published ? DomainResult<>{DomainStatus::Applied, std::monostate{}} : DomainResult<>{published.status, {}};
}
std::optional<PlayerSnapshot::Hover> System::overhead(PlayerId id,uint64_t tick) const {
    const auto hover=state_.hover.find(id); const auto *source=ports_.players.find(id);
    if(hover==state_.hover.end() || !source || !source->entered || hover->second.expires<=tick) return {};
    PlayerSnapshot::Hover result{hover->second.text,hover->second.revision,{}};
    for(const auto &[recipient,player]:ports_.players.all()) if(player.entered) {
        const auto senderRelation=state_.relations.find({id,recipient}), receiverRelation=state_.relations.find({recipient,id});
        if((senderRelation!=state_.relations.end() && (senderRelation->second.flags&2)) ||
           (receiverRelation!=state_.relations.end() && (receiverRelation->second.flags&4))) continue;
        result.recipients.insert(recipient);
    }
    return result;
}
void System::step(TickContext tick) {
    std::erase_if(state_.hover,[&](const auto &entry){const auto *p=ports_.players.find(entry.first);return !p || !p->entered || entry.second.expires<=tick.tick;});
    std::erase_if(state_.relations,[&](const auto &entry){return !ports_.players.find(entry.first.first) || !ports_.players.find(entry.first.second);});
}
}
