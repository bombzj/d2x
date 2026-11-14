#include "presentation/scene_view.hpp"
#include "presentation/graphics/text_list.hpp"
#include <iterator>

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
    if (value.revision != hirelingListView_.revision || value.npc != hirelingListView_.npc) {
        if (value.offers != hirelingListView_.offers) {
            hirelingListLayout_.clear(value.offers.size());
            const auto &labels = assets_.hirelingMenuText;
            const auto &font = assets_.font;
            const int width = textListWidth(hirelingListContentBounds());
            const auto stat = [&](const std::string &label, int number) {
                return labels.space + labels.space + label + labels.colon + labels.space + std::to_string(number);
            };
            for (int index = 0; index < int(value.offers.size()); ++index) {
                const auto &offer = value.offers[size_t(index)];
                const auto &name = offer.slot == 0x421 && !labels.jarulf.empty() ? labels.jarulf : offer.name;
                const auto title = name + labels.space + labels.dash + stat(labels.level, offer.level) +
                    stat(labels.life, offer.life) + stat(labels.defense, offer.defense) + stat(labels.cost, offer.price);
                auto lines = wrapClassicText(title, font, width);
                // The native second segment starts with two TBL spaces even
                // when there is no description to append.
                auto description = wrapClassicText(labels.space + labels.space + offer.description, font, width);
                lines.insert(lines.end(), std::make_move_iterator(description.begin()), std::make_move_iterator(description.end()));
                hirelingListLayout_.add(index, std::move(lines));
            }
        }
        hirelingListView_ = value;
    }
    return hirelingListView_;
}
} // namespace d2x
