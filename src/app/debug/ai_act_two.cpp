#include "ai_impl.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
void DebugAi::Impl::configureActTwoQuest(Goal &goal,QuestId id) const {
    using Fact=QuestFact;using Kind=Fact::Kind;
    using Proof=std::vector<std::vector<Fact>>;
    const auto town=definitions->area("Act 2 - Town"),arcane=definitions->area("Act 2 - Arcane");
    goal.questTown=town;
    const auto valley=definitions->area("Act 2 - Valley of the Kings"),lair=definitions->area("Act 2 - Duriel's Lair");
    const auto object=[&](std::string_view name,int operation) {
        std::optional<uint16_t> result;
        for(size_t row=0;row<definitions->objects.rows().size();++row)
            if(definitions->objects.value(row,"Name")==name && definitions->objects.number(row,"OperateFn")==operation) {
                const auto found=definitions->objects.number(row,"Id");
                if(!found || *found<0 || *found>UINT16_MAX || result) throw std::invalid_argument("Ambiguous Act-II quest object");
                result=uint16_t(*found);
            }
        if(!result) throw std::invalid_argument("MPQ lacks verified Act-II object: "+std::string(name));
        return *result;
    };
    const auto flags=[](QuestId quest,uint16_t value,bool any=false,uint16_t clear=0) {
        Fact f;f.kind=Kind::Flags;f.quest=quest;f.value=value;f.any=any;f.clear=clear;return f;
    };
    const auto item=[&](std::string code) {
        if(!definitions->item(code)) throw std::invalid_argument("MPQ lacks Act-II quest item: "+code);
        Fact f;f.kind=Kind::Item;f.item=std::move(code);return f;
    };
    const auto in=[](uint16_t area) {Fact f;f.kind=Kind::Area;f.value=area;return f;};
    const auto fact=[](Kind kind) {Fact f;f.kind=kind;return f;};
    const auto wp=[](uint16_t area) {Fact f;f.kind=Kind::Waypoint;f.value=area;return f;};
    const auto submitted=flags(QuestId::HoradricStaff,1);
    const auto late=flags(QuestId::SevenTombs,0x2019,true);
    const auto assembled=Proof{{item("hst")},{submitted}};
    const auto material=[&](std::string code) {auto proof=assembled;proof.push_back({item(std::move(code))});return proof;};
    const auto explained=[&](uint16_t mask) {auto proof=assembled;proof.push_back({flags(QuestId::HoradricStaff,mask)});return proof;};
    const auto node=[&](std::string name,QuestStep step,Proof proof,std::vector<size_t> needs={},bool retain=false,int townOrder=0) {
        step.skipPending=false;
        const auto index=goal.steps.size();goal.steps.push_back(std::move(step));
        goal.requirements.push_back({std::move(name),std::move(needs),std::move(proof),retain,townOrder});return index;
    };
    const auto talk=[&](QuestId quest,std::string_view npc,uint16_t topic,uint16_t mask,bool optional=false) {
        QuestStep s;s.action=QuestAction::Talk;s.stateQuest=quest;s.area=town;s.target=definitions->selector(Json{{"npc",npc}});
        s.topics={topic};s.flags=mask;s.optional=optional;return s;
    };
    const auto acquire=[&](QuestId quest,std::string_view level,std::string code,std::string_view name,int operation) {
        QuestStep s;s.action=QuestAction::Acquire;s.stateQuest=quest;s.area=definitions->area(std::string(level));s.item=std::move(code);
        if(operation) s.source=object(name,operation);
        return s;
    };
    const auto hunt=[&](QuestId quest,std::string_view level,std::string_view monster) {
        QuestStep s;s.action=QuestAction::Hunt;s.stateQuest=quest;s.area=definitions->area(std::string(level));
        s.target=definitions->selector(Json{{"monster",monster}});return s;
    };
    const auto portal=[&](QuestId quest,uint16_t from,uint16_t to) {
        QuestStep s;s.action=QuestAction::Portal;s.stateQuest=quest;s.area=to;s.sourceArea=from;s.preferWaypoint=true;return s;
    };
    // These are shared fact requirements, not recursively expanded recipes.
    // Original A2Q2 CheckItemsAndState prioritizes cube, scroll, amulet, shaft;
    // holding hst selects 339 instead, which sets all component explanation bits.
    const auto cube=node("cube-owned",acquire(QuestId::HoradricStaff,"Act 2 - Tomb 2 Treasure","box","chest",39),material("box"));
    const auto shaft=node("shaft-owned",acquire(QuestId::HoradricStaff,"Act 2 - Lair 1 Treasure","msf","chest",41),material("msf"));
    const auto amulet=node("amulet-owned",acquire(QuestId::HoradricStaff,"Act 2 - Tomb 3 Treasure","vip","taintedsunaltar",24),material("vip"));
    const auto cubeTalk=node("cube-explanation",talk(QuestId::HoradricStaff,"cain2",338,0x40),explained(0x40),{cube},false,10);
    const auto amuletTalk=node("amulet-explanation",talk(QuestId::HoradricStaff,"cain2",336,0x10),explained(0x10),{amulet},false,30);
    const auto shaftTalk=node("shaft-explanation",talk(QuestId::HoradricStaff,"cain2",337,0x20),explained(0x20),{shaft},false,40);
    QuestStep combine;combine.action=QuestAction::Combine;combine.stateQuest=QuestId::HoradricStaff;combine.item="hst";combine.produced="box";
    auto readyStaff=item("hst");readyStaff.kind=Kind::BackpackItem;
    const auto staff=node("staff-ready",combine,{{readyStaff},{submitted}},{cube,shaft,amulet,cubeTalk,amuletTalk,shaftTalk});
    const auto staffTalk=node("assembled-staff-explanation",talk(QuestId::HoradricStaff,"cain2",339,0x400),
        {{submitted},{flags(QuestId::HoradricStaff,0x400)}},{staff},false,5);

    // Owning a traded amulet/staff is not evidence of personal Sun reward.
    // Keep the real altar operation and the private pending/reward query.
    QuestStep altar;altar.action=QuestAction::Operate;altar.stateQuest=QuestId::TaintedSun;
    altar.area=definitions->area("Act 2 - Tomb 3 Treasure");altar.target=Selector{2,object("taintedsunaltar",24),"Tainted Sun altar"};
    altar.flags=2;altar.refreshFlags=true;
    const auto sunAltar=node("sun-personal-altar-result",altar,{{flags(QuestId::TaintedSun,0x2003,true)}});
    const auto sunReward=node("sun-town-confirmation",talk(QuestId::TaintedSun,"drognan",371,1),
        {{flags(QuestId::TaintedSun,1)}},{sunAltar},false,50);
    auto arcaneProof=Proof{{in(arcane)},{wp(arcane)}};
    auto intro=talk(QuestId::ArcaneSanctuary,"drognan",373,4,true);
    auto introProof=arcaneProof;introProof.push_back({flags(QuestId::ArcaneSanctuary,0x1d,true)});
    introProof.push_back({late});introProof.push_back({submitted});
    const auto drognan=node("palace-introduction",intro,introProof,{sunReward},true,60);
    intro=talk(QuestId::ArcaneSanctuary,"jerhyn",377,8,true);
    introProof=arcaneProof;introProof.push_back({flags(QuestId::ArcaneSanctuary,0x19,true)});
    introProof.push_back({late});introProof.push_back({submitted});
    const auto jerhyn=node("palace-admission-dialogue",intro,introProof,{drognan},true,70);
    intro=talk(QuestId::SevenTombs,"jerhyn",430,4,true);
    introProof=arcaneProof;introProof.push_back({flags(QuestId::SevenTombs,0x3d,true)});
    introProof.push_back({late});introProof.push_back({submitted});
    const auto tombIntro=node("seven-tombs-introduction",intro,introProof,{jerhyn},true,80);
    QuestStep basement;basement.action=QuestAction::Travel;basement.stateQuest=QuestId::ArcaneSanctuary;
    basement.area=definitions->area("Act 2 - Basement 3");
    auto basementProof=arcaneProof;basementProof.push_back({in(*basement.area)});
    const auto cellar=node("palace-portal-source",basement,basementProof,{tombIntro});
    auto entry=portal(QuestId::ArcaneSanctuary,*basement.area,arcane);
    entry.target=Selector{2,object("portal",34),"palace Arcane portal"};
    const auto arcaneAccess=node("arcane-access",entry,arcaneProof,{cellar});
    const auto summonerKill=node("summoner-death",hunt(QuestId::Summoner,"Act 2 - Arcane","summoner"),
        {{flags(QuestId::Summoner,0x2003,true)},{fact(Kind::Corpse)}},{arcaneAccess},true);
    QuestStep journal;journal.action=QuestAction::Operate;journal.stateQuest=QuestId::ArcaneSanctuary;journal.area=arcane;
    journal.target=Selector{2,object("Tome",42),"Horazon's journal"};journal.topics={396};journal.publishesTombSymbol=true;
    const auto clue=node("current-game-tomb-clue",journal,{{fact(Kind::TombClue)}},{arcaneAccess,summonerKill});
    auto observedPortal=fact(Kind::PortalObserved);observedPortal.value=valley;
    // A valid initialization clue does not mean this game's red portal was
    // created. Callback396 creates it separately; a personal unlocked canyon
    // waypoint is a legal alternative. The subsequent portal still needs a
    // real assigned unit before interaction, even after a confirmed reading.
    const auto portalReading=node("canyon-portal-reading",journal,
        {{fact(Kind::JournalRead)},{observedPortal},{wp(valley)}},{clue,arcaneAccess},true);
    const auto summonerComment=node("summoner-town-confirmation",talk(QuestId::Summoner,"drognan",422,1),
        {{flags(QuestId::Summoner,1)}},{clue,summonerKill},false,90);
    auto comment=talk(QuestId::ArcaneSanctuary,"drognan",399,1);comment.clearFlags=2;
    const auto arcaneComment=node("journal-town-confirmation",comment,{{flags(QuestId::ArcaneSanctuary,1,false,2)}},{clue,summonerComment},false,100);
    const auto canyon=node("canyon-arrival",portal(QuestId::ArcaneSanctuary,arcane,valley),
        {{in(valley)},{fact(Kind::TrueTombArea)},{in(lair)}},{clue,portalReading,staff,staffTalk});
    QuestStep tomb;tomb.action=QuestAction::Travel;tomb.stateQuest=QuestId::SevenTombs;tomb.trueTomb=true;
    const auto trueTomb=node("true-tomb-arrival",tomb,{{fact(Kind::TrueTombArea)}},{clue,canyon,staff,staffTalk});
    QuestStep submit;submit.action=QuestAction::SubmitStaff;submit.stateQuest=QuestId::HoradricStaff;submit.trueTomb=true;submit.item="hst";
    submit.target=Selector{2,object("orifice",25),"Horadric orifice"};
    const auto staffSubmitted=node("personal-staff-submission",submit,{{submitted}},{staff,staffTalk,trueTomb});
    auto entrance=portal(QuestId::SevenTombs,valley,lair);entrance.sourceArea.reset();entrance.preferWaypoint=false;
    entrance.target=Selector{2,object("Duriel's Lair",43),"opened tomb entrance"};
    entrance.witness=Selector{2,object("orifice",25),"Horadric orifice"};
    const auto lairArrival=node("duriel-lair-arrival",entrance,{{in(lair)},{late}},{trueTomb,staffSubmitted});
    const auto duriel=node("duriel-death",hunt(QuestId::SevenTombs,"Act 2 - Duriel's Lair","duriel"),
        {{flags(QuestId::SevenTombs,0x2039,true)},{fact(Kind::Corpse)}},{lairArrival},true);
    auto tyraelStep=talk(QuestId::SevenTombs,"tyrael1",302,8);tyraelStep.area=lair;
    const auto tyrael=node("tyrael-passage",tyraelStep,{{late}},{lairArrival,duriel});
    auto homePortal=portal(QuestId::SevenTombs,lair,town);homePortal.preferWaypoint=false;
    const auto home=node("tyrael-town-portal",homePortal,{{in(town),late}},{tyrael});
    const auto jerhynReturn=node("jerhyn-return-confirmation",talk(QuestId::SevenTombs,"jerhyn",442,0x10),
        {{flags(QuestId::SevenTombs,0x11,true)}},{home},false,110);
    const auto meshif=node("meshif-passage",talk(QuestId::SevenTombs,"meshif1",450,1),
        {{flags(QuestId::SevenTombs,1)}},{jerhynReturn},false,120);
    QuestStep passage;passage.action=QuestAction::ActTravel;passage.stateQuest=QuestId::SevenTombs;
    passage.area=definitions->area("Act 3 - Town");passage.target=definitions->selector(Json{{"npc","meshif1"}});
    auto actChange=fact(Kind::ActChange);actChange.value=15; // Separate native act-change word, not A2Q6's slot14.
    const auto actThree=node("act-three-arrival",passage,{{in(*passage.area),flags(QuestId::SevenTombs,1),actChange}},{meshif});

    auto atmaStep=talk(QuestId::RadamentsLair,"atma",304,4,true);
    const auto atma=node("radament-introduction",atmaStep,{{flags(QuestId::RadamentsLair,0x2007,true)}},{},true);
    const auto radament=node("radament-death",hunt(QuestId::RadamentsLair,"Act 2 - Sewer 1 C","radament"),
        {{flags(QuestId::RadamentsLair,0x2003,true)},{fact(Kind::Corpse)}},{atma},true);
    const auto book=node("skill-book-owned",acquire(QuestId::RadamentsLair,"Act 2 - Sewer 1 C","ass",{},0),
        {{item("ass")},{fact(Kind::BookUsed)}},{radament});
    QuestStep use;use.action=QuestAction::UseItem;use.stateQuest=QuestId::RadamentsLair;use.item="ass";
    const auto bookUsed=node("skill-book-consumed",use,{{fact(Kind::BookUsed)}},{radament,book},true);
    const auto radamentReward=node("radament-town-reward",talk(QuestId::RadamentsLair,"atma",334,1),
        {{flags(QuestId::RadamentsLair,1)}},{radament,bookUsed},false,1);
    auto scrollProof=explained(8);scrollProof.push_back({item("tr1")});
    const auto scroll=node("horadric-scroll-owned",acquire(QuestId::HoradricStaff,"Act 2 - Sewer 1 C","tr1","chest",40),scrollProof,{radament});
    auto scrollStep=talk(QuestId::HoradricStaff,"cain2",335,8);scrollStep.item="tr1";
    const auto scrollTalk=node("horadric-scroll-explanation",scrollStep,explained(8),{scroll},false,20);
    switch(id) {
    case QuestId::RadamentsLair:goal.roots={bookUsed,radamentReward,scrollTalk};break;
    case QuestId::TaintedSun:goal.roots={amulet,sunReward};break;
    case QuestId::ArcaneSanctuary:goal.roots={clue,summonerComment,arcaneComment};break;
    case QuestId::Summoner:goal.roots={clue,summonerComment};break;
    case QuestId::HoradricStaff:goal.roots={staffSubmitted};break;
    case QuestId::SevenTombs:goal.roots={actThree};break;
    default:throw std::invalid_argument("No verified Act-II condition graph");
    }
}
} // namespace d2x
