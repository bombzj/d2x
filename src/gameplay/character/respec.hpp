#pragma once
#include "record.hpp"
namespace d2x {
// Refund only allocated base points; item/passive projections remain derived.
bool refundCharacterPoints(CharacterRecord &);
}
