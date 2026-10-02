#include "gameplay/session/session.hpp"
#include "hud_layout.hpp"
#include "presentation/scene_view.hpp"
#include "content/monsters/monster_enchantment.hpp"
#include <algorithm>
#include <sstream>
#include <tuple>

namespace d2x {
namespace {
void imageAt(const Sprite *image, Rectangle bounds, Color tint = WHITE) {
    if (!image || !image->texture.id)
        return;
    const auto &texture = image->texture;
    DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)}, bounds, {0, 0}, 0, tint);
}
} // namespace
void SceneView::orb(bool mana, float fraction) const {
    const auto *image = assets_.orbs.frame(0, mana ? 1 : 0);
    if (!image)
        return;
    auto bounds = hudGlobe(mana);
    const auto &texture = image->texture;
    int filled = int(std::round(std::clamp(fraction, 0.f, 1.f) * texture.height));
    if (filled > 0) {
        float empty = float(texture.height - filled);
        auto destination = bounds;
        destination.y += empty * hudScale;
        destination.height = filled * hudScale;
        DrawTexturePro(texture, {0, empty, float(texture.width), float(filled)}, destination, {0, 0}, 0,
                       WHITE);
    }
    // Original foreground rim/fingers, not the opaque empty-globe panel backing.
    imageAt(assets_.globeOverlap.frame(0, mana ? 1 : 0), hudRect(mana ? 691 : 28, mana ? 96 : 93, 82, 88));
}
void SceneView::drawSkillIcon(std::optional<int> skill, Rectangle bounds) const {
    const auto &player = session_.state().player;
    const auto *entry = skill ? session_.content().skills.find(*skill) : nullptr;
    auto icon = skill ? assets_.skillIcons.find(*skill) : assets_.skillIcons.end();
    const auto *image = !skill ? &assets_.attackIcon
                              : icon != assets_.skillIcons.end() ? &icon->second.sprite : nullptr;
    bool available = !player.dead && (!skill || (entry && entry->executable() && session_.skillAvailable(*skill)));
    if (session_.region().definition.safe)
        available &= entry && entry->allowedInTown;
    if (entry && entry->auraImplemented && session_.effectiveSkillRank(*skill) > 0)
        if (const auto aura = resolveAura(session_.content(), *skill, session_.effectiveSkillRank(*skill)))
            available &= player.mana >= aura->manaPerPulse;
    if (entry && entry->spell && session_.effectiveSkillRank(*skill) > 0) {
        const auto resolved = resolveSkill(*entry->spell,
            session_.effectiveSkillRank(*skill), player.skillRanks, session_.fireMasteryPercent(),
            session_.lightningMasteryPercent(), session_.characterStats().combat.coldSkillDamagePercent);
        available &= player.mana >= std::max(player.channelSkill() == *skill ? 0.f : float(entry->spell->startMana), resolved.manaCost);
        if (resolved.delayFrames > 0)
            available &= session_.state().frame >= player.skillDelayUntil;
        if (resolved.requiresShield) available &= bool(player.equipment.shield);
        if (resolved.weapon) available &= session_.weaponSkillReady(resolved);
    }
    imageAt(image, bounds, available ? WHITE : Color{255, 64, 64, 255});
}
void SceneView::drawControlPanel() const {
    const auto &player = session_.state().player;
    DrawRectangle(0, H - HUD, W, HUD, BLACK);
    // Use the original globe silhouette to mask the world above the panel.
    // The panel then supplies the original empty bowl and glass highlights.
    for (bool mana : {false, true})
        imageAt(assets_.orbs.frame(0, mana ? 1 : 0), hudGlobe(mana), BLACK);
    constexpr std::array<float, 6> offsets{0, 165, 293, 421, 549, 683};
    for (int i = 0; i < int(offsets.size()); ++i) {
        const auto &part = assets_.panel.frames.at(i);
        imageAt(&part, hudRect(offsets[i], float(part.texture.height), float(part.texture.width),
                               float(part.texture.height)));
    }
    orb(false, player.hp / session_.characterStats().maxLife);
    orb(true, player.mana / session_.characterStats().maxMana);
    drawSkillIcon(view_.leftSkill, hudSkillSlot(false));
    drawSkillIcon(view_.rightSkill, hudSkillSlot(true));
    auto stamina = hudStamina();
    const float staminaFraction = std::clamp(player.stamina / session_.characterStats().maxStamina, 0.f, 1.f);
    stamina.width *= staminaFraction;
    const auto effects = player.combatEffects.entries();
    const bool blueStamina = std::any_of(effects.begin(), effects.end(), [&](const auto &effect) {
            return effect.activeAt(session_.state().frame) && effect.spec.state.staminaBarBlue;
        });
    // OpenDiablo2 HUD supplies ordinary/low stamina colors and alpha. Blue
    // uses its shared UI blue; the original 1.13c bar color remains unverified.
    const Color staminaColor = blueStamina ? Color{105, 105, 255, 200}
        : staminaFraction < .25f ? Color{255, 0, 0, 200} : Color{175, 136, 72, 200};
    DrawRectangleRec(stamina, staminaColor);
    const auto &thresholds = session_.experienceThresholds();
    const size_t level = size_t(player.level);
    if (level + 1 < thresholds.size() && thresholds[level + 1] > thresholds[level]) {
        auto experience = hudExperience();
        const auto gained = player.experience - thresholds[level];
        const auto needed = thresholds[level + 1] - thresholds[level];
        experience.width *= std::clamp(double(gained) / double(needed), 0.0, 1.0);
        experience.height = 2 * hudScale;
        DrawRectangleRec(experience, WHITE);
    }
    imageAt(assets_.runButton.frame(0, player.running ? 2 : 0), hudRunButton());
    imageAt(assets_.attributeButtons.frame(0, player.unspentAttributes > 0
        ? (view_.pointButtonPressed == false ? 1 : 0) : 2), hudCharacterButton());
    imageAt(assets_.attributeButtons.frame(0, player.unspentSkills > 0
        ? (view_.pointButtonPressed == true ? 1 : 0) : 2), hudSkillTreeButton());
    imageAt(assets_.miniPanelToggle.frame(0, view_.miniPanelOpen ? 2 : 0), hudMenuButton());
    if (view_.miniPanelOpen) {
        if (const auto *background = assets_.miniPanel.frame(0, 0))
            imageAt(background, hudMiniPanel(*background));
        constexpr int frames[] = {0, 2, 4, 8, 10, 12, 14};
        for (int index = 0; index < 7; ++index)
            if (const auto *icon = assets_.miniPanelButtons.frame(0, frames[index]))
                imageAt(icon, hudMiniButton(*icon, index),
                        index == 4 ? Color{120, 120, 120, 255} : WHITE);
    }
}
std::optional<int> SceneView::miniPanelAt(Vec mouse) const {
    if (!view_.miniPanelOpen) return std::nullopt;
    const auto *background = assets_.miniPanel.frame(0, 0);
    if (!background || !CheckCollisionPointRec(rv(mouse), hudMiniPanel(*background)))
        return std::nullopt;
    constexpr int frames[] = {0, 2, 4, 8, 10, 12, 14};
    for (int index = 0; index < 7; ++index)
        if (const auto *icon = assets_.miniPanelButtons.frame(0, frames[index]);
            icon && CheckCollisionPointRec(rv(mouse), hudMiniButton(*icon, index)))
            return index;
    return -1;
}
std::vector<std::string> SceneView::auraSkillDetails(int skill, int rank, bool nextLevel) const {
    auto learned = session_.state().player.skillRanks;
    if (nextLevel) ++learned[skill];
    const auto aura = resolveAura(session_.content(), skill, rank, learned, session_.fireMasteryPercent(),
        session_.lightningMasteryPercent(), session_.characterStats().combat.coldSkillDamagePercent,
        session_.effectiveSkillRank(99) + (nextLevel && skill == 99 ? 1 : 0));
    if (!aura) return {};
    std::vector<std::string> lines;
    auto number = [](float value) { return std::string(TextFormat("%.1f", value)); };
    const auto &stats = aura->modifiers;
    const auto &combat = stats.combat;
    lines.push_back("Radius: " + number(aura->radius * 2.f / 3.f) + " yards");
    if (combat.damagePercent)
        lines.push_back("Damage: +" + std::to_string(combat.damagePercent + aura->ownerDamageBonus) + "%" +
            (aura->ownerDamageBonus ? " / Party: +" + std::to_string(combat.damagePercent) + "%" : ""));
    if (combat.attackRatingPercent) lines.push_back("Attack rating: +" + std::to_string(combat.attackRatingPercent) + "%");
    if (combat.defensePercent) lines.push_back("Defense: " + std::string(combat.defensePercent > 0 ? "+" : "") + std::to_string(combat.defensePercent) + "%");
    if (combat.attackRate > 0) lines.push_back("Attack speed: +" + std::to_string(combat.attackRate) + "%");
    if (stats.velocityPercent < 0) lines.push_back("Slows enemies: " + std::to_string(-stats.velocityPercent) + "%");
    else if (stats.velocityPercent > 0) lines.push_back("Movement speed: +" + std::to_string(stats.velocityPercent) + "%");
    for (const auto &[label, resist, maximum] : std::vector<std::tuple<const char *, int, int>>{
        {"Fire", stats.fireResist, combat.fireMaxResist}, {"Cold", stats.coldResist, combat.coldMaxResist},
        {"Lightning", stats.lightningResist, combat.lightningMaxResist}}) {
        if (!resist && !maximum) continue;
        std::string line = std::string(label) + " resist: " + (resist > 0 ? "+" : "") + std::to_string(resist) + "%";
        if (maximum) line += " / Maximum: +" + std::to_string(maximum) + "%";
        lines.push_back(std::move(line));
    }
    if (aura->element >= 0) {
        const char *element = aura->element == 2 ? "Fire" : aura->element == 3 ? "Lightning" : aura->element == 4 ? "Cold" : "Magic";
        lines.push_back(std::string(element) + " damage: " + number(aura->minimumDamage) + "-" + number(aura->maximumDamage) +
            " / " + number(float(aura->periodFrames) / 25.f) + " seconds");
        const auto &own = aura->ownerModifiers.combat;
        const int minimum = own.fireMinimum + own.lightningMinimum + own.coldMinimum;
        const int maximum = own.fireMaximum + own.lightningMaximum + own.coldMaximum;
        if (maximum) lines.push_back("Attack damage: +" + std::to_string(minimum) + "-" + std::to_string(maximum));
    }
    if (aura->lifePerPulse > 0) lines.push_back("Heals: " + number(aura->lifePerPulse) + " / " + number(float(aura->periodFrames) / 25.f) + " seconds");
    if (aura->harmfulDurationPercent < 100) lines.push_back("Poison / curse duration reduction: " + std::to_string(100 - aura->harmfulDurationPercent) + "%");
    if (combat.manaRecovery) lines.push_back("Mana recovery: +" + std::to_string(combat.manaRecovery) + "%");
    if (stats.staminaPercent) lines.push_back("Maximum stamina: +" + std::to_string(stats.staminaPercent) + "%");
    if (stats.staminaRecoveryBonus) lines.push_back("Stamina recovery: +" + std::to_string(stats.staminaRecoveryBonus) + "%");
    if (combat.thornsPercent) lines.push_back("Damage returned: " + std::to_string(combat.thornsPercent) + "%");
    if (combat.concentrationChance) lines.push_back("Uninterruptible attack chance: " + std::to_string(combat.concentrationChance) + "%");
    if (aura->redemptionChance) {
        lines.push_back("Corpse redemption chance: " + std::to_string(aura->redemptionChance) + "%");
        lines.push_back("Life / mana per corpse: " + number(aura->redemptionLife) + " / " + number(aura->redemptionMana));
    }
    if (aura->manaPerPulse > 0) lines.push_back("Mana per pulse: " + number(aura->manaPerPulse));
    const auto *entry = session_.content().skills.find(skill);
    const int baseRank = learned.contains(skill) ? learned.at(skill) : 0;
    if (entry && entry->passiveAttackRatingPerBaseRank)
        lines.push_back("Passive attack rating: +" + std::to_string(baseRank * entry->passiveAttackRatingPerBaseRank) + "%");
    if (entry && entry->passiveMaxResistElement >= 0)
        lines.push_back("Passive maximum resist: +" + std::to_string(baseRank / 2) + "%");
    return lines;
}
bool SceneView::leftSkillAllowed(int skill) const {
    const auto *entry = session_.content().skills.find(skill);
    return entry && entry->leftAllowed;
}
std::vector<std::optional<int>> SceneView::skillChoices(bool right) const {
    std::vector<std::optional<int>> choices{std::nullopt}; // ordinary attack
    const auto *tree = session_.content().skills.tree(session_.characterCode());
    if (!tree) return choices;
    for (int id : tree->commonSkills) {
        const auto *entry = session_.content().skills.find(id);
        if (!entry || (right == false && !entry->leftAllowed)) continue;
        if (entry->sourceName == "Throw" || entry->sourceName == "Left Hand Throw" ||
            entry->basicAction == BasicSkillAction::Attack || entry->basicAction == BasicSkillAction::LeftHandSwing ||
            entry->sourceName == "Unsummon")
            choices.push_back(id);
    }
    for (const auto &[id, entry] : session_.content().skills.skills)
        if (entry.classCode == session_.characterCode() && !entry.passive &&
            (right || entry.leftAllowed) && session_.skillAvailable(id))
            choices.push_back(id);
    return choices;
}
void SceneView::drawSkillControls(Vec mouse) const {
    if (view_.blocksWorld() || view_.inventory.drag || view_.inventory.split ||
        view_.inventory.goldDialog || view_.inventory.identify)
        return;
    std::optional<std::optional<int>> hovered;
    if (view_.skillPicker) {
        bool right = *view_.skillPicker;
        auto choices = skillChoices(right);
        for (size_t i = 0; i < choices.size(); ++i) {
            auto bounds = hudPickerSlot(right, int(i), int(choices.size()));
            drawSkillIcon(choices[i], bounds);
            const auto &hotkeys = session_.state().player.skillHotkeys;
            for (size_t key = 0; key < hotkeys.size(); ++key)
                if (hotkeys[key].right == right && hotkeys[key].skill == choices[i].value_or(-1)) {
                    auto label = "F" + std::to_string(key + 1);
                    painter_.label(label, int(bounds.x + 3), int(bounds.y + bounds.height - 12), 10, gold);
                }
            if (CheckCollisionPointRec(rv(mouse), bounds)) {
                hovered.emplace(choices[i]);
                DrawRectangleLinesEx(bounds, 1, gold);
            }
        }
    } else {
        for (bool right : {false, true})
            if (CheckCollisionPointRec(rv(mouse), hudSkillSlot(right)))
                hovered.emplace(right ? view_.rightSkill : view_.leftSkill);
    }
    if (hovered) {
        auto choice = *hovered;
        auto entry = choice ? session_.content().skills.find(*choice) : nullptr;
        auto name = entry ? entry->name : "Attack";
        std::string detail;
        std::vector<std::string> detailLines;
        if (entry && entry->spell && session_.effectiveSkillRank(*choice) > 0) {
            const auto value = resolveSkill(*entry->spell,
                session_.effectiveSkillRank(*choice), session_.state().player.skillRanks,
                session_.fireMasteryPercent(), session_.lightningMasteryPercent(),
                session_.characterStats().combat.coldSkillDamagePercent);
            detail = "Mana " + std::string(TextFormat("%.1f", value.manaCost));
            if (value.effect == SkillBehavior::Teleport) detail += " / Teleport to clear ground";
            else if (value.effect == SkillBehavior::HolyBolt)
                detail += " / Undead magic " + std::string(TextFormat("%.1f-%.1f", value.minimumDamage, value.maximumDamage)) +
                    " / Ally healing " + std::string(TextFormat("%.1f-%.1f", value.healingMinimum, value.healingMaximum));
            else if (value.effect == SkillBehavior::Inferno)
                detail = "Mana/sec " + std::string(TextFormat("%.1f", value.manaCost * 12.5f)) +
                    " / Damage/sec " + std::string(TextFormat("%.1f-%.1f", value.minimumDamage * 25, value.maximumDamage * 25));
            else if (value.appliedEffect) {
                detail += " / Defense +" + std::to_string(value.appliedEffect->modifiers.combat.defensePercent) + "% / " +
                    std::to_string(value.appliedEffect->duration.value() / 25) + " seconds";
                if (value.effect == SkillBehavior::HolyShield)
                    detail += " / Block +" + std::to_string(value.appliedEffect->modifiers.combat.blockBonus) +
                        " / Smite " + std::to_string(value.appliedEffect->modifiers.combat.smiteMinimum) +
                        "-" + std::to_string(value.appliedEffect->modifiers.combat.smiteMaximum);
                if (value.effect == SkillBehavior::ShiverArmor || value.effect == SkillBehavior::ChillingArmor)
                    detail += " / Retaliate cold " + std::string(TextFormat("%.1f-%.1f", value.minimumDamage, value.maximumDamage)) +
                        " / Chill " + std::string(TextFormat("%.1fs", value.coldDuration));
            }
            else if (value.effect == SkillBehavior::StaticField)
                detail += " / " + std::to_string(int(value.staticPercent)) + "% current life, range " +
                    std::to_string(int(value.staticRadius));
            else if (value.blizzard)
                detail += " / Cold damage per shard " + std::string(TextFormat("%.1f-%.1f", value.minimumDamage, value.maximumDamage)) +
                    " / Duration " + std::string(TextFormat("%.1fs", value.missileLifetime)) +
                    " / Delay " + std::string(TextFormat("%.1fs", float(value.delayFrames) / 25.f));
            else if (value.frozenOrb)
                detail += " / Cold damage per bolt " + std::string(TextFormat("%.1f-%.1f", value.minimumDamage, value.maximumDamage)) +
                    " / Chill " + std::string(TextFormat("%.1fs", value.coldDuration)) +
                    " / Delay " + std::string(TextFormat("%.1fs", float(value.delayFrames) / 25.f));
            else if (value.freezingArea)
                detail += " / Cold damage " + std::string(TextFormat("%.1f-%.1f", value.minimumDamage, value.maximumDamage)) +
                    " / Freeze " + std::string(TextFormat("%.2fs", float(value.freezingArea->freezeFrames) / 25.f));
            else if (value.weapon && value.poisonDuration > 0)
                detail += " / Poison " + std::string(TextFormat("%.1f-%.1f over %.1fs",
                    value.minimumDamage * value.poisonDuration * 25.f,
                    value.maximumDamage * value.poisonDuration * 25.f, value.poisonDuration));
            else if (value.effect == SkillBehavior::Zeal)
                detail += " / Attacks " + std::to_string(value.weapon->attacks) +
                    " / Physical +" + std::to_string(value.weapon->damagePercent) + "%";
            else if (value.effect == SkillBehavior::Vengeance)
                detail += " / Fire " + std::to_string(value.weapon->elementPercent[0]) +
                    "% / Cold " + std::to_string(value.weapon->elementPercent[1]) +
                    "% / Lightning " + std::to_string(value.weapon->elementPercent[2]) + "%";
            else if (value.effect == SkillBehavior::Sacrifice)
                detail += " / Physical +" + std::to_string(value.weapon->damagePercent) +
                    "% / Attack +" + std::to_string(value.weapon->attackRating) +
                    "% / Self damage " + std::to_string(value.weapon->selfDamagePercent) + "%";
            else if (value.effect == SkillBehavior::Smite)
                detail += " / Shield damage +" + std::to_string(value.weapon->damagePercent) +
                    "% / Stun " + std::string(TextFormat("%.2fs", float(value.weapon->stunFrames) / 25.f));
            else if (value.effect == SkillBehavior::Conversion)
                detail += " / Convert " + std::to_string(value.weapon->conversionChance) +
                    "% / " + std::to_string(value.weapon->conversionFrames / 25) + " seconds";
            else if (value.effect == SkillBehavior::Charge)
                detail += " / Physical +" + std::to_string(value.weapon->damagePercent) +
                    "% / Attack +" + std::to_string(value.weapon->attackRating) + "%";
            else if (value.weapon && value.missileImpact && value.missileImpact->areaMissile)
                detail += " / Fire " + std::string(TextFormat("%.1f-%.1f", value.minimumDamage, value.maximumDamage)) +
                    " + weapon fire damage";
            else detail += " / Damage " + std::string(TextFormat("%.1f", value.minimumDamage)) +
                "-" + std::string(TextFormat("%.1f", value.maximumDamage));
        } else if (entry && entry->auraImplemented) {
            detailLines = auraSkillDetails(*choice, session_.effectiveSkillRank(*choice));
        } else detail = !entry ? "Normal weapon attack"
            : entry->sourceName == "Throw" || entry->sourceName == "Left Hand Throw"
                ? "Throw equipped weapon; consumes one from the stack"
            : entry->basicAction == BasicSkillAction::Attack || entry->basicAction == BasicSkillAction::LeftHandSwing
                ? "Uses the current basic melee damage"
            : entry->sourceName == "Unsummon"
                ? "No summoned ally is available"
                : "Effect not implemented";
        if (detailLines.empty()) detailLines.push_back(detail);
        const int width = std::min(520, W - 20);
        std::vector<std::string> wrapped;
        for (const auto &line : detailLines) {
            std::istringstream words(line);
            std::string word, current;
            while (words >> word) {
                const auto candidate = current.empty() ? word : current + " " + word;
                if (!current.empty() && painter_.measure(candidate, 12) > width - 20) {
                    wrapped.push_back(current);
                    current = word;
                } else current = candidate;
            }
            if (!current.empty()) wrapped.push_back(current);
        }
        const int height = 30 + int(wrapped.size()) * 16;
        const float y = std::clamp(H - (view_.skillPicker ? 108 : 55) * hudScale - height - 8,
            5.f, float(std::max(5, H - height - 5)));
        DrawRectangle((W - width) / 2, int(y), width, height, {0, 0, 0, 225});
        painter_.centered(name, int(y + 7), 16, gold);
        for (size_t index = 0; index < wrapped.size(); ++index)
            painter_.centered(wrapped[index], int(y + 29 + index * 16), 12);
    }
    for (bool mana : {false, true}) {
        if (!CheckCollisionPointRec(rv(mouse), hudGlobe(mana)))
            continue;
        const auto &p = session_.state().player;
        auto text = std::string(mana ? "Mana: " : "Life: ") + std::to_string(int(mana ? p.mana : p.hp)) +
                    " / " + std::to_string(mana ? session_.characterStats().maxMana : session_.characterStats().maxLife);
        auto globe = hudGlobe(mana);
        painter_.label(text, int(globe.x + (globe.width - painter_.measure(text, 12)) / 2), int(globe.y - 20),
                       12, parchment);
    }
    std::string hint;
    if (CheckCollisionPointRec(rv(mouse), hudRunButton()))
        hint = "Walk / Run [SPACE]";
    if (CheckCollisionPointRec(rv(mouse), hudMenuButton()))
        hint = view_.miniPanelOpen ? "Close mini panel" : "Open mini panel";
    if (auto button = miniPanelAt(mouse); button && *button >= 0) {
        constexpr const char *labels[] = {"Character [C]", "Inventory [I]", "Skill Tree [S]",
                                          "Automap [TAB]", "Message unavailable", "Quest Log [Q]",
                                          "Game menu [Esc]"};
        hint = labels[*button];
    }
    if (CheckCollisionPointRec(rv(mouse), hudStamina()))
        hint = "Stamina: " + std::to_string(int(session_.state().player.stamina)) + " / " +
               std::to_string(session_.characterStats().maxStamina);
    if (session_.state().player.unspentAttributes > 0 &&
        CheckCollisionPointRec(rv(mouse), hudCharacterButton())) {
        hint = "New attribute points [A]";
    }
    if (session_.state().player.unspentSkills > 0 &&
        CheckCollisionPointRec(rv(mouse), hudSkillTreeButton())) {
        hint = "New skill points [S]";
    }
    if (CheckCollisionPointRec(rv(mouse), hudExperience())) {
        const auto &thresholds = session_.experienceThresholds();
        const size_t level = size_t(session_.state().player.level);
        hint = "Experience: " + std::to_string(session_.state().player.experience) + " / " +
               (level + 1 < thresholds.size() ? std::to_string(thresholds[level + 1]) : "MAX");
    }
    if (!hint.empty())
        painter_.centered(hint, H - HUD - 24, 12, parchment);
}
} // namespace d2x
