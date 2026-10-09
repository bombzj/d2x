#include "system.hpp"
#include "server/player_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/effects/system.hpp"
#include <algorithm>
#include <limits>
#include <type_traits>
namespace d2x::server::progression {
namespace {
DomainResult<> commit(transactions::System &transactions, const PlayerState &source,
    const ActorContext &actor, CharacterRecord record, transactions::ResourceRefresh refresh, uint64_t award = 0) {
    auto prepared = transactions.prepare(transactions::CharacterEdit{actor, source.inventoryRevision,
        source.characterRevision, std::move(record), refresh, award});
    if (!prepared) return {prepared.status, {}};
    return transactions.commit(std::move(*prepared.value));
}
}
DomainResult<> System::execute(const ActorContext &actor, const Request &request) {
    const auto *player = ports_.players.find(actor.player);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area || player->persistent.player.hp <= 0)
        return {DomainStatus::InvalidActor, {}};
    if (!player->rules.character) return {DomainStatus::Unavailable, {}};
    if (!request.count || request.count > 100) return {DomainStatus::InvalidRequest, {}};
    auto record = player->persistent.player;
    auto refresh = transactions::ResourceRefresh::Clamp;
    const auto status = std::visit([&]<class T>(const T &operation) -> DomainStatus {
        if constexpr (std::is_same_v<T, AllocateAttribute>) {
            if (record.unspentAttributes < int(request.count)) return DomainStatus::Conflict;
            int *value = nullptr;
            switch (operation.attribute) {
            case Attribute::Strength: value = &record.allocated.strength; break;
            case Attribute::Dexterity: value = &record.allocated.dexterity; break;
            case Attribute::Vitality: value = &record.allocated.vitality; break;
            case Attribute::Energy: value = &record.allocated.energy; break;
            }
            if (!value || *value > INT32_MAX - int(request.count)) return DomainStatus::InvalidRequest;
            *value += int(request.count); record.unspentAttributes -= int(request.count);
            refresh = transactions::ResourceRefresh::AttributeGain;
            return DomainStatus::Applied;
        } else {
            const auto rule = player->rules.character->learning.find(operation.id);
            if (rule == player->rules.character->learning.end()) return DomainStatus::InvalidRequest;
            const auto &learning = rule->second;
            const auto current = record.skillRanks.find(operation.id);
            const int rank = current == record.skillRanks.end() ? 0 : current->second;
            if (request.count != 1 || learning.classCode != player->definition.code || learning.maximumRank <= 0 ||
                rank < 0 || rank >= learning.maximumRank || record.level < int64_t(learning.requiredLevel) + rank ||
                record.unspentSkills <= 0) return DomainStatus::Conflict;
            const auto &attributes = player->totals.character;
            const int values[]{attributes.strength, attributes.dexterity, attributes.vitality, attributes.energy};
            for (size_t index = 0; index < learning.requiredAttributes.size(); ++index)
                if (values[index] < learning.requiredAttributes[index]) return DomainStatus::Conflict;
            for (const auto prerequisite : learning.prerequisites) {
                const auto learned = record.skillRanks.find(prerequisite);
                if (learned == record.skillRanks.end() || learned->second <= 0) return DomainStatus::Conflict;
            }
            record.skillRanks[operation.id] = rank + 1; --record.unspentSkills;
            return DomainStatus::Applied;
        }
    }, request.intent);
    if (status != DomainStatus::Applied) return {status, {}};
    return commit(ports_.transactions, *player, actor, std::move(record), refresh);
}
DomainResult<CharacterRecord> addExperience(CharacterRecord record, const CharacterDefinition &definition, const CharacterRules &rules, uint64_t amount) {
    const auto &thresholds = rules.experience;
    if (record.level < 1 || size_t(record.level) >= thresholds.size()) return {DomainStatus::InvalidRequest, {}};
    const auto maximum = thresholds.back();
    if (record.experience >= maximum) return {DomainStatus::Conflict, {}};
    record.experience += std::min(amount, maximum - record.experience);
    const int previous = record.level;
    while (size_t(record.level + 1) < thresholds.size() && record.experience >= thresholds[size_t(record.level + 1)]) ++record.level;
    const int gained = record.level - previous;
    const int64_t points = int64_t(gained) * definition.statPerLevel;
    if (points < 0 || points > INT32_MAX - int64_t(record.unspentAttributes) ||
        gained > INT32_MAX - int64_t(record.unspentSkills)) return {DomainStatus::Capacity, {}};
    record.unspentAttributes += int(points); record.unspentSkills += gained;
    return {DomainStatus::Applied, std::move(record)};
}
DomainResult<> System::award(const Award &award) {
    const auto *player = ports_.players.find(award.player);
    if (!player || player->persistent.player.hp <= 0) return {DomainStatus::InvalidActor, {}};
    if (!player->rules.character || player->rules.character->experience.size() < 3) return {DomainStatus::Unavailable, {}};
    if (!award.sourceOccurrence || !award.experience) return {DomainStatus::InvalidRequest, {}};
    if (award.sourceOccurrence <= player->lastExperienceAward) return {DomainStatus::Stale, {}};
    auto planned = addExperience(player->persistent.player, player->definition, *player->rules.character, award.experience);
    if (!planned) return {planned.status, {}};
    auto record = std::move(*planned.value);
    const int gained = record.level - player->persistent.player.level;
    const ActorContext actor{player->player, player->actor, player->area, 0, 0, award.tick};
    auto events=gained?ports_.effects.prepareItemEvents(actor,{ItemSkillEvent::LevelUp},{},player->position):DomainResult<effects::ItemEventPlan>{DomainStatus::Applied,effects::ItemEventPlan{}};
    if(!events) return {events.status,{}};
    const auto result=commit(ports_.transactions, *player, actor, std::move(record), gained ? transactions::ResourceRefresh::LevelUp
        : transactions::ResourceRefresh::Clamp, award.sourceOccurrence);
    if(result) ports_.effects.commitItemEvents(std::move(*events.value));
    return result;
}
StepStatus System::step(TickContext, FrameFacts &) { return StepStatus::Complete; }
}
