#include "ai_impl.hpp"
#include <stdexcept>

namespace d2x {
void DebugAi::Impl::configureActFourQuest(Goal &goal,QuestId id) const {
    using F=QuestFact;using K=F::Kind;using Proof=std::vector<std::vector<F>>;
    const auto town=definitions->area("Act 4 - Town"),river=definitions->area("Act 4 - Lava 1"),sanctum=definitions->area("Act 4 - Diablo 1");
    goal.questTown=town;
    const auto flag=[](QuestId q,uint16_t mask,bool any=false) {F f;f.kind=K::Flags;f.quest=q;f.value=mask;f.any=any;return f;};
    const auto item=[&](std::string code) {if(!definitions->item(code)) throw std::runtime_error("Missing original quest item: "+code);F f;f.kind=K::Item;f.item=std::move(code);return f;};
    const auto fact=[](K kind) {F f;f.kind=kind;return f;};
    const auto object=[&](std::string_view name,int operation) {
        std::optional<uint16_t> identity;
        for(size_t row=0;row<definitions->objects.rows().size();++row)
            if(definitions->objects.value(row,"Name")==name && definitions->objects.number(row,"OperateFn")==operation) {
                const auto value=definitions->objects.number(row,"Id");
                if(identity || !value || *value<0 || *value>UINT16_MAX) throw std::runtime_error("Ambiguous Act-IV object");
                identity=uint16_t(*value);
            }
        if(!identity) throw std::runtime_error("Missing original Act-IV object");
        return Selector{2,*identity,std::string(name)};
    };
    const auto node=[&](std::string name,QuestStep step,Proof proof,std::vector<size_t> needs={},bool retain=false) {
        step.skipPending=false;const auto index=goal.steps.size();goal.steps.push_back(std::move(step));
        goal.requirements.push_back({std::move(name),std::move(needs),std::move(proof),retain,0});return index;
    };
    const auto talk=[&](QuestId q,std::string npc,uint16_t topic,uint16_t flags) {
        QuestStep s;s.action=QuestAction::Talk;s.stateQuest=q;s.area=town;s.target=definitions->selector(Json{{"npc",npc}});s.topics={topic};s.flags=flags;return s;
    };
    const auto hunt=[&](QuestId q,uint16_t level,Json target) {
        QuestStep s;s.action=QuestAction::Hunt;s.stateQuest=q;s.area=level;s.target=definitions->selector(target);return s;
    };
    const auto angel=QuestId::FallenAngel,forge=QuestId::HellsForge,terror=QuestId::TerrorsEnd;
    const auto angelDone=flag(angel,0x2003,true),forgeDone=flag(forge,3,true),diabloDone=flag(terror,0x2003,true);
    const auto intro=node("izual-assignment",talk(angel,"tyrael2",670,4),{{flag(angel,0x2007,true)}});
    auto s=hunt(angel,definitions->area("Act 4 - Mesa 2"),Json{{"monster","izual"}});
    const auto izual=node("izual-dead",s,{{angelDone},{fact(K::Corpse)}},{intro},true);
    // A4Q1 topic676 requires REWARDPENDING, not the optional ghost675 flag.
    s=talk(angel,"tyrael2",676,1);s.skillPointReward=2;
    const auto angelReward=node("izual-two-skill-points",s,{{flag(angel,1)}},{izual});

    const auto altar=object("Hellforge",49);
    F opened=fact(K::ObjectOpened);opened.value=altar.identity;opened.area=river;
    s=talk(forge,"cain4",679,0);s.produced="mss";
    const auto stone=node("soulstone-owned",s,{{item("mss")},{forgeDone},{opened}});
    s=hunt(forge,river,Json{{"superunique","The Feature Creep"}});
    // A4Q3 requires the actual hammer, not personal kill credit or a retained
    // corpse. A legitimately observed ground hammer already supplies it.
    auto groundHammer=item("hfh");groundHammer.kind=K::GroundItem;
    const auto hephasto=node("hellforge-hammer-source",s,{{item("hfh")},{groundHammer},{forgeDone},{fact(K::Corpse)}},{},true);
    s={};s.action=QuestAction::Acquire;s.stateQuest=forge;s.area=river;s.item="hfh";s.clearCombat=true;
    const auto hammer=node("hellforge-hammer-owned",s,{{item("hfh")},{forgeDone}},{hephasto});
    s={};s.action=QuestAction::Operate;s.stateQuest=forge;s.area=river;s.target=altar;s.item="mss";
    const auto placed=node("soulstone-placed",s,{{opened},{forgeDone}},{stone},true);
    s={};s.action=QuestAction::EquipWeapon;s.stateQuest=forge;s.area=river;s.item="hfh";
    auto equipped=item("hfh");equipped.kind=K::Equipped;
    const auto weapon=node("hellforge-hammer-equipped",s,{{equipped},{forgeDone}},{hammer,placed});
    s.action=QuestAction::SmashForge;s.target=altar;
    const auto smashed=node("soulstone-smashed",s,{{forgeDone}},{weapon,placed});
    s={};s.action=QuestAction::EquipWeapon;s.stateQuest=forge;
    const auto restored=node("forge-original-weapon-restored",s,{{forgeDone,fact(K::EquipmentRestored)}},{smashed});
    const auto forgeReward=node("hellforge-confirmation",talk(forge,"cain4",680,1),{{flag(forge,1),fact(K::EquipmentRestored)}},{restored});

    const auto diabloTarget=definitions->selector(Json{{"monster","diablo"}});
    auto diabloPresent=fact(K::MonsterObserved);diabloPresent.value=diabloTarget.identity;diabloPresent.area=sanctum;
    // A4Q2 creates Diablo only after all five seals and three seal bosses.
    // His actual assignment resumes that world even if old corpses withdrew.
    std::vector<size_t> seals,bosses;
    for(size_t row=0;row<definitions->objects.rows().size();++row) {
        if(definitions->objects.value(row,"Name")!="Seal" || definitions->objects.number(row,"InitFn")!=56) continue;
        const auto identity=definitions->objects.number(row,"Id"),op=definitions->objects.number(row,"OperateFn");
        if(!identity || !op || (*op!=52 && *op!=54 && *op!=55 && *op!=56)) throw std::runtime_error("Unverified Diablo seal operation");
        s={};s.action=QuestAction::Operate;s.stateQuest=terror;s.area=sanctum;s.target=Selector{2,uint16_t(*identity),"Diablo seal"};
        auto open=fact(K::ObjectOpened);open.value=uint16_t(*identity);
        const auto seal=node("seal-open-"+std::to_string(*identity),s,{{open},{diabloDone},{diabloPresent}},{},true);seals.push_back(seal);
        if(*op==52) continue;
        const auto name=*op==54?"Infector of Souls":*op==55?"Lord De Seis":"Grand Vizier of Chaos";
        s=hunt(terror,sanctum,Json{{"superunique",name}});
        bosses.push_back(node(std::string("seal-boss-dead-")+name,s,{{diabloDone},{fact(K::Corpse)},{diabloPresent}},{seal},true));
    }
    if(seals.size()!=5 || bosses.size()!=3) throw std::runtime_error("Current MPQ must provide five native seals and three seal bosses");
    auto needs=seals;needs.insert(needs.end(),bosses.begin(),bosses.end());
    s=hunt(terror,sanctum,Json{{"monster","diablo"}});
    for(size_t row=0;row<definitions->objects.rows().size();++row) if(definitions->objects.number(row,"InitFn")==55) {
        if(s.witness) throw std::runtime_error("Ambiguous Diablo birth anchor");
        s.witness=Selector{2,uint16_t(*definitions->objects.number(row,"Id")),"Diablo birth anchor"};
    }
    if(!s.witness) throw std::runtime_error("Missing original Diablo Init55 anchor");
    const auto diablo=node("diablo-dead",s,{{diabloDone},{fact(K::Corpse)}},needs,true);
    auto changed=fact(K::ActChange);changed.value=28;
    const auto harrogath=object("Harrogath",73);
    auto portalBorn=fact(K::ObjectOpened);portalBorn.value=harrogath.identity;portalBorn.area=town;portalBorn.any=true;
    s=talk(terror,"tyrael2",20000,0x200);s.witness=harrogath;
    const auto portalReady=node("tyrael-harrogath-portal",s,{{flag(terror,0x200)},{changed},{portalBorn}},{diablo});
    s={};s.action=QuestAction::Portal;s.stateQuest=terror;s.sourceArea=town;s.area=definitions->area("Act 5 - Town");s.target=harrogath;
    const auto arrival=node("act-five-arrival",s,{{changed}},{portalReady});
    switch(id) {
    case QuestId::FallenAngel:goal.roots={angelReward};break;
    case QuestId::HellsForge:goal.roots={forgeReward};break;
    case QuestId::TerrorsEnd:goal.roots={arrival};break;
    default:throw std::runtime_error("Not an Act-IV quest");
    }
}
} // namespace d2x
