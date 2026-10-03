#pragma once
#include "gameplay/quest/id.hpp"
#include "hud_layout.hpp"
#include "presentation/inventory/inventory_panel.hpp"
#include "presentation/graphics/primitives.hpp"
#include <array>

namespace d2x {
inline constexpr std::array questDisplayOrder = {
    ActOneQuest::DenOfEvil, ActOneQuest::SistersBurialGrounds,
    ActOneQuest::SearchForCain, ActOneQuest::ForgottenTower,
    ActOneQuest::ToolsOfTheTrade, ActOneQuest::SistersToTheSlaughter};
inline constexpr std::array actTwoQuestOrder = {
    QuestId::RadamentsLair, QuestId::HoradricStaff, QuestId::TaintedSun,
    QuestId::ArcaneSanctuary, QuestId::Summoner, QuestId::SevenTombs};
inline QuestId displayedQuest(int act, int index) {
    return (act == 1 ? actTwoQuestOrder : questDisplayOrder).at(size_t(index));
}
inline Rectangle questBounds() { return classicPanelBounds(false, 64); }
inline Rectangle questArtRect(float x, float y, float width, float height) {
    const auto panel = questBounds();
    return {panel.x + x * inventoryScale, panel.y + y * inventoryScale,
            width * inventoryScale, height * inventoryScale};
}
inline Rectangle questIconBounds(int index) {
    return questArtRect(20.f + float(index % 3) * 100.f,
                        28.f + float(index / 3) * 95.f, 80, 95);
}
inline Rectangle questTabBounds(int index) { return questArtRect(6.f + index * 61.f, 2, 61, 31); }
inline Rectangle questCloseBounds() { return questArtRect(278, 391, 32, 32); }
inline Rectangle questReplayBounds() { return questArtRect(228, 393, 32, 32); }
inline Rectangle questNoticeBounds() {
    const float size = 32 * classicPanelScale;
    return {32 * classicPanelScale, hudGlobe(false).y - size, size, size};
}
} // namespace d2x
