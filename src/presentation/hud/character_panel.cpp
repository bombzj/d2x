#include "presentation/scene_view.hpp"
#include "character_panel.hpp"
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

    const auto &player = characterView_;
    const auto &stats = characterView_;
    const auto &equipment = characterView_;
    cell(player.name, 10, 9, 173, 20, 14, gold, true);
    cell(player.className, 191, 9, 120, 20, 14, gold, true);
    cell("Level", 11, 35, 45, 13, 10, parchment, true);
    cell(player.number("level", player.level), 11, 48, 45, 20, 13, gold, true);
    cell("Experience", 61, 35, 121, 13, 10, parchment, true);
    cell(player.number("experience", player.experience), 61, 48, 121, 20, 12, gold, true);
    auto next = player.nextLevelExperience ? std::to_string(*player.nextLevelExperience) : "MAX";
    cell("Next Level", 191, 35, 120, 13, 10, parchment, true);
    cell(next, 191, 48, 120, 20, 12, gold, true);

    constexpr float attributeY[] = {83, 146, 230, 295};
    const char *names[] = {"Strength", "Dexterity", "Vitality", "Energy"};
    constexpr const char *attributeStats[]{"strength","dexterity","vitality","energy"};
    const int values[] = {stats.attributes[0], stats.attributes[1], stats.attributes[2], stats.attributes[3]};
    for (int index = 0; index < 4; ++index) {
        cell(names[index], 18, attributeY[index], 57, 19, 11, parchment);
        cell(player.number(attributeStats[index], values[index]), 75, attributeY[index], 38, 19, 12, gold, true);
        if (player.unspentAttributes > 0) {
            auto button = characterAddButton(index);
            const bool hovered = CheckCollisionPointRec(rv(mouse), button);
            const auto &texture = assets_.attributeButtons.frames[hovered ? 1 : 0].texture;
            DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)},
                           button, {0, 0}, 0, WHITE);
        }
    }

    const auto leftAction = characterView_.actionDisplay(view_.leftSkill);
    const auto rightAction = characterView_.actionDisplay(view_.rightSkill);
    paired("Damage", leftAction.damage, 83);
    paired("Attack Rating", leftAction.attackRating, 105);
    paired("Damage", rightAction.damage, 146);
    paired("Attack Rating", rightAction.attackRating, 168);
    paired("Defense", player.number("armorclass", equipment.defense), 190);
    paired("Stamina", player.number("stamina", int(player.stamina)) + "/" + player.number("maxstamina", stats.maxStamina), 230);
    paired("Life", player.number("hitpoints", int(player.hp)) + "/" + player.number("maxhp", stats.maxLife), 252);
    paired("Mana", player.number("mana", int(player.mana)) + "/" + player.number("maxmana", stats.maxMana), 295);
    paired("Fire Resist", player.number("fireresist", stats.resistances[0]) + "%", 337);
    paired("Cold Resist", player.number("coldresist", stats.resistances[1]) + "%", 358);
    paired("Lightning Resist", player.number("lightresist", stats.resistances[2]) + "%", 380);
    paired("Poison Resist", player.number("poisonresist", stats.resistances[3]) + "%", 402);
    const auto &combat = stats;
    cell("Block " + player.number("toblock", equipment.blockChance) + "%", 18, 317, 132, 12, 9, parchment);
    cell("Physical Resist " + player.number("damageresist", std::clamp(combat.physicalResist, -100, 50)) +
             "%", 18, 330, 132, 12, 9, parchment);
    cell("Magic Resist " + player.number("magicresist", std::clamp(combat.magicResist, -100, 75)) +
             "%", 18, 343, 132, 12, 9, parchment);
    cell("Damage -" + player.number("normal_damage_reduction", combat.flatPhysicalReduction) + " / " +
             player.number("magic_damage_reduction", combat.flatMagicReduction), 18, 356, 132, 12, 9, parchment);
    cell("Poison Length -" + player.number("poisonlengthresist", std::clamp(combat.poisonLengthResist, 0, 100)) +
             "%", 18, 391, 132, 12, 9, parchment);
    cell("Fire Absorb " + player.number("fireabsorb", std::clamp(combat.fireAbsorbPercent, 0, 40)) +
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
