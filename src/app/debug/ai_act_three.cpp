#include "ai_impl.hpp"
#include <stdexcept>

namespace d2x {
void DebugAi::Impl::configureActThreeQuest(Goal &goal,QuestId id) const {
    using F=QuestFact;using K=F::Kind;using Proof=std::vector<std::vector<F>>;
    const auto town=definitions->area("Act 3 - Town"),councilArea=definitions->area("Act 3 - Travincal");
    goal.questTown=town;
    const auto item=[&](std::string code) {if(!definitions->item(code)) throw std::runtime_error("Missing original quest item: "+code);F f;f.kind=K::Item;f.item=std::move(code);return f;};
    const auto flag=[](QuestId q,uint16_t mask,bool any=false,uint16_t clear=0) {F f;f.kind=K::Flags;f.quest=q;f.value=mask;f.any=any;f.clear=clear;return f;};
    const auto fact=[](K kind) {F f;f.kind=kind;return f;};
    const auto in=[](uint16_t area) {F f;f.kind=K::Area;f.value=area;return f;};
    const auto object=[&](std::string_view name,int op) {
        std::optional<uint16_t> found;
        for(size_t row=0;row<definitions->objects.rows().size();++row)
            if(definitions->objects.value(row,"Name")==name && definitions->objects.number(row,"OperateFn")==op) {
                const auto identity=definitions->objects.number(row,"Id");
                if(!identity || *identity<0 || *identity>UINT16_MAX || found) throw std::runtime_error("Ambiguous original Act-III object");
                found=uint16_t(*identity);
            }
        if(!found) throw std::runtime_error("Missing original Act-III object: "+std::string(name));
        return Selector{2,*found,std::string(name)};
    };
    const auto node=[&](std::string name,QuestStep step,Proof proof,std::vector<size_t> needs={},bool retain=false,int order=0) {
        step.skipPending=false;const auto index=goal.steps.size();goal.steps.push_back(std::move(step));
        goal.requirements.push_back({std::move(name),std::move(needs),std::move(proof),retain,order});return index;
    };
    const auto talk=[&](QuestId q,std::string npc,uint16_t topic,uint16_t flags,bool optional=false) {
        QuestStep s;s.action=QuestAction::Talk;s.stateQuest=q;s.area=town;s.target=definitions->selector(Json{{"npc",npc}});
        s.topics={topic};s.flags=flags;s.optional=optional;return s;
    };
    const auto acquire=[&](QuestId q,std::string level,std::string code,std::string_view source,int op) {
        QuestStep s;s.action=QuestAction::Acquire;s.stateQuest=q;s.area=definitions->area(level);s.item=std::move(code);
        s.clearCombat=level=="Act 3 - Travincal" && op==0;
        if(op) s.source=object(source,op).identity;
        return s;
    };
    const auto bird=QuestId::GoldenBird,blade=QuestId::BladeOfTheOldReligion,khalim=QuestId::KhalimsWill;
    const auto temple=QuestId::BlackenedTemple,guardian=QuestId::Guardian,tome=QuestId::LamEsensTome;
    const auto birdGranted=flag(bird,3,true),orbDone=flag(khalim,1);
    QuestStep search;search.action=QuestAction::SearchDrop;search.stateQuest=bird;search.item="j34";search.clearCombat=true;
    for(const auto level:{"Act 3 - Jungle 1","Act 3 - Jungle 2","Act 3 - Jungle 3","Act 3 - Kurast 1","Act 3 - Kurast 2","Act 3 - Kurast 3","Act 3 - Travincal"})
        search.searchAreas.push_back(definitions->area(level));
    const auto figurine=node("figurine-owned",search,{{item("j34")},{item("g34")},{item("xyz")},{birdGranted}});
    auto s=talk(bird,"cain3",527,4);
    const auto figurineTalk=node("figurine-explanation",s,{{flag(bird,4)},{item("g34")},{item("xyz")},{birdGranted}},{figurine},false,10);
    s=talk(bird,"meshif2",529,0);s.item="j34";s.produced="g34";
    const auto birdItem=node("golden-bird-owned",s,{{item("g34")},{item("xyz")},{birdGranted}},{figurine,figurineTalk},false,20);
    const auto birdTalk=node("bird-explanation",talk(bird,"cain3",531,0x10),
        {{flag(bird,0x10)},{item("xyz")},{birdGranted}},{birdItem},false,30);
    s=talk(bird,"alkor",534,2);s.item="g34";
    const auto delivered=node("bird-delivered",s,{{birdGranted}},{birdItem,birdTalk},false,40);
    s=talk(bird,"alkor",538,1);s.produced="xyz";
    const auto potion=node("life-potion-owned",s,{{item("xyz")},{flag(bird,1,false,0x20)}},{delivered},false,50);
    s={};s.action=QuestAction::UseItem;s.stateQuest=bird;s.item="xyz";
    const auto potionUsed=node("life-potion-consumed",s,{{flag(bird,1,false,0x20)}},{potion},true);

    const auto bladeDone=flag(blade,1),bladeReturned=flag(blade,0x41,true);
    s={};s.action=QuestAction::Operate;s.stateQuest=blade;s.area=definitions->area("Act 3 - Jungle 3");s.target=object("gidbinn",31);
    const auto altar=node("gidbinn-altar-operated",s,{{item("g33")},{bladeReturned}},{},true);
    s.action=QuestAction::SearchDrop;s.item="g33";s.witness=s.target;s.target.reset();s.clearCombat=true;
    const auto gidbinn=node("gidbinn-owned",s,{{item("g33")},{bladeReturned}},{altar});
    s=talk(blade,"ormus",587,0x40);s.item="g33";
    const auto bladeBack=node("gidbinn-returned",s,{{bladeReturned}},{gidbinn},false,10);
    s=talk(blade,"ormus",593,0x100);s.produced="rin";
    const auto ring=node("gidbinn-ring",s,{{flag(blade,0x100)},{bladeDone}},{bladeBack},false,20);
    const auto hire=node("iron-wolf-reward",talk(blade,"asheara",589,1),{{bladeDone}},{bladeBack,ring},false,30);

    const auto tomeItem=node("lam-tome-owned",acquire(tome,"Act 3 - Temple 1","bbb","LamTome",28),{{item("bbb")},{flag(tome,1)}});
    s=talk(tome,"alkor",564,1);s.item="bbb";
    const auto tomeReward=node("lam-tome-reward",s,{{flag(tome,1)}},{tomeItem});

    // A3Q2 NpcActivate offers543 before part explanations when STARTED is
    // absent. Materials may be gathered first; this only orders native topics.
    const auto introduction=node("khalim-introduction",talk(khalim,"cain3",543,4),{{flag(khalim,4)},{orbDone}});

    std::vector<size_t> council;
    for(const auto name:{"Ismail Vilehand","Geleb Flamefinger","Toorc Icefist"}) {
        s={};s.action=QuestAction::Hunt;s.stateQuest=temple;s.area=councilArea;s.target=definitions->selector(Json{{"superunique",name}});
        council.push_back(node(std::string("council-dead-")+name,s,{{flag(temple,0x2011,true)},{fact(K::Corpse)}},{},true));
    }
    const auto willProof=[&](std::string code) {return Proof{{item(std::move(code))},{item("qf2")},{orbDone}};};
    const auto cube=node("khalim-cube-owned",acquire(khalim,"Act 3 - Travincal","box",{},0),willProof("box"),council);
    const auto eye=node("khalim-eye-owned",acquire(khalim,"Act 3 - Spider 2","qey","chest",58),willProof("qey"));
    const auto brain=node("khalim-brain-owned",acquire(khalim,"Act 3 - Dungeon 2 Treasure","qbr","chest",59),willProof("qbr"));
    s={};s.action=QuestAction::Operate;s.stateQuest=khalim;s.area=definitions->area("Act 3 - Sewer 1");s.target=object("sewer lever",45);
    auto stairs=fact(K::ObjectOpened);stairs.value=object("sewer stairs",44).identity;
    const auto lever=node("sewer-stairs-open",s,{{stairs},{in(definitions->area("Act 3 - Sewer 2"))},{item("qhr")},{item("qf2")},{orbDone}},{},true);
    const auto heart=node("khalim-heart-owned",acquire(khalim,"Act 3 - Sewer 2","qhr","chest",57),willProof("qhr"),{lever});
    const auto flail=node("khalim-flail-owned",acquire(khalim,"Act 3 - Travincal","qf1",{},0),willProof("qf1"),council);
    const auto explanation=[&](std::string name,uint16_t topic,uint16_t mask,size_t ingredient,int order) {
        return node(std::move(name),talk(khalim,"cain3",topic,mask),{{flag(khalim,mask)},{item("qf2")},{orbDone}},{ingredient,introduction},false,order);
    };
    const auto eyeTalk=explanation("khalim-eye-explanation",545,8,eye,20);
    const auto brainTalk=explanation("khalim-brain-explanation",546,0x10,brain,40);
    const auto heartTalk=explanation("khalim-heart-explanation",544,0x40,heart,30);
    const auto flailTalk=explanation("khalim-flail-explanation",547,0x20,flail,10);
    s={};s.action=QuestAction::Combine;s.stateQuest=khalim;s.item="qf2";s.produced="box";s.ingredients={"qey","qbr","qhr","qf1"};
    const auto will=node("khalims-will-owned",s,{{item("qf2")},{orbDone}},{cube,eye,brain,heart,flail,eyeTalk,brainTalk,heartTalk,flailTalk});
    const auto willTalk=node("khalims-will-explanation",talk(khalim,"cain3",548,0x80),{{flag(khalim,0x80)},{orbDone}},{will,introduction},false,5);
    s={};s.action=QuestAction::EquipWeapon;s.stateQuest=khalim;s.item="qf2";s.area=councilArea;
    auto equipped=item("qf2");equipped.kind=K::Equipped;
    const auto weapon=node("khalims-will-equipped",s,{{equipped},{orbDone}},{will,willTalk});
    s.action=QuestAction::SmashOrb;s.target=object("compellingorb",53);s.flags=1;
    const auto smashed=node("compelling-orb-smashed",s,{{orbDone}},{weapon});
    s={};s.action=QuestAction::EquipWeapon;s.stateQuest=khalim;
    const auto restored=node("original-weapon-restored",s,{{orbDone,fact(K::EquipmentRestored)}},{smashed});
    auto councilNeeds=council;councilNeeds.push_back(restored);
    const auto templeReward=node("blackened-temple-confirmation",talk(temple,"cain3",626,1),{{flag(temple,1),fact(K::EquipmentRestored)}},councilNeeds);
    s={};s.action=QuestAction::Hunt;s.stateQuest=guardian;s.area=definitions->area("Act 3 - Mephisto 3");s.target=definitions->selector(Json{{"monster","mephisto"}});
    const auto mephisto=node("mephisto-dead",s,{{flag(guardian,0x2003,true)},{fact(K::Corpse)}},{templeReward,restored},true);
    s={};s.action=QuestAction::Portal;s.stateQuest=guardian;s.area=definitions->area("Act 4 - Town");s.sourceArea=definitions->area("Act 3 - Mephisto 3");s.target=object("portal",46);
    auto changed=fact(K::ActChange);changed.value=23;
    const auto passage=node("act-four-arrival",s,{{changed}},{mephisto});
    switch(id) {
    case QuestId::GoldenBird:goal.roots={potionUsed};break;
    case QuestId::BladeOfTheOldReligion:goal.roots={hire};break;
    case QuestId::LamEsensTome:goal.roots={tomeReward};break;
    case QuestId::KhalimsWill:goal.roots={restored};break;
    case QuestId::BlackenedTemple:goal.roots={templeReward};break;
    case QuestId::Guardian:goal.roots={passage};break;
    default:throw std::runtime_error("Not an Act-III quest");
    }
}
} // namespace d2x
