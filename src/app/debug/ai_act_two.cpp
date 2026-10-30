#include "ai_impl.hpp"
#include <stdexcept>

namespace d2x {
void DebugAi::Impl::configureActTwoQuest(Goal &goal,QuestId id) const {
    const auto town=definitions->area("Act 2 - Town");
    const size_t begin=goal.steps.size();
    const auto push=[&](QuestStep s)->QuestStep & {
        s.stateQuest=id;s.skipPending=false;goal.steps.push_back(std::move(s));return goal.steps.back();
    };
    const auto object=[&](std::string_view name,int operation) {
        // Chest names are deliberately shared in Objects.txt. Resolve both
        // the MPQ identity and its original operation, never the first name.
        std::optional<uint16_t> result;
        for(size_t row=0;row<definitions->objects.rows().size();++row)
            if(definitions->objects.value(row,"Name")==name && definitions->objects.number(row,"OperateFn")==operation) {
                const auto found=definitions->objects.number(row,"Id");
                if(!found || *found<0 || *found>UINT16_MAX || result) throw std::invalid_argument("Ambiguous Act-II quest object");
                result=uint16_t(*found);
            }
        if(!result) throw std::invalid_argument("MPQ lacks the verified Act-II quest object: "+std::string(name));
        return *result;
    };
    const auto talk=[&](std::string_view npc,uint16_t topic,uint16_t flags,uint16_t skip=1) ->QuestStep & {
        QuestStep s;s.action=QuestAction::Talk;s.area=town;s.target=definitions->selector(Json{{"npc",npc}});
        s.topics={topic};s.flags=flags;s.skipAny=skip;return push(std::move(s));
    };
    const auto home=[&]() {QuestStep s;s.action=QuestAction::ReturnTown;push(std::move(s));};
    const auto travel=[&](std::string_view level)->QuestStep & {
        QuestStep s;s.action=QuestAction::Travel;s.area=definitions->area(std::string(level));return push(std::move(s));
    };
    const auto hunt=[&](std::string_view level,std::string_view monster,uint16_t skip)->QuestStep & {
        QuestStep s;s.action=QuestAction::Hunt;s.area=definitions->area(std::string(level));
        s.target=definitions->selector(Json{{"monster",monster}});s.skipAny=skip;return push(std::move(s));
    };
    const auto acquire=[&](std::string_view level,std::string code,std::string_view name,int operation)->QuestStep & {
        if(!definitions->item(code)) throw std::invalid_argument("MPQ lacks the verified Act-II quest item: "+code);
        QuestStep s;s.action=QuestAction::Acquire;s.area=definitions->area(std::string(level));s.item=std::move(code);
        if(operation) s.source=object(name,operation);
        return push(std::move(s));
    };
    const auto arcane=[&]() {
        configureActTwoQuest(goal,QuestId::TaintedSun);
        talk("drognan",373,4,0x19).stateQuest=QuestId::ArcaneSanctuary;
        talk("jerhyn",377,8,0x11).stateQuest=QuestId::ArcaneSanctuary;
        // A2Q6 assignment is independent of the palace admission dialogue.
        auto &assignment=talk("jerhyn",430,4,0x39);assignment.stateQuest=QuestId::SevenTombs;
        auto &basement=travel("Act 2 - Basement 3");basement.skipTombKnown=true;basement.stateQuest=QuestId::ArcaneSanctuary;
        QuestStep portal;portal.action=QuestAction::Portal;portal.area=definitions->area("Act 2 - Arcane");
        portal.target=Selector{2,object("portal",34),"palace Arcane portal"};portal.skipTombKnown=true;
        push(std::move(portal)).stateQuest=QuestId::ArcaneSanctuary;
        auto &summoner=hunt("Act 2 - Arcane","summoner",0x2003);summoner.stateQuest=QuestId::Summoner;summoner.skipTombKnown=true;
        QuestStep journal;journal.action=QuestAction::Operate;journal.area=definitions->area("Act 2 - Arcane");
        journal.target=Selector{2,object("Tome",42),"Horazon's journal"};journal.topics={396};
        journal.publishesTombSymbol=true;journal.skipTombKnown=true;push(std::move(journal)).stateQuest=QuestId::ArcaneSanctuary;
    };
    const auto canyon=[&]() {
        std::vector<uint16_t> beyond;
        for(int i=1;i<=7;++i) beyond.push_back(definitions->area("Act 2 - Tomb Tal "+std::to_string(i)));
        beyond.push_back(definitions->area("Act 2 - Duriel's Lair"));
        const auto valley=definitions->area("Act 2 - Valley of the Kings"),sanctuary=definitions->area("Act 2 - Arcane");
        auto &basement=travel("Act 2 - Basement 3");basement.skipAreas=beyond;
        basement.skipAreas.push_back(valley);basement.skipAreas.push_back(sanctuary);
        QuestStep palace;palace.action=QuestAction::Portal;palace.area=sanctuary;palace.skipAreas=beyond;
        palace.skipAreas.push_back(valley);palace.target=Selector{2,object("portal",34),"palace Arcane portal"};push(std::move(palace));
        QuestStep portal;portal.action=QuestAction::Portal;portal.area=valley;portal.skipAreas=beyond;push(std::move(portal));
    };
    // D2MOO A2Q1..6 ScrollMessage/MonsterKilled/ChangedLevel callbacks and
    // ObjMode24/25/34/39..43. Only replicated client state skips a prerequisite.
    switch(id) {
    case QuestId::RadamentsLair: {
        auto &intro=talk("atma",304,4,0x2003);intro.optional=true;
        hunt("Act 2 - Sewer 1 C","radament",0x2003);
        acquire("Act 2 - Sewer 1 C","ass",{},0);
        QuestStep book;book.action=QuestAction::UseItem;book.item="ass";push(std::move(book));
        auto &scrollChest=acquire("Act 2 - Sewer 1 C","tr1","chest",40);
        scrollChest.stateQuest=QuestId::HoradricStaff;scrollChest.skipAny=9;
        home();talk("atma",334,1);
        auto &scroll=talk("cain2",335,8,9);scroll.stateQuest=QuestId::HoradricStaff;scroll.item="tr1";
        break;
    }
    case QuestId::TaintedSun: {
        // The altar is also the real amulet source. Its destruction, rather
        // than an invented monster drop, completes the field objective.
        auto &amulet=acquire("Act 2 - Tomb 3 Treasure","vip","taintedsunaltar",24);
        amulet.skipAny=1;amulet.produced="hst";
        home();talk("drognan",371,1);break;
    }
    case QuestId::ArcaneSanctuary: {
        arcane();home();
        // A2Q5's outstanding kill comment takes precedence over A2Q4's
        // journal comment at the same NPC. Confirm that real quest first.
        talk("drognan",422,1).stateQuest=QuestId::Summoner;
        talk("drognan",399,1,0).clearFlags=2;break;
    }
    case QuestId::Summoner:
        arcane();home();talk("fara",428,1);break;
    case QuestId::HoradricStaff: {
        auto &box=acquire("Act 2 - Tomb 2 Treasure","box","chest",39);box.skipAny=1;box.produced="hst";
        home();talk("cain2",338,0x40,0x41);
        auto &shaft=acquire("Act 2 - Lair 1 Treasure","msf","chest",41);shaft.skipAny=0x801;shaft.produced="hst";
        home();talk("cain2",337,0x20,0x821);
        configureActTwoQuest(goal,QuestId::TaintedSun);
        // A previously completed Sun can leave its amulet in this save. If
        // not carried, the normal altar acquisition must supply the material.
        auto &amulet=acquire("Act 2 - Tomb 3 Treasure","vip","taintedsunaltar",24);amulet.skipAny=0x801;amulet.produced="hst";
        home();talk("cain2",336,0x10,0x811);
        QuestStep combine;combine.action=QuestAction::Combine;combine.item="hst";combine.produced="box";push(std::move(combine));
        talk("cain2",339,0x400,0x401);
        arcane();canyon();
        QuestStep staff;staff.action=QuestAction::SubmitStaff;staff.trueTomb=true;staff.item="hst";
        staff.target=Selector{2,object("orifice",25),"Horadric orifice"};push(std::move(staff));break;
    }
    case QuestId::SevenTombs: {
        const size_t prerequisites=goal.steps.size();
        configureActTwoQuest(goal,QuestId::HoradricStaff);
        // A new game may restore the opened tomb from personal submission,
        // but only an actual journal response discloses which tomb it is.
        arcane();canyon();
        const auto lair=definitions->area("Act 2 - Duriel's Lair");
        for(size_t i=prerequisites;i<goal.steps.size();++i) {
            auto &s=goal.steps[i];s.parentSkipAny|=0x2019;s.skipAreas.push_back(lair);
        }
        QuestStep tomb;tomb.action=QuestAction::Travel;tomb.trueTomb=true;tomb.skipAny=0x2019;tomb.skipAreas={lair};push(std::move(tomb));
        QuestStep entrance;entrance.action=QuestAction::Portal;entrance.area=definitions->area("Act 2 - Duriel's Lair");
        entrance.target=Selector{2,object("Duriel's Lair",43),"opened tomb entrance"};entrance.skipAny=0x2019;push(std::move(entrance));
        hunt("Act 2 - Duriel's Lair","duriel",0x2039);
        auto &tyrael=talk("tyrael1",302,8,0x2019);tyrael.area=definitions->area("Act 2 - Duriel's Lair");
        // Tyrael grants PRIMARYGOALDONE before the player uses his portal.
        // That flag cannot stand in for observing the actual town arrival.
        QuestStep portal;portal.action=QuestAction::Portal;portal.area=town;portal.skipAny=0x11;push(std::move(portal));
        talk("jerhyn",442,0x10,0x11);talk("meshif1",450,1);
        QuestStep passage;passage.action=QuestAction::ActTravel;passage.area=definitions->area("Act 3 - Town");
        passage.target=definitions->selector(Json{{"npc","meshif1"}});push(std::move(passage));break;
    }
    default:throw std::invalid_argument("No verified Act-II quest recipe");
    }
    if(id!=goal.questId) for(size_t i=begin;i<goal.steps.size();++i) {
        // Preserve conditions on nested dependencies. A completed parent
        // prerequisite skips its own steps; journal travel may still be
        // required when the current game has not published the tomb symbol.
        auto &s=goal.steps[i];
        if(s.stateQuest==id && !s.publishesTombSymbol && !s.skipTombKnown) s.skipAny|=1;
    }
}
}
