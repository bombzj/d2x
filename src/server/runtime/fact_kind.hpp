#pragma once
#include <cstddef>
#include <cstdint>

namespace d2x::server {
#define D2X_FACT(Type) struct Type;
#include "fact_catalog.inc"
#undef D2X_FACT

enum class FactKind : uint8_t {
#define D2X_FACT(Type) Type,
#include "fact_catalog.inc"
#undef D2X_FACT
    Count
};
inline constexpr size_t factKindCount = size_t(FactKind::Count);
template<class T> struct FactTraits;
#define D2X_FACT(Type) template<> struct FactTraits<Type> {static constexpr FactKind kind = FactKind::Type;};
#include "fact_catalog.inc"
#undef D2X_FACT
template<class T> concept RegisteredFact = requires { FactTraits<T>::kind; };
template<RegisteredFact T> inline constexpr FactKind factKind = FactTraits<T>::kind;
}
