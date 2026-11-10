#include "ai_impl.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace d2x {
bool DebugAi::Impl::questReceiveRewards(Context &c,const QuestStep &step) {
    const auto &view=c.session.read();const auto &world=view.world;
    auto codes=step.rewardCodes;
    if(step.stateQuest==QuestId::RescueOnMountArreat) if(const auto flags=questFlags(c,*step.stateQuest))
        codes.resize(std::min(codes.size(),size_t(*flags&0x80?1:*flags&0x40?2:3)));
    std::vector<uint32_t> rewards;
    for(const auto &[id,item]:world.items) {
        if(questState.questRun.rewardBefore.contains(id) ||
            (!onlineItemOwnedBy(item,view.load.playerUnitId) && item.mode!=3)) continue;
        const auto decoded=c.inventory.read().items.find(id);
        if(decoded==c.inventory.read().items.end() || !decoded->second.decoded) continue;
        if(step.rareReward?decoded->second.quality==6:std::find(codes.begin(),codes.end(),item.code)!=codes.end()) rewards.push_back(id);
    }
    for(const auto id:rewards) {
        const auto &item=world.items.at(id);
        if(onlineItemOwnedBy(item,view.load.playerUnitId)) {
            if(item.mode==4) {
                if(questState.questRun.pending && questState.questRun.pending->action!=QuestPending::Action::Place) questState.questRun.pending.reset();
                questStoreCursor(c,id);return false;
            }
            continue;
        }
        if(questState.questRun.pending && questState.questRun.pending->action==QuestPending::Action::Pickup) {checkQuestPending(c);return false;}
        if(questState.questRun.pending && questState.questRun.pending->action==QuestPending::Action::Topic) questState.questRun.pending.reset();
        if(world.npcRequested) {c.control.closeNpc();cancelWalk(c);return false;}
        if(!approachPickup(c,item)) return false;
        OnlineItemCommand pickup;pickup.action=OnlineItemAction::Pickup;pickup.item=id;pickup.itemRevision=item.revision;
        questItemRequest(c,pickup,QuestPending::Action::Pickup,"quest-reward-pickup-submitted");return false;
    }
    const auto ownedReward=[&](uint32_t id){return onlineItemOwnedBy(world.items.at(id),view.load.playerUnitId);};
    const bool complete=step.rareReward?std::any_of(rewards.begin(),rewards.end(),ownedReward):
        std::all_of(codes.begin(),codes.end(),[&](const auto &code) {
            return std::any_of(rewards.begin(),rewards.end(),[&](uint32_t id){return world.items.at(id).code==code && ownedReward(id);});
        });
    if(complete) {note("quest-actual-rewards-observed",step.rareReward?"new class rare":"native rescue rune entitlement");return true;}
    wait("quest-reward-inventory","Waiting for the actually created reward items; no repeated reward topic");checkQuestPending(c);return false;
}
bool DebugAi::Impl::questService(Context &c,const QuestStep &step) {
    const auto &view=c.session.read();const auto &world=view.world;
    const bool socket=step.stateQuest==QuestId::SiegeOnHarrogath;
    const auto flags=questFlags(c,*step.stateQuest);
    const auto transformed=[&](uint32_t id) {
        const auto item=world.items.find(id);const auto decoded=c.inventory.read().items.find(id);
        return item!=world.items.end() && item->second.code==step.item && onlineItemOwnedBy(item->second,view.load.playerUnitId) &&
            decoded!=c.inventory.read().items.end() && decoded->second.decoded &&
            (socket?decoded->second.sockets>0:!decoded->second.personalizedName.empty());
    };
    if(questState.questRun.materialPhase>=2) {
        std::optional<uint32_t> result;
        if(questState.questRun.material && transformed(*questState.questRun.material)) result=questState.questRun.material;
        for(const auto &[id,item]:world.items) if(!result && !questState.questRun.materialBefore.contains(id) && transformed(id)) result=id;
        const auto &request=world.itemRequest;
        if(result && questState.questRun.materialPhase==2 && questState.questRun.pending && request && request->sequence==questState.questRun.pending->sequence &&
            request->state==OnlineItemRequest::State::Updated) {
            questState.questRun.material=result;questState.questRun.materialPhase=3;questState.questRun.pending.reset();note("quest-service-item-observed",socket?"socket count":"personalized name");
        }
        if(result && questState.questRun.materialPhase==3) {
            if(!questStoreCursor(c,*result)) return false;
            questState.questRun.pending.reset();questState.questRun.materialPhase=4;
            QuestPending p;p.action=QuestPending::Action::Service;p.unit=questState.questRun.materialNpc;p.deadline=Clock::now()+std::chrono::seconds(20);
            questState.questRun.pending=p;
        }
        if(questState.questRun.materialPhase==4) {
            if(flags && (*flags&1)) return true;
            refreshQuestNpc(c);wait("quest-service-flags","Stored actual service output; querying the consumed entitlement");checkQuestPending(c);return false;
        }
        wait("quest-service-result","Waiting for the native service and transformed item; no replay");checkQuestPending(c);return false;
    }
    if(!flags || !(*flags&2)) throw std::runtime_error("Quest service requires the real pending reward entitlement");
    if(!questState.questRun.material) {
        for(const auto &[id,item]:world.items) {
            if(item.code!=step.item || item.mode!=0 || item.page!=1 || !item.positionKnown || !onlineItemOwnedBy(item,view.load.playerUnitId)) continue;
            const auto decoded=c.inventory.read().items.find(id);
            if(decoded==c.inventory.read().items.end() || !decoded->second.decoded || !decoded->second.identified ||
                decoded->second.runeword || (socket?(decoded->second.sockets || decoded->second.filledSockets):!decoded->second.personalizedName.empty())) continue;
            questState.questRun.material=id;break;
        }
        if(!questState.questRun.material) throw std::runtime_error("Quest service needs a spare eligible identified backpack cap; equipped items are never selected");
        for(const auto &[id,item]:world.items) if(item.code==step.item) questState.questRun.materialBefore.insert(id);
    }
    if(!questState.questRun.materialPhase) {
        const auto npc=find(c,*step.target);
        if(!npc) {if(!approachKnown(c,*step.target)) explore(c,step.target->name);return false;}
        if (!prepareNpcService(c, npc->key, DebugActionOwner::Goal)) return false;
        const auto &item=world.items.at(*questState.questRun.material);OnlineItemCommand take;
        take.action=OnlineItemAction::Take;take.item=item.id;take.itemRevision=item.revision;
        if(!questItemRequest(c,take,QuestPending::Action::Take,"quest-service-take-submitted")) return false;
        questState.questRun.materialNpc=npc->key;questState.questRun.materialPhase=1;return false;
    }
    const auto item=world.items.find(*questState.questRun.material);
    if(item==world.items.end() || item->second.mode!=4) {wait("quest-service-cursor","Waiting for the reserved item on Cursor");checkQuestPending(c);return false;}
    if(!questState.questRun.materialNpc || world.npcRequested!=questState.questRun.materialNpc->id || !world.npcConversation ||
        world.npcConversation->source!=questState.questRun.materialNpc->id) throw std::runtime_error("Reward NPC conversation ended with the reserved item on Cursor");
    questState.questRun.pending.reset();OnlineItemCommand service;service.action=OnlineItemAction::QuestService;
    service.npc=questState.questRun.materialNpc->id;service.item=item->first;service.itemRevision=item->second.revision;
    if(questItemRequest(c,service,QuestPending::Action::Service,"quest-service-submitted")) questState.questRun.materialPhase=2;
    return false;
}
bool DebugAi::Impl::questRescue(Context &c,const QuestStep &step) {
    const auto &world=c.session.read().world;const auto flags=questFlags(c,*step.stateQuest);
    if(flags && (*flags&0x2003)) return true;
    const auto status=world.quests.statuses[questDefinition(*step.stateQuest).nativeSlot];
    if(status==12) throw std::runtime_error("Original prisoner rescue reports failure; dead prisoners are not counted as saved");
    if(step.witness && !questState.questRun.encounterWitness) {
        float best=std::numeric_limits<float>::max();
        for(const auto &[key,unit]:world.units) {
            const auto seen=std::pair{key,unit.assignmentRevision};
            if(key.type!=step.witness->type || unit.classId!=step.witness->identity || !unit.position || onlineMonsterCorpse(unit) ||
                questState.questRun.approachedWitnesses.contains(seen)) continue;
            const Vec at{float(unit.position->x),float(unit.position->y)};
            const bool openCage=std::any_of(world.units.begin(),world.units.end(),[&](const auto &entry) {
                const auto &door=entry.second;return entry.first.type==step.target->type && door.classId==step.target->identity &&
                    door.position && onlineMonsterCorpse(door) && (at-Vec{float(door.position->x),float(door.position->y)}).length()<20;
            });
            const float distance=(at-c.position).length();
            if(openCage && distance<best) {best=distance;questState.questRun.encounterWitness=seen;}
        }
    }
    if(questState.questRun.encounterWitness) {
        const auto seen=*questState.questRun.encounterWitness;const auto prisoner=world.units.find(seen.first);
        if(prisoner!=world.units.end() && prisoner->second.assignmentRevision==seen.second && prisoner->second.position &&
            !onlineMonsterCorpse(prisoner->second)) {
            const auto at=*prisoner->second.position;
            if((Vec{float(at.x),float(at.y)}-c.position).length()>7) {
                if(approachPoint(c,at,"actual freed prisoner activation",6)) return false;
            } else {
                // Fn131 samples nearby players every25 frames. Real proximity,
                // rather than ranged door damage alone, starts the escape.
                if(questState.questRun.witnessUntil==Clock::time_point{}) questState.questRun.witnessUntil=Clock::now()+std::chrono::seconds(2);
                if(Clock::now()<questState.questRun.witnessUntil) {
                    cancelWalk(c);wait("quest-prisoner-escape","Allowing the actual nearby prisoner/door callback to run");return false;
                }
            }
        }
        questState.questRun.approachedWitnesses.insert(seen);questState.questRun.encounterWitness.reset();questState.questRun.witnessUntil={};
        note("quest-prisoner-proximity-observed","Only the native counter/private flags prove rescue");
    }
    if(const auto door=find(c,*step.target)) {
        if(!c.combat.hostile(*door)) throw std::runtime_error("Prison door is not a legitimately attackable native monster");
        if(!c.combat.monsterTargetEligible(*door,false)) {
            waitForWorldResult(c,step,"Waiting for the observed prison door's actual attack eligibility");return false;
        }
        target=door->key;targetAssignment=door->assignmentRevision;
        // Destructible gates are combat targets. Their occupied centre is not
        // an approach goal; use the same attack stances as every other hunt.
        approachEnemy(c,*door,"assigned prison door");
        return false;
    }
    if(world.quests.rescuedBarbsRemaining==0) {
        if(!questState.questRun.pending && c.control.refreshQuests()) {
            QuestPending p;p.action=QuestPending::Action::Object;p.deadline=Clock::now()+std::chrono::seconds(20);questState.questRun.pending=p;
        }
        wait("quest-prisoner-entitlement","Waiting for actual personal rescue eligibility after the native counter");checkQuestPending(c);return false;
    }
    if(!approachKnown(c,*step.target)) explore(c,"remaining actual prisoner cages",false,ExplorationFocus::Units);
    return false;
}
bool DebugAi::Impl::questEncounter(Context &c,const QuestStep &step) {
    if(const auto portal=find(c,*step.target,true);portal && portal->mode && *portal->mode!=0 && c.town.permitsInteraction(c.session.read(),portal->key) &&
        (!step.status || c.session.read().world.quests.statuses[questDefinition(*step.stateQuest).nativeSlot]==step.status)) return true;
    const auto witness=step.witness?find(c,*step.witness,true):std::nullopt;
    if(witness) {
        questState.questRun.encounterWitness=std::pair{witness->key,witness->assignmentRevision};
        // The scene anchor stays fixed when the witness starts departing.
        if(!questState.questRun.encounterPosition || questState.questRun.encounterArea!=area) {
            questState.questRun.encounterPosition=witness->position;questState.questRun.encounterStandoff.reset();questState.questRun.encounterAnchored=false;
        }
        questState.questRun.encounterArea=area;
    }
    if(!questState.questRun.encounterWitness || questState.questRun.encounterArea!=area) {
        // Reach the actual encounter before treating peripheral monsters as
        // its required clear. Ordinary defensive combat still protects travel.
        if(step.witness && approachKnown(c,*step.witness)) return false;
        if(approachKnown(c,*step.target)) return false;
        explore(c,"actual encounter witness and portal");return false;
    }
    if(questState.questRun.encounterWitness) {
        // Clearing a remote enemy or evading missiles can retire the actual
        // witness from the active replica. Return to its observed position,
        // without requiring it to be assigned again or searching elsewhere.
        const auto anchor=questState.questRun.encounterPosition;
        const bool outsideView=anchor && (Vec{float(anchor->x),float(anchor->y)}-c.position).length()>eventPolicy.standoffMax+3;
        const bool tooClose=anchor && (Vec{float(anchor->x),float(anchor->y)}-c.position).length()<eventPolicy.standoffMin;
        if(anchor && (!questState.questRun.encounterAnchored || outsideView || tooClose)) {
            if(questState.questRun.encounterStandoff && frontierCoolingDown(*questState.questRun.encounterStandoff,2)) questState.questRun.encounterStandoff.reset();
            if(!questState.questRun.encounterStandoff) questState.questRun.encounterStandoff=encounterStance(c,*anchor);
            const auto stance=questState.questRun.encounterStandoff;
            // Observe from nearby floor, never occupy the witness's feet or
            // its short departure path to the awaited portal.
            if(stance && (Vec{float(stance->x),float(stance->y)}-c.position).length()>1) {
                if(!approachPoint(c,*stance,"observed encounter standoff",1)) {
                    questState.questRun.encounterStandoff.reset();
                    waitForWorldResult(c,step,"Observed encounter stance could not be reached; waiting for a usable approach");
                }
                return false;
            }
            if(!stance) {waitForWorldResult(c,step,"No safe known standoff near the observed encounter; retaining the scene");return false;}
        }
        questState.questRun.encounterAnchored=true;
        std::optional<OnlineUnit> enemy;float nearest=std::numeric_limits<float>::max();
        for(const auto &[key,unit]:c.session.read().world.units) {
            (void)key;
            if(!step.clearCombat || !encounterEnemy(c,unit)) continue;
            const float distance=(Vec{float(unit.position->x),float(unit.position->y)}-c.position).length();
            if(distance<nearest) {nearest=distance;enemy=unit;}
        }
        if(enemy) {
            approachEnemy(c,*enemy,"remaining assigned encounter hostile");return false;
        }
        waitForWorldResult(c,step,"Encounter witnessed; waiting here for actual enemies, portal or quest state, including witness departure");return false;
    }
    if(approachKnown(c,*step.target)) return false;
    if(step.witness && approachKnown(c,*step.witness)) return false;
    explore(c,"actual throne encounter and chamber portal");return false;
}
} // namespace d2x
