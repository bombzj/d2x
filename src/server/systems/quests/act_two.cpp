#include "system.hpp"
#include "detail.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/world/system.hpp"
#include "gameplay/quest/acts/act_two_state.hpp"
#include "gameplay/quest/acts/act_three_state.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include "core/random.hpp"
#include "world/interaction_geometry.hpp"
#include <algorithm>

namespace d2x::server::quests {
QuestFact System::fact(PlayerId player,const CharacterRecord &record,RegionId region) const {
    QuestFact result{player,record,ports_.settings.difficulty,state_.actOne.denRemaining,state_.actOne.stones};
    const auto *area=ports_.areas.find(region);
    result.rescued=state_.actFive.rescued;
    if(const auto *p=ports_.players.find(player);p && record.completedActs.at(size_t(ports_.settings.difficulty)).at(1)) {
        const auto &q=record.quests.at(size_t(ports_.settings.difficulty))[questIndex(QuestId::KhalimsWill)];
        const auto &items=p->rules.character->laterQuests;
        uint8_t status=0;
        if(q.stage>=4) status=13;
        else if(detail::actTwoCarried(*p,items.khalimWill,ports_.settings.difficulty)) status=6;
        else {
            std::array<bool,4> parts{};unsigned count=0;
            for(size_t i=0;i<4;++i) {parts[i]=detail::actTwoCarried(*p,items.khalimParts[i],ports_.settings.difficulty)!=nullptr;count+=parts[i];}
            status=count==4?uint8_t((q.flags&khalimWillExplained)?5:7):!parts[0]?uint8_t(q.stage?1:0):!parts[1]?2:!parts[2]?4:uint8_t(4*parts[3]+3);
        }
        result.khalimStatus=status;
    }
    if(area && area->definition.act==1) {
        result.eclipse=state_.actTwo.dark;
        if(record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::ArcaneSanctuary)).stage>=uint32_t(ArcaneStage::JournalRead)) result.tombOffset=state_.actTwo.tombOffset;
        const auto *p=ports_.players.find(player);
        if(p) {
            const auto &book=record.quests.at(size_t(ports_.settings.difficulty));
            const auto &staff=book.at(questIndex(QuestId::HoradricStaff));
            const auto &items=p->rules.character->actTwo;
            const bool explained=staff.flags&staffScrollExplained;
            uint8_t status=0;
            if(record.completedActs.at(size_t(ports_.settings.difficulty)).at(0)) {
                if(staff.stage>=uint32_t(StaffStage::Submitted)) status=book.at(questIndex(QuestId::SevenTombs)).stage>=uint32_t(TombsStage::PassageGranted)?13:uint8_t(6-bool(staff.flags&staffAssemblyExplained));
                else if(detail::actTwoCarried(*p,items.staff,ports_.settings.difficulty)) status=uint8_t(6-bool(staff.flags&staffAssemblyExplained));
                else {
                    const bool cube=detail::actTwoCarried(*p,items.cube,ports_.settings.difficulty);
                    const bool shaft=detail::actTwoCarried(*p,items.shaft,ports_.settings.difficulty);
                    const bool amulet=detail::actTwoCarried(*p,items.amulet,ports_.settings.difficulty);
                    if(cube && shaft && amulet) status=staff.flags&staffCubeExplained?3:explained?2:4;
                    else if(detail::actTwoCarried(*p,items.scroll,ports_.settings.difficulty) && !explained) status=1;
                    else if(explained) status=2;
                    else if(cube || shaft || amulet) status=4;
                }
            }
            result.staffStatus=status;
        }
    }
    return result;
}
bool System::allowsTravel(const CharacterRecord &record,RegionId from,RegionId to) const {
    const auto &book=record.quests.at(size_t(ports_.settings.difficulty));
    if(int(to)==73) return int(from)==int(state_.actTwo.tomb) && state_.actTwo.tombOpen;
    if(int(from)==40 && int(to)==50) return book.at(questIndex(QuestId::ArcaneSanctuary)).stage>=uint32_t(ArcaneStage::PalaceOpened) || book.at(questIndex(QuestId::SevenTombs)).stage>=uint32_t(TombsStage::DurielSlain);
    if(int(to)==100) return state_.actThree.orbSmashed || book.at(questIndex(QuestId::KhalimsWill)).stage>=4;
    if(int(from)==102 && int(to)==103) return book.at(questIndex(QuestId::Guardian)).stage>=4;
    if(int(to)==113) return book.at(questIndex(QuestId::SiegeOnHarrogath)).stage>=3;
    if(int(to)==128) return book.at(questIndex(QuestId::RiteOfPassage)).stage>=4;
    if(int(to)==132) return (int(from)!=131 || state_.actFive.throneDeparted) && book.at(questIndex(QuestId::RiteOfPassage)).stage>=4;
    if(int(from)==103 && int(to)==109) return book.at(questIndex(QuestId::TerrorsEnd)).stage>=4;
    if(int(from)==132 && int(to)==109) return book.at(questIndex(QuestId::EveOfDestruction)).stage>=5 && book.at(questIndex(QuestId::EveOfDestruction)).flags&baalTyraelSpoken;
    if(int(from)==109 && int(to)==121) return book.at(questIndex(QuestId::PrisonOfIce)).stage>=5;
    return true;
}
StepStatus System::actTwoStep(TickContext tick) {
    bool blocked=false;
    for(auto pending=state_.actTwo.journalReads.begin();pending!=state_.actTwo.journalReads.end();) {
        const auto *p=ports_.players.find(pending->first);
        const auto *area=p?ports_.areas.find(p->area):nullptr;
        const auto lease=state_.actTwo.journals.find(pending->first);
        if(!p || !p->entered || p->actor!=pending->second.actor || p->area!=pending->second.area || !area || area->generation!=pending->second.areaGeneration || p->persistent.player.hp<=0 || lease==state_.actTwo.journals.end()) {pending=state_.actTwo.journalReads.erase(pending);continue;}
        auto actor=pending->second;actor.tick=tick.tick;
        const auto result=operateActTwo(actor,lease->second,0,42,p->position);
        if(result.status==DomainStatus::Capacity) {blocked=true;++pending;continue;}
        state_.actTwo.journals.erase(lease);pending=state_.actTwo.journalReads.erase(pending);
    }
    for(const auto &[region,area]:ports_.areas.all()) {
        (void)region;if(area.definition.staffTomb) state_.actTwo.tombOffset=uint16_t(*area.definition.staffTomb-66);
    }
    for(const auto &[id,m]:ports_.monsters.read().actors) {
        if(m.life>0 || m.owner || m.identity.origin==SpawnOrigin::Debug) continue;
        if(int(m.area)==49 && m.identity.monster=="radament" && !state_.objectives.at(questIndex(QuestId::RadamentsLair))) {
            captureGoal(QuestId::RadamentsLair,uint32_t(RadamentStage::Slain),m.area);
            state_.actTwo.radament=id;state_.actTwo.bookPosition=m.position;
            state_.actTwo.radamentBooks=state_.goals.at(QuestId::RadamentsLair).eligible;
        }
        if(int(m.area)==74 && m.identity.monster=="summoner") captureGoal(QuestId::Summoner,uint32_t(SummonerStage::Slain),m.area);
        if(int(m.area)==73 && m.identity.monster=="duriel") {
            captureGoal(QuestId::SevenTombs,uint32_t(TombsStage::DurielSlain),m.area);state_.actTwo.durielSlain=true;
        }
    }
    if(state_.actTwo.sunScheduled && !state_.actTwo.altarDestroyed && tick.tick>=state_.actTwo.sunAt) state_.actTwo.dark=true;
    if(state_.actTwo.tombAt && !state_.actTwo.tombOpen && tick.tick>=state_.actTwo.tombAt) {
        const auto &area=ports_.areas.at(state_.actTwo.tomb);
        const auto entrance=std::find_if(area.definition.objects.begin(),area.definition.objects.end(),[](const auto &o){return o.questSpawn;});
        if(entrance!=area.definition.objects.end() && tick.tick>=state_.actTwo.tombAt+entrance->rule.openingTicks) {ports_.world.openTombWall(state_.actTwo.tomb);state_.actTwo.tombOpen=true;}
    }
    for(const auto &[id,p]:ports_.players.all()) {
        if(!p.entered) continue;
        const auto &area=ports_.areas.at(p.area);const ActorContext actor{id,p.actor,p.area,area.generation,0,tick.tick};
        auto record=p.persistent.player;auto &book=record.quests.at(size_t(ports_.settings.difficulty));const auto before=book;
        auto &radament=book.at(questIndex(QuestId::RadamentsLair));
        if(area.definition.act==1 && int(p.area)!=40 && radament.stage==uint32_t(RadamentStage::Assigned)) radament.stage=uint32_t(RadamentStage::LeftTown);
        if((int(p.area)==44 || int(p.area)==45) && !state_.actTwo.sunScheduled && !state_.actTwo.altarDestroyed) {
            auto random=ports_.random;const auto delay=limitedRandom(random,2)+15;
            state_.actTwo.sunAt=(tick.tick/20+delay+1)*20;state_.actTwo.sunScheduled=true;ports_.random=random;
        }
        auto &sun=book.at(questIndex(QuestId::TaintedSun));
        if(area.definition.act==1 && state_.actTwo.dark && !sun.stage) sun.stage=uint32_t(SunStage::Darkness);
        if(int(p.area)==74) {
            auto &arcane=book.at(questIndex(QuestId::ArcaneSanctuary));arcane.stage=std::max(arcane.stage,uint32_t(ArcaneStage::Entered));
            auto &summoner=book.at(questIndex(QuestId::Summoner));
            if(!summoner.stage) for(const auto &[monsterId,monster]:ports_.monsters.read().actors) {
                (void)monsterId;
                if(monster.area==p.area && monster.identity.monster=="summoner" && monster.identity.origin!=SpawnOrigin::Debug && monster.life>0 && area.definition.activation.nearby(p.position,monster.position)) {summoner.stage=uint32_t(SummonerStage::Encountered);break;}
            }
        }
        const auto &items=p.rules.character->actTwo;auto &staff=book.at(questIndex(QuestId::HoradricStaff));
        if(staff.stage>=uint32_t(StaffStage::Submitted) && !state_.actTwo.tombAt && !state_.actTwo.tombOpen && area.definition.staffTomb && int(p.area)==*area.definition.staffTomb && !area.definition.openedTombWall.empty()) {state_.actTwo.tomb=p.area;state_.actTwo.tombAt=tick.tick;}
        if(staff.stage<uint32_t(StaffStage::Submitted)) {
            if(detail::actTwoCarried(p,items.staff,ports_.settings.difficulty)) staff.stage=uint32_t(StaffStage::Assembled);
            else if(detail::actTwoCarried(p,items.scroll,ports_.settings.difficulty) || detail::actTwoCarried(p,items.shaft,ports_.settings.difficulty) || detail::actTwoCarried(p,items.amulet,ports_.settings.difficulty) || detail::actTwoCarried(p,items.cube,ports_.settings.difficulty)) staff.stage=std::max(staff.stage,uint32_t(StaffStage::ArtifactsFound));
        }
        const auto observation=std::tuple{p.area,state_.actTwo.dark,p.inventoryRevision};
        const bool changed=!std::equal(before.begin(),before.end(),book.begin(),[](const auto &a,const auto &b){return a.stage==b.stage && a.flags==b.flags;});
        if(changed || (area.definition.act==1 && (!state_.actTwo.observed.contains(id) || state_.actTwo.observed.at(id)!=observation))) {
            if(commit(actor,std::move(record))) state_.actTwo.observed.insert_or_assign(id,observation);else blocked=true;
        }
        if(state_.actTwo.radamentBooks.contains(id) && !state_.pending.contains(id)) {
            if(p.persistent.player.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::RadamentsLair)).flags&radamentBookUsed) {state_.actTwo.radamentBooks.erase(id);continue;}
            if(int(p.area)==49 && p.persistent.player.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::RadamentsLair)).stage>=uint32_t(RadamentStage::Slain) && p.persistent.player.hp>0)
                if(!prepareReward(actor,state_.actTwo.radament,RewardKind::SkillBook,false,state_.actTwo.bookPosition)) blocked=true;
        }
    }
    std::erase_if(state_.actTwo.observed,[&](const auto &entry){return !ports_.players.find(entry.first);});
    std::erase_if(state_.actTwo.radamentBooks,[&](PlayerId id){return !ports_.players.find(id);});
    std::erase_if(state_.actTwo.journals,[&](const auto &entry){const auto *p=ports_.players.find(entry.first);return !p || !p->entered || int(p->area)!=74 || p->persistent.player.hp<=0;});
    std::erase_if(state_.actTwo.orifices,[&](const auto &entry){
        const auto *p=ports_.players.find(entry.first);if(!p || !p->entered) return true;
        const auto *area=ports_.areas.find(p->area);
        if(area && p->persistent.player.hp>0) for(const auto &object:area->definition.objects)
            if(object.id==entry.second && interactionClear(area->definition.collision,p->position,
                {object.id,object.position,object.position,object.rule.width,object.rule.height,float(object.rule.range),true})) return false;
        const auto sent=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Player,entry.first,p->area},{NpcServiceFact{entry.second,1,0}}});
        if(!sent) blocked=true;
        return bool(sent);
    });
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
