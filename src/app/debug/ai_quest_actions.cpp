#include "ai_impl.hpp"
#include "client/inventory_actions.hpp"
#include "contracts/online_npc_speech.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
void DebugAi::Impl::refreshQuestNpc(Context &c) {
    if(!questState.questRun.pending || !questState.questRun.pending->unit) return;
    auto &pending=*questState.questRun.pending;
    checkQuestPending(c);
    if(!pending.refreshIssued) {
        if(c.session.read().world.npcRequested && !c.control.closeNpc()) return;
        pending.beforeRevision=std::max(pending.beforeRevision,c.control.quests().playerFlagsRevision);
        cancelWalk(c);pending.refreshIssued=true;note("quest-dialogue-closed","Allowing the native NPCDEACTIVATE callback to finish");return;
    }
    // Original Rcv0x40 -> QUESTS_StatusCallback sends private 0x28 flags,
    // not just public status. Reopening the NPC is unnecessary and can
    // interrupt a departure/placement scene after the accepted delivery.
    if(!pending.statusRequested && c.control.refreshQuests(pending.beforeRevision)) {
        pending.statusRequested=true;pending.deadline=Clock::now()+std::chrono::seconds(20);
        note("quest-status-requested","Reading private quest flags without reopening the NPC");
    }
}
std::optional<OnlineItem> DebugAi::Impl::accessibleItem(const Context &c,std::string_view code) const {
    for(const auto &[id,item]:c.session.read().world.items) {
        if(item.code!=code || !onlineItemOwnedBy(item,c.session.read().load.playerUnitId) || !item.positionKnown ||
            !(item.mode==4 || item.mode==2 || (item.mode==0 && (item.page==1 || item.page==4)))) continue;
        const auto decoded=c.inventory.read().items.find(id);
        if(decoded!=c.inventory.read().items.end() && decoded->second.decoded) return item;
    }
    return {};
}
bool DebugAi::Impl::questStoreCursor(Context &c,uint32_t id) {
    const auto &view=c.session.read();const auto &world=view.world;const auto wire=world.items.find(id);
    if(wire==world.items.end() || !onlineItemOwnedBy(wire->second,view.load.playerUnitId))
        throw std::runtime_error("Quest reward/source item is no longer owned");
    if(wire->second.mode==0 && (wire->second.page==1 || wire->second.page==5)) return true;
    if(wire->second.mode!=4) return wire->second.mode==1 || wire->second.mode==2;
    if(questState.questRun.pending && questState.questRun.pending->action==QuestPending::Action::Place) {checkQuestPending(c);return false;}
    const auto cell = firstBackpackSpace(view, c.inventory.read(), id);
    if(!cell) throw std::runtime_error("No verified backpack space for the quest item/reward");
    OnlineItemCommand place;place.action=OnlineItemAction::Place;place.item=id;place.itemRevision=wire->second.revision;
    place.page=0;place.x=uint8_t(cell->x);place.y=uint8_t(cell->y);
    if(!settleItemInput(c)) return false;
    if(!submitItem(c,place,"quest-place")) return false;
    QuestPending p;p.action=QuestPending::Action::Place;p.item=id;p.sequence=c.session.read().world.itemRequest->sequence;
    p.deadline=Clock::now()+std::chrono::seconds(12);questState.questRun.pending=p;note("quest-item-place-submitted",std::to_string(id));return false;
}
bool DebugAi::Impl::quietQuestNpc(Context &c,const OnlineUnit &npc) {
    if(npc.position && approachPoint(c,*npc.position,"quest NPC alert",4)) {
        questState.questRun.missingTopicSince={};
        return false;
    }
    const auto now=Clock::now();
    const auto grace=std::chrono::milliseconds(std::clamp<uint64_t>(
        2ULL*c.session.read().latencyMilliseconds.value_or(0)+200,600,2000));
    if(questState.questRun.missingTopicSince==Clock::time_point{}) questState.questRun.missingTopicSince=now;
    wait("quest-npc-alert","Observing the named NPC for a new quest alert");
    return now-questState.questRun.missingTopicSince>=grace;
}
bool DebugAi::Impl::questJournalTalk(Context &c,const QuestStep &step) {
    const auto &view=c.session.read();
    const auto &world=view.world;
    const auto slot=questDefinition(*step.stateQuest).nativeSlot;
    if(world.quests.statuses[slot]!=step.journalTalkStatus) return true;
    const auto npc=find(c,*step.target);
    if(!npc) return !approachKnown(c,*step.target);
    const auto now=Clock::now();
    const auto &menu=world.npcConversation;
    if(menu && menu->type==1 && menu->source==npc->key.id) {
        questState.questRun.journalVisit=true;
        auto &receipt=questState.questRun.journalReceipts[questState.questRun.step];
        const auto flags=questFlags(c,*step.stateQuest).value_or(0);
        if(const auto line=nextAutomaticNpcSpeech(*menu)) {
            // The server chooses the next line, including prerequisite greetings.
            if(receipt.heard.contains({flags,*line})) {receipt.unproductiveFlags=flags;return true;}
            if(!c.control.acknowledgeNpc(*line)) return false;
            receipt.heard.emplace(flags,*line);
            if(world.npcRequested==npc->key.id) return false;
        } else {
            if(menu->acknowledged.empty()) {receipt.unproductiveFlags=flags;return true;}
            if(!c.control.closeNpc()) return false;
        }
        cancelWalk(c);
        questState.questRun.missingTopicRefresh=world.quests.playerFlagsRevision;
        c.control.refreshQuests(*questState.questRun.missingTopicRefresh);
        setActionDeadline(std::chrono::seconds(20),false);
        questState.questRun.missingTopicSince={};
        return false;
    }
    if(questState.questRun.missingTopicRefresh) {
        if(world.quests.playerFlagsRevision<=*questState.questRun.missingTopicRefresh) {
            if(now>=actionDeadline) return true; // Advice can yield to the field objective.
            c.control.refreshQuests(*questState.questRun.missingTopicRefresh);
            wait("quest-journal-refresh","Waiting for the journal after the NPC conversation");
            return false;
        }
        questState.questRun.missingTopicRefresh.reset();
    }
    if(world.npcRequested==npc->key.id || world.questAlerts.contains(npc->key)) {
        questState.questRun.journalVisit=true;
        questState.questRun.missingTopicSince={};
        walk(c,{.goal=DebugWalkGoal::Unit,.unit=OnlineUnitKey{1,npc->key.id},.interact=true});
        return false;
    }
    return quietQuestNpc(c,*npc);
}
bool DebugAi::Impl::questTalk(Context &c,const Goal &goal,const QuestStep &step) {
    if(step.journalTalkStatus) return questJournalTalk(c,step);
    const auto &view=c.session.read();const auto &world=view.world;
    const auto stateQuest=step.stateQuest.value_or(goal.questId);const auto slot=questDefinition(stateQuest).nativeSlot;
    const auto flags=questFlags(c,stateQuest);
    bool done=flags && step.flags && (*flags&step.flags)==step.flags && !(*flags&step.clearFlags);
    if(step.witness && !step.flags) if(const auto witness=find(c,*step.witness,true);witness && witness->mode && *witness->mode!=0) done=true;
    if(step.produced=="bkd") done=owned(c,"bkd") && !owned(c,"bks");
    else if(!step.produced.empty() && step.produced!="rin")
        done=owned(c,step.produced) && (step.item.empty() || !owned(c,step.item));
    else if(!step.item.empty() && step.produced.empty()) done=done && !owned(c,step.item);
    if(step.optional && questState.questRun.pending && questState.questRun.pending->action==QuestPending::Action::Topic &&
        world.quests.revision>questState.questRun.pending->beforeRevision &&
        world.quests.statuses[slot] && *world.quests.statuses[slot]>0) done=true;
    if(done) {
        if(questState.questRun.rewardTracked && (step.rareReward || !step.rewardCodes.empty())) return questReceiveRewards(c,step);
        if((step.skillPointReward || goal.questId==QuestId::DenOfEvil) && step.flags==1 && questState.questRun.skillPointsBefore) {
            const auto points=world.playerAttributes.find(5);
            if(points==world.playerAttributes.end() || uint64_t(points->second)<uint64_t(*questState.questRun.skillPointsBefore)+std::max(1u,step.skillPointReward)) {
                wait("quest-reward","Reward flag arrived; waiting for the actual skill point");checkQuestPending(c);return false;
            }
        }
        if(goal.questId==QuestId::SistersBurialGrounds && step.flags==1 && questState.questRun.rewardTracked && !world.hireling && !world.deadHirelingName) {
            wait("quest-reward","Waiting for the normal Rogue assignment/preserved mercenary record");checkQuestPending(c);return false;
        }
        if(!step.produced.empty() && step.produced!="rin") {
            const auto scroll=accessibleItem(c,step.produced);
            if(!scroll) throw std::runtime_error("Translated scroll is owned outside accessible inventory; put it in the backpack");
            if(scroll->mode==4) {
                if(questState.questRun.pending && questState.questRun.pending->action==QuestPending::Action::Topic) questState.questRun.pending.reset();
                return questStoreCursor(c,scroll->id);
            }
        }
        if(step.produced=="rin" && questState.questRun.rewardTracked) {
            for(const auto &[id,item]:world.items) if(item.code=="rin" && !questState.questRun.rewardBefore.contains(id) && onlineItemOwnedBy(item,view.load.playerUnitId)) {
                if(questState.questRun.pending && questState.questRun.pending->action!=QuestPending::Action::Place) questState.questRun.pending.reset();
                return questStoreCursor(c,id);
            }
            for(const auto &[id,item]:world.items) if(item.code=="rin" && item.mode==3 && item.positionKnown && !questState.questRun.rewardBefore.contains(id)) {
                if(questState.questRun.pending && questState.questRun.pending->action==QuestPending::Action::Pickup) {checkQuestPending(c);return false;}
                if(questState.questRun.pending && questState.questRun.pending->action==QuestPending::Action::Topic) questState.questRun.pending.reset();
                if(world.npcRequested) {c.control.closeNpc();cancelWalk(c);return false;}
                if(!approachPickup(c,item)) return false;
                const uint32_t reward=id;OnlineItemCommand pickup;pickup.action=OnlineItemAction::Pickup;pickup.item=reward;pickup.itemRevision=item.revision;
                if(!submitItem(c,pickup,"quest-reward")) return false;
                QuestPending p;p.action=QuestPending::Action::Pickup;p.item=reward;p.sequence=c.session.read().world.itemRequest->sequence;
                p.deadline=Clock::now()+std::chrono::seconds(12);questState.questRun.pending=p;note("quest-reward-pickup-submitted",std::to_string(reward));return false;
            }
            wait("quest-reward","Quest granted; waiting for the actual newly created reward ring");checkQuestPending(c);return false;
        }
        return true;
    }
    if(questState.questRun.worldWait) {
        if(find(c,*step.target)) questState.questRun.worldWait.reset();
        else {
            waitForWorldResult(c,step,"Waiting for the encounter's actual NPC assignment; retaining this position");return false;
        }
    }
    if(questState.questRun.pending) {
        if(questState.questRun.pending->action==QuestPending::Action::Introduction) {
            checkQuestPending(c);
            if(world.quests.revision<=questState.questRun.pending->beforeRevision) {
                if(!questState.questRun.pending->statusRequested && c.control.refreshQuests()) {
                    questState.questRun.pending->statusRequested=true;
                    questState.questRun.pending->deadline=Clock::now()+std::chrono::seconds(20);
                    note("quest-status-requested","Querying the actual prerequisite greeting result without replaying its topic");
                }
                wait("quest-introduction-result","Waiting for the actually offered prerequisite greeting to commit");return false;
            }
            cancelWalk(c);questState.questRun.pending.reset();greetingIssued=false;
            note("quest-prerequisite-greeting-observed","Continuing with the next actually offered topic");return false;
        }
        if(step.optional && questState.questRun.pending->action==QuestPending::Action::Topic && !questState.questRun.pending->refreshIssued) {
            // A1Q1/A1Q2 queue their introduction status until NPCDEACTIVATE.
            // End normal listening once; only the later real state/status
            // completes this optional step, never the queued acknowledgement.
            if(world.npcRequested) c.control.closeNpc();
            cancelWalk(c);questState.questRun.pending->refreshIssued=true;
            note("quest-introduction-closed","Waiting for the native post-conversation quest update");
            checkQuestPending(c);return false;
        }
        // Retail ScrollMessage callbacks (notably Cain's A2Q2 335..338)
        // change private flags without immediately sending another 0x28.
        // Close once and request 0x40 for the actual private record. This
        // is a status query, never a replay of the accepted topic or
        // evidence of completion; the checks above still require its result.
        if(questState.questRun.pending->action==QuestPending::Action::Topic) {
            if(step.witness) {
                // A world-creating callback may still be preparing its actual
                // result. Query flags without closing its live conversation.
                if(!questState.questRun.pending->statusRequested && c.control.refreshQuests()) questState.questRun.pending->statusRequested=true;
            } else refreshQuestNpc(c);
            if(questState.questRun.pending->statusRequested && world.quests.playerFlagsRevision>questState.questRun.pending->beforeRevision) {
                questState.questRun.pending.reset();greetingIssued=false;
                note("quest-dialogue-state-observed","Re-evaluating required results after the actual private state update");return false;
            }
        }
        wait("quest-dialogue-result","Waiting for actual quest/item state after the offered topic");checkQuestPending(c);return false;
    }
    if(questState.questRun.missingTopicRefresh) {
        if(world.quests.playerFlagsRevision<=*questState.questRun.missingTopicRefresh) {
            if(Clock::now()>=actionDeadline) throw std::runtime_error("Quest topic recovery received no fresh private quest snapshot");
            if(!c.control.refreshQuests(*questState.questRun.missingTopicRefresh)) {
                wait("quest-topic-refresh","Waiting to refresh private quest data after closing the unavailable topic");return false;
            }
            wait("quest-topic-refresh","Waiting for private quest data before reopening the NPC");return false;
        }
        questState.questRun.missingTopicRefresh.reset();
        note("quest-topic-state-refreshed","Rechecking the actual offered topic after the private snapshot");
    }
    const auto npc=find(c,*step.target);
    if(!npc) {
        if(step.resultWaitSeconds) {
            waitForWorldResult(c,step,"Waiting for the encounter's actual NPC assignment; retaining this position");return false;
        }
        if(approachKnown(c,*step.target)) return false;
        if(step.optional) {note("optional-quest-npc-not-exposed",step.target->name);return true;}
        explore(c,"quest NPC "+step.target->name,false,ExplorationFocus::Units);return false;
    }
    if(!world.npcConversation || world.npcConversation->type!=1 || world.npcConversation->source!=npc->key.id) {
        if(world.npcRequested!=npc->key.id && !world.questAlerts.contains(npc->key)) {
            if(step.optional) return quietQuestNpc(c,*npc);
            if(questState.questRun.awaitingTopicAlert) {
                if(Clock::now()>=actionDeadline) throw std::runtime_error("Required quest result is missing and the NPC has no new quest alert");
                wait("quest-topic-wait","Waiting for the required result or a new NPC quest alert");return false;
            }
        }
        wait("quest-dialogue","Opening the actual quest NPC conversation");
        walk(c,{.goal=DebugWalkGoal::Unit,.unit=OnlineUnitKey{1,npc->key.id},.interact=true,.waitQuestAlert=true});return false;
    }
    if(!greetingIssued && npc->classId) {greetingIssued=c.control.greetNpc(*npc->classId);if(greetingIssued) return false;}
    const auto conversation=*world.npcConversation;
    const auto automatic = nextAutomaticNpcSpeech(conversation);
    auto &receipt=questState.questRun.journalReceipts[questState.questRun.step];
    for(const auto &message:conversation.messages) {
        const auto topic=message.stringId;
        if(conversation.acknowledged.contains(topic) || (automatic && *automatic!=topic) ||
            (!automatic && message.menu!=2) || receipt.heard.contains({flags.value_or(0),topic})) continue;
        if(step.rareReward || !step.rewardCodes.empty()) {
            questState.questRun.rewardBefore.clear();for(const auto &[id,item]:world.items) {(void)item;questState.questRun.rewardBefore.insert(id);}
            questState.questRun.rewardTracked=true;
        }
        if(step.produced=="rin") {
            questState.questRun.rewardBefore.clear();for(const auto &[id,item]:world.items) if(item.code=="rin") questState.questRun.rewardBefore.insert(id);
            questState.questRun.rewardTracked=true;
        }
        if(goal.questId==QuestId::SistersBurialGrounds && step.flags==1) questState.questRun.rewardTracked=true;
        if(step.skillPointReward || (goal.questId==QuestId::DenOfEvil && step.flags==1)) {
            // Retail omits zero-valued stats from its initial stat-list
            // replication. An absent newskills entry on this ready player is
            // zero, not a reason to miss the later actual +1 reward update.
            const auto points=world.playerAttributes.find(5);
            questState.questRun.skillPointsBefore=points==world.playerAttributes.end()?0:points->second;
        }
        const auto before=world.quests.playerFlagsRevision;
        if(c.control.acknowledgeNpc(topic)) {
            receipt.heard.emplace(flags.value_or(0),topic);
            QuestPending p;p.action=QuestPending::Action::Topic;p.unit=npc->key;p.beforeRevision=before;
            p.deadline=Clock::now()+std::chrono::seconds(20);questState.questRun.pending=p;++interactions;note("quest-topic-submitted",std::to_string(topic));
        }
        return false;
    }
    {
        const auto now=Clock::now();
        if(questState.questRun.awaitingTopicAlert && now>=actionDeadline) {
            if(step.optional) return true;
            throw std::runtime_error("Quest NPC repeatedly offered no actionable topic for the required result");
        }
        if(questState.questRun.missingTopicSince==Clock::time_point{}) questState.questRun.missingTopicSince=now;
        const auto grace=std::chrono::milliseconds(std::clamp<uint64_t>(2ULL*view.latencyMilliseconds.value_or(0)+200,600,2000));
        if(now-questState.questRun.missingTopicSince<grace) {wait("quest-topic-wait","Allowing the offered quest topic to arrive");return false;}
        if(world.npcRequested==npc->key.id && !c.control.closeNpc()) return false;
        if(!questState.questRun.awaitingTopicAlert) setActionDeadline(std::chrono::seconds(20),false);
        questState.questRun.awaitingTopicAlert=true;
        questState.questRun.missingTopicSince={};
        questState.questRun.missingTopicRefresh=c.control.quests().playerFlagsRevision;
        c.control.refreshQuests(*questState.questRun.missingTopicRefresh);
        greetingIssued=false;cancelWalk(c);
        note("quest-topic-refresh","Expected topic absent; re-evaluating private state and the NPC alert");
        return false;
    }
}
bool DebugAi::Impl::questReadScroll(Context &c,const QuestStep &step) {
    const auto &world=c.session.read().world;
    if(questState.questRun.pending) {
        if(world.quests.cainStones && world.quests.cainStonesRevision>questState.questRun.pending->beforeRevision) return true;
        wait("quest-scroll-result","Waiting for the original 0x50 stone-order response");checkQuestPending(c);return false;
    }
    const auto item=accessibleItem(c,step.item);
    if(!item || item->mode!=0 || item->page!=1) throw std::runtime_error("Deciphered scroll must be in the real backpack before reading");
    const auto before=world.quests.cainStonesRevision;OnlineItemCommand use;use.action=OnlineItemAction::Use;use.item=item->id;use.itemRevision=item->revision;
    if(!submitItem(c,use,"quest-scroll")) return false;
    QuestPending p;p.action=QuestPending::Action::Scroll;p.item=item->id;p.beforeRevision=before;
    p.sequence=c.session.read().world.itemRequest->sequence;p.deadline=Clock::now()+std::chrono::seconds(20);questState.questRun.pending=p;
    note("quest-scroll-read-submitted",step.item);return false;
}
bool DebugAi::Impl::questOperate(Context &c,const QuestStep &step) {
    const auto &world=c.session.read().world;const auto flags=questFlags(c,step.stateQuest.value_or(goals[cursor].questId));
    bool modeCompletes=false;
    if(step.action==QuestAction::Operate && step.target && !step.flags && !step.witness && step.topics.empty()) {
        for(size_t row=0;row<definitions->objects.rows().size();++row) if(definitions->objects.number(row,"Id")==step.target->identity) {
            const auto operation=definitions->objects.number(row,"OperateFn");
            // ObjMode31 and45 accept neutral only. An actual non-neutral
            // assignment also resumes an activation done by another player;
            // item/drop/travel postconditions remain separate graph nodes.
            modeCompletes=operation==31 || operation==45 || operation==49 ||
                (definitions->objects.value(row,"Name")=="Seal" && operation>=52 && operation<=56);break;
        }
        if(modeCompletes) if(const auto object=find(c,*step.target,true);object && object->mode && *object->mode!=0) {
            note("quest-one-time-object-observed",std::to_string(object->key.id));return true;
        }
    }
    if(step.refreshFlags && !questState.questRun.pending && step.target && !(flags && (*flags&step.flags)==step.flags)) {
        const auto object=find(c,*step.target,true);
        if(object && object->mode && *object->mode!=0) {
            // A shared open object is not personal eligibility. Query the
            // private record once; do not replay an altar operation merely
            // because its earlier inventory result arrived before packet28.
            QuestPending p;p.action=QuestPending::Action::Object;p.unit=object->key;p.assignment=object->assignmentRevision;
            p.deadline=Clock::now()+std::chrono::seconds(20);questState.questRun.pending=p;
            note("quest-open-object-private-query");
        }
    }
    if(step.refreshFlags && questState.questRun.pending && !questState.questRun.pending->statusRequested &&
        (step.topics.empty() || questState.questRun.pending->refreshIssued)) {
        const auto before=world.quests.revision;
        if(c.control.refreshQuests()) {
            if(step.status) questState.questRun.pending->beforeRevision=before;
            questState.questRun.pending->statusRequested=true;questState.questRun.pending->deadline=Clock::now()+std::chrono::seconds(20);
            note("quest-status-requested","Private object eligibility, without replaying its operation");
        }
        return false;
    }
    if(step.publishesTombSymbol && !questState.questRun.rewardTracked) {
        // Retail grants the journal flags while opening it; this payload can
        // precede the reading acknowledgement that creates its canyon portal.
        // An already completed journal may have a valid initialization payload.
        questState.questRun.clueBefore=flags && (*flags&1)?0:world.quests.staffTombRevision;
        questState.questRun.rewardTracked=true;
    }
    if(!step.topics.empty() && world.npcConversation && world.npcConversation->type!=1 && step.target) {
        const auto unit=world.units.find({2,world.npcConversation->source});
        if(unit!=world.units.end() && unit->second.classId==step.target->identity) for(const auto &message:world.npcConversation->messages)
            if(!world.npcConversation->acknowledged.contains(message.stringId)) {
                if(c.control.acknowledgeNpc(message.stringId)) {
                    QuestPending p;p.action=QuestPending::Action::Object;p.unit=unit->first;
                    p.assignment=unit->second.assignmentRevision;p.beforeRevision=questState.questRun.clueBefore;p.refreshIssued=true;
                    p.deadline=Clock::now()+std::chrono::seconds(30);questState.questRun.pending=p;
                    note(step.publishesTombSymbol?"quest-journal-read-submitted":"quest-object-reading-submitted",std::to_string(message.stringId));
                }
                return false;
            }
    }
    if(step.publishesTombSymbol && questState.questRun.pending && questState.questRun.pending->refreshIssued &&
        !questState.questRun.pending->statusRequested) {
        // Original QUESTS_StatusCallback (C2S40) publishes the private flags
        // and the combined 50/id1 tomb clue after the journal callback.
        if(c.control.refreshQuests()) {
            questState.questRun.pending->statusRequested=true;note("quest-status-requested","Horazon journal: original 0x40 query");
            questState.questRun.pending->deadline=Clock::now()+std::chrono::seconds(30);
        }
        return false;
    }
    if(step.publishesTombSymbol && questState.questRun.pending && questState.questRun.pending->refreshIssued &&
        world.quests.staffTombRevision>questState.questRun.pending->beforeRevision && world.quests.staffTombOffset &&
        *world.quests.staffTombOffset<7 && flags && (*flags&1)) {
        questState.tombSymbol=world.quests.staffTombOffset;note("quest-tomb-symbol-observed",std::to_string(*questState.tombSymbol));return true;
    }
    if(step.status && questState.questRun.pending && questState.questRun.pending->refreshIssued && questState.questRun.pending->statusRequested &&
        world.quests.revision>questState.questRun.pending->beforeRevision &&
        world.quests.statuses[questDefinition(step.stateQuest.value_or(goals[cursor].questId)).nativeSlot]==step.status) return true;
    if(!step.publishesTombSymbol && step.flags && flags && (*flags&step.flags)==step.flags) return true;
    if(step.witness && find(c,*step.witness)) return true;
    if(step.optional && questState.questRun.pending && world.quests.statuses[goals[cursor].questSlot] &&
        *world.quests.statuses[goals[cursor].questSlot]>0) return true;
    if(questState.questRun.pending) {
        if(!step.publishesTombSymbol && step.witness && !step.topics.empty()) {
            wait("quest-object-callback",questState.questRun.pending->refreshIssued?"Waiting for the acknowledged reading's encounter witness":
                "Waiting for the submitted object's reading");
            checkQuestPending(c);return false;
        }
        const auto unit=questState.questRun.pending->unit?world.units.find(*questState.questRun.pending->unit):world.units.end();
        if(!step.publishesTombSymbol && !step.flags && unit!=world.units.end() && unit->second.assignmentRevision==questState.questRun.pending->assignment && unit->second.mode && *unit->second.mode!=0) {
            note("quest-object-mode-observed",std::to_string(unit->first.id));questState.questRun.pending.reset();
            if(modeCompletes) return true;
        } else {
            wait("quest-object-result","Waiting for the actual object mode/quest flag");checkQuestPending(c);
            continuation=Continuation::WorldResult;return false;
        }
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
        if(step.spawnSource && questState.questSpawnAnchor && questState.questSpawnAnchor->first==area) {
            const auto anchor=questState.questSpawnAnchor->second;
            waitAtWorldEvent(c,step,anchor,"Waiting at the assigned birth marker for the actual quest object");return false;
        }
        if(approachKnown(c,*selector)) return false;
        if(step.optional) {note("optional-quest-object-not-exposed","Direct tower entry is permitted by A1Q5");return true;}
        explore(c,selector->name,false,ExplorationFocus::Units);return false;
    }
    wait("quest-object","Approaching and operating the actual quest object");
    const DebugWalkIntent intent{.goal=DebugWalkGoal::Unit,.unit=OnlineUnitKey{unit->key.type,unit->key.id},.interact=true,.purpose="quest-object"};
    if(walk(c,intent)) observeQuestInteraction(c,intent);
    return false;
}
bool DebugAi::Impl::questPortal(Context &c,const QuestStep &step) {
    if(step.cinematic && c.session.read().world.cinematic==step.cinematic) return true;
    if(c.town.read().area==step.area) {
        if(!step.cinematic) return true;
        waitForWorldResult(c,step,"Destination observed; waiting for the original cinematic notification");return false;
    }
    if(questState.questRun.portalDeadline!=Clock::time_point{} && Clock::now()>=questState.questRun.portalDeadline)
        throw std::runtime_error("Quest portal did not reach its destination within the event wait budget");
    if(navigation.travelDestination && (navigation.travelPhase==TravelPhase::WaypointWait || (navigation.travelPhase==TravelPhase::CrossWait && navigation.legUnit))) {
        travel(c,*navigation.travelDestination);return false;
    }
    const bool observedPortal=std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &target) {
        return target.interaction==OnlineMapInteraction::Portal && target.destination==step.area &&
            c.session.read().world.units.contains(target.unit);
    });
    if(!questState.questRun.pending && !observedPortal && step.preferWaypoint && step.area && navigation.knownWaypointAreas.contains(*step.area))
        return travel(c,*step.area);
    if(questState.questRun.pending) {
        const auto &pending=*questState.questRun.pending;
        if(pending.action==QuestPending::Action::Object && pending.unit) {
            const auto unit=c.session.read().world.units.find(*pending.unit);
            if(unit!=c.session.read().world.units.end() && unit->second.assignmentRevision==pending.assignment &&
                c.town.projectedObjectMode(c.session.read(),*pending.unit)==2 &&
                std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &target) {
                    return target.unit==*pending.unit && (target.interaction==OnlineMapInteraction::Portal ||
                        (step.target && target.interaction==OnlineMapInteraction::Door && unit->second.classId==step.target->identity));
                })) {
                note("quest-portal-activation-observed",std::to_string(pending.unit->id));
                questState.questRun.pending.reset();cancelWalk(c);return false;
            }
            waitForWorldResult(c,step,"Waiting for OPENED, including the MPQ silent ENDANIM deadline");
            checkQuestPending(c);return false;
        } else wait("quest-portal-result","Waiting for the actual quest destination scene");
        checkQuestPending(c);return false;
    }
    if(approachWorldTrigger(c,step)) return false;
    if(questState.questRun.worldWait && !worldResultReady(c,step)) {
        const auto anchor=questState.questRun.worldWait->anchor;
        waitAtWorldEvent(c,step,{uint16_t(anchor.x),uint16_t(anchor.y)},questState.questRun.worldWait->expected,questState.questRun.worldWait->vicinity);return false;
    }
    for(const auto &target:c.town.read().mapTargets) if((target.interaction==OnlineMapInteraction::Portal ||
        (step.target && (target.interaction==OnlineMapInteraction::Door || target.interaction==OnlineMapInteraction::Exit))) &&
        ((!step.cinematic && target.destination==step.area) || (step.target && target.unit.type==step.target->type && [&] {
            const auto u=c.session.read().world.units.find(target.unit);
            return u!=c.session.read().world.units.end() && u->second.classId==step.target->identity;
        }()))) {
        wait("quest-portal","Using the actually assigned quest portal");
        bool activation=false;
        auto interactionUnit=target.unit;
        const auto unit=c.session.read().world.units.find(target.unit);
        if(target.unit.type==2 && unit!=c.session.read().world.units.end() && unit->second.classId)
            for(size_t row=0;row<definitions->objects.rows().size();++row)
                if(definitions->objects.number(row,"Id")==*unit->second.classId &&
                    (definitions->objects.number(row,"OperateFn")==34 || definitions->objects.number(row,"OperateFn")==66)) {
                    const auto mode=c.town.projectedObjectMode(c.session.read(),target.unit);
                    if(!mode || *mode==1) {
                        waitAtWorldEvent(c,step,{uint16_t(c.position.x+.5f),uint16_t(c.position.y+.5f)},
                            "Waiting for the assigned entrance's MPQ ENDANIM");return false;
                    }
                    activation=*mode==0;
                    if(*mode==2 && definitions->objects.number(row,"OperateFn")==66) {
                        for(const auto &exit:c.town.read().mapTargets)
                            if(exit.unit.type==5 && exit.interaction==OnlineMapInteraction::Exit &&
                                exit.destination==step.area && c.town.permitsInteraction(c.session.read(),exit.unit)) {
                                interactionUnit=exit.unit;break;
                            }
                    }
                }
        if(!activation && waypointBeforeDeparture(c)) return false;
        const DebugWalkIntent intent{.goal=DebugWalkGoal::Unit,.unit=OnlineUnitKey{interactionUnit.type,interactionUnit.id},.interact=true,.purpose="quest-portal",.activation=activation};
        if(walk(c,intent)) observeQuestInteraction(c,intent);
        return false;
    }
    // A quest portal can already exist while its opening animation makes it
    // unselectable. That is an event at a known position, not a missing unit
    // to search for across the level. Keep its hint even after room release.
    if(step.target && area && (!step.sourceArea || step.sourceArea==area)) {
        const auto portal=find(c,*step.target,true);
        const auto hint=portal?portal->position:c.town.knownTargetPosition(*area,step.target->type,step.target->identity);
        if(hint) {
            const OnlinePoint anchor=portal?OnlinePoint{uint16_t(c.position.x+.5f),uint16_t(c.position.y+.5f)}:*hint;
            waitAtWorldEvent(c,step,anchor,portal?"Waiting for the assigned quest portal to become selectable":
                "Waiting for the quest portal to be reassigned at its observed position");
            return false;
        }
    }
    if(area && step.area) if(const auto hint=c.town.knownConnectionPosition(*area,*step.area)) {
        waitAtWorldEvent(c,step,*hint,"Waiting for the real portal assignment at its observed position");return false;
    }
    if(step.sourceArea && c.town.read().area!=step.sourceArea) {
        travel(c,*step.sourceArea);return false;
    }
    if(step.witness && area) {
        const auto witness=find(c,*step.witness,true);
        const auto marker=witness?witness->position:c.town.knownTargetPosition(*area,step.witness->type,step.witness->identity);
        if(marker) {
            const Vec at=Vec{float(marker->x),float(marker->y)}+step.witnessOffset.value_or(Vec{});
            if(at.x<0 || at.y<0 || at.x>65535 || at.y>65535) throw std::runtime_error("Quest event anchor is outside native coordinates");
            const OnlinePoint birth{uint16_t(at.x),uint16_t(at.y)};
            waitAtWorldEvent(c,step,birth,"Waiting for the actual selectable portal beside "+step.witness->name,
                step.witnessOffset?3.f:8.f);
            return false;
        }
    }
    explore(c,"the actual assigned quest portal",false,ExplorationFocus::Units);return false;
}
bool DebugAi::Impl::questImbue(Context &c,const Goal &goal,const QuestStep &step) {
    const auto &view=c.session.read();const auto &world=view.world;const auto flags=questFlags(c,goal.questId);
    const auto rare=[&](uint32_t id) {
        const auto item=world.items.find(id);const auto decoded=c.inventory.read().items.find(id);
        return item!=world.items.end() && onlineItemOwnedBy(item->second,view.load.playerUnitId) && item->second.code==step.item &&
            decoded!=c.inventory.read().items.end() && decoded->second.decoded && decoded->second.quality==6;
    };
    if(questState.questRun.materialPhase>=2) {
        std::optional<uint32_t> result;
        if(questState.questRun.material && rare(*questState.questRun.material)) result=questState.questRun.material;
        if(!result) for(const auto &[id,item]:world.items) {
            (void)item;if(!questState.questRun.materialBefore.contains(id) && rare(id)) {result=id;break;}
        }
        const auto &request=world.itemRequest;
        if(result && questState.questRun.materialPhase==2 && questState.questRun.pending && request && request->sequence==questState.questRun.pending->sequence &&
            request->state==OnlineItemRequest::State::Updated) {
            // The normal service consumes the entitlement only once; retain
            // the actual transformed/new GUID before placing the rare item.
            questState.questRun.material=*result;questState.questRun.materialPhase=3;questState.questRun.pending.reset();note("imbue-rare-result-observed",std::to_string(*result));
        }
        if(result && questState.questRun.materialPhase==3) {
            if(!questStoreCursor(c,*result)) return false;
            questState.questRun.pending.reset();
            if(flags && (*flags&1)) return true;
            // SUnitNpc's Charsi service calls ACT1Q3_SetRewardGranted without
            // UpdatePlayerFlags. Observe the completed service and store its
            // actual output before querying the private flags with 0x40.
            QuestPending p;p.action=QuestPending::Action::Imbue;p.unit=questState.questRun.materialNpc;
            p.deadline=Clock::now()+std::chrono::seconds(20);questState.questRun.pending=p;questState.questRun.materialPhase=4;
        }
        if(questState.questRun.materialPhase==4) {
            if(flags && (*flags&1)) return true;
            refreshQuestNpc(c);wait("quest-imbue-flags","Rare output stored; querying the actual consumed imbue entitlement");
            checkQuestPending(c);return false;
        }
        wait("quest-imbue-result","Waiting for native service result and the actual rare item");checkQuestPending(c);return false;
    }
    if(!flags || !(*flags&2)) throw std::runtime_error("Imbue needs the actual pending Malus reward");
    if(!questState.questRun.material) {
        // Reserve a spare normal backpack cap. Never select equipped items,
        // rare/unique gear or an arbitrary valuable base from this character.
        for(const auto &[id,item]:world.items) {
            if(item.code!=step.item || item.mode!=0 || item.page!=1 || !item.positionKnown || !onlineItemOwnedBy(item,view.load.playerUnitId)) continue;
            const auto decoded=c.inventory.read().items.find(id);
            if(decoded==c.inventory.read().items.end() || !decoded->second.decoded || decoded->second.quality!=2 ||
                decoded->second.sockets || decoded->second.filledSockets || decoded->second.runeword || (item.flags&0x1000u)) continue;
            questState.questRun.material=id;break;
        }
        if(!questState.questRun.material) throw std::runtime_error("Full Tools of the Trade needs the reserved unsocketed normal backpack cap; prepare it with the debug Act-I save recipe");
        for(const auto &[id,item]:world.items) if(item.code==step.item) questState.questRun.materialBefore.insert(id);
    }
    if(questState.questRun.materialPhase==0) {
        const auto npc=find(c,*step.target);
        if(!npc) {if(!approachKnown(c,*step.target)) explore(c,"Charsi's original imbue service");return false;}
        if (!prepareNpcService(c, npc->key, DebugActionOwner::Goal)) return false;
        const auto item=world.items.find(*questState.questRun.material);
        if(item==world.items.end() || item->second.mode!=0 || item->second.page!=1) throw std::runtime_error("Reserved imbue base changed before take");
        const uint32_t material=item->first;OnlineItemCommand take;take.action=OnlineItemAction::Take;take.item=material;take.itemRevision=item->second.revision;
        if(!submitItem(c,take,"quest-imbue-take")) return false;
        questState.questRun.materialNpc=npc->key;
        QuestPending p;p.action=QuestPending::Action::Take;p.item=material;p.sequence=c.session.read().world.itemRequest->sequence;
        p.deadline=Clock::now()+std::chrono::seconds(12);questState.questRun.pending=p;questState.questRun.materialPhase=1;cancelWalk(c);
        note("imbue-base-take-submitted",std::to_string(material));return false;
    }
    if(questState.questRun.materialPhase==1) {
        const auto item=world.items.find(*questState.questRun.material);
        if(item==world.items.end() || item->second.mode!=4 || !onlineItemOwnedBy(item->second,view.load.playerUnitId)) {
            wait("quest-imbue-cursor","Waiting for the actual reserved base on Cursor");checkQuestPending(c);return false;
        }
        if(!questState.questRun.materialNpc || !world.npcConversation || world.npcConversation->type!=1 ||
            world.npcRequested!=questState.questRun.materialNpc->id || world.npcConversation->source!=questState.questRun.materialNpc->id)
            throw std::runtime_error("Imbue NPC conversation ended with the reserved base on Cursor");
        const uint32_t material=item->first;questState.questRun.pending.reset();OnlineItemCommand imbue;imbue.action=OnlineItemAction::QuestService;
        imbue.npc=questState.questRun.materialNpc->id;imbue.item=material;imbue.itemRevision=item->second.revision;
        if(!submitItem(c,imbue,"quest-imbue")) return false;
        QuestPending p;p.action=QuestPending::Action::Imbue;p.item=material;p.sequence=c.session.read().world.itemRequest->sequence;
        p.deadline=Clock::now()+std::chrono::seconds(20);questState.questRun.pending=p;questState.questRun.materialPhase=2;
        note("imbue-service-submitted",std::to_string(material));return false;
    }
    wait("quest-imbue-result","Waiting for the original service result; no repeated imbue request");checkQuestPending(c);return false;
}
bool DebugAi::Impl::questActTravel(Context &c,const Goal &goal,const QuestStep &step) {
    if(c.town.read().area==step.area) {
        if(questFinished(c,goal)) return true;
        wait("quest-act-flags","Destination town observed; waiting for final quest/change flags");checkQuestPending(c);return false;
    }
    if(questState.questRun.pending) {wait("quest-act-travel-result","Waiting for the real normal act transfer");checkQuestPending(c);return false;}
    if(waypointBeforeDeparture(c)) return false;
    const auto npc=find(c,*step.target);
    if(!npc) {if(!approachKnown(c,*step.target)) explore(c,"the actual NPC act travel service");return false;}
    if (!prepareNpcService(c, npc->key, DebugActionOwner::Goal)) return false;
    const auto &conversation=c.town.read().npcConversation;
    if(!conversation || !conversation->travelDestination || conversation->travelDestination!=step.area) {
        if(actionDeadline==Clock::time_point{}) setActionDeadline(std::chrono::seconds(20),false);
        if(Clock::now()>=actionDeadline) throw std::runtime_error("NPC did not offer the actual act travel service");
        wait("quest-act-service","Waiting for the eligible act travel service");return false;
    }
    if(!c.control.npcTravel(*conversation->travelDestination)) {wait("quest-act-service","Normal NPC travel was not accepted");return false;}
    QuestPending p;p.action=QuestPending::Action::Travel;p.unit=npc->key;p.deadline=Clock::now()+std::chrono::seconds(30);
    questState.questRun.pending=p;cancelWalk(c);note("quest-act-travel-submitted",std::to_string(*step.area));return false;
}
}
