#pragma once
#include "contracts/inventory.hpp"

namespace d2x {
class IInventoryClient {
  public:
    virtual ~IInventoryClient() = default;
    // Borrow is valid until the next read or adapter destruction; consumers copy on revision changes.
    virtual const InventoryView &read() const = 0;
    virtual InventoryError preview(const InventoryIntent &intent) const = 0;
    virtual void submit(InventoryIntent intent) = 0;
    virtual std::optional<Cell> beltSpace(std::string_view code) const = 0;
};
} // namespace d2x
