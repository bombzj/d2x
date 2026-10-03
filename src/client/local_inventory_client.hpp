#pragma once
#include "client/inventory_client.hpp"

namespace d2x {
class GameSession;
class LocalInventoryClient final : public IInventoryClient {
    GameSession &session_;
    mutable InventoryView cached_;
  public:
    explicit LocalInventoryClient(GameSession &session) : session_(session) {}
    const InventoryView &read() const override;
    InventoryError preview(const InventoryIntent &intent) const override;
    void submit(InventoryIntent intent) override;
    std::optional<Cell> beltSpace(std::string_view code) const override;
};
} // namespace d2x
