#include "gameplay_dispatch.hpp"
#include <optional>

namespace d2x::hosting::handlers {
RequestResult ItemToCube(GameplayContext &, net::protocol::Reader &in) {
    in.u32(); in.u32(); in.finish();
    return {RequestStatus::NotImplemented, CommandStatus::NotImplemented, "ItemToCube"};
}
RequestResult MercenaryItem(GameplayContext &context, net::protocol::Reader &in) {
    const auto body=in.u16(); in.finish();
    return submitGameplay(context,server::companions::Request{server::companions::Action::Equipment,{},body});
}
RequestResult InventoryToBelt(GameplayContext &, net::protocol::Reader &in) {
    in.u32(); in.finish();
    return {RequestStatus::NotImplemented, CommandStatus::NotImplemented, "InventoryToBelt"};
}
namespace {
using namespace server::inventory;
using net::protocol::Reader;
RequestResult reject() { return {RequestStatus::Rejected, CommandStatus::InvalidRequest}; }
const InputItem *item(const InputState &state, uint32_t id) {
    const auto found = state.items.find(EntityId{id});
    return found == state.items.end() ? nullptr : &found->second;
}
std::optional<EquipmentSlot> slot(uint32_t body, unsigned set) {
    if (body == 4 || body == 5) return weaponHandSlot(body == 5, set);
    return body >= 1 && body <= 10 ? std::optional<EquipmentSlot>{EquipmentSlot(body - 1)} : std::nullopt;
}
const InputItem *equipped(const InputState &state, EquipmentSlot at) {
    const ContainerLocation location = at == EquipmentSlot::Belt ? ContainerLocation{state.containers.beltEquipment, {}}
        : ContainerLocation{state.containers.equipment, {int(at), 0}};
    for (const auto &[id, value] : state.items) { (void)id; if (value.location == location) return &value; }
    return nullptr;
}
RequestResult take(GameplayContext &context, Reader &in, Source source) {
    const auto id = in.u32(); in.finish();
    const auto state = context.host.inventoryInput(context.player);
    const auto *value = state ? item(*state, id) : nullptr;
    if (!value) return reject();
    return submitGameplay(context, Request{MoveItem{value->handle, ContainerLocation{state->containers.cursor, {}}}, source});
}
RequestResult equip(GameplayContext &context, Reader &in, EquipmentMode mode) {
    const auto id = in.u32(), body = in.u32(); in.finish();
    const auto state = context.host.inventoryInput(context.player);
    const auto *value = state ? item(*state, id) : nullptr;
    const auto at = state ? slot(body, state->weaponSet) : std::nullopt;
    if (!value || !at) return reject();
    Request request{d2x::EquipItem{value->handle, at}, Source::Cursor, mode, state->weaponSet};
    if (const auto *previous = equipped(*state, *at)) request.equipmentGuards.push_back(previous->handle);
    if (body == 4 || body == 5)
        if (const auto *opposite = equipped(*state, weaponHandSlot(body == 4, state->weaponSet)))
            request.equipmentGuards.push_back(opposite->handle);
    return submitGameplay(context, std::move(request));
}
}
RequestResult TakeItem(GameplayContext &context, Reader &in) { return take(context, in, Source::Stored); }
RequestResult TakeBeltItem(GameplayContext &context, Reader &in) { return take(context, in, Source::Belt); }
RequestResult PlaceItem(GameplayContext &context, Reader &in) {
    const auto id = in.u32(), x = in.u32(), y = in.u32(), page = in.u32(); in.finish();
    // Authorization is revalidated in the inventory domain.
    if (page != 0 && page != 2 && page != 3 && page != 4) return {RequestStatus::Rejected};
    if (x > 15 || y > 15) return reject();
    const auto state = context.host.inventoryInput(context.player);
    const auto *value = state ? item(*state, id) : nullptr;
    if (!value || (page==2 && !state->trade)) return reject();
    return submitGameplay(context, Request{MoveItem{value->handle, ContainerLocation{page == 4 ? state->containers.stash : page == 3 ? state->containers.cube : page == 2 ? state->trade : state->containers.backpack, {int(x), int(y)}}}});
}
RequestResult EquipItem(GameplayContext &context, Reader &in) { return equip(context, in, EquipmentMode::Insert); }
RequestResult EquipItemIndirect(GameplayContext &context, Reader &in) { return equip(context, in, EquipmentMode::Indirect); }
RequestResult SwapEquipment(GameplayContext &context, Reader &in) { return equip(context, in, EquipmentMode::Swap); }
RequestResult EquipTwoHanded(GameplayContext &context, Reader &in) { return equip(context, in, EquipmentMode::TwoHanded); }
RequestResult UnequipItem(GameplayContext &context, Reader &in) {
    const auto body = in.u16(); in.finish();
    const auto state = context.host.inventoryInput(context.player);
    const auto at = state ? slot(body, state->weaponSet) : std::nullopt;
    const auto *value = at ? equipped(*state, *at) : nullptr;
    if (!value) return reject();
    return submitGameplay(context, Request{d2x::EquipItem{value->handle, {}, ContainerLocation{state->containers.cursor, {}}},
        Source::Cursor, EquipmentMode::Insert, state->weaponSet});
}
RequestResult SwapItem(GameplayContext &context, Reader &in) {
    const auto first = in.u32(), second = in.u32(), x = in.u32(), y = in.u32(); in.finish();
    if (x > 15 || y > 15) return reject();
    const auto state = context.host.inventoryInput(context.player);
    const auto *a = state ? item(*state, first) : nullptr, *b = state ? item(*state, second) : nullptr;
    if (!a || !b) return reject();
    if (b->location.container != state->containers.backpack && b->location.container != state->containers.stash && b->location.container != state->containers.cube && (!state->trade || b->location.container!=state->trade)) return {RequestStatus::Rejected};
    return submitGameplay(context, Request{SwapItems{a->handle, b->handle,
        ContainerLocation{b->location.container, {int(x), int(y)}}}});
}
RequestResult PlaceBeltItem(GameplayContext &context, Reader &in) {
    const auto id = in.u32(), cell = in.u32(); in.finish();
    if (cell >= 16) return reject();
    const auto state = context.host.inventoryInput(context.player);
    const auto *value = state ? item(*state, id) : nullptr;
    if (!value) return reject();
    return submitGameplay(context, Request{MoveItem{value->handle, ContainerLocation{state->containers.belt, {int(cell % 4), int(cell / 4)}}}});
}
RequestResult SwapBeltItem(GameplayContext &context, Reader &in) {
    const auto first = in.u32(), second = in.u32(); in.finish();
    const auto state = context.host.inventoryInput(context.player);
    const auto *a = state ? item(*state, first) : nullptr, *b = state ? item(*state, second) : nullptr;
    if (!a || !b) return reject();
    return submitGameplay(context, Request{SwapItems{a->handle, b->handle}, Source::Belt});
}
RequestResult SwitchWeapons(GameplayContext &context, Reader &in) {
    in.finish();
    const auto state = context.host.inventoryInput(context.player);
    if (!state) return reject();
    return submitGameplay(context, Request{SwitchWeaponSet{}, Source::Cursor, EquipmentMode::Insert, state->weaponSet});
}
RequestResult StackItem(GameplayContext &context, Reader &in) {
    const auto source = in.u32(), target = in.u32(); in.finish();
    const auto state = context.host.inventoryInput(context.player);
    const auto *a = state ? item(*state, source) : nullptr, *b = state ? item(*state, target) : nullptr;
    if (!a || !b) return reject();
    return submitGameplay(context, Request{MergeStacks{a->handle, b->handle}});
}
RequestResult LoadBook(GameplayContext &context, Reader &in) {
    const auto source = in.u32(), target = in.u32(); in.finish();
    const auto state = context.host.inventoryInput(context.player);
    const auto *a = state ? item(*state, source) : nullptr, *b = state ? item(*state, target) : nullptr;
    if (!a || !b) return reject();
    if (a->location.container!=state->containers.cursor ||
        (b->location.container!=state->containers.backpack && b->location.container!=state->containers.stash && b->location.container!=state->containers.cube))
        return reject();
    return submitGameplay(context, Request{d2x::LoadBook{a->handle, b->handle}});
}

}
