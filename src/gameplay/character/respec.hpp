#pragma once
namespace d2x {
struct CharacterRecord;
enum class PointRefund { Attributes, Skills, All };
// Refund only allocated base points; item/passive projections remain derived.
bool refundCharacterPoints(CharacterRecord &,PointRefund = PointRefund::All);
}
