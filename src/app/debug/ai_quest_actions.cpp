#include "ai_impl.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace d2x {
void DebugAi::Impl::refreshQuestNpc(Context &c) {
    if(!questRun.pending || !questRun.pending->unit) return;
    auto &pending=*questRun.pending;const auto source=*pending.unit;
    checkQuestPending(c);
    if(!pending.refreshIssued) {
        if(c.session.read().world.npcRequested) c.session.close_npc();
        cancelWalk(c);pending.refreshIssued=true;note("quest-flag-refresh","Reward effect observed; querying the NPC without replaying its topic");return;
    }
    if(c.session.read().world.npcRequested==source.id) return;
    walk(c,{{"goal","unit"},{"unitType",source.type},{"unitId",source.id},{"interact",true}});
}
std::optional<OnlineItem> DebugAi::Impl::accessibleItem(const Context &c,std::string_view code) const {
    for(const auto &[id,item]:c.session.read().world.items) {
        if(item.code!=code || !onlineItemOwnedBy(item,c.session.read().load.playerUnitId) || !item.positionKnown ||
            !(item.mode==4 || item.mode==2 || (item.mode==0 && item.page==1))) continue;
        const auto decoded=c.inventory.read().items.find(id);
        if(decoded!=c.inventory.read().items.end() && decoded->second.decoded) return item;
    }
    return {};
}
bool DebugAi::Impl::questStoreCursor(Context &c,uint32_t id) {
    const auto &world=c.session.read().world;const auto wire=world.items.find(id);
    if(wire==world.items.end() || !onlineItemOwnedBy(wire->second,c.session.read().load.playerUnitId))
        throw std::runtime_error("Quest reward/source item is no longer owned");
    if(wire->second.mode==0 && (wire->second.page==1 || wire->second.page==5)) return true;
    if(wire->second.mode!=4) return wire->second.mode==1 || wire->second.mode==2;
    if(questRun.pending && questRun.pending->action==QuestPending::Action::Place) {checkQuestPending(c);return false;}
    const auto &inventory=c.inventory.read();const auto decoded=inventory.items.find(id);
    if(decoded==inventory.items.end() || !decoded->second.decoded || decoded->second.width<1 || decoded->second.height<1)
        throw std::runtime_error("Quest cursor item shape is unknown");
    const auto &shape=decoded->second;std::optional<OnlinePoint> cell;
    for(int y=0;y+shape.height<=inventory.rows && !cell;++y) for(int x=0;x+shape.width<=inventory.columns && !cell;++x) {
        bool free=true;
        for(const auto &[otherId,item]:world.items) {
            if(otherId==id || !onlineItemOwnedBy(item,c.session.read().load.playerUnitId) || item.mode!=0 || item.page!=1) continue;
            const auto other=inventory.items.find(otherId);
            if(!item.positionKnown || other==inventory.items.end() || !other->second.decoded || other->second.width<1 || other->second.height<1)
                throw std::runtime_error("Backpack placement is unknown; no guessed quest-item placement");
            if(x<int(item.x)+other->second.width && x+shape.width>item.x && y<int(item.y)+other->second.height && y+shape.height>item.y) {free=false;break;}
        }
        if(free) cell=OnlinePoint{uint16_t(x),uint16_t(y)};
    }
    if(!cell) throw std::runtime_error("Backpack has no real free space for the quest item/reward");
    OnlineItemCommand place;place.action=OnlineItemAction::Place;place.item=id;place.itemRevision=wire->second.revision;
    place.page=0;place.x=uint8_t(cell->x);place.y=uint8_t(cell->y);
    if(!c.inventory.submit(c.session,place)) {wait("quest-place",c.inventory.reason());return false;}
    QuestPending p;p.action=QuestPending::Action::Place;p.item=id;p.sequence=c.session.read().world.itemRequest->sequence;
    p.deadline=Clock::now()+std::chrono::seconds(12);questRun.pending=p;note("quest-item-place-submitted",std::to_string(id));return false;
}
bool DebugAi::Impl::questTalk(Context &c,const Goal &goal,const QuestStep &step) {
    const auto &view=c.session.read();const auto &world=view.world;const auto flags=questFlags(c,goal.questId);
    bool done=flags && step.flags && (*flags&step.flags)==step.flags;
    if(step.produced=="bkd") done=owned(c,"bkd") && !owned(c,"bks");
    else if(!step.item.empty() && step.produced.empty()) done=done && !owned(c,step.item);
    if(step.optional && questRun.pending && world.quests.revision>questRun.pending->beforeRevision &&
        world.quests.statuses[goal.questSlot] && *world.quests.statuses[goal.questSlot]>0) done=true;
    if(done) {
        if(goal.questId==QuestId::DenOfEvil && step.flags==1 && questRun.skillPointsBefore) {
            const auto points=world.playerAttributes.find(5);
            if(points==world.playerAttributes.end() || uint64_t(points->second)<uint64_t(*questRun.skillPointsBefore)+1) {
                wait("quest-reward","Reward flag arrived; waiting for the actual skill point");checkQuestPending(c);return false;
            }
        }
        if(goal.questId==QuestId::SistersBurialGrounds && step.flags==1 && questRun.rewardTracked && !world.hireling && !world.deadHirelingName) {
            wait("quest-reward","Waiting for the normal Rogue assignment/preserved mercenary record");checkQuestPending(c);return false;
        }
        if(step.produced=="bkd") {
            const auto scroll=accessibleItem(c,"bkd");
            if(!scroll) throw std::runtime_error("Translated scroll is owned outside accessible inventory; put it in the backpack");
            if(scroll->mode==4) {
                if(questRun.pending && questRun.pending->action==QuestPending::Action::Topic) questRun.pending.reset();
                return questStoreCursor(c,scroll->id);
            }
        }
        if(step.produced=="rin" && questRun.rewardTracked) {
            for(const auto &[id,item]:world.items) if(item.code=="rin" && !questRun.rewardBefore.contains(id) && onlineItemOwnedBy(item,view.load.playerUnitId)) {
                if(questRun.pending && questRun.pending->action!=QuestPending::Action::Place) questRun.pending.reset();
                return questStoreCursor(c,id);
            }
            for(const auto &[id,item]:world.items) if(item.code=="rin" && item.mode==3 && item.positionKnown && !questRun.rewardBefore.contains(id)) {
                if(questRun.pending && questRun.pending->action==QuestPending::Action::Pickup) {checkQuestPending(c);return false;}
                if(world.npcRequested) {c.session.close_npc();cancelWalk(c);return false;}
                const Vec position{float(item.groundX),float(item.groundY)};
                if((position-c.position).length()>3) {approachPoint(c,{item.groundX,item.groundY},"Cain's actual reward ring",2);return false;}
                const uint32_t reward=id;OnlineItemCommand pickup;pickup.action=OnlineItemAction::Pickup;pickup.item=reward;pickup.itemRevision=item.revision;
                if(!c.inventory.submit(c.session,pickup)) {wait("quest-reward",c.inventory.reason());return false;}
                QuestPending p;p.action=QuestPending::Action::Pickup;p.item=reward;p.sequence=c.session.read().world.itemRequest->sequence;
                p.deadline=Clock::now()+std::chrono::seconds(12);questRun.pending=p;note("quest-reward-pickup-submitted",std::to_string(reward));return false;
            }
            wait("quest-reward","Quest granted; waiting for the actual newly created reward ring");checkQuestPending(c);return false;
        }
        return true;
    }
    if(questRun.pending) {
        if(step.optional && questRun.pending->action==QuestPending::Action::Topic && !questRun.pending->refreshIssued) {
            // A1Q1/A1Q2 queue their introduction status until NPCDEACTIVATE.
            // End normal listening once; only the later real state/status
            // completes this optional step, never the queued acknowledgement.
            if(world.npcRequested) c.session.close_npc();
            cancelWalk(c);questRun.pending->refreshIssued=true;
            note("quest-introduction-closed","Waiting for the native post-conversation quest update");
            checkQuestPending(c);return false;
        }
        bool committed=false;
        const auto points=world.playerAttributes.find(5);
        if(goal.questId==QuestId::DenOfEvil && step.flags==1 && questRun.skillPointsBefore && points!=world.playerAttributes.end())
            committed=uint64_t(points->second)>=uint64_t(*questRun.skillPointsBefore)+1;
        if(goal.questId==QuestId::ToolsOfTheTrade && !step.item.empty()) committed=!owned(c,step.item);
        if(goal.questId==QuestId::SearchForCain && step.produced=="rin" && questRun.rewardTracked)
            for(const auto &[id,item]:world.items) if(item.code=="rin" && !questRun.rewardBefore.contains(id) &&
                (onlineItemOwnedBy(item,view.load.playerUnitId) || (item.mode==3 && item.positionKnown &&
                (Vec{float(item.groundX),float(item.groundY)}-c.position).length()<16))) committed=true;
        if(world.quests.revision>questRun.pending->beforeRevision && world.quests.statuses[goal.questSlot]==13) committed=true;
        if(committed) refreshQuestNpc(c);
        wait("quest-dialogue-result","Waiting for actual quest/item state after the offered topic");checkQuestPending(c);return false;
    }
    const auto npc=find(c,*step.target);
    if(!npc) {
        if(approachKnown(c,*step.target)) return false;
        if(step.optional) {note("optional-quest-npc-not-exposed",step.target->name);return true;}
        explore(c,"quest NPC "+step.target->name);return false;
    }
    if(world.npcRequested && world.npcRequested!=npc->key.id) {c.session.close_npc();return false;}
    if(!world.npcConversation || world.npcConversation->type!=1 || world.npcConversation->source!=npc->key.id) {
        wait("quest-dialogue","Opening the actual quest NPC conversation");
        walk(c,{{"goal","unit"},{"unitType",1},{"unitId",npc->key.id},{"interact",true}});return false;
    }
    if(!greetingIssued && npc->classId) {greetingIssued=c.session.confirm_npc_voice(*npc->classId);if(greetingIssued) return false;}
    const auto conversation=*world.npcConversation;
    // Prefer the registered quest topic; automatic greetings remain normal
    // listening input, never permission to send an absent story message.
    for(const auto topic:step.topics) for(const auto &message:conversation.messages) {
        if(message.stringId!=topic || conversation.acknowledged.contains(topic)) continue;
        if(goal.questId==QuestId::SearchForCain && topic==118) {
            questRun.rewardBefore.clear();for(const auto &[id,item]:world.items) if(item.code=="rin") questRun.rewardBefore.insert(id);
            questRun.rewardTracked=true;
        }
        if(goal.questId==QuestId::SistersBurialGrounds && topic==92) questRun.rewardTracked=true;
        if(goal.questId==QuestId::DenOfEvil && topic==76) {
            const auto points=world.playerAttributes.find(5);if(points!=world.playerAttributes.end()) questRun.skillPointsBefore=points->second;
        }
        const auto before=world.quests.revision;
        if(c.session.acknowledge_npc_message(topic)) {
            QuestPending p;p.action=QuestPending::Action::Topic;p.unit=npc->key;p.beforeRevision=before;
            p.deadline=Clock::now()+std::chrono::seconds(20);questRun.pending=p;++interactions;note("quest-topic-submitted",std::to_string(topic));
        }
        return false;
    }
    for(const auto &message:conversation.messages) if(message.menu==0 && !conversation.acknowledged.contains(message.stringId)) {
        c.session.acknowledge_npc_message(message.stringId);return false;
    }
    if(step.optional) {note("optional-quest-introduction-unavailable","Original objective may be done without this introduction");return true;}
    if(actionDeadline==Clock::time_point{}) actionDeadline=Clock::now()+std::chrono::seconds(20);
    if(Clock::now()>=actionDeadline) throw std::runtime_error("Quest NPC did not offer the required native topic");
    wait("quest-topic-wait","Waiting for the actual offered quest topic");return false;
}
bool DebugAi::Impl::questReadScroll(Context &c,const QuestStep &step) {
    const auto &world=c.session.read().world;
    if(questRun.pending) {
        if(world.quests.cainStones && world.quests.cainStonesRevision>questRun.pending->beforeRevision) return true;
        wait("quest-scroll-result","Waiting for the original 0x50 stone-order response");checkQuestPending(c);return false;
    }
    const auto item=accessibleItem(c,step.item);
    if(!item || item->mode!=0 || item->page!=1) throw std::runtime_error("Deciphered scroll must be in the real backpack before reading");
    const auto before=world.quests.cainStonesRevision;OnlineItemCommand use;use.action=OnlineItemAction::Use;use.item=item->id;use.itemRevision=item->revision;
    if(!c.inventory.submit(c.session,use)) {wait("quest-scroll",c.inventory.reason());return false;}
    QuestPending p;p.action=QuestPending::Action::Scroll;p.item=item->id;p.beforeRevision=before;
    p.sequence=c.session.read().world.itemRequest->sequence;p.deadline=Clock::now()+std::chrono::seconds(20);questRun.pending=p;
    note("quest-scroll-read-submitted",step.item);return false;
}
bool DebugAi::Impl::questOperate(Context &c,const QuestStep &step) {
    const auto &world=c.session.read().world;const auto flags=questFlags(c,goals[cursor].questId);
    if(!step.topics.empty() && world.npcConversation && world.npcConversation->type!=1 && step.target) {
        const auto unit=world.units.find({2,world.npcConversation->source});
        if(unit!=world.units.end() && unit->second.classId==step.target->identity) for(const auto &message:world.npcConversation->messages)
            if(std::find(step.topics.begin(),step.topics.end(),message.stringId)!=step.topics.end() && !world.npcConversation->acknowledged.contains(message.stringId)) {
                c.session.acknowledge_npc_message(message.stringId);return false;
            }
    }
    if(step.flags && flags && (*flags&step.flags)==step.flags) return true;
    if(step.witness && find(c,*step.witness)) return true;
    if(step.optional && questRun.pending && world.quests.statuses[goals[cursor].questSlot] &&
        *world.quests.statuses[goals[cursor].questSlot]>0) return true;
    if(questRun.pending) {
        const auto unit=questRun.pending->unit?world.units.find(*questRun.pending->unit):world.units.end();
        if(!step.flags && unit!=world.units.end() && unit->second.assignmentRevision==questRun.pending->assignment && unit->second.mode && *unit->second.mode!=0) {
            note("quest-object-mode-observed",std::to_string(unit->first.id));questRun.pending.reset();
        } else {wait("quest-object-result","Waiting for the actual object mode/quest flag");checkQuestPending(c);return false;}
    }
    std::optional<Selector> selector=step.target;
    if(step.action==QuestAction::Stones) {
        if(!world.quests.cainStones) throw std::runtime_error("Stone order is unknown; the real deciphered scroll must be read first");
        bool all=true;
        for(const auto id:*world.quests.cainStones) {
            if(std::find(step.objects.begin(),step.objects.end(),id)==step.objects.end()) throw std::runtime_error("Native stone order does not match current MPQ objects");
            const Selector stone{2,uint16_t(id),"ordered Cairn stone"};const auto unit=find(c,stone,true);
            if(unit && unit->mode && *unit->mode!=0) continue;
            selector=stone;all=false;break;
        }
        if(all) return true;
    }
    if(!selector) throw std::runtime_error("Quest operation has no verified object identity");
    const auto unit=find(c,*selector,true);
    if(!unit) {
        if(approachKnown(c,*selector)) return false;
        if(step.optional) {note("optional-quest-object-not-exposed","Direct tower entry is permitted by A1Q5");return true;}
        explore(c,selector->name);return false;
    }
    wait("quest-object","Approaching and operating the actual quest object");
    const Json intent{{"goal","unit"},{"unitType",unit->key.type},{"unitId",unit->key.id},{"interact",true},{"purpose","quest-object"}};
    if(walk(c,intent)) observeQuestInteraction(c,intent);
    return false;
}
bool DebugAi::Impl::questPortal(Context &c,const QuestStep &step) {
    if(c.town.read().area==step.area) return true;
    if(questRun.pending) {wait("quest-portal-result","Waiting for the actual quest destination scene");checkQuestPending(c);return false;}
    for(const auto &target:c.town.read().mapTargets) if(target.interaction==OnlineMapInteraction::Portal && target.destination==step.area) {
        wait("quest-portal","Using the actually assigned quest portal");
        const Json intent{{"goal","unit"},{"unitType",target.unit.type},{"unitId",target.unit.id},{"interact",true},{"purpose","quest-portal"}};
        if(walk(c,intent)) observeQuestInteraction(c,intent);
        return false;
    }
    explore(c,"the real quest portal after stone activation");return false;
}
bool DebugAi::Impl::questImbue(Context &c,const Goal &goal,const QuestStep &step) {
    const auto &view=c.session.read();const auto &world=view.world;const auto flags=questFlags(c,goal.questId);
    const auto rare=[&](uint32_t id) {
        const auto item=world.items.find(id);const auto decoded=c.inventory.read().items.find(id);
        return item!=world.items.end() && onlineItemOwnedBy(item->second,view.load.playerUnitId) && item->second.code==step.item &&
            decoded!=c.inventory.read().items.end() && decoded->second.decoded && decoded->second.quality==6;
    };
    if(questRun.materialPhase>=2) {
        std::optional<uint32_t> result;
        if(questRun.material && rare(*questRun.material)) result=questRun.material;
        if(!result) for(const auto &[id,item]:world.items) {
            (void)item;if(!questRun.materialBefore.contains(id) && rare(id)) {result=id;break;}
        }
        const auto &request=world.itemRequest;
        if(result && questRun.materialPhase==2 && questRun.pending && request && request->sequence==questRun.pending->sequence &&
            request->state==OnlineItemRequest::State::Updated) {
            // The normal service consumes the entitlement only once; retain
            // the actual transformed/new GUID before placing the rare item.
            questRun.material=*result;questRun.materialPhase=3;questRun.pending.reset();note("imbue-rare-result-observed",std::to_string(*result));
        }
        if(result && questRun.materialPhase==3) {
            if(!questStoreCursor(c,*result)) return false;
            questRun.pending.reset();
            if(flags && (*flags&1)) return true;
            // SUnitNpc's Charsi service calls ACT1Q3_SetRewardGranted without
            // UpdatePlayerFlags. Observe the completed service and store its
            // actual output before reopening the NPC for the full snapshot.
            QuestPending p;p.action=QuestPending::Action::Imbue;p.unit=questRun.materialNpc;
            p.deadline=Clock::now()+std::chrono::seconds(20);questRun.pending=p;questRun.materialPhase=4;
        }
        if(questRun.materialPhase==4) {
            if(flags && (*flags&1)) return true;
            refreshQuestNpc(c);wait("quest-imbue-flags","Rare output stored; querying the actual consumed imbue entitlement");
            checkQuestPending(c);return false;
        }
        wait("quest-imbue-result","Waiting for native service result and the actual rare item");checkQuestPending(c);return false;
    }
    if(!flags || !(*flags&2)) throw std::runtime_error("Imbue needs the actual pending Malus reward");
    if(!questRun.material) {
        // Reserve a spare normal backpack cap. Never select equipped items,
        // rare/unique gear or an arbitrary valuable base from this character.
        for(const auto &[id,item]:world.items) {
            if(item.code!=step.item || item.mode!=0 || item.page!=1 || !item.positionKnown || !onlineItemOwnedBy(item,view.load.playerUnitId)) continue;
            const auto decoded=c.inventory.read().items.find(id);
            if(decoded==c.inventory.read().items.end() || !decoded->second.decoded || decoded->second.quality!=2 ||
                decoded->second.sockets || decoded->second.filledSockets || decoded->second.runeword || (item.flags&0x1000u)) continue;
            questRun.material=id;break;
        }
        if(!questRun.material) throw std::runtime_error("Full Tools of the Trade needs the reserved unsocketed normal backpack cap; prepare it with the debug Act-I save recipe");
        for(const auto &[id,item]:world.items) if(item.code==step.item) questRun.materialBefore.insert(id);
    }
    if(questRun.materialPhase==0) {
        const auto npc=find(c,*step.target);
        if(!npc) {if(!approachKnown(c,*step.target)) explore(c,"Charsi's original imbue service");return false;}
        if(world.npcRequested && world.npcRequested!=npc->key.id) {c.session.close_npc();return false;}
        if(!world.npcConversation || world.npcConversation->type!=1 || world.npcConversation->source!=npc->key.id) {
            walk(c,{{"goal","unit"},{"unitType",1},{"unitId",npc->key.id},{"interact",true}});return false;
        }
        const auto item=world.items.find(*questRun.material);
        if(item==world.items.end() || item->second.mode!=0 || item->second.page!=1) throw std::runtime_error("Reserved imbue base changed before take");
        const uint32_t material=item->first;OnlineItemCommand take;take.action=OnlineItemAction::Take;take.item=material;take.itemRevision=item->second.revision;
        if(!c.inventory.submit(c.session,take)) {wait("quest-imbue-take",c.inventory.reason());return false;}
        questRun.materialNpc=npc->key;
        QuestPending p;p.action=QuestPending::Action::Take;p.item=material;p.sequence=c.session.read().world.itemRequest->sequence;
        p.deadline=Clock::now()+std::chrono::seconds(12);questRun.pending=p;questRun.materialPhase=1;cancelWalk(c);
        note("imbue-base-take-submitted",std::to_string(material));return false;
    }
    if(questRun.materialPhase==1) {
        const auto item=world.items.find(*questRun.material);
        if(item==world.items.end() || item->second.mode!=4 || !onlineItemOwnedBy(item->second,view.load.playerUnitId)) {
            wait("quest-imbue-cursor","Waiting for the actual reserved base on Cursor");checkQuestPending(c);return false;
        }
        if(!questRun.materialNpc || !world.npcConversation || world.npcConversation->type!=1 ||
            world.npcRequested!=questRun.materialNpc->id || world.npcConversation->source!=questRun.materialNpc->id)
            throw std::runtime_error("Imbue NPC conversation ended with the reserved base on Cursor");
        const uint32_t material=item->first;questRun.pending.reset();OnlineItemCommand imbue;imbue.action=OnlineItemAction::QuestService;
        imbue.npc=questRun.materialNpc->id;imbue.item=material;imbue.itemRevision=item->second.revision;
        if(!c.inventory.submit(c.session,imbue)) {wait("quest-imbue",c.inventory.reason());return false;}
        QuestPending p;p.action=QuestPending::Action::Imbue;p.item=material;p.sequence=c.session.read().world.itemRequest->sequence;
        p.deadline=Clock::now()+std::chrono::seconds(20);questRun.pending=p;questRun.materialPhase=2;
        note("imbue-service-submitted",std::to_string(material));return false;
    }
    wait("quest-imbue-result","Waiting for the original service result; no repeated imbue request");checkQuestPending(c);return false;
}
bool DebugAi::Impl::questActTravel(Context &c,const Goal &goal,const QuestStep &step) {
    if(c.town.read().area==step.area) {
        if(questFinished(c,goal)) return true;
        wait("quest-act-flags","Act-II town observed; waiting for final Act-I quest/change flags");checkQuestPending(c);return false;
    }
    if(questRun.pending) {wait("quest-act-travel-result","Waiting for the real normal act transfer");checkQuestPending(c);return false;}
    const auto npc=find(c,*step.target);
    if(!npc) {if(!approachKnown(c,*step.target)) explore(c,"Warriv's actual Go East service");return false;}
    const auto &world=c.session.read().world;
    if(world.npcRequested && world.npcRequested!=npc->key.id) {c.session.close_npc();return false;}
    if(!world.npcConversation || world.npcConversation->type!=1 || world.npcConversation->source!=npc->key.id) {
        walk(c,{{"goal","unit"},{"unitType",1},{"unitId",npc->key.id},{"interact",true}});return false;
    }
    const auto &conversation=c.town.read().npcConversation;
    if(!conversation || !conversation->travelDestination || conversation->travelDestination!=step.area) {
        if(actionDeadline==Clock::time_point{}) actionDeadline=Clock::now()+std::chrono::seconds(20);
        if(Clock::now()>=actionDeadline) throw std::runtime_error("Warriv did not offer the actual Act-II travel service");
        wait("quest-act-service","Waiting for the eligible Go East service");return false;
    }
    if(!c.session.npc_travel(*conversation->travelDestination)) {wait("quest-act-service","Normal NPC travel was not accepted");return false;}
    QuestPending p;p.action=QuestPending::Action::Travel;p.unit=npc->key;p.deadline=Clock::now()+std::chrono::seconds(30);
    questRun.pending=p;cancelWalk(c);note("quest-go-east-submitted",std::to_string(*step.area));return false;
}
}
