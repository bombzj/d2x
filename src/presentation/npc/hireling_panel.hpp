#pragma once
#include "gameplay/items/equipment_rules.hpp"
#include <array>
#include <string>
#include <vector>
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
Rectangle hirelingPanelRect(float x, float y, float width, float height);
Rectangle hirelingCloseArt();
Rectangle hirelingCloseHit();
Rectangle hirelingListBounds();
Rectangle hirelingListContentBounds();
float hirelingListCancelBaseline();
int hirelingRowExtent(int lineCount);

struct HirelingListLayout {
    struct Row { int offer; std::vector<std::string> lines; };
    std::vector<int> lineCounts;
    std::vector<Row> rows;
    void clear(size_t expectedRows);
    void add(int offer, std::vector<std::string> lines);
    int lastPageFirst() const;
    int pageSize(int first) const;
    int hit(int first, Vec mouse) const;
};
} // namespace d2x
