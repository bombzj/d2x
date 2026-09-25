#pragma once
#include "resources/archive.hpp"
#include <filesystem>
#include <optional>
#include <raylib.h>

namespace d2x {
struct CharacterChoice {
    std::filesystem::path path;
    std::string characterClass, name;
    bool created = false;
};
std::optional<CharacterChoice> chooseCharacter(Archives &archives, RenderTexture2D target);
} // namespace d2x