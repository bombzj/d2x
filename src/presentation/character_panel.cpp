#include "scene_view.hpp"
#include "character_panel.hpp"
#include "character_action_stats.hpp"
#include <algorithm>

namespace d2x {
void SceneView::drawCharacter(Vec mouse) const {
    if (!view_.characterOpen) return;
    const auto panel = characterBounds();
    drawPanelFrame(false);
    if (assets_.inventoryPanel.frames.size() >= 4)
        for (int index = 0; index < 4; ++index) {
            const auto &tile = assets_.inventoryPanel.frames[index].texture;
            DrawTexturePro(tile, {0, 0, float(tile.width), float(tile.height)},
                           {panel.x + (index % 2) * 256 * inventoryScale,
                            panel.y + (index / 2) * 256 * inventoryScale,
                            tile.width * inventoryScale, tile.height * inventoryScale},
                           {0, 0}, 0, WHITE);
        }
    const auto close = characterClose();
    const Color cross = CheckCollisionPointRec(rv(mouse), close)
        ? Color{232, 216, 179, 255} : Color{153, 150, 140, 255};
    const float inset = close.width * .27f;
    DrawLineEx({close.x + inset, close.y + inset},
               {close.x + close.width - inset, close.y + close.height - inset}, 3, cross);
    DrawLineEx({close.x + close.width - inset, close.y + inset},
               {close.x + inset, close.y + close.height - inset}, 3, cross);

    // invchar.dc6 supplies the boxes; inventory.txt only describes the other half's item slots.
    // Coordinates are in the original 320 x 432 character art, before inventoryScale.
    auto cell = [&](const std::string &value, float x, float y, float width, float height,
                    int size, Color color, bool centered = false) {
        const auto box = characterArtRect(x, y, width, height);
        while (size > 8 && painter_.measure(value, size) > box.width - 5)
            --size;
        const int textWidth = painter_.measure(value, size);
        const int textX = int(box.x + (centered ? (box.width - textWidth) / 2 : 4));
        painter_.label(value, textX, int(box.y + (box.height - size) / 2), size, color);
    };
    auto paired = [&](const std::string &label, const std::string &value, float y) {
        cell(label, 160, y, 95, 19, 11, parchment);
        cell(value, 255, y, 57, 19, 11, gold, true);
    };

    const auto &player = session_.state().player;
    const auto &stats = session_.characterStats();
    const auto &equipment = session_.equipmentStats();
    cell(player.name, 10, 9, 173, 20, 14, gold, true);
    cell(session_.characterName(), 191, 9, 120, 20, 14, gold, true);
    cell("Level", 11, 35, 45, 13, 10, parchment, true);
    cell(std::to_string(player.level), 11, 48, 45, 20, 13, gold, true);
    cell("Experience", 61, 35, 121, 13, 10, parchment, true);
    cell(std::to_string(player.experience), 61, 48, 121, 20, 12, gold, true);
    const auto &thresholds = session_.experienceThresholds();
    auto next = size_t(player.level + 1) < thresholds.size()
                    ? std::to_string(thresholds[size_t(player.level + 1)]) : "MAX";
    cell("Next Level", 191, 35, 120, 13, 10, parchment, true);
    cell(next, 191, 48, 120, 20, 12, gold, true);

    constexpr float attributeY[] = {83, 146, 230, 295};
    const char *names[] = {"Strength", "Dexterity", "Vitality", "Energy"};
    const int values[] = {stats.strength, stats.dexterity, stats.vitality, stats.energy};
    for (int index = 0; index < 4; ++index) {
        cell(names[index], 18, attributeY[index], 57, 19, 11, parchment);
        cell(std::to_string(values[index]), 75, attributeY[index], 38, 19, 12, gold, true);
        if (player.unspentAttributes > 0) {
            auto button = characterAddButton(index);
            const bool hovered = CheckCollisionPointRec(rv(mouse), button);
            const auto &texture = assets_.attributeButtons.frames[hovered ? 1 : 0].texture;
            DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)},
                           button, {0, 0}, 0, WHITE);
        }
    }

    const auto leftAction = characterActionStats(session_, view_.leftSkill);
    const auto rightAction = characterActionStats(session_, view_.rightSkill);
    paired("Damage", leftAction.damage, 83);
    paired("Attack Rating", leftAction.attackRating, 105);
    paired("Damage", rightAction.damage, 146);
    paired("Attack Rating", rightAction.attackRating, 168);
    paired("Defense", std::to_string(equipment.defense), 190);
    paired("Stamina", std::to_string(int(player.stamina)) + "/" + std::to_string(stats.maxStamina), 230);
    paired("Life", std::to_string(int(player.hp)) + "/" + std::to_string(stats.maxLife), 252);
    paired("Mana", std::to_string(int(player.mana)) + "/" + std::to_string(stats.maxMana), 295);
    paired("Fire Resist", std::to_string(stats.fireResist) + "%", 337);
    paired("Cold Resist", std::to_string(stats.coldResist) + "%", 358);
    paired("Lightning Resist", std::to_string(stats.lightningResist) + "%", 380);
    paired("Poison Resist", std::to_string(stats.poisonResist) + "%", 402);
    const auto &combat = stats.combat;
    cell("Block " + std::to_string(equipment.blockChance) + "%", 18, 317, 132, 12, 9, parchment);
    cell("Physical Resist " + std::to_string(std::clamp(combat.physicalResist, -100, 50)) +
             "%", 18, 330, 132, 12, 9, parchment);
    cell("Magic Resist " + std::to_string(std::clamp(combat.magicResist, -100, 75)) +
             "%", 18, 343, 132, 12, 9, parchment);
    cell("Damage -" + std::to_string(combat.flatPhysicalReduction) + " / " +
             std::to_string(combat.flatMagicReduction), 18, 356, 132, 12, 9, parchment);
    cell("Poison Length -" + std::to_string(std::clamp(combat.poisonLengthResist, 0, 100)) +
             "%", 18, 391, 132, 12, 9, parchment);
    cell("Fire Absorb " + std::to_string(std::clamp(combat.fireAbsorbPercent, 0, 40)) +
             "%", 18, 404, 132, 12, 9, parchment);
    if (player.unspentAttributes > 0) {
        const auto &texture = assets_.attributePoints.frames[0].texture;
        const auto box = characterArtRect(3, 365, float(texture.width), float(texture.height));
        DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)},
                       box, {0, 0}, 0, WHITE);
        cell("Stat Points", 8, 369, 72, 19, 10, parchment);
        cell(std::to_string(player.unspentAttributes), 80, 369, 38, 19, 11, gold, true);
    }
}
} // namespace d2x
