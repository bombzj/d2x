#pragma once
#include "d2s_items.hpp"
#include "gameplay/session/character_save.hpp"

namespace d2x {
void initializeD2sInventory(CharacterSaveData &snapshot, const ClassicData &content);
void importD2sItem(CharacterSaveData &snapshot, const D2sItem &item, const ClassicData &content,
                   bool hireling, EntityId corpse = {}, EntityId socketHost = {}, unsigned socketIndex = 0);
D2sItem exportD2sItem(const CharacterSaveData &snapshot, const ItemInstance &item, const ClassicData &content);
} // namespace d2x
