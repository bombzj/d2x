#include "d2s_items.hpp"
#include "content/items/native_save_data.hpp"

namespace d2x {
D2sItemRead readD2sItem(std::span<const uint8_t> bytes,const ClassicData &content) {
    return content.nativeItemSave?readNativeSavedItem(bytes,*content.nativeItemSave):readNativeSavedItem(bytes,prepareNativeItemSaveRules(content));
}
Bytes writeD2sItem(const D2sItem &item,const ClassicData &content) {
    return content.nativeItemSave?writeNativeSavedItem(item,*content.nativeItemSave):writeNativeSavedItem(item,prepareNativeItemSaveRules(content));
}
}