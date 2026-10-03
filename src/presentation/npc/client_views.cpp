#include "presentation/scene_view.hpp"

namespace d2x {
const ShopView &SceneView::shopView() const {
    const auto &value = npcClient_.shop(view_.dialogueObject, view_.shopGamble);
    if (value.revision != shopView_.revision || value.npc != shopView_.npc || value.gamble != shopView_.gamble)
        shopView_ = value;
    return shopView_;
}
const HirelingView &SceneView::hirelingView() const {
    const auto &value = npcClient_.hireling();
    if (value.revision != hirelingView_.revision) hirelingView_ = value;
    return hirelingView_;
}
const HirelingListView &SceneView::hirelingListView() const {
    const auto &value = npcClient_.hirelings(view_.dialogueObject);
    if (value.revision != hirelingListView_.revision || value.npc != hirelingListView_.npc)
        hirelingListView_ = value;
    return hirelingListView_;
}
} // namespace d2x
