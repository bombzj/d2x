#pragma once
#include "d2s_items.hpp"
#include "gameplay/session/session_snapshot.hpp"

namespace d2x {
void initializeD2sInventory(SessionSnapshot &snapshot, const ClassicData &content);
void importD2sItem(SessionSnapshot &snapshot, const D2sItem &item, const ClassicData &content, bool hireling);
D2sItem exportD2sItem(const SessionSnapshot &snapshot, const ItemInstance &item, const ClassicData &content);
} // namespace d2x