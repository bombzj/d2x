#include "ai_impl.hpp"
#include <stdexcept>

namespace d2x {
void DebugAi::Impl::configureActFiveQuest(Goal &goal,QuestId id) const {
    using F=QuestFact;using K=F::Kind;using Proof=std::vector<std::vector<F>>;
    const auto town=definitions->area("Act 5 - Town"),iceArea=definitions->area("Act 5 - Ice Cave 1A"),
        summit=definitions->area("Act 5 - Mountain Top"),throne=definitions->area("Act 5 - Throne Room"),
        chamber=definitions->area("Act 5 - World Stone");
    goal.questTown=town;
    const auto flag=[](QuestId q,uint16_t mask,bool any=false) {F f;f.kind=K::Flags;f.quest=q;f.value=mask;f.any=any;return f;};
    const auto item=[&](std::string code) {if(!definitions->item(code)) throw std::runtime_error("Missing native quest item: "+code);F f;f.kind=K::Item;f.item=std::move(code);return f;};
    const auto fact=[](K kind) {F f;f.kind=kind;return f;};
    const auto object=[&](std::string name,int operation) {
        const auto value=definitions->object(name);
        for(size_t row=0;row<definitions->objects.rows().size();++row)
            if(definitions->objects.number(row,"Id")==value && definitions->objects.number(row,"OperateFn")==operation)
                return Selector{2,value,std::move(name)};
        throw std::runtime_error("Act-V object does not match its native operation");
    };
    const auto node=[&](std::string name,QuestStep step,Proof proof,std::vector<size_t> needs={},bool retain=false) {
        step.skipPending=false;const auto index=goal.steps.size();goal.steps.push_back(std::move(step));
        goal.requirements.push_back({std::move(name),std::move(needs),std::move(proof),retain,0});return index;
    };
    const auto talk=[&](QuestId q,std::string npc,uint16_t topic,uint16_t flags,uint16_t level=0) {
        QuestStep s;s.action=QuestAction::Talk;s.stateQuest=q;s.area=level?level:town;
        s.target=definitions->selector(Json{{"npc",npc}});s.topics={topic};s.flags=flags;return s;
    };
    const auto hunt=[&](QuestId q,uint16_t level,Json target) {
        QuestStep s;s.action=QuestAction::Hunt;s.stateQuest=q;s.area=level;s.target=definitions->selector(target);return s;
    };
    const auto intro=[&](QuestId q,std::string npc,uint16_t topic) {
        auto s=talk(q,std::move(npc),topic,4);s.optional=true;
        return node("optional-assignment-"+std::to_string(questDefinition(q).nativeSlot),s,{{flag(q,0x2007,true)}},{},true);
    };
    const auto siege=QuestId::SiegeOnHarrogath,rescue=QuestId::RescueOnMountArreat,ice=QuestId::PrisonOfIce,
        betrayal=QuestId::BetrayalOfHarrogath,trial=QuestId::RiteOfPassage,baal=QuestId::EveOfDestruction;
    auto s=hunt(siege,definitions->area("Act 5 - Siege 1"),Json{{"superunique","Siege Boss"}});s.flags=2;
    const auto shenk=node("shenk-dead",s,{{flag(siege,0x2003,true)},{fact(K::Corpse)}},{intro(siege,"larzuk",20077)},true);
    const auto socketReady=node("larzuk-socket-entitlement",talk(siege,"larzuk",20090,0x22),{{flag(siege,0x22)},{flag(siege,1)}},{shenk});
    auto socket=socketReady;
    if(goal.useServiceRewards) {
        s={};s.action=QuestAction::Service;s.stateQuest=siege;s.area=town;s.target=definitions->selector(Json{{"npc","larzuk"}});s.item="cap";
        socket=node("larzuk-socket-reward-used",s,{{flag(siege,1)}},{socketReady});
    }

    s=hunt(rescue,definitions->area("Act 5 - Barricade 1"),Json{{"monster","prisondoor"}});s.action=QuestAction::Rescue;
    s.witness=definitions->selector(Json{{"monster","act5pow"}});
    const auto freed=node("prisoners-rescued",s,{{flag(rescue,0x2003,true)}},{intro(rescue,"qual-kehk",20096)});
    s=talk(rescue,"qual-kehk",20110,1);s.rewardCodes={"r07","r08","r09"};
    const auto runes=node("qual-kehk-runes",s,{{flag(rescue,1)}},{freed});

    const auto iceIntro=intro(ice,"malah",20116);const auto frozen=object("fana",67);
    s={};s.action=QuestAction::Operate;s.stateQuest=ice;s.area=iceArea;s.target=frozen;s.topics={20131};s.status=3;s.refreshFlags=true;
    // A5Q3's reading advances the game's fState/fLastState, not the
    // character's ENTERAREA bit. A normal query's status3 also resumes an
    // already-read game; carrying ice satisfies the same dependency.
    auto readingKnown=fact(K::Status);readingKnown.quest=ice;readingKnown.value=3;
    const auto found=node("frozen-anya-spoken",s,{{readingKnown},{item("ice")},{flag(ice,0x2003,true)}},{iceIntro},true);
    s=talk(ice,"malah",20127,0);s.produced="ice";
    const auto potion=node("malah-defrost-potion",s,{{item("ice")},{flag(ice,0x2003,true)}},{found});
    s={};s.action=QuestAction::Operate;s.stateQuest=ice;s.area=iceArea;s.target=frozen;s.item="ice";s.flags=2;
    const auto thawed=node("anya-thawed",s,{{flag(ice,0x2003,true)}},{potion},true);
    s=talk(ice,"malah",20132,0);s.produced="tr2";
    const auto scroll=node("resistance-scroll-owned",s,{{item("tr2")},{flag(ice,0x80)}},{thawed});
    s=talk(ice,"drehya",20136,0x400);s.rareReward=true;
    // A5Q3 Malah20132 also returns an outside Anya whose room went inactive.
    // Collect the scroll before looking for her town reward.
    const auto rare=node("anya-class-rare",s,{{flag(ice,0x400)}},{scroll});
    s={};s.action=QuestAction::UseItem;s.stateQuest=ice;s.item="tr2";
    const auto resistance=node("resistance-scroll-used",s,{{flag(ice,0x80)}},{scroll});
    // A5Q3 grants the quest only after both NPC rewards; CUSTOM3 proves the
    // separate consumable was used, including capped displayed resistances.
    s=talk(ice,"drehya",20136,0x481);
    const auto iceDone=node("anya-all-rewards",s,{{flag(ice,0x481)}},{rare,resistance});

    auto portalFact=fact(K::PortalObserved);portalFact.value=definitions->area("Act 5 - Temple Entrance");
    s=talk(betrayal,"drehya",20137,4);
    const auto redPortal=node("anya-nihlathak-assignment",s,{{flag(betrayal,0x2007,true)},{portalFact}},{iceDone});
    s={};s.action=QuestAction::Portal;s.stateQuest=betrayal;s.sourceArea=town;s.area=portalFact.value;s.preferWaypoint=false;
    auto temple=fact(K::Area);temple.value=portalFact.value;
    auto templeWaypoint=fact(K::Waypoint);templeWaypoint.value=definitions->area("Act 5 - Temple 2");
    const auto templeEntry=node("nihlathak-temple-entry",s,{{temple},{templeWaypoint},{flag(betrayal,0x2003,true)}},{redPortal},true);
    s=hunt(betrayal,definitions->area("Act 5 - Temple Boss"),Json{{"monster","nihlathakboss"}});s.flags=2;
    const auto nihlathak=node("nihlathak-dead",s,{{flag(betrayal,0x2003,true)},{fact(K::Corpse)}},{templeEntry},true);
    const auto nameReady=node("anya-personalization-entitlement",talk(betrayal,"drehya",20148,0x12),{{flag(betrayal,0x12)},{flag(betrayal,1)}},{nihlathak});
    auto personalized=nameReady;
    if(goal.useServiceRewards) {
        s={};s.action=QuestAction::Service;s.stateQuest=betrayal;s.area=town;s.target=definitions->selector(Json{{"npc","drehya"}});s.item="cap";
        personalized=node("anya-personalization-used",s,{{flag(betrayal,1)}},{nameReady});
    }

    const auto trialIntro=intro(trial,"qual-kehk",20153);
    const auto ancient=[&](std::string_view name) {
        auto target=definitions->selector(Json{{"superunique",name}});
        // A5Q5 allocates three dedicated MonStats classes on the summit.
        // Their real assignment is sufficient; an optional name/modifier
        // decoration must not hide an otherwise observed Ancient.
        target.superUnique.reset();return target;
    };
    const auto altar=object("ancientsaltar",65);
    s={};s.action=QuestAction::Operate;s.stateQuest=trial;s.area=summit;s.target=altar;s.topics={20002};s.flags=id==baal?0:1;
    s.witness=ancient("Ancient Barbarian 1");
    Proof altarProof{{flag(trial,1),fact(K::SummitPassage)}};
    for(const auto name:{"Ancient Barbarian 1","Ancient Barbarian 2","Ancient Barbarian 3"}) {
        auto observed=fact(K::MonsterObserved);observed.value=definitions->selector(Json{{"superunique",name}}).identity;observed.area=summit;
        altarProof.push_back({observed});
    }
    const auto awakened=node("ancients-awakened",s,altarProof,{trialIntro});
    std::vector<size_t> ancients;
    for(const auto name:{"Ancient Barbarian 1","Ancient Barbarian 2","Ancient Barbarian 3"}) {
        s=hunt(trial,summit,Json{{"superunique",name}});
        s.target=ancient(name);
        // Never retain a corpse across an actual portal/leave reset.
        auto passage=fact(K::SummitPassage);passage.value=s.target->identity;
        ancients.push_back(node(std::string("ancient-dead-")+name,s,{{flag(trial,1),passage},{fact(K::Corpse)}},{awakened}));
    }
    s=hunt(trial,summit,Json{{"superunique","Ancient Barbarian 3"}});s.target=ancient("Ancient Barbarian 3");s.flags=1;
    const auto trialDone=node("ancients-personal-reward",s,{{flag(trial,1)}},ancients);

    s={};s.action=QuestAction::Portal;s.stateQuest=baal;s.sourceArea=summit;s.area=definitions->area("Act 5 - Baal Temple 1");
    s.target=object("To The Worldstone Keep Level 1",66);
    auto keep=fact(K::Area);keep.value=*s.area;
    auto keepWaypoint=fact(K::Waypoint);keepWaypoint.value=definitions->area("Act 5 - Baal Temple 2");
    Proof keepProof{{keep,flag(trial,1)},{keepWaypoint,flag(trial,1)},{flag(baal,1)}};
    for(const auto level:{"Act 5 - Baal Temple 2","Act 5 - Baal Temple 3","Act 5 - Throne Room","Act 5 - World Stone"}) {
        auto entered=fact(K::Area);entered.value=definitions->area(level);keepProof.push_back({entered,flag(trial,1)});
    }
    ancients.push_back(trialDone);
    const auto keepEntry=node("worldstone-keep-entry",s,std::move(keepProof),ancients,true);
    s={};s.action=QuestAction::Encounter;s.stateQuest=baal;s.area=throne;s.target=object("The Worldstone Chamber",70);
    s.witness=definitions->selector(Json{{"monster","baalthrone"}});s.clearCombat=true;s.status=3;
    s.residualRange=64;s.stageSound=16; // BaalThrone Fn134 clearance and broadcast laugh.
    auto chamberGate=fact(K::ObjectOpened);chamberGate.value=s.target->identity;chamberGate.area=throne;chamberGate.any=true;
    auto chamberOpen=fact(K::Status);chamberOpen.quest=baal;chamberOpen.value=3;
    // Init75 displays the portal in mode1 before any wave. A5Q6's status3
    // comes from OpenWorldStoneChamber, after throne Baal actually leaves.
    const auto waves=node("throne-waves-and-baal-departure",s,{{chamberGate,chamberOpen},{flag(baal,1)}},{keepEntry},true);
    s.action=QuestAction::Portal;s.sourceArea=throne;s.area=chamber;s.witness.reset();s.clearCombat=false;s.status.reset();
    auto chamberArea=fact(K::Area);chamberArea.value=chamber;
    const auto entered=node("worldstone-chamber-entry",s,{{chamberArea},{flag(baal,1)}},{waves},true);
    s=hunt(baal,chamber,Json{{"monster","baalcrab"}});s.flags=1;
    const auto slain=node("baal-dead-personal-completion",s,{{flag(baal,1)}},{entered},true);
    const auto exit=object("strlastcinematic",72);
    s=talk(baal,"tyrael3",20175,0x80,chamber);
    s.resultWaitSeconds=90;
    const auto farewell=node("tyrael-final-dialogue",s,{{flag(baal,0x81)}},{slain});
    // A5Q6 creates the final portal at NPCDEACTIVATE, after normal listening
    // closes. Keep that callback separate from the object's observation.
    s={};s.action=QuestAction::Encounter;s.stateQuest=baal;s.area=chamber;s.target=exit;
    s.witness=definitions->selector(Json{{"monster","tyrael3"}});
    auto exitReady=fact(K::ObjectOpened);exitReady.value=exit.identity;exitReady.area=chamber;exitReady.any=true;
    const auto exitObserved=node("tyrael-final-exit-observed",s,{{flag(baal,0x81),exitReady}},{farewell});
    s={};s.action=QuestAction::Portal;s.stateQuest=baal;s.sourceArea=chamber;s.area=town;s.target=exit;s.cinematic=7;
    auto finale=fact(K::Cinematic);finale.value=7;
    const auto final=node("worldstone-finale-received",s,{{finale},{fact(K::InitiallyRewarded)}},{exitObserved});
    switch(id) {
    case QuestId::SiegeOnHarrogath:goal.roots={socket};break;
    case QuestId::RescueOnMountArreat:goal.roots={runes};break;
    case QuestId::PrisonOfIce:goal.roots={iceDone};break;
    case QuestId::BetrayalOfHarrogath:goal.roots={personalized};break;
    case QuestId::RiteOfPassage:goal.roots={trialDone};break;
    case QuestId::EveOfDestruction:goal.roots={final};break;
    default:throw std::runtime_error("Not an Act-V quest");
    }
}
} // namespace d2x
