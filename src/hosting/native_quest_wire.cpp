#include "native_quest_wire.hpp"
#include "content/classic_data.hpp"
#include "persistence/d2s_quests.hpp"
#include "persistence/d2s_fixed_sections.hpp"
#include "hosting/protocol/message_catalog.hpp"
#include "gameplay/quest/den_of_evil.hpp"
#include "gameplay/quest/search_for_cain.hpp"
namespace d2x {
std::vector<Bytes> nativeQuestUpdate(const ClassicData &data,const server::QuestFact &fact) {
    using namespace hosting; D2sFixedSections sections;
    const auto &record=fact.record;
    if(!record.nativeSaveSections.empty()) sections=readD2sFixedSections(std::span(reinterpret_cast<const uint8_t *>(record.nativeSaveSections.data()),record.nativeSaveSections.size()));
    exportD2sQuests(record,sections,data.npcDialogues);
    const auto flags=std::span<const uint8_t>(sections.quests).subspan(10+size_t(fact.difficulty)*96,96);
    std::vector<Bytes> result;
    result.push_back(encodeServerPacket(ServerMessage::PlayerQuests,[&](auto &out){out.u8(1);out.u32(uint32_t(record.id.value));out.u8(0);out.append(flags);}));
    const auto &book=record.quests.at(size_t(fact.difficulty));
    // A1Q1..6 StatusCycler: native quest numbers follow record slots,
    // not the journal's display order (Tools=3, Cain=4).
    for(const auto &definition:questsForAct(0)) {
        const auto stage=book.at(questIndex(definition.id)).stage;uint8_t status=0;
        switch(definition.id) {
        case QuestId::DenOfEvil: status=stage>=4?13:stage==3?5:stage==2?(fact.remaining<=5?4:2):uint8_t(stage);break;
        case QuestId::SistersBurialGrounds: status=stage>=4?13:uint8_t(stage);break;
        case QuestId::ToolsOfTheTrade: status=stage>=6?13:stage>=5?5:stage>=3?3:stage==2?2:uint8_t(stage);break;
        case QuestId::SearchForCain: status=stage>=8?13:stage>=7?6:stage>=5?5:stage>=4?4:stage>=2?3:uint8_t(stage);break;
        case QuestId::ForgottenTower: status=stage>=4?13:stage==3?4:stage==2?3:stage==1?1:0;break;
        case QuestId::SistersToTheSlaughter: status=stage>=5?13:stage>=3?3:uint8_t(stage);break;
        default: break;
        }
        result.push_back(encodeServerPacket(ServerMessage::QuestUpdate,[&](auto &out){out.u8(uint8_t(definition.nativeSlot));out.u8(0);out.u8(status);out.u16(definition.id==QuestId::DenOfEvil?uint16_t(fact.remaining):0);}));
    }
    unsigned seen=0;bool valid=true;
    for(const int stone:fact.stones) {if(stone<17 || stone>21 || (seen&(1u<<(stone-17)))) {valid=false;break;}seen|=1u<<(stone-17);}
    if(valid && book.at(questIndex(QuestId::SearchForCain)).stage>=uint32_t(CainStage::ScrollTranslated))
        result.push_back(encodeServerPacket(ServerMessage::QuestPayload,[&](auto &out){out.u16(4);for(int stone:fact.stones) out.u16(uint16_t(stone-17));out.u16(0);}));
    return result;
}
}
