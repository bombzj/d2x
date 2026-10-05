#pragma once
#include "character_frontend.hpp"
namespace d2x {
std::optional<CharacterChoice> chooseFrontend(Archives &, RenderTexture2D,
                                              const std::filesystem::path &onlineConfig,
                                              const std::string &debugPipe,
                                              bool returnToLocalCharacters = false);
}
