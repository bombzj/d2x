#pragma once
#include "gameplay/items/equipment_rules.hpp"
#include <array>
#include "presentation/hud/classic_panel.hpp"

namespace d2x {
extern const std::array<EquipmentSlot, 7> hirelingEquipmentOrder;
// PetUI.cpp (1.13c RVA 5C7F0): x=15, bottom=60, stride=56.
Rectangle hirelingPortraitBounds(float offset = 0);
Rectangle hirelingLifeBounds(float offset = 0);
// The native mercenary handlers hit y=24..60; the life bar is read-only.
Rectangle hirelingPortraitHitBounds(float offset = 0);
inline constexpr float hirelingPortraitHoverSeconds = .7f;
Rectangle hirelingPanelBounds();
Rectangle hirelingArtRect(float x, float y, float width, float height);
Rectangle hirelingClose();
Rectangle hirelingListBounds();
Rectangle hirelingListRow(int row);
Rectangle hirelingListCancel();
Rectangle hirelingScrollBounds();
inline constexpr int hirelingVisibleRows = 8;
} // namespace d2x
