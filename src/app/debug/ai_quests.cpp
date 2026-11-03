#include "ai_impl.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace d2x {
void DebugAi::Impl::configureQuest(Goal &goal) const {
    static constexpr std::array<std::pair<std::string_view,QuestId>,27> quests{{
        {"den-of-evil",QuestId::DenOfEvil},{"sisters-burial-grounds",QuestId::SistersBurialGrounds},
        {"search-for-cain",QuestId::SearchForCain},{"forgotten-tower",QuestId::ForgottenTower},
        {"tools-of-the-trade",QuestId::ToolsOfTheTrade},{"sisters-to-the-slaughter",QuestId::SistersToTheSlaughter},
        {"radaments-lair",QuestId::RadamentsLair},{"horadric-staff",QuestId::HoradricStaff},
        {"tainted-sun",QuestId::TaintedSun},{"arcane-sanctuary",QuestId::ArcaneSanctuary},
        {"summoner",QuestId::Summoner},{"seven-tombs",QuestId::SevenTombs},
        {"golden-bird",QuestId::GoldenBird},{"blade-of-the-old-religion",QuestId::BladeOfTheOldReligion},
        {"lam-esens-tome",QuestId::LamEsensTome},{"khalims-will",QuestId::KhalimsWill},
        {"blackened-temple",QuestId::BlackenedTemple},{"guardian",QuestId::Guardian},
        {"fallen-angel",QuestId::FallenAngel},{"hells-forge",QuestId::HellsForge},{"terrors-end",QuestId::TerrorsEnd},
        {"siege-on-harrogath",QuestId::SiegeOnHarrogath},{"rescue-on-mount-arreat",QuestId::RescueOnMountArreat},
        {"prison-of-ice",QuestId::PrisonOfIce},{"betrayal-of-harrogath",QuestId::BetrayalOfHarrogath},
        {"rite-of-passage",QuestId::RiteOfPassage},{"eve-of-destruction",QuestId::EveOfDestruction}}};
    const auto found=std::find_if(quests.begin(),quests.end(),[&](const auto &v){return v.first==goal.quest;});
    if(found==quests.end()) throw std::invalid_argument("No verified quest recipe: "+goal.quest);
    goal.questId=found->second;goal.questSlot=questDefinition(goal.questId).nativeSlot;
    if(questDefinition(goal.questId).act==1) {configureActTwoQuest(goal,goal.questId);return;}
    if(questDefinition(goal.questId).act==2) {configureActThreeQuest(goal,goal.questId);return;}
    if(questDefinition(goal.questId).act==3) {configureActFourQuest(goal,goal.questId);return;}
    if(questDefinition(goal.questId).act==4) {configureActFiveQuest(goal,goal.questId);return;}
    const auto town=definitions->area("Act 1 - Town");
    const auto object=[&](std::string_view name,int operation) {
        const auto id=definitions->object(std::string(name));
        for(size_t row=0;row<definitions->objects.rows().size();++row)
            if(definitions->objects.number(row,"Id")==id && definitions->objects.number(row,"OperateFn")==operation) return id;
        throw std::invalid_argument("Current MPQ object does not match the verified quest operation: "+std::string(name));
    };
    const auto talk=[&](std::string_view npc,uint16_t topic,uint16_t flags,bool optional=false,std::string item={},std::string produced={}) {
        QuestStep s;s.action=QuestAction::Talk;s.area=town;s.target=definitions->selector(Json{{"npc",npc}});
        s.topics={topic};s.flags=flags;s.optional=optional;s.item=std::move(item);s.produced=std::move(produced);
        s.skipPending=optional;goal.steps.push_back(std::move(s));
    };
    const auto hunt=[&](std::string_view level,Json target,uint16_t flags) {
        QuestStep s;s.action=QuestAction::Hunt;s.area=definitions->area(std::string(level));
        s.target=definitions->selector(target);s.flags=flags;goal.steps.push_back(std::move(s));
    };
    const auto home=[&]() {QuestStep s;s.action=QuestAction::ReturnTown;s.skipPending=false;goal.steps.push_back(s);};
    // Native topics/operations below are A1Q1..6 ScrollMessage and ObjMode
    // contracts. Areas and object/monster identities resolve against this MPQ.
    switch(goal.questId) {
    case QuestId::DenOfEvil: {
        talk("akara",64,4,true);
        QuestStep s;s.action=QuestAction::Clear;s.area=definitions->area("Act 1 - Cave 1");s.flags=2;goal.steps.push_back(s);
        home();talk("akara",76,1);break;
    }
    case QuestId::SistersBurialGrounds:
        talk("kashya",81,4,true);hunt("Act 1 - Graveyard",{{"monster","bloodraven"}},2);
        home();talk("kashya",92,1);break;
    case QuestId::SearchForCain: {
        talk("akara",97,4,true);
        QuestStep tree;tree.action=QuestAction::Acquire;tree.area=definitions->area("Act 1 - Wilderness 4");
        tree.item="bks";tree.produced="bkd";tree.source=object("Inifuss",12);goal.steps.push_back(tree);
        home();goal.steps.back().skipPending=true;talk("akara",112,0,false,"bks","bkd");goal.steps.back().skipPending=true;
        QuestStep read;read.action=QuestAction::ReadScroll;read.item="bkd";goal.steps.push_back(read);
        QuestStep stones;stones.action=QuestAction::Stones;stones.area=definitions->area("Act 1 - Wilderness 3");
        for(const auto name:{"StoneAlpha","StoneBeta","StoneGamma","StoneDelta","StoneLambda"}) stones.objects.push_back(object(name,9));
        goal.steps.push_back(stones);
        QuestStep portal;portal.action=QuestAction::Portal;portal.area=definitions->area("Act 1 - Tristram");
        portal.sourceArea=stones.area;
        // The fifth operation starts the Cairn missile/portal event. Keep
        // its observed stone neighborhood subscribed until the door arrives.
        portal.witness=Selector{2,object("StoneLambda",9),"activated Cairn stones"};goal.steps.push_back(portal);
        QuestStep cage;cage.action=QuestAction::Operate;cage.area=portal.area;cage.target=Selector{2,object("Gibbet",10),"Gibbet"};
        cage.flags=2;cage.witness=definitions->selector(Json{{"monster","cain1"}});goal.steps.push_back(cage);
        home();talk("akara",118,1,false,{},"rin");break;
    }
    case QuestId::ForgottenTower: {
        QuestStep tome;tome.action=QuestAction::Operate;tome.area=definitions->area("Act 1 - Wilderness 3");
        tome.target=Selector{2,object("TowerTome",6),"TowerTome"};tome.flags=4;tome.optional=true;tome.topics={127};goal.steps.push_back(tome);
        hunt("Act 1 - Crypt 3 E",{{"superunique","The Countess"}},1);home();talk("akara",143,1);break;
    }
    case QuestId::ToolsOfTheTrade: {
        talk("charsi",146,4,true);hunt("Act 1 - Barracks",{{"superunique","The Smith"}},0);
        QuestStep malus;malus.action=QuestAction::Acquire;malus.area=definitions->area("Act 1 - Barracks");
        malus.item="hdm";malus.source=definitions->malusSource();goal.steps.push_back(malus);
        home();talk("charsi",163,2,false,"hdm");
        if(goal.useServiceRewards) {
            QuestStep imbue;imbue.action=QuestAction::Imbue;imbue.area=town;imbue.target=definitions->selector(Json{{"npc","charsi"}});
            imbue.item="cap";imbue.skipPending=false;
            if(!definitions->item(imbue.item)) throw std::invalid_argument("MPQ has no normal cap for the requested debug imbue material");
            goal.steps.push_back(imbue);
        }
        break;
    }
    case QuestId::SistersToTheSlaughter: {
        talk("cain5",166,4,true);hunt("Act 1 - Catacombs 4",{{"monster","andariel"}},2);home();
        // A1Q6 ScrollMessage183 grants the reward before the independent
        // act-change word is written by the normal transfer.
        talk("warriv1",183,1);
        QuestStep passage;passage.action=QuestAction::ActTravel;passage.area=definitions->area("Act 2 - Town");
        passage.target=definitions->selector(Json{{"npc","warriv1"}});passage.skipPending=false;goal.steps.push_back(passage);break;
    }
    default:throw std::invalid_argument("Quest recipe is outside Act I");
    }
    // The same fact graph used by later acts also resumes A1. A return to
    // town is a consequence of a still-needed NPC action, never a separate
    // unconditional trip. Original topics/operations above remain unchanged.
    using F=QuestFact;using K=F::Kind;using Proof=std::vector<std::vector<F>>;
    auto steps=std::move(goal.steps);goal.questTown=town;
    const auto flag=[&](uint16_t mask,bool any=false) {F f;f.kind=K::Flags;f.quest=goal.questId;f.value=mask;f.any=any;return f;};
    const auto item=[](std::string code) {F f;f.kind=K::Item;f.item=std::move(code);return f;};
    const auto fact=[](K kind,uint16_t value=0) {F f;f.kind=kind;f.value=value;return f;};
    const auto node=[&](std::string name,size_t source,Proof proof,std::vector<size_t> needs={},bool retain=false) {
        const auto index=goal.steps.size();auto step=steps.at(source);step.skipPending=false;
        goal.steps.push_back(std::move(step));goal.requirements.push_back({std::move(name),std::move(needs),std::move(proof),retain,0});return index;
    };
    const auto done=flag(1),fieldDone=flag(0x2003,true);
    switch(goal.questId) {
    case QuestId::DenOfEvil:case QuestId::SistersBurialGrounds:case QuestId::SistersToTheSlaughter: {
        const auto intro=node("optional-assignment",0,{{flag(0x2007,true)}},{},true);
        const auto killed=node("field-objective",1,{{fieldDone}}, {intro},true);
        const auto reward=node("personal-reward",3,{{done}},{killed});
        if(goal.questId==QuestId::SistersToTheSlaughter)
            goal.roots={node("act-two-arrival",4,{{fact(K::ActChange,7)}},{reward})};
        else goal.roots={reward};
        break;
    }
    case QuestId::ForgottenTower: {
        const auto intro=node("optional-tower-tome",0,{{flag(0x2007,true)}},{},true);
        goal.roots={node("countess-personal-result",1,{{done}},{intro},true)};break;
    }
    case QuestId::ToolsOfTheTrade: {
        const auto intro=node("optional-charsi-assignment",0,{{flag(7,true)}},{},true);
        const auto smith=node("smith-or-owned-malus",1,{{item("hdm")},{flag(3,true)},{fact(K::Corpse)}},{intro},true);
        const auto malus=node("malus-owned",2,{{item("hdm")},{flag(3,true)}},{smith});
        const auto returned=node("malus-returned",4,{{flag(3,true)}},{malus});
        goal.roots={goal.useServiceRewards?node("charsi-imbue-used",5,{{done}},{returned}):returned};break;
    }
    case QuestId::SearchForCain: {
        const auto intro=node("optional-cain-assignment",0,{{flag(0x2007,true)}},{},true);
        const auto scroll=node("inifuss-scroll-owned",1,{{item("bks")},{item("bkd")},{fieldDone}},{intro});
        // Open the return route while already visiting Inifuss. Reuse the
        // normal waypoint interaction and require its actual unlock result.
        QuestStep waypoint;waypoint.action=QuestAction::Travel;waypoint.area=steps.at(1).area;waypoint.activateWaypoint=true;
        const auto waypointSource=steps.size();steps.push_back(waypoint);
        const auto woodWaypoint=node("dark-wood-waypoint",waypointSource,{{fact(K::Waypoint,*waypoint.area)},{fieldDone}},{scroll});
        // A waypoint may already have been opened on the way to the tree;
        // satisfying it must not erase the independent scroll requirement.
        const auto decoded=node("inifuss-scroll-decoded",3,{{item("bkd")},{fieldDone}},{scroll,woodWaypoint});
        const auto read=node("current-game-cairn-order",4,{{fact(K::CairnOrder)},{fieldDone}},{decoded});
        const auto tristram=*steps.at(6).area;
        const auto stones=node("cairn-portal-observed",5,{{fact(K::PortalObserved,tristram)},{fact(K::Area,tristram)},{fieldDone}},{read},true);
        const auto arrived=node("tristram-arrival",6,{{fact(K::Area,tristram)},{fieldDone}},{stones});
        const auto rescue=node("cain-rescued",7,{{fieldDone}},{arrived},true);
        goal.roots={node("cain-personal-reward",9,{{done}},{rescue})};break;
    }
    default:break;
    }
}
std::optional<uint16_t> DebugAi::Impl::questFlags(const Context &c,QuestId id) const {
    const auto &flags=c.session.read().world.quests.playerFlags;
    return flags?std::optional{(*flags)[questDefinition(id).nativeSlot]}:std::nullopt;
}
bool DebugAi::Impl::questFinished(const Context &c,const Goal &goal) const {
    const auto flags=questFlags(c,goal.questId);
    if(!flags) return false;
    if(!(*flags&1)) {
        if(goal.useServiceRewards) return false;
        // These native quests retain REWARDPENDING until the optional item
        // service is used. A5 also requires its final NPC acknowledgement.
        switch(goal.questId) {
        case QuestId::ToolsOfTheTrade:return (*flags&2)!=0;
        case QuestId::SiegeOnHarrogath:return (*flags&0x22)==0x22;
        case QuestId::BetrayalOfHarrogath:return (*flags&0x12)==0x12;
        default:return false;
        }
    }
    if(goal.questId==QuestId::RadamentsLair && (*flags&0x20)) return false;
    if(goal.questId==QuestId::ArcaneSanctuary && (*flags&2)) return false;
    if(goal.questId==QuestId::GoldenBird && ((*flags&0x20) || owned(c,"xyz"))) return false;
    if(goal.questId==QuestId::PrisonOfIce && ((*flags&0x480)!=0x480 || owned(c,"tr2"))) return false;
    if(questWeaponRestore && (goal.questId==QuestId::KhalimsWill || goal.questId==QuestId::BlackenedTemple || goal.questId==QuestId::Guardian || goal.questId==QuestId::HellsForge)) {
        const auto original=c.session.read().world.items.find(*questWeaponRestore);
        if(original==c.session.read().world.items.end() || original->second.mode!=1 || original->second.body!=4 ||
            !onlineItemOwnedBy(original->second,c.session.read().load.playerUnitId)) return false;
    }
    if(goal.questId==QuestId::Guardian)
        return c.session.read().world.quests.playerFlags && ((*c.session.read().world.quests.playerFlags)[23]&1);
    if(goal.questId==QuestId::TerrorsEnd)
        return c.session.read().world.quests.playerFlags && ((*c.session.read().world.quests.playerFlags)[28]&1);
    if(goal.questId==QuestId::SevenTombs)
        return c.session.read().world.quests.playerFlags && ((*c.session.read().world.quests.playerFlags)[15]&1);
    // Native Act-I change record (slot 7) is separate from Andariel's reward.
    return goal.questId!=QuestId::SistersToTheSlaughter ||
        (c.session.read().world.quests.playerFlags && ((*c.session.read().world.quests.playerFlags)[7]&1));
}
bool DebugAi::Impl::questTransaction(const Context &c) const {
    if(cursor>=goals.size() || goals[cursor].kind!=Kind::Quest || supplyStage!=SupplyStage::None) return false;
    if(questRun.pending && questRun.pending->sequence) return true;
    if(questRun.recoveringItem && (onlineHasCursorItem(c.session.read().world.items,c.session.read().load.playerUnitId) ||
        c.session.read().world.storage.kind!=OnlineStorageKind::None)) return true;
    if(questRun.step>=goals[cursor].steps.size()) return false;
    const auto action=goals[cursor].steps[questRun.step].action;
    const auto &world=c.session.read().world;
    return (action==QuestAction::Imbue && questRun.materialPhase>0) ||
        (action==QuestAction::Service && questRun.materialPhase>0) ||
        (action==QuestAction::EquipWeapon && questRun.materialPhase>0) ||
        (action==QuestAction::Combine && (world.storage.kind==OnlineStorageKind::Cube || world.storage.requested==OnlineStorageKind::Cube)) ||
        (action==QuestAction::SubmitStaff && questRun.materialNpc && world.staffSource==questRun.materialNpc->id);
}
void DebugAi::Impl::questStepDone(Context &c,std::string evidence) {
    if(cursor<goals.size() && questRun.step<goals[cursor].steps.size() &&
        goals[cursor].steps[questRun.step].publishesTombSymbol && questRun.pending &&
        questRun.pending->refreshIssued && questRun.pending->statusRequested && tombSymbol && *tombSymbol<7)
        journalReadGame=c.session.read().gameGeneration;
    releaseCombat(c);
    if(c.session.read().world.npcRequested) c.control.closeNpc();
    if(c.session.read().world.waypointSource) c.control.useWaypoint(0);
    cancelWalk(c);note("quest-step-completed",std::move(evidence));
    if(cursor<goals.size() && !goals[cursor].requirements.empty()) {
        auto observations=std::move(questRun.observations);
        if(goals[cursor].requirements.at(questRun.step).retainObservation) observations.insert(questRun.step);
        const auto completed=questRun.step;questRun={};questRun.initialized=true;questRun.step=completed;
        questRun.observations=std::move(observations);resetStep();return;
    }
    const auto step=questRun.step+1;questRun={};questRun.initialized=true;questRun.step=step;resetStep();
}
bool DebugAi::Impl::prepareQuest(Context &c,const Goal &goal) {
    if(goal.kind!=Kind::Quest) return true;
    if(supplyStage!=SupplyStage::None) return true;
    const auto flags=questFlags(c,goal.questId);
    if(!flags) {wait("quest-state","Waiting for actual native character quest flags");return false;}
    if(questRun.initialInventory || questRun.recoveringItem || (!questRun.initialized && onlineHasCursorItem(c.session.read().world.items,c.session.read().load.playerUnitId)))
        if(!canInput(c,DebugActionOwner::Goal,DebugAllInputs) || !recoverQuestInventory(c)) return false;
    if(!questRun.pending && !questTransaction(c)) {
        // D2MOO D2C_OriginalQuestFlags: COMPLETEDNOW means this player
        // cannot obtain this game's result. Reward/pending/primary evidence
        // takes precedence: never discard an earned but unclaimed reward.
        // QUESTS_RefreshStatus also reports status 12 for a game whose
        // objective finished without this player's PRIMARYGOALDONE. A late
        // join need not have been present for SetCompletionFlag's 0x4000.
        const auto status=c.session.read().world.quests.statuses[goal.questSlot];
        if(((*flags&0x4000) || status==12) && !(*flags&0x2003)) {
            advance(c,std::string(status==12?"Native quest status 12 excludes this character in this game: ":
                "Native COMPLETEDNOW excludes this character in this game: ")+goal.quest,true);return false;
        }
        if(goal.questId==QuestId::SearchForCain && (*flags&0x8000) && !(*flags&3)) {
            advance(c,"Cain rescue is already unavailable in this save (COMPLETEDBEFORE)",true);return false;
        }
    }
    if(!questRun.initialized) {
        questSpawnAnchor.reset();
        const auto &clue=c.session.read().world.quests;
        const auto arcane=questFlags(c,QuestId::ArcaneSanctuary);
        // StatusCallback includes the real tomb offset for an already-read
        // journal. The zero field sent for unrelated progress is insufficient.
        if(!tombSymbol && arcane && (*arcane&0x2001) && clue.staffTombRevision &&
            clue.staffTombOffset && *clue.staffTombOffset<7) {
            tombSymbol=clue.staffTombOffset;note("quest-initial-tomb-symbol-observed",std::to_string(*tombSymbol));
        }
        if(questFinished(c,goal)) {
            advance(c,std::string((*flags&1)?"Already completed native quest: ":
                "Quest objectives completed; unused service reward retained: ")+goal.quest);return false;
        }
        if(c.session.read().load.act!=questDefinition(goal.questId).act) {
            if(!goal.questTown) throw std::runtime_error("Quest recipe has no verified native town");
            if(!canInput(c,DebugActionOwner::Goal,DebugAllInputs)) return false;
            if(!waypointMemoryRevision && knownWaypointAreas.empty()) {
                if(!reachTown(c)) return false;
                activateWaypoint(c);wait("quest-act-waypoint","Observing the normal town waypoint menu before cross-act recovery");return false;
            }
            travel(c,*goal.questTown);return false;
        }
        questRun.initialized=true;note("quest-started",goal.quest);
    }
    for(const auto &step:goal.steps) if(step.action==QuestAction::Hunt && step.witness && step.area==area)
        if(const auto anchor=find(c,*step.witness,true);anchor && anchor->position) questSpawnAnchor=std::pair{*area,*anchor->position};
    for(const auto &step:goal.steps) if(step.spawnSource && step.area==area)
        if(const auto anchor=find(c,*step.spawnSource,true);anchor && anchor->position) questSpawnAnchor=std::pair{*area,*anchor->position};
    if(!goal.requirements.empty()) return selectQuestRequirement(c,goal);
    while(questRun.step<goal.steps.size() && goal.steps[questRun.step].skipPending && (*flags&3))
        questStepDone(c,"Native reward/pending flag already satisfies the field objective");
    if(questRun.step>=goal.steps.size()) {
        if(!questFinished(c,goal)) {wait("quest-result","Waiting for the actual final quest and act-change flags");return false;}
        advance(c,"Native quest/reward completion observed: "+goal.quest);return false;
    }
    return true;
}
DebugAi::Impl::Goal DebugAi::Impl::questLeaf(const Goal &goal) const {
    if(goal.kind!=Kind::Quest || questRun.step>=goal.steps.size() || supplyStage!=SupplyStage::None) return goal;
    const auto &step=goal.steps[questRun.step];Goal leaf;
    leaf.area=step.area;leaf.target=step.target;leaf.item=step.item;leaf.sourceObject=step.source;
    if(goal.questTown && step.area==goal.questTown && area!=goal.questTown &&
        step.action!=QuestAction::Portal && step.action!=QuestAction::ActTravel) {
        leaf.kind=Kind::ReturnTown;return leaf;
    }
    switch(step.action) {
    case QuestAction::Hunt:case QuestAction::Rescue:leaf.kind=Kind::Hunt;break;
    case QuestAction::Acquire:leaf.kind=Kind::Acquire;break;
    case QuestAction::ReturnTown:leaf.kind=Kind::ReturnTown;break;
    case QuestAction::SearchDrop:
        leaf.kind=Kind::Quest;
        if(!leaf.area && questRun.materialPhase<step.searchAreas.size())
            leaf.area=step.searchAreas[questRun.materialPhase];
        break;
    default:leaf.kind=Kind::Quest;break;
    }
    return leaf;
}
void DebugAi::Impl::checkQuestPending(const Context &c) const {
    if(!questRun.pending) return;
    const auto &p=*questRun.pending;const auto &request=c.session.read().world.itemRequest;
    if(p.resultObserved) return;
    if(p.sequence && request) {
        if(p.sequence!=request->sequence) throw std::runtime_error("Quest item operation was replaced before its result was observed");
        if(request->state==OnlineItemRequest::State::Rejected || request->state==OnlineItemRequest::State::TimedOut || request->state==OnlineItemRequest::State::Interrupted)
            throw std::runtime_error("Quest item operation did not confirm its actual result; no blind retry");
    }
    if(!questWaitRequiresInput(c) && Clock::now()>=p.deadline) throw std::runtime_error("Quest operation's actual result was not observed: "+phase);
}
void DebugAi::Impl::observeQuestResult(Context &c) {
    if(!questRun.pending || cursor>=goals.size() || goals[cursor].kind!=Kind::Quest || questRun.step>=goals[cursor].steps.size()) return;
    auto &pending=*questRun.pending;if(pending.resultObserved) return;
    const auto &goal=goals[cursor];const auto &step=goal.steps[questRun.step];const auto &world=c.session.read().world;
    const auto flags=questFlags(c,step.stateQuest.value_or(goal.questId));
    const auto item=world.items.find(pending.item);
    const bool ours=item!=world.items.end() && onlineItemOwnedBy(item->second,c.session.read().load.playerUnitId);
    const auto unit=pending.unit?world.units.find(*pending.unit):world.units.end();
    // Operate66 sends private sound 19 for a denied neutral door. Correlate
    // it with this exact submission, even if the reply preceded this AI tick.
    if(pending.action==QuestPending::Action::Object && step.action==QuestAction::Portal &&
        goal.questId==QuestId::EveOfDestruction && area==step.sourceArea &&
        unit!=world.units.end() && unit->second.classId==547 && unit->second.mode==0 &&
        unit->second.assignmentRevision==pending.assignment && pending.unit) {
        const auto player=c.session.read().load.playerUnitId;
        if(player && std::any_of(world.combatEvents.begin(),world.combatEvents.end(),[&](const auto &event) {
            return event.sequence>pending.beforeRevision && event.packet==0x2c && event.source==OnlineUnitKey{0,*player} && event.auxiliary==19;
        })) {
            const auto personal=questFlags(c,QuestId::RiteOfPassage);
            if(!personal || !(*personal&1) || summitTrialKills)
                throw std::runtime_error("Summit passage refused after checking the current trial and personal entitlement");
            summitTrialKills.emplace();
            questRun.pending.reset();
            note("summit-trial-required","The door refused personal passage in this game; returning to the altar");
            return;
        }
    }
    bool observed=false;
    switch(pending.action) {
    case QuestPending::Action::RecoverTake:observed=ours && item->second.mode==4;break;
    case QuestPending::Action::RecoverPlace:observed=ours && item->second.mode==0 && item->second.page==1;break;
    case QuestPending::Action::RecoverOpen:
        observed=world.storage.kind!=OnlineStorageKind::None && world.storage.source==pending.item;break;
    case QuestPending::Action::RecoverClose:
        observed=world.storage.kind==OnlineStorageKind::None && world.storage.requested==OnlineStorageKind::None;break;
    case QuestPending::Action::Take:observed=ours && item->second.mode==4;break;
    case QuestPending::Action::Place:observed=ours && item->second.mode==0 && item->second.page==1;break;
    case QuestPending::Action::StoreCube:observed=ours && item->second.mode==0 && item->second.page==4;break;
    case QuestPending::Action::Pickup:observed=ours && item->second.mode!=3;break;
    case QuestPending::Action::OpenCube:observed=world.storage.kind==OnlineStorageKind::Cube && world.storage.source==pending.item;break;
    case QuestPending::Action::CloseCube:observed=world.storage.kind==OnlineStorageKind::None && world.storage.requested==OnlineStorageKind::None;break;
    case QuestPending::Action::Transmute:
        observed=accessibleItem(c,step.item).has_value() && (step.ingredients.empty()? !owned(c,"msf") && !owned(c,"vip"):
            std::none_of(step.ingredients.begin(),step.ingredients.end(),[&](const auto &code){return owned(c,code);}));break;
    case QuestPending::Action::Use: {
        if(step.item=="xyz") {observed=!owned(c,step.item) && flags && (*flags&1) && !(*flags&0x20);break;}
        if(step.item=="tr2") {observed=!owned(c,step.item) && flags && (*flags&0x80);break;}
        const auto points=world.playerAttributes.find(5);
        observed=!owned(c,step.item) && questRun.skillPointsBefore && points!=world.playerAttributes.end() &&
            uint64_t(points->second)>=uint64_t(*questRun.skillPointsBefore)+1;break;
    }
    case QuestPending::Action::Scroll:observed=world.quests.cainStones && world.quests.cainStonesRevision>pending.beforeRevision;break;
    case QuestPending::Action::Travel:observed=step.area && c.town.read().area==step.area && questFinished(c,goal);break;
    case QuestPending::Action::Portal:observed=step.area && c.town.read().area==step.area;break;
    case QuestPending::Action::SubmitStaff:
        if(world.staffRevision>pending.beforeRevision && world.staffResult==4) throw std::runtime_error("The original orifice rejected this staff");
        {const auto staff=questFlags(c,QuestId::HoradricStaff);observed=staff && (*staff&1) && !owned(c,step.item);}break;
    case QuestPending::Action::Introduction:observed=world.quests.revision>pending.beforeRevision;break;
    case QuestPending::Action::Topic:
        observed=flags && step.flags && (*flags&step.flags)==step.flags && !(*flags&step.clearFlags);
        if(step.optional) observed|=world.quests.revision>pending.beforeRevision && world.quests.statuses[questDefinition(step.stateQuest.value_or(goal.questId)).nativeSlot].value_or(0)>0;
        if(!step.item.empty() && step.produced.empty()) observed=observed && !owned(c,step.item);
        if(!step.produced.empty()) observed=observed && owned(c,step.produced);
        if(!step.produced.empty() && step.produced!="rin")
            observed=owned(c,step.produced) && (step.item.empty() || !owned(c,step.item));
        if(step.produced=="rin" && questRun.rewardTracked) {
            const bool reward=std::any_of(world.items.begin(),world.items.end(),[&](const auto &entry) {
                const auto &[id,ring]=entry;return ring.code=="rin" && !questRun.rewardBefore.contains(id) &&
                    (onlineItemOwnedBy(ring,c.session.read().load.playerUnitId) || ring.mode==3);
            });
            observed=observed && reward;
        }
        if((step.skillPointReward || goal.questId==QuestId::DenOfEvil) && step.flags==1 && questRun.skillPointsBefore) {
            const auto points=world.playerAttributes.find(5);
            observed=observed && points!=world.playerAttributes.end() && uint64_t(points->second)>=uint64_t(*questRun.skillPointsBefore)+std::max(1u,step.skillPointReward);
        }
        if(goal.questId==QuestId::SistersBurialGrounds && step.flags==1 && questRun.rewardTracked)
            observed=observed && (world.hireling || world.deadHirelingName);
        if(step.witness) if(const auto witness=find(c,*step.witness,true);witness && witness->mode && *witness->mode!=0) observed=true;
        break;
    case QuestPending::Action::Object:
        if(!goal.requirements.empty() && std::any_of(goal.requirements.at(questRun.step).alternatives.begin(),goal.requirements.at(questRun.step).alternatives.end(),[](const auto &all) {
            return std::any_of(all.begin(),all.end(),[](const auto &fact){return fact.kind==QuestFact::Kind::ObjectOpened;});
        })) {observed=questRequirementSatisfied(c,goal,questRun.step);break;}
        if(step.action==QuestAction::SubmitStaff) observed=pending.unit && world.staffSource==pending.unit->id;
        else if(step.action==QuestAction::Portal) {
            if(step.witness) {
                const auto entrance=step.target?find(c,*step.target,true):std::nullopt;
                observed=entrance && c.town.permitsInteraction(c.session.read(),entrance->key);
            } else observed=unit!=world.units.end() && unit->second.assignmentRevision==pending.assignment &&
                unit->second.mode && *unit->second.mode!=0 &&
                std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &target) {
                    return target.unit==unit->first && (target.interaction==OnlineMapInteraction::Portal ||
                        (step.target && target.interaction==OnlineMapInteraction::Door && unit->second.classId==step.target->identity));
                });
        }
        else if(step.publishesTombSymbol) observed=pending.refreshIssued && flags && (*flags&1) &&
            world.quests.staffTombRevision>pending.beforeRevision && world.quests.staffTombOffset && *world.quests.staffTombOffset<7;
        else if(step.action==QuestAction::SmashOrb) observed=flags && (*flags&1) && !owned(c,step.item);
        else if(step.action==QuestAction::SmashForge) observed=flags && (*flags&3) && !owned(c,step.item);
        else if(step.action==QuestAction::Rescue) observed=flags && (*flags&0x2003);
        else if(step.status) observed=questRequirementSatisfied(c,goal,questRun.step) ||
            (pending.refreshIssued && pending.statusRequested && world.quests.revision>pending.beforeRevision &&
             world.quests.statuses[questDefinition(step.stateQuest.value_or(goal.questId)).nativeSlot]==step.status);
        else if(step.flags) observed=flags && (*flags&step.flags)==step.flags;
        else if(step.witness && !step.topics.empty()) observed=false; // A reading completes on its witness below.
        else observed=unit!=world.units.end() && unit->second.assignmentRevision==pending.assignment && unit->second.mode && *unit->second.mode!=0;
        if(step.witness && step.action!=QuestAction::Portal) observed|=find(c,*step.witness).has_value();
        if(step.optional) observed|=world.quests.statuses[goal.questSlot].value_or(0)>0;
        break;
    case QuestPending::Action::Imbue:
        observed=questRun.materialPhase==4 && flags && (*flags&1);
        if(!observed && questRun.materialPhase==2 && world.itemRequest && world.itemRequest->sequence==pending.sequence &&
            world.itemRequest->state==OnlineItemRequest::State::Updated)
            for(const auto &[id,entry]:c.inventory.read().items) {
                const auto wire=world.items.find(id);
                if(wire!=world.items.end() && wire->second.code==step.item &&
                    (questRun.material==id || !questRun.materialBefore.contains(id)) && entry.decoded && entry.quality==6 &&
                    onlineItemOwnedBy(wire->second,c.session.read().load.playerUnitId)) {observed=true;break;}
            }
        break;
    case QuestPending::Action::Service:
        if(questRun.materialPhase==4) {observed=flags && (*flags&1);break;}
        if(ours) if(const auto decoded=c.inventory.read().items.find(pending.item);decoded!=c.inventory.read().items.end() && decoded->second.decoded)
            observed=goals[cursor].questId==QuestId::SiegeOnHarrogath?decoded->second.sockets>0:!decoded->second.personalizedName.empty();
        break;
    }
    if(observed) {pending.resultObserved=true;note("quest-pending-result-observed");}
    else if(pending.action==QuestPending::Action::Portal) {
        const auto now=Clock::now();
        if(now<pending.deadline && recoverTravel(c,true)) {questRun.pending.reset();return;}
        // A selectable portal may ignore the first click during opening, or
        // its server approach may be interrupted. An idle client need not
        // receive another position correction. Re-enter ordinary interaction
        // only in the same loaded source scene, with the same assignment.
        if(now>=pending.retryAt && now<pending.deadline && submittedTravel && !inputBlocked && !c.moving) {
            const auto &sent=*submittedTravel;const auto &view=c.session.read();
            if(view.stage==OnlineStage::ProtocolReady && view.load.serverLoadComplete &&
                view.gameGeneration==sent.game && world.areaGeneration==sent.world && area==sent.sourceArea &&
                pending.unit==sent.unit && unit!=world.units.end() && unit->second.assignmentRevision==sent.assignment &&
                c.town.interactionReady(view,sent.unit,c.position)) {
                note("quest-portal-retry","Destination absent; retrying normal interaction with "+std::to_string(sent.unit.id));
                cancelWalk(c);submittedTravel.reset();questRun.pending.reset();return;
            }
        }
        checkQuestPending(c);
    }
    else checkQuestPending(c);
}
void DebugAi::Impl::observeQuestInteraction(Context &c,const Json &intent) {
    if(questRun.pending || cursor>=goals.size() || goals[cursor].kind!=Kind::Quest) return;
    if(intent.value("purpose",std::string{})=="quest-orb" || intent.value("purpose",std::string{})=="quest-forge") {
        const bool forge=intent.value("purpose",std::string{})=="quest-forge";
        const unsigned hits=forge?3:2;
        if(questRun.step>=goals[cursor].steps.size() || goals[cursor].steps[questRun.step].action!=(forge?QuestAction::SmashForge:QuestAction::SmashOrb) ||
            questRun.materialPhase>=hits) throw std::runtime_error("Unexpected extra quest weapon operation");
        if(forge && !questRun.materialPhase) {
            questRun.rewardBefore.clear();for(const auto &[id,item]:c.session.read().world.items) {(void)item;questRun.rewardBefore.insert(id);}
        }
        const auto now=Clock::now();++questRun.materialPhase;++interactions;
        questRun.sweepReady=now+std::chrono::milliseconds(600);
        if(questRun.materialPhase==hits) {
            QuestPending pending;pending.action=QuestPending::Action::Object;
            pending.unit=OnlineUnitKey{intent.at("unitType").get<uint8_t>(),intent.at("unitId").get<uint32_t>()};
            const auto unit=c.session.read().world.units.find(*pending.unit);
            if(unit!=c.session.read().world.units.end()) pending.assignment=unit->second.assignmentRevision;
            pending.deadline=now+std::chrono::seconds(forge?40:20);questRun.pending=pending;
        }
        note(forge?"quest-forge-operation-submitted":"quest-orb-operation-submitted",std::to_string(questRun.materialPhase));cancelWalk(c);return;
    }
    QuestPending p;p.action=intent.value("purpose",std::string{})=="quest-portal"?QuestPending::Action::Portal:QuestPending::Action::Object;
    if(intent.value("activation",false)) p.action=QuestPending::Action::Object;
    p.unit=OnlineUnitKey{intent.at("unitType").get<uint8_t>(),intent.at("unitId").get<uint32_t>()};
    if(intent.value("activation",false)) {
        const auto &sent=c.control.interactionSubmission();
        p.beforeRevision=sent && sent->target==*p.unit?sent->combatSequence:UINT64_MAX;
    }
    if(p.action==QuestPending::Action::Portal) rememberTravel(c,*p.unit);
    const auto unit=c.session.read().world.units.find(*p.unit);
    if(unit!=c.session.read().world.units.end()) p.assignment=unit->second.assignmentRevision;
    const auto now=Clock::now();p.deadline=now+std::chrono::seconds(30);
    if(p.action==QuestPending::Action::Portal) {
        const auto &step=goals[cursor].steps.at(questRun.step);
        if(questRun.portalDeadline==Clock::time_point{})
            questRun.portalDeadline=now+std::chrono::seconds(step.resultWaitSeconds?step.resultWaitSeconds:unsigned(eventPolicy.waitSeconds));
        p.deadline=questRun.portalDeadline;p.retryAt=now+std::chrono::seconds(3);
    }
    questRun.pending=p;
    ++interactions;note("quest-interaction-submitted",std::to_string(p.unit->id));cancelWalk(c);
}
void DebugAi::Impl::quest(Context &c,const Goal &goal) {
    if(questRun.step>=goal.steps.size()) return;
    auto step=goal.steps[questRun.step];
    if(!recoverQuestInventory(c,&step)) return;
    const auto stateQuest=step.stateQuest.value_or(goal.questId);
    const auto state=questFlags(c,stateQuest);
    const auto parentState=questFlags(c,goal.questId);
    const auto tombOffset=tombSymbol;
    if(step.skipGranted) if(const auto prerequisite=questFlags(c,*step.skipGranted);prerequisite && (*prerequisite&1)) {
        questStepDone(c,"Actually granted prerequisite skips its old material/introductory steps");return;
    }
    if(step.item=="ass" && !questRun.pending && !questRun.skillPointsBefore && state &&
        (*state&0x2003) && !(*state&0x20) && !owned(c,"ass")) {
        questStepDone(c,"Actual Radament flags confirm the skill book was already used");return;
    }
    if((step.skipAny && state && (*state&step.skipAny)) ||
        (step.parentSkipAny && parentState && (*parentState&step.parentSkipAny)) ||
        (step.skipTombKnown && tombOffset && *tombOffset<7) ||
        (area && std::find(step.skipAreas.begin(),step.skipAreas.end(),*area)!=step.skipAreas.end())) {
        questStepDone(c,"Actual prerequisite state already satisfies this step");return;
    }
    if(step.trueTomb) {
        if(!tombOffset || *tombOffset>=7) {wait("quest-tomb-symbol","Waiting for Horazon's actual published tomb symbol");return;}
        step.area=definitions->area("Act 2 - Tomb Tal "+std::to_string(*tombOffset+1));
    }
    if(step.optional && step.flags) {
        const auto flags=state;
        if(flags && (*flags&step.flags)==step.flags) {
            questStepDone(c,"Optional introduction already confirmed by native quest flags");return;
        }
    }
    if(step.action==QuestAction::Talk && step.flags && state && (*state&step.flags)==step.flags &&
        !(*state&step.clearFlags) && step.item.empty() && step.produced.empty() && !questRun.pending &&
        !questRun.rewardTracked && !questRun.skillPointsBefore) {
        // Reused prerequisites can already be satisfied while we are in a
        // special area with no static route back to that NPC. Test the same
        // replicated dialogue postcondition before arranging needless travel.
        questStepDone(c,"Actual dialogue prerequisite already confirmed");return;
    }
    if(step.action==QuestAction::Acquire && (owned(c,step.item) || (!step.produced.empty() && owned(c,step.produced)))) {
        questStepDone(c,"Already-owned quest item satisfies acquisition: "+step.item);return;
    }
    if(step.action!=QuestAction::Portal && step.action!=QuestAction::ActTravel && step.area && (area!=step.area || travelDestination)) {
        const bool arrived=goal.questTown && step.area==goal.questTown?reachTown(c):travel(c,*step.area);
        if(!arrived) return;
    }
    bool done=false;
    switch(step.action) {
    case QuestAction::Travel:
        done=!step.activateWaypoint || (area && knownWaypointAreas.contains(*area));
        if(!done && !activateWaypoint(c,true)) explore(c,"required quest-route waypoint",false,ExplorationFocus::Units);
        break;
    case QuestAction::UseItem:done=questUseItem(c,step);break;
    case QuestAction::Combine:done=questCombine(c,step);break;
    case QuestAction::SubmitStaff:done=questSubmitStaff(c,step);break;
    case QuestAction::SearchDrop:done=questSearchDrop(c,step);break;
    case QuestAction::EquipWeapon:done=questEquipWeapon(c,step);break;
    case QuestAction::SmashOrb:done=questSmashOrb(c,step);break;
    case QuestAction::SmashForge:done=questSmashForge(c,step);break;
    case QuestAction::Service:done=questService(c,step);break;
    case QuestAction::Rescue:done=questRescue(c,step);break;
    case QuestAction::Encounter:done=questEncounter(c,step);break;
    case QuestAction::Talk:done=questTalk(c,goal,step);break;
    case QuestAction::ReturnTown:done=reachTown(c);break;
    case QuestAction::Acquire:
        done=(!step.produced.empty() && owned(c,step.produced)) || acquireItem(c,questLeaf(goal));break;
    case QuestAction::ReadScroll:done=questReadScroll(c,step);break;
    case QuestAction::Stones:case QuestAction::Operate:done=questOperate(c,step);break;
    case QuestAction::Portal:done=questPortal(c,step);break;
    case QuestAction::Imbue:done=questImbue(c,goal,step);break;
    case QuestAction::ActTravel:done=questActTravel(c,goal,step);break;
    case QuestAction::Clear: {
        auto &population=populationMemory[*area];
        const auto flags=state;
        if((flags && (*flags&0x2002)) || (c.session.read().world.quests.denRemaining==0 &&
            c.session.read().world.quests.statuses[goal.questSlot]==5)) {done=true;break;}
        const auto remaining=c.session.read().world.quests.denRemaining;
        if(remaining!=questRun.denRemaining) {questRun.denRemaining=remaining;note("den-counter-observed",remaining?std::to_string(*remaining):"unknown");}
        std::optional<OnlineUnit> enemy;float nearest=std::numeric_limits<float>::max();
        for(const auto &[key,unit]:c.session.read().world.units) if(key.type==1 && unit.position && c.combat.hostile(unit) && c.combat.monsterTargetEligible(unit,false)) {
            const float distance=(Vec{float(unit.position->x),float(unit.position->y)}-c.position).length();
            if(distance<nearest) {nearest=distance;enemy=unit;}
        }
        if(enemy) {
            if(openDoor(c)) break;
            if(approachPoint(c,*enemy->position,"remaining assigned Den hostile",2)) break;
        }
        if(Clock::now()<population.readyAt) {
            wait("clear-room-assignments","Waiting briefly for units following the host's newly subscribed rooms");break;
        }
        // Retirement is not death. Reobserve a last assigned hostile after
        // its room left the subscription, rather than blindly sweeping rooms.
        const auto &world=c.session.read().world;const auto &rooms=c.town.navigationRooms();
        std::optional<OnlinePoint> remembered;float distance=std::numeric_limits<float>::max();
        for(auto it=population.enemies.begin();it!=population.enemies.end();) {
            const auto unit=world.units.find(it->first);const auto point=it->second.position;
            const auto room=std::find_if(rooms.begin(),rooms.end(),[&](const auto &r) {
                return r.level==area && point.x>=r.x && point.x<r.x+r.width && point.y>=r.y && point.y<r.y+r.height;
            });
            if(unit==world.units.end() && room!=rooms.end() &&
                world.rooms.contains({uint8_t(room->level),uint16_t(room->x/5),uint16_t(room->y/5)})) {
                it=population.enemies.erase(it);continue; // Actual resubscription invalidated this old position hint.
            }
            const float span=(Vec{float(point.x),float(point.y)}-c.position).length();
            if(span<distance) {distance=span;remembered=point;}
            ++it;
        }
        if(remembered) {
            if(distance<=2) {wait("clear-target-ready","Waiting for the observed remaining hostile's attackable state");break;}
            if(approachPoint(c,*remembered,"last observed remaining hostile",2)) break;
        }
        explore(c,"host room subscriptions that may reveal remaining hostiles");
        if(phase=="explore-wait" && !c.walk.active() && Clock::now()>=questRun.sweepReady) {
            // Refresh the authoritative counter without erasing empty-room
            // knowledge and walking the same clear rooms every two seconds.
            c.control.refreshQuests();
            questRun.sweepReady=Clock::now()+std::chrono::seconds(5);++questRun.sweeps;
            note("clear-status-requested",std::to_string(questRun.sweeps));
        }
        break;
    }
    case QuestAction::Hunt: {
        const auto flags=state;
        if(step.flags && flags && (*flags&step.flags)==step.flags) {done=true;break;}
        const auto unit=find(c,*step.target,true);
        if(unit && (onlineMonsterCorpse(*unit) || c.combat.monsterTargetEligible(*unit,false))) questRun.worldWait.reset();
        if(unit && onlineMonsterCorpse(*unit)) {
            if(step.flags && (!flags || (*flags&step.flags)!=step.flags)) {
                if(!questRun.pending) {
                    if(!c.control.refreshQuests()) break;
                    QuestPending p;p.action=QuestPending::Action::Object;p.unit=unit->key;p.assignment=unit->assignmentRevision;
                    p.deadline=Clock::now()+std::chrono::seconds(20);questRun.pending=p;
                }
                wait("quest-kill-entitlement","Actual corpse observed; waiting for the required personal quest flags");checkQuestPending(c);break;
            }
            // Return to the real reward NPC even if the full player flag
            // snapshot is deferred until the next conversation. The outer
            // quest never completes from a corpse alone.
            done=true;break;
        }
        if(!unit) {
            if(step.witness && questSpawnAnchor && questSpawnAnchor->first==area) {
                if(!questRun.encounterAnchored && approachPoint(c,questSpawnAnchor->second,"observed quest birth anchor",8)) break;
                const auto anchor=questSpawnAnchor->second;
                if((Vec{float(anchor.x),float(anchor.y)}-c.position).length()>10) {
                    explore(c,"reachable approach to the observed quest birth anchor");break;
                }
                questRun.encounterAnchored=true;
                waitForWorldResult(c,step,"Waiting at the actually observed birth anchor for the original boss assignment");break;
            }
            if(!approachKnown(c,*step.target)) explore(c,step.target->name,false,ExplorationFocus::Units);
            break;
        }
        if(!c.combat.hostile(*unit)) throw std::runtime_error("Quest hunt target is not an actual hostile");
        if(!c.combat.monsterTargetEligible(*unit,false)) {
            waitForWorldResult(c,step,"Waiting for the observed hunt target's actual attack eligibility");break;
        }
        target=unit->key;targetAssignment=unit->assignmentRevision;
        if(!approachPoint(c,*unit->position,"quest hunt target",2)) explore(c,"reachable quest target");
        break;
    }
    }
    if(done && !goal.requirements.empty()) {
        const auto &node=goal.requirements.at(questRun.step);
        const bool linkedObject=std::any_of(node.alternatives.begin(),node.alternatives.end(),[](const auto &all) {
            return std::any_of(all.begin(),all.end(),[](const auto &fact){return fact.kind==QuestFact::Kind::ObjectOpened;});
        });
        if(linkedObject && !questRequirementSatisfied(c,goal,questRun.step)) {
            if(!questRun.pending) {
                QuestPending pending;pending.action=QuestPending::Action::Object;pending.deadline=Clock::now()+std::chrono::seconds(20);
                if(step.target) if(const auto source=find(c,*step.target,true)) {pending.unit=source->key;pending.assignment=source->assignmentRevision;}
                questRun.pending=pending;
            }
            wait("quest-linked-object","Waiting for the linked quest object's actual opened mode: "+node.name);
            checkQuestPending(c);return;
        }
    }
    if(done) questStepDone(c,goal.quest+" step "+std::to_string(questRun.step));
}
}
