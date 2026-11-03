#pragma once
#include <cstdint>
#include <string>

namespace d2x::server {
struct PlayerMessageFact { uint8_t type{}; std::string name; std::string other{}; uint32_t value{}; uint8_t parameter{}; std::string characterClass{}; };
}
