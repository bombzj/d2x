#pragma once
#include "fact_kind.hpp"
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <variant>

namespace d2x::server {
// Owning value with a payload-independent layout. Small, frequently published
// facts stay inline; large snapshots own their storage and copy independently.
// Only a producer instantiates storage operations for its concrete payload.
class DomainFact {
  public:
    static constexpr size_t inlineCapacity = 128;
    template<class T> static constexpr bool storesInline = sizeof(T) <= inlineCapacity &&
        alignof(T) <= alignof(std::max_align_t) && std::is_nothrow_move_constructible_v<T>;
  private:
    struct Operations {
        FactKind kind;
        void (*destroy)(DomainFact &) noexcept;
        void (*copy)(DomainFact &, const DomainFact &);
        void (*move)(DomainFact &, DomainFact &) noexcept;
    };
    alignas(std::max_align_t) std::byte local_[inlineCapacity];
    void *heap_{};
    const Operations *operations_{};
    template<class T> T *local() noexcept { return std::launder(reinterpret_cast<T *>(local_)); }
    template<class T> const T *local() const noexcept { return std::launder(reinterpret_cast<const T *>(local_)); }
    template<class T, class U> void construct(U &&value) {
        if constexpr (storesInline<T>) std::construct_at(reinterpret_cast<T *>(local_), std::forward<U>(value));
        else heap_ = new T(std::forward<U>(value));
        operations_ = &operations<T>();
    }
    template<class T> static const Operations &operations() {
        static constexpr Operations value{
            factKind<T>,
            [](DomainFact &fact) noexcept {
                if constexpr (storesInline<T>) std::destroy_at(fact.local<T>());
                else delete static_cast<T *>(fact.heap_);
            },
            [](DomainFact &to, const DomainFact &from) {
                if constexpr (storesInline<T>) to.construct<T>(*from.local<T>());
                else to.construct<T>(*static_cast<const T *>(from.heap_));
            },
            [](DomainFact &to, DomainFact &from) noexcept {
                if constexpr (storesInline<T>) {
                    to.construct<T>(std::move(*from.local<T>()));
                    std::destroy_at(from.local<T>());
                } else {
                    to.heap_ = std::exchange(from.heap_, nullptr);
                    to.operations_ = from.operations_;
                }
                from.operations_ = nullptr;
            }
        };
        return value;
    }
    void reset() noexcept;
    void *data() noexcept { return heap_ ? heap_ : static_cast<void *>(local_); }
    const void *data() const noexcept { return heap_ ? heap_ : static_cast<const void *>(local_); }
    template<RegisteredFact T> friend T *factIf(DomainFact *) noexcept;
    template<RegisteredFact T> friend const T *factIf(const DomainFact *) noexcept;
  public:
    DomainFact(); // Preserve the former default alternative: ManaFact{}.
    template<class T> requires RegisteredFact<std::remove_cvref_t<T>>
    DomainFact(T &&value) { construct<std::remove_cvref_t<T>>(std::forward<T>(value)); }
    DomainFact(const DomainFact &);
    DomainFact(DomainFact &&) noexcept;
    DomainFact &operator=(const DomainFact &);
    DomainFact &operator=(DomainFact &&) noexcept;
    ~DomainFact();
    // Moved-from values remain destructible/assignable, but cannot be published.
    bool hasValue() const noexcept { return operations_ != nullptr; }
    FactKind kind() const noexcept { return operations_ ? operations_->kind : FactKind::Count; }
    size_t index() const noexcept { return size_t(kind()); }
};

template<RegisteredFact T> T *factIf(DomainFact *fact) noexcept {
    return fact && fact->kind() == factKind<T> ? std::launder(static_cast<T *>(fact->data())) : nullptr;
}
template<RegisteredFact T> const T *factIf(const DomainFact *fact) noexcept {
    return fact && fact->kind() == factKind<T> ? std::launder(static_cast<const T *>(fact->data())) : nullptr;
}
template<RegisteredFact T> bool factHolds(const DomainFact &fact) noexcept { return fact.kind() == factKind<T>; }
template<RegisteredFact T> T &factGet(DomainFact &fact) {
    if (auto *value = factIf<T>(&fact)) return *value;
    throw std::bad_variant_access{};
}
template<RegisteredFact T> const T &factGet(const DomainFact &fact) {
    if (const auto *value = factIf<T>(&fact)) return *value;
    throw std::bad_variant_access{};
}
template<RegisteredFact T> T &&factGet(DomainFact &&fact) { return std::move(factGet<T>(fact)); }
}
