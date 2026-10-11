#include "system.hpp"
#include "detail.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/social/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/inventory/planning.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/progression/system.hpp"
#include "gameplay/quest/catalog.hpp"
#include "gameplay/quest/search_for_cain.hpp"
#include "gameplay/quest/acts/act_two_state.hpp"
#include "gameplay/quest/acts/act_three_state.hpp"
#include "gameplay/quest/acts/act_four_state.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include <algorithm>
#include <memory>
#include <variant>

namespace d2x::server::quests {
bool System::pendingGoals(PlayerId player) const {
    const auto *p=ports_.players.find(player);if(!p) return false;
    return std::any_of(state_.goals.begin(),state_.goals.end(),[&](const auto &entry) {
        const auto &goal=entry.second;
        return goal.eligible.contains(player) && goal.actors.at(player)==p->actor &&
            (p->persistent.player.quests.at(size_t(ports_.settings.difficulty))[questIndex(entry.first)].stage<goal.stage ||
             goal.consumeForgeItems || goal.consumeKhalimItems);
    });
}
bool System::credited(PlayerId player,QuestId quest) const {
    const auto *p=ports_.players.find(player);
    const auto actor=state_.creditedActors.find(player);
    return p && actor!=state_.creditedActors.end() && actor->second==p->actor &&
        state_.credited.contains(player) && state_.credited.at(player).contains(quest);
}
void System::creditPlayer(PlayerId player,QuestId quest) {
    const auto &p=*ports_.players.find(player);
    if(!state_.creditedActors.contains(player) || state_.creditedActors.at(player)!=p.actor) state_.credited[player].clear();
    state_.creditedActors[player]=p.actor;state_.credited[player].insert(quest);
}
bool System::rewardPending(const CharacterRecord &record,QuestId quest) const {
    const auto &q=record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(quest));
    if(q.stage>=questDefinition(quest).completedStage) return true;
    // Personal saved entitlements survive a completed/closed game. These
    // include non-RewardPending bits used by Tyrael, Gidbinn and the Council.
    switch(quest) {
    case QuestId::DenOfEvil: case QuestId::SistersBurialGrounds: case QuestId::RadamentsLair:
    case QuestId::FallenAngel: case QuestId::SiegeOnHarrogath: case QuestId::RescueOnMountArreat:
    case QuestId::BetrayalOfHarrogath: return q.stage>=3;
    case QuestId::SearchForCain: return q.stage>=7 || (q.flags&cainRescuedByRogues);
    case QuestId::ToolsOfTheTrade: case QuestId::PrisonOfIce: case QuestId::GoldenBird: return q.stage>=5;
    case QuestId::SistersToTheSlaughter: return q.stage>=3;
    case QuestId::SevenTombs: return q.stage>=3;
    case QuestId::Summoner: return q.stage>=2;
    case QuestId::TaintedSun: return q.stage>=3;
    case QuestId::HellsForge: case QuestId::BladeOfTheOldReligion: return q.stage>=4;
    case QuestId::BlackenedTemple: return q.stage>=3;
    default: return false;
    }
}
bool System::entitled(PlayerId player,const CharacterRecord &record,QuestId quest) const {
    if(rewardPending(record,quest) || credited(player,quest)) return true;
    const auto *p=ports_.players.find(player);if(!p) return false;
    const auto [begin,end]=state_.goals.equal_range(quest);
    for(auto it=begin;it!=end;++it) {
        const auto &goal=it->second;
        if(!goal.eligible.contains(player) || goal.actors.at(player)!=p->actor) continue;
        if(goal.primaryGoal) return true;
        auto pending=record;
        pending.quests.at(size_t(ports_.settings.difficulty))[questIndex(quest)].stage=goal.stage;
        if(rewardPending(pending,quest)) return true;
    }
    return false;
}
bool System::unavailable(PlayerId player,const CharacterRecord &record,QuestId quest) const {
    if(entitled(player,record,quest)) return false;
    // These native NPC callbacks accept a real carried artifact independently
    // of the game's intro flag. A missed public objective cannot invent it.
    if(const auto *p=ports_.players.find(player)) {
        const auto carried=[&](std::string_view code){return detail::actTwoCarried(*p,code,ports_.settings.difficulty)!=nullptr;};
        const auto &items=p->rules.character->laterQuests;
        if(quest==QuestId::GoldenBird && (carried(items.figurine) || carried(items.bird))) return false;
        if(quest==QuestId::BladeOfTheOldReligion && carried(items.gidbinn)) return false;
        if(quest==QuestId::LamEsensTome && carried(items.tome)) return false;
        if(quest==QuestId::ToolsOfTheTrade && p->persistent.player.level>=8 && carried("hdm")) return false;
    }
    const auto &definition=questDefinition(quest);
    return (state_.initialFlags[definition.nativeSlot]&0x8000) || state_.objectives[questIndex(quest)];
}
std::optional<PlayerId> System::killerPlayer(EntityId actor) const {
    if(const auto *player=ports_.players.findActor(actor)) return player->player;
    if(const auto pet=ports_.monsters.read().actors.find(actor);pet!=ports_.monsters.read().actors.end()) return pet->second.owner;
    return {};
}
void System::initialize(const CharacterRecord &creator) {
    // QUESTS_SequenceCycler selects the first player's completed set once.
    // Horadric Staff has bNoSetState and remains available in completed games.
    for(const auto &q:questDefinitions) {
        const auto stage=creator.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(q.id)).stage;
        bool pending=false;
        switch(q.id) {
        case QuestId::DenOfEvil: case QuestId::SistersBurialGrounds: case QuestId::RadamentsLair:
        case QuestId::SistersToTheSlaughter: case QuestId::SiegeOnHarrogath: case QuestId::RescueOnMountArreat:
        case QuestId::BetrayalOfHarrogath: pending=stage>=3;break;
        case QuestId::ToolsOfTheTrade: case QuestId::GoldenBird: case QuestId::PrisonOfIce: pending=stage>=5;break;
        case QuestId::SearchForCain: pending=stage>=7;break;
        case QuestId::TaintedSun: case QuestId::FallenAngel: pending=stage>=3;break;
        case QuestId::HellsForge: pending=stage>=4;break;
        case QuestId::Summoner: pending=stage>=2;break;
        default: break;
        }
        if(q.id!=QuestId::HoradricStaff && (stage>=q.completedStage || pending)) state_.initialFlags[q.nativeSlot]=0x8000;
    }
    const auto &book=creator.quests.at(size_t(ports_.settings.difficulty));
    state_.actOne.cainOffer=(state_.initialFlags[2]&0x8000)!=0;
    state_.actOne.burialOffer=(state_.initialFlags[1]&0x8000)!=0;
    state_.actOne.toolsOffer=(state_.initialFlags[4]&0x8000)!=0;
    state_.actThree.orbSmashed=book[questIndex(QuestId::KhalimsWill)].stage>=4;
    state_.actThree.mephistoSlain=book[questIndex(QuestId::Guardian)].stage>=4;
    state_.actFive.anyaThawed=book[questIndex(QuestId::PrisonOfIce)].stage>=5;
    state_.actFive.ancientsDefeated=book[questIndex(QuestId::RiteOfPassage)].stage>=4;
    if(book[questIndex(QuestId::SearchForCain)].stage>=8) {
        state_.actOne.cainRescued=true;state_.actOne.restoreCairnStones=true;state_.actOne.activatedStones=5;
    }
    const auto sun=book[questIndex(QuestId::TaintedSun)].stage;
    state_.actTwo.dark=sun==1 || sun==2;state_.actTwo.sunScheduled=state_.actTwo.dark;
}
std::array<uint16_t,48> System::gameFlags() const {
    auto flags=state_.initialFlags;
    for(const auto &q:questDefinitions) if(state_.objectives[questIndex(q.id)]) flags[q.nativeSlot]|=0x2000;
    return flags;
}
std::set<PlayerId> System::eligibleParty(QuestId quest,uint32_t stage,RegionId area,std::optional<Vec> source,std::optional<PlayerId> killer) const {
    std::set<PlayerId> direct,result;
    const auto &definition=questDefinition(quest);
    if(state_.initialFlags[definition.nativeSlot]&0x8000) return result;
    for(const auto &[id,p]:ports_.players.all()) {
        if(!p.entered || rewardPending(p.persistent.player,quest) || unavailable(id,p.persistent.player,quest) ||
           p.persistent.player.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(quest)).stage>=stage) continue;
        if(quest==QuestId::BetrayalOfHarrogath && !rewardPending(p.persistent.player,QuestId::PrisonOfIce)) continue;
        if(quest==QuestId::RiteOfPassage && p.persistent.player.level<20*(ports_.settings.difficulty+1)) continue;
        if(quest==QuestId::DenOfEvil) {
            if(!state_.actOne.contributors.contains(id) || state_.actOne.contributors.at(id)!=p.actor) continue;
        } else if(killer!=id && p.area!=area) continue;
        // The original killer callbacks explicitly credit Countess, Andariel,
        // Mephisto and Izual's killer before iterating nearby units.
        if(killer==id) {direct.insert(id);continue;}
        if(source) {
            const auto &layout=ports_.areas.at(area).definition.activation;
            const auto *room=layout.room(p.position),*target=layout.room(*source);
            if(!room || !target) continue;
            const auto near=layout.nearRooms(*room);
            if(room!=target && std::find(near.begin(),near.end(),target)==near.end()) continue;
        }
        direct.insert(id);
    }
    result=direct;
    for(const auto &[id,p]:ports_.players.all()) {
        if(!p.entered || ports_.areas.at(p.area).definition.act!=definition.act ||
            rewardPending(p.persistent.player,quest) || unavailable(id,p.persistent.player,quest) ||
            p.persistent.player.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(quest)).stage>=stage) continue;
        if(quest==QuestId::BetrayalOfHarrogath && !rewardPending(p.persistent.player,QuestId::PrisonOfIce)) continue;
        if(quest==QuestId::RiteOfPassage && p.persistent.player.level<20*(ports_.settings.difficulty+1)) continue;
        if(std::any_of(direct.begin(),direct.end(),[&](PlayerId member){return ports_.social.sameParty(id,member);})) result.insert(id);
    }
    return result;
}
std::set<PlayerId> System::eligibleDelivery(QuestId quest,uint32_t stage,PlayerId courier,bool allInAct) const {
    std::set<PlayerId> result;
    for(const auto &[id,p]:ports_.players.all()) {
        if(!p.entered || (quest!=QuestId::GoldenBird && quest!=QuestId::ToolsOfTheTrade && ports_.areas.at(p.area).definition.act!=questDefinition(quest).act) ||
            p.persistent.player.quests.at(size_t(ports_.settings.difficulty))[questIndex(quest)].stage>=stage) continue;
        if(quest==QuestId::ToolsOfTheTrade && p.persistent.player.level<8) continue;
        if(id==courier || allInAct || ports_.social.sameParty(id,courier)) result.insert(id);
    }
    return result;
}
void System::captureGoal(QuestId quest,uint32_t stage,RegionId area,std::optional<Vec> source,std::optional<PlayerId> killer) {
    if(state_.initialFlags[questDefinition(quest).nativeSlot]&0x8000) return;
    captureGoal(quest,stage,eligibleParty(quest,stage,area,source,killer));
}
void System::captureGoal(QuestId quest,uint32_t stage,std::set<PlayerId> eligible,bool attributeReward,bool consumeForgeItems) {
    if(state_.objectives.at(questIndex(quest))) return;
    Goal goal;goal.stage=stage;goal.eligible=std::move(eligible);goal.attributeReward=attributeReward;goal.consumeForgeItems=consumeForgeItems;
    queueGoal(quest,std::move(goal));
    state_.objectives.at(questIndex(quest))=true;
}
void System::shareProgress(QuestId quest,uint32_t stage,std::set<PlayerId> eligible) {
    Goal goal;goal.stage=stage;goal.eligible=std::move(eligible);goal.primaryGoal=false;
    queueGoal(quest,std::move(goal));
}
void System::queueGoal(QuestId quest,Goal goal) {
    for(const auto id:goal.eligible) {
        const auto &p=*ports_.players.find(id);
        if(goal.primaryGoal) {
            creditPlayer(id,quest);
        }
        goal.actors[id]=p.actor;
        if(quest==QuestId::RiteOfPassage) {
            constexpr std::array<uint64_t,3> rewards{1400000,20000000,40000000};
            const auto &thresholds=p.rules.character->experience;const auto level=size_t(p.persistent.player.level);
            if(level+1<thresholds.size() && p.persistent.player.experience<thresholds.back())
                goal.experience[id]=std::min(rewards.at(size_t(ports_.settings.difficulty)),thresholds[level+1]-thresholds[level]);
        }
    }
    // A later NPC acknowledgement must not overwrite recipients still waiting
    // for an earlier shared stage to commit under output backpressure.
    state_.goals.emplace(quest,std::move(goal));
}
StepStatus System::flushGoals(TickContext tick) {
    bool blocked=false;
    for(auto it=state_.goals.begin();it!=state_.goals.end();) {
        for(auto member=it->second.eligible.begin();member!=it->second.eligible.end();) {
            const auto *p=ports_.players.find(*member);
            // A disconnected actor retained for recovery still owns a frozen
            // award. Settle it before recovery export, without admitting it.
            if(!p || p->actor!=it->second.actors.at(*member)) {member=it->second.eligible.erase(member);continue;}
            auto record=p->persistent.player;
            auto &quest=record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(it->first));
            const bool trading=std::any_of(p->persistent.inventory.containers.begin(),p->persistent.inventory.containers.end(),
                [](const auto &entry){return entry.second.spec.kind==ContainerKind::Trade;});
            const bool consumeItems=it->second.consumeForgeItems || (it->second.consumeKhalimItems && !trading);
            const bool alreadyApplied=quest.stage>=it->second.stage;
            if(alreadyApplied && !consumeItems) {member=it->second.eligible.erase(member);continue;}
            if(!alreadyApplied) {
                quest.stage=it->second.stage;
                if(it->first==QuestId::RiteOfPassage) {
                    if(const auto reward=it->second.experience.find(*member);reward!=it->second.experience.end()) {
                        const auto planned=progression::addExperience(record,p->definition,*p->rules.character,reward->second);
                        if(!planned) {blocked=true;++member;continue;}
                        record=*planned.value;
                    }
                    auto &baal=record.quests.at(size_t(ports_.settings.difficulty))[questIndex(QuestId::EveOfDestruction)];
                    if(!baal.stage && !unavailable(*member,record,QuestId::EveOfDestruction)) baal.stage=1;
                }
                if(it->second.attributeReward) {
                    if(record.unspentAttributes>INT32_MAX-lamTomeAttributeReward) {blocked=true;++member;continue;}
                    record.unspentAttributes+=lamTomeAttributeReward;
                }
                if(it->first==QuestId::RadamentsLair && !(quest.flags&radamentBookUsed)) quest.flags|=radamentBookPending;
                if(it->first==QuestId::ArcaneSanctuary) quest.flags|=arcaneCommentPending;
                if(it->first==QuestId::Guardian) quest.flags|=guardianSpeechPending;
                if(it->first==QuestId::TerrorsEnd) quest.flags|=terrorTyraelPending|terrorCainPending;
                if(it->first==QuestId::RescueOnMountArreat) quest.flags=(quest.flags&~rescueCountMask)|state_.actFive.rescued;
                if(it->first==QuestId::KhalimsWill) {
                    auto &temple=record.quests.at(size_t(ports_.settings.difficulty))[questIndex(QuestId::BlackenedTemple)];
                    if(temple.stage==3) temple.stage=4;
                }
            }
            const auto &area=ports_.areas.at(p->area);
            const ActorContext actor{p->player,p->actor,p->area,area.generation,0,tick.tick};
            DomainResult<> result;
            if(consumeItems) {
                auto rules=std::make_shared<EquipmentRules>(*p->rules.equipment);
                inventory::detail::Draft draft(*p,*p->rules.items,*rules,*p->rules.character);
                const auto &codes=p->rules.character->laterQuests;
                bool valid=true;
                for(auto item=draft.edit.inventory.items.begin();item!=draft.edit.inventory.items.end();) {
                    const auto &value=item->second;
                    const bool consume=it->second.consumeForgeItems?(value.definition==codes.soulstone || value.definition==codes.hammer):
                        value.definition==codes.khalimWill || std::find(codes.khalimParts.begin(),codes.khalimParts.end(),value.definition)!=codes.khalimParts.end();
                    if(!consume || !std::holds_alternative<ContainerLocation>(value.location) ||
                       value.nativeQuestDifficulty<unsigned(ports_.settings.difficulty)) {++item;continue;}
                    if(value.revision==UINT64_MAX) {valid=false;break;}
                    draft.edit.changes.push_back({value.id,value.revision+1,ItemChangeKind::Removed,value.location,{},0});
                    rules->items.erase(value.id);item=draft.edit.inventory.items.erase(item);
                }
                if(!valid) {blocked=true;++member;continue;}
                transactions::InventoryEdit edit{actor,p->inventoryRevision,p->characterRevision,std::move(draft.edit.inventory),std::move(draft.edit.changes),record.weaponSet};
                edit.character=record;edit.equipment=std::move(rules);edit.facts.emplace_back(fact(p->player,record,p->area));
                auto plan=ports_.transactions.prepare(std::move(edit));
                result=plan?ports_.transactions.commit(std::move(*plan.value)):DomainResult<>{plan.status,{}};
            } else {
                const bool levelUp=record.level>p->persistent.player.level;
                result=commit(actor,std::move(record),levelUp);
            }
            if(result) member=it->second.eligible.erase(member);
            else {blocked=true;++member;}
        }
        if(it->second.eligible.empty()) it=state_.goals.erase(it);else ++it;
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
