#include "system.hpp"
#include "detail.hpp"
#include "server/player_store.hpp"
#include "server/systems/npc/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/runtime/facts/quest_signals.hpp"
#include "gameplay/quest/acts/act_two_state.hpp"
#include "gameplay/quest/catalog.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::quests {
std::optional<System::PalaceGuardSpeech> System::preparePalaceGuardSpeech(const NpcRule &npc) const {
    if(npc.code!="act2guard2" || !state_.controlRandom) return {};
    const auto expected=*state_.controlRandom;auto next=expected;
    const auto text=uint16_t(state_.npcStory.palaceOpen?187+limitedRandom(next,3):186);
    if(!npc.questMessages.contains(text)) return {};
    return PalaceGuardSpeech{{QuestId::ArcaneSanctuary,{0,text}},expected,next};
}
void System::publishPalaceGuardSpeech(const PalaceGuardSpeech &speech) {
    if(state_.controlRandom!=speech.expected) throw std::logic_error("Stale palace guard speech random state");
    state_.controlRandom=speech.next;
}
std::optional<Dialogue> System::actTwoDialogue(const ActorContext &actor,const NpcRule &npc) const {
    const auto *p=ports_.players.find(actor.player);if(!p) return {};
    const auto &record=p->persistent.player;
    const auto &book=record.quests.at(size_t(ports_.settings.difficulty));
    const auto available=[&](QuestId id){return !unavailable(actor.player,record,id);};
    const auto stage=[&](QuestId id){return book.at(questIndex(id)).stage;};
    const auto emit=[&](QuestId id,uint16_t text,uint8_t menu=1)->std::optional<Dialogue> {
        if(!npc.questMessages.contains(text)) return {};
        return Dialogue{id,{menu,text}};
    };
    // Original A2Q0 is a saved arrival flag, separate from PlrIntro.
    if(npc.code=="jerhyn" && !record.questPreludes.at(size_t(ports_.settings.difficulty)).at(size_t(QuestPreludeId::LutGholeinArrival)))
        return emit(QuestId::SevenTombs,253,0);
    const auto tombs=stage(QuestId::SevenTombs);
    if(npc.code=="tyrael1" && tombs<uint32_t(TombsStage::TyraelRescued) &&
       (tombs==uint32_t(TombsStage::DurielSlain) || state_.actTwo.durielSlain)) return emit(QuestId::SevenTombs,302);
    if(npc.code=="jerhyn" && tombs==uint32_t(TombsStage::TyraelRescued)) return emit(QuestId::SevenTombs,442);
    if(npc.code=="meshif1" && tombs==uint32_t(TombsStage::JerhynConfirmed)) return emit(QuestId::SevenTombs,450);
    if(npc.code=="atma" && available(QuestId::RadamentsLair)) {
        const auto radament=stage(QuestId::RadamentsLair);
        if((!radament && !state_.started.contains(QuestId::RadamentsLair)) || radament==uint32_t(RadamentStage::Slain)) return emit(QuestId::RadamentsLair,radament?334:304,radament?1:0);
        if(radament<uint32_t(RadamentStage::Slain) && (radament || state_.started.contains(QuestId::RadamentsLair))) return emit(QuestId::RadamentsLair,310,2);
    }
    const auto staff=stage(QuestId::HoradricStaff);
    if(npc.code=="cain2" && staff<uint32_t(StaffStage::Submitted)) {
        const auto &items=p->rules.character->actTwo;
        const auto flags=book.at(questIndex(QuestId::HoradricStaff)).flags;
        if(detail::actTwoCarried(*p,items.staff,ports_.settings.difficulty) && !(flags&staffAssemblyExplained)) return emit(QuestId::HoradricStaff,339,0);
        if(detail::actTwoCarried(*p,items.cube,ports_.settings.difficulty) && !(flags&staffCubeExplained)) return emit(QuestId::HoradricStaff,338,0);
        if(detail::actTwoCarried(*p,items.scroll,ports_.settings.difficulty) && !(flags&staffScrollExplained)) return emit(QuestId::HoradricStaff,335,0);
        if(detail::actTwoCarried(*p,items.amulet,ports_.settings.difficulty) && !(flags&staffHeadExplained)) return emit(QuestId::HoradricStaff,336,0);
        if(detail::actTwoCarried(*p,items.shaft,ports_.settings.difficulty) && !(flags&staffShaftExplained)) return emit(QuestId::HoradricStaff,337,0);
    }
    const auto sun=stage(QuestId::TaintedSun);
    if(npc.code=="drognan") {
        if(available(QuestId::TaintedSun) && sun==uint32_t(SunStage::Darkness)) return emit(QuestId::TaintedSun,348);
        if(sun==uint32_t(SunStage::AltarDestroyed)) return emit(QuestId::TaintedSun,371);
        if(available(QuestId::ArcaneSanctuary) && sun>=uint32_t(SunStage::AltarDestroyed) && !stage(QuestId::ArcaneSanctuary)) return emit(QuestId::ArcaneSanctuary,373);
    }
    if(npc.code=="jerhyn") {
        if(available(QuestId::ArcaneSanctuary) && stage(QuestId::ArcaneSanctuary)==uint32_t(ArcaneStage::PalaceOpened)) return emit(QuestId::ArcaneSanctuary,377);
        if(available(QuestId::SevenTombs) && stage(QuestId::ArcaneSanctuary)>0 && !tombs) return emit(QuestId::SevenTombs,430);
    }
    // A2Q3/A2Q5 completion comments are offered by every original town participant.
    static constexpr std::pair<std::string_view,uint16_t> sunComments[]{
        {"atma",368},{"warriv2",366},{"greiz",363},{"elzix",364},{"lysander",370},
        {"cain2",372},{"meshif1",367},{"geglash",365},{"jerhyn",362}};
    if(sun==uint32_t(SunStage::AltarDestroyed)) for(const auto &[code,text]:sunComments) if(npc.code==code) return emit(QuestId::TaintedSun,text);
    static constexpr std::pair<std::string_view,uint16_t> summonerComments[]{
        {"atma",427},{"warriv2",424},{"greiz",419},{"elzix",423},{"drognan",422},
        {"lysander",426},{"cain2",429},{"meshif1",425},{"jerhyn",421},{"geglash",420},{"fara",428}};
    if(stage(QuestId::Summoner)==uint32_t(SummonerStage::Slain)) for(const auto &[code,text]:summonerComments) if(npc.code==code) return emit(QuestId::Summoner,text);
    static constexpr std::pair<std::string_view,uint16_t> arcaneComments[]{
        {"atma",406},{"warriv2",403},{"greiz",397},{"elzix",400},{"jerhyn",398},
        {"drognan",399},{"lysander",405},{"cain2",407},{"meshif1",402},{"geglash",401},{"fara",404}};
    if(book.at(questIndex(QuestId::ArcaneSanctuary)).flags&arcaneCommentPending)
        for(const auto &[code,text]:arcaneComments) if(npc.code==code) return emit(QuestId::ArcaneSanctuary,text);
    if(available(QuestId::RadamentsLair) && npc.code=="atma" && stage(QuestId::RadamentsLair)<uint32_t(RadamentStage::Slain)) return emit(QuestId::RadamentsLair,stage(QuestId::RadamentsLair)==1?310:317,2);
    return {};
}
DomainResult<> System::acknowledgeMeshifPassage(const ActorContext &actor,CharacterRecord record) {
    const auto &player=*ports_.players.find(actor.player);
    const auto difficulty=size_t(ports_.settings.difficulty);
    auto &book=record.quests.at(difficulty);
    const auto quest=QuestId::SevenTombs;
    const auto index=questIndex(quest);
    book[index].stage=uint32_t(TombsStage::PassageGranted);

    // A2Q6 publishes the reward bit before clearing ENTERAREA. Later
    // queries and saves see the cleared bit from the character transaction.
    auto receipt=fact(actor.player,record,actor.area);
    auto &wireRecord=receipt.record.quests.at(difficulty)[index].nativeRecord;
    wireRecord=uint16_t(wireRecord.value()|0x10);
    receipt.publication=QuestPublication::Private;
    std::vector<DomainFact> facts;
    if(receipt.credited[index]) facts.emplace_back(QuestStatusFact{quest,6});
    facts.emplace_back(std::move(receipt));

    std::vector<ItemChange> removed;
    const auto &staff=book[questIndex(QuestId::HoradricStaff)];
    if(staff.nativeRecord.value_or(actTwoLegacyRecord(QuestId::HoradricStaff,staff))&1) {
        const auto &codes=player.rules.character->actTwo;
        for(const auto &code:{codes.shaft,codes.amulet,codes.staff}) {
            const auto *item=detail::actTwoCarried(player,code,int(difficulty));
            if(!item) continue;
            if(item->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
            removed.push_back({item->id,item->revision+1,ItemChangeKind::Removed,item->location,{},0});
        }
    }
    if(removed.empty()) {
        transactions::CharacterEdit edit{actor,player.inventoryRevision,player.characterRevision,std::move(record)};
        edit.facts=std::move(facts);
        auto plan=ports_.transactions.prepare(std::move(edit));
        return plan?ports_.transactions.commit(std::move(*plan.value)):DomainResult<>{plan.status,{}};
    }
    auto inventory=player.persistent.inventory;
    for(const auto &change:removed) inventory.items.erase(change.item);
    transactions::InventoryEdit edit{actor,player.inventoryRevision,player.characterRevision,std::move(inventory),std::move(removed),record.weaponSet};
    edit.character=std::move(record);edit.facts=std::move(facts);
    auto plan=ports_.transactions.prepare(std::move(edit));
    return plan?ports_.transactions.commit(std::move(*plan.value)):DomainResult<>{plan.status,{}};
}
DomainResult<> System::acknowledgeActTwo(const ActorContext &actor,const Request &request,const NpcRule &npc) {
    const auto *p=ports_.players.find(actor.player);auto record=p->persistent.player;
    auto &book=record.quests.at(size_t(ports_.settings.difficulty));auto &q=book.at(questIndex(request.quest));
    const auto message=*request.message;
    if(npc.code=="act2guard2") {
        if(request.quest!=QuestId::ArcaneSanctuary || message<186 || message>189) return {DomainStatus::InvalidRequest,{}};
        q.nativeRecord=uint16_t(q.nativeRecord.value_or(actTwoLegacyRecord(request.quest,q))|(message==186?0x80:0x100));
        return commit(actor,std::move(record),false,{},false);
    }
    const bool primaryGoal=(q.nativeRecord.value_or(actTwoLegacyRecord(request.quest,q))&0x2000)!=0;
    std::optional<travel::SpecialPortalPlan> portal;
    switch(message) {
    case 253: record.questPreludes.at(size_t(ports_.settings.difficulty)).at(size_t(QuestPreludeId::LutGholeinArrival))=true;break;
    case 304:
        q.stage=uint32_t(RadamentStage::Assigned);
        return startQuest(actor,std::move(record),QuestId::RadamentsLair,*request.npc);
    case 334: if(q.stage!=uint32_t(RadamentStage::Slain)) return {DomainStatus::Stale,{}};q.stage=uint32_t(RadamentStage::Rewarded);break;
    case 335: return prepareReward(actor,*request.npc,RewardKind::ExplainStaffScroll,true);
    case 336: q.flags|=staffHeadExplained|staffScrollExplained;break;
    case 337: q.flags|=staffShaftExplained|staffScrollExplained;break;
    case 338: q.flags|=staffCubeExplained|staffScrollExplained;break;
    case 339: q.flags|=staffExplanationMask;break;
    case 348: q.stage=uint32_t(SunStage::Explained);break;
    case 373: q.stage=uint32_t(ArcaneStage::PalaceOpened);break;
    case 377: q.stage=uint32_t(ArcaneStage::JerhynBriefed);break;
    case 430: q.stage=uint32_t(TombsStage::Assigned);break;
    case 302: {
        if((q.stage<uint32_t(TombsStage::DurielSlain) && !state_.actTwo.durielSlain) || state_.actTwo.tyraelPortal ||
            state_.initialFlags[questDefinition(QuestId::SevenTombs).nativeSlot]&0x8000) return {DomainStatus::Conflict,{}};
        if(!state_.actTwo.tyraelPortal) {
            const auto result=ports_.travel.prepareSpecialPortal(actor,travel::SpecialPortalKind::Tyrael,0,p->position);
            if(!result) return {result.status,{}};
            portal=*result.value;
        }
        q.stage=std::max(q.stage,uint32_t(TombsStage::TyraelRescued));break;
    }
    case 442: q.stage=uint32_t(TombsStage::JerhynConfirmed);break;
    case 450: return acknowledgeMeshifPassage(actor,std::move(record));
    default:
        if(request.quest==QuestId::TaintedSun && q.stage==uint32_t(SunStage::AltarDestroyed)) q.stage=uint32_t(SunStage::Confirmed);
        else if(request.quest==QuestId::Summoner && q.stage==uint32_t(SummonerStage::Slain)) q.stage=uint32_t(SummonerStage::Confirmed);
        else if(request.quest==QuestId::ArcaneSanctuary && q.stage==uint32_t(ArcaneStage::JournalRead)) q.flags&=~arcaneCommentPending;
        else return {DomainStatus::Applied,std::monostate{}};
    }
    const auto eligible=message==302?eligibleParty(QuestId::SevenTombs,uint32_t(TombsStage::TyraelRescued),actor.area,{},{}):std::set<PlayerId>{};
    const auto result=commit(actor,std::move(record),false,eligible.contains(actor.player)?std::optional{QuestId::SevenTombs}:std::nullopt);
    if(result) {
        auto &runtime=state_.actTwo.runtime;auto &story=state_.npcStory;
        switch(message) {
        case 253:break;
        case 334:
            if(primaryGoal) runtime[0].state=5;
            if(primaryGoal || !runtime[0].active) advancePalaceSequence();
            break;
        case 373:runtime[3].state=2;story.palaceOpen=true;break;
        case 377:runtime[3].state=3;runtime[3].last=3;break;
        case 430:runtime[5].state=2;runtime[5].last=1;break;
        case 302:runtime[5].state=4;break;
        case 442:runtime[5].state=5;break;
        default:
            if(request.quest==QuestId::TaintedSun && primaryGoal && runtime[2].active) {
                runtime[2].state=5;advancePalaceSequence();
            }
            break;
        }
    }
    if(result && portal) {
        ports_.travel.commitSpecialPortal(std::move(*portal));state_.actTwo.tyraelPortal=true;
        captureGoal(QuestId::SevenTombs,uint32_t(TombsStage::TyraelRescued),eligible);
    }
    return result;
}
}
