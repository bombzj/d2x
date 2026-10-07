#pragma once
#include "presentation/graphics/original_menu.hpp"
#include <span>
#include <optional>
#include <string>
#include <string_view>

namespace d2x {
Rectangle npcMenuBounds(const OriginalMenu &, Vec point, Rectangle viewport, std::string_view speaker, std::span<const std::string> options);
std::optional<size_t> npcMenuHit(const OriginalMenu &, Vec point, Rectangle viewport, std::string_view speaker,
    std::span<const std::string> options, Vec mouse);
void drawNpcMenu(const OriginalMenu &, Vec point, Rectangle viewport, std::string_view speaker,
    std::span<const std::string> options, Vec mouse, std::string_view status = {});
} // namespace d2x
