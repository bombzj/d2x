#include "system.hpp"
#include "detail.hpp"
#include "server/player_store.hpp"
#include "server/systems/npc/system.hpp"
#include "server/systems/travel/system.hpp"
#include "gameplay/quest/acts/act_two_state.hpp"
#include <algorithm>

namespace d2x::server::quests {
std::optional<Dialogue> System::actTwoDialogue(const ActorContext &actor,const NpcRule &npc) const {
    const auto *p=ports_.players.find(actor.player);if(!p) return {};
    const auto &record=p->persistent.player;
    const auto &book=record.quests.at(size_t(ports_.settings.difficulty));
    const auto stage=[&](QuestId id){return book.at(questIndex(id)).stage;};
    const auto emit=[&](QuestId id,uint16_t text,uint8_t menu=1)->std::optional<Dialogue> {
        if(!npc.questMessages.contains(text)) return {};
        return Dialogue{id,{menu,text}};
    };
    // Original A2Q0 is a saved arrival flag, separate from PlrIntro.
    if(npc.code=="jerhyn" && !record.questPreludes.at(size_t(ports_.settings.difficulty)).at(size_t(QuestPreludeId::LutGholeinArrival)))
        return emit(QuestId::SevenTombs,253,0);
    const auto tombs=stage(QuestId::SevenTombs);
    if(npc.code=="tyrael1" && tombs>=uint32_t(TombsStage::DurielSlain)) return emit(QuestId::SevenTombs,302);
    if(npc.code=="jerhyn" && tombs==uint32_t(TombsStage::TyraelRescued)) return emit(QuestId::SevenTombs,442);
    if(npc.code=="meshif1" && tombs==uint32_t(TombsStage::JerhynConfirmed)) return emit(QuestId::SevenTombs,450,2);
    if(npc.code=="atma") {
        const auto radament=stage(QuestId::RadamentsLair);
        if(!radament || radament==uint32_t(RadamentStage::Slain)) return emit(QuestId::RadamentsLair,radament?334:304,radament?1:0);
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
        if(sun==uint32_t(SunStage::Darkness)) return emit(QuestId::TaintedSun,348);
        if(sun==uint32_t(SunStage::AltarDestroyed)) return emit(QuestId::TaintedSun,371);
        if(sun>=uint32_t(SunStage::AltarDestroyed) && !stage(QuestId::ArcaneSanctuary)) return emit(QuestId::ArcaneSanctuary,373);
    }
    if(npc.code=="jerhyn") {
        if(stage(QuestId::ArcaneSanctuary)==uint32_t(ArcaneStage::PalaceOpened)) return emit(QuestId::ArcaneSanctuary,377);
        if(stage(QuestId::ArcaneSanctuary)>0 && !tombs) return emit(QuestId::SevenTombs,430);
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
    if(npc.code=="atma" && stage(QuestId::RadamentsLair)<uint32_t(RadamentStage::Slain)) return emit(QuestId::RadamentsLair,stage(QuestId::RadamentsLair)==1?310:317,2);
    return {};
}
DomainResult<> System::acknowledgeActTwo(const ActorContext &actor,const Request &request,const NpcRule &) {
    const auto *p=ports_.players.find(actor.player);auto record=p->persistent.player;
    auto &book=record.quests.at(size_t(ports_.settings.difficulty));auto &q=book.at(questIndex(request.quest));
    const auto message=*request.message;
    std::optional<travel::SpecialPortalPlan> portal;
    switch(message) {
    case 253: record.questPreludes.at(size_t(ports_.settings.difficulty)).at(size_t(QuestPreludeId::LutGholeinArrival))=true;break;
    case 304: q.stage=uint32_t(RadamentStage::Assigned);break;
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
        if(q.stage<uint32_t(TombsStage::DurielSlain)) return {DomainStatus::Conflict,{}};
        if(!state_.actTwo.tyraelPortal) {
            const auto result=ports_.travel.prepareSpecialPortal(actor,travel::SpecialPortalKind::Tyrael,0,p->position);
            if(!result) return {result.status,{}};
            portal=*result.value;
        }
        q.stage=std::max(q.stage,uint32_t(TombsStage::TyraelRescued));break;
    }
    case 442: q.stage=uint32_t(TombsStage::JerhynConfirmed);break;
    case 450: q.stage=uint32_t(TombsStage::PassageGranted);break;
    default:
        if(request.quest==QuestId::TaintedSun && q.stage==uint32_t(SunStage::AltarDestroyed)) q.stage=uint32_t(SunStage::Confirmed);
        else if(request.quest==QuestId::Summoner && q.stage==uint32_t(SummonerStage::Slain)) q.stage=uint32_t(SummonerStage::Confirmed);
        else if(request.quest==QuestId::ArcaneSanctuary && q.stage==uint32_t(ArcaneStage::JournalRead)) q.flags&=~arcaneCommentPending;
        else return {DomainStatus::Applied,std::monostate{}};
    }
    const auto result=commit(actor,std::move(record));
    if(result && portal) {ports_.travel.commitSpecialPortal(std::move(*portal));state_.actTwo.tyraelPortal=true;}
    return result;
}
}
