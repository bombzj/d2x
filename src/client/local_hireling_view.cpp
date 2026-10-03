#include "client/local_npc_client.hpp"
#include "gameplay/session/session.hpp"
#include "gameplay/model/state.hpp"
#include "content/classic_data.hpp"
#include <utility>

namespace d2x {
namespace {
std::string translatedName(const ClassicData &content, std::string_view key) {
    const auto found = content.itemStrings.find(key);
    return found == content.itemStrings.end() ? std::string(key) : found->second;
}
}
const HirelingView &LocalNpcClient::hireling() const {
    if (hireling_.revision == session_.viewRevision()) return hireling_;
    HirelingView view;
    view.revision = session_.viewRevision();
    const auto &player = session_.state().player;
    const auto &merc = player.hireling;
    view.actor = player.id; view.id = merc.id; view.position = merc.pos;
    view.known = session_.hirelingDefinition() != nullptr; view.active = merc.active();
    view.name = translatedName(session_.content(), merc.nameKey);
    view.level = merc.level; view.life = merc.hp; view.experience = merc.experience;
    if (view.known) {
        const auto stats = session_.hirelingStats();
        view.maximumLife = stats.base.life; view.strength = stats.base.strength;
        view.dexterity = stats.base.dexterity; view.defense = stats.base.defense;
        view.damageMinimum = stats.displayDamageMin; view.damageMaximum = stats.displayDamageMax;
        view.nextExperience = stats.base.nextExperience;
        view.resistances = {stats.fireResist, stats.coldResist, stats.lightningResist, stats.poisonResist};
    }
    for (const auto &pet : session_.state().companions)
        if (pet.hp > 0 && pet.allegiance.owner == player.id) ++view.summonCounts[pet.summonSkill];
    hireling_ = std::move(view);
    return hireling_;
}
const HirelingListView &LocalNpcClient::hirelings(EntityId npc) const {
    if (hirelings_.revision == session_.viewRevision() && hirelings_.npc == npc) return hirelings_;
    HirelingListView view;
    view.revision = session_.viewRevision(); view.npc = npc;
    view.actor = session_.state().player.id; view.gold = session_.state().player.character.gold;
    const auto &content = session_.content();
    view.cancelLabel = translatedName(content, "Cancel");
    if (const auto *offers = session_.hirelingOffers(npc)) for (const auto &offer : *offers) {
        HirelingOfferView value;
        value.slot = offer.slot; value.name = translatedName(content, offer.nameKey);
        value.level = offer.level; value.life = offer.stats.life;
        value.defense = offer.stats.defense; value.price = offer.stats.price;
        for (const auto &definition : content.hirelings) if (definition.sourceRow == offer.sourceRow) {
            const auto found = content.hirelingDescriptions.find(definition.description);
            if (found != content.hirelingDescriptions.end()) value.description = found->second;
            break;
        }
        view.offers.push_back(std::move(value));
    }
    hirelings_ = std::move(view);
    return hirelings_;
}
} // namespace d2x
