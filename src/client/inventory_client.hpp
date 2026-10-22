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
    virtual bool selectStoragePage(unsigned) { return false; }
    virtual bool selectSharedStorage(bool) { return false; }
    virtual bool navigateStorage(StorageNavigation) { return false; }
    virtual bool selectStorageMaterials(bool) { return false; }
    virtual bool withdrawStorageMaterial(std::string_view, unsigned, bool) { return false; }
    virtual bool storeMaterialInCube(std::string_view, unsigned) { return false; }
    virtual bool upgradeStorageMaterial(std::string_view, unsigned) { return false; }
    virtual bool depositStorageMaterial(ItemHandle) { return false; }
    virtual bool depositAllStorageMaterials() { return false; }
    virtual bool markStorageIndex(unsigned) { return false; }
    virtual std::optional<Cell> beltSpace(std::string_view code) const = 0;
    virtual bool submitStaff(EntityId source,std::optional<ItemHandle> item) = 0;
};
} // namespace d2x
