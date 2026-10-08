#include "native_quest_wire.hpp"
#include "content/classic_data.hpp"
#include "persistence/d2s_quests.hpp"
#include "persistence/d2s_fixed_sections.hpp"
#include "hosting/protocol/message_catalog.hpp"
#include "gameplay/quest/den_of_evil.hpp"
namespace d2x {
std::vector<Bytes> nativeQuestUpdate(const ClassicData &data,const server::QuestFact &fact) {
    using namespace hosting; D2sFixedSections sections;
    const auto &record=fact.record;
    if(!record.nativeSaveSections.empty()) sections=readD2sFixedSections(std::span(reinterpret_cast<const uint8_t *>(record.nativeSaveSections.data()),record.nativeSaveSections.size()));
    exportD2sQuests(record,sections,data.npcDialogues);
    const auto flags=std::span<const uint8_t>(sections.quests).subspan(10+size_t(fact.difficulty)*96,96);
    std::vector<Bytes> result;
    result.push_back(encodeServerPacket(ServerMessage::PlayerQuests,[&](auto &out){out.u8(1);out.u32(uint32_t(record.id.value));out.u8(0);out.append(flags);}));
    const auto stage=DenStage(record.quests.at(size_t(fact.difficulty)).at(questIndex(QuestId::DenOfEvil)).stage);
    const uint8_t status=stage==DenStage::Unstarted?0:stage==DenStage::Assigned?1:stage==DenStage::Entered?(fact.remaining<=5?4:2):stage==DenStage::Cleared?5:13;
    result.push_back(encodeServerPacket(ServerMessage::QuestUpdate,[&](auto &out){out.u8(1);out.u8(0);out.u8(status);out.u16(uint16_t(fact.remaining));}));
    return result;
}
}
