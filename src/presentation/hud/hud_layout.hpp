#pragma once
#include "presentation/graphics/primitives.hpp"
#include <array>

namespace d2x {
// One coordinate system for drawing and hit testing the original 800-pixel panel.
// Native DC6 panel frames are bottom-aligned; the globes rise above the 55px strip.
float hudScale();
// UI glyphs keep their MPQ pixel size. The outer viewport already scales the
// whole canvas; widening the HUD must not enlarge text a second time.
float hudTextScale();
Rectangle hudRect(float x, float fromBottom, float width, float height);
Rectangle hudSkillSlot(bool right);
Rectangle hudGlobe(bool mana);
Rectangle hudRunButton();
Rectangle hudStamina();
Rectangle hudExperience();
Rectangle hudBeltSlot(int column, int row = 0);
Rectangle hudCharacterButton();
Rectangle hudSkillTreeButton();
Rectangle hudMenuButton();
Rectangle hudMenuButtonArt(const Sprite &image);
Rectangle hudMiniPanel(const Sprite &image, int offset = 0);
Rectangle hudMiniButton(const Sprite &image, int index, int offset = 0);
Rectangle hudPickerSlot(bool right, int column, int row);
bool hudSurface(Vec mouse);
} // namespace d2x
