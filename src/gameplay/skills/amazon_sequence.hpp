#pragma once
#include "spear_spec.hpp"
#include <string_view>
namespace d2x {
// D2Common SequenceTbls: seq 1 (Jab) and seq 8 (Impale).
SpearSequence amazonWeaponSequence(int sequence, std::string_view weapon);
}
