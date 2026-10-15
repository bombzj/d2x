#pragma once
#include <bit>
#include <cmath>
#include <compare>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace d2x {
// Native life, mana, stamina and damage: signed 32-bit storage, eight fraction
// bits. The float conversion is a presentation boundary, never the stored value.
class Fixed8 {
    int32_t value_{};
    struct Raw {};
    constexpr Fixed8(int32_t value, Raw) noexcept : value_(value) {}
  public:
    constexpr Fixed8() noexcept = default;
    template<class Number> requires std::is_arithmetic_v<Number>
    Fixed8(Number value) {
        const auto scaled = static_cast<long double>(value) * 256;
        if (!std::isfinite(scaled) || scaled < std::numeric_limits<int32_t>::min() ||
            scaled > std::numeric_limits<int32_t>::max())
            throw std::overflow_error("Value exceeds native eight-bit fixed-point range");
        value_ = int32_t(scaled);
    }
    static constexpr Fixed8 fromRaw(int32_t value) noexcept { return Fixed8(value, Raw{}); }
    static Fixed8 fromRaw(int64_t value) {
        if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max())
            throw std::overflow_error("Value exceeds native eight-bit fixed-point range");
        return fromRaw(int32_t(value));
    }
    constexpr int32_t raw() const noexcept { return value_; }
    constexpr operator float() const noexcept { return float(value_) / 256.f; }
    constexpr auto operator<=>(const Fixed8 &) const noexcept = default;
    constexpr bool operator==(const Fixed8 &) const noexcept = default;
    template<class Number> requires std::is_arithmetic_v<Number>
    auto operator<=>(Number other) const noexcept {
        return static_cast<long double>(value_) <=> static_cast<long double>(other) * 256;
    }
    template<class Number> requires std::is_arithmetic_v<Number>
    bool operator==(Number other) const noexcept {
        return static_cast<long double>(value_) == static_cast<long double>(other) * 256;
    }
    Fixed8 &operator+=(Fixed8 other) noexcept {
        value_ = std::bit_cast<int32_t>(uint32_t(value_) + uint32_t(other.value_));
        return *this;
    }
    Fixed8 &operator-=(Fixed8 other) noexcept {
        value_ = std::bit_cast<int32_t>(uint32_t(value_) - uint32_t(other.value_));
        return *this;
    }
    friend Fixed8 operator+(Fixed8 first, Fixed8 second) noexcept { return first += second; }
    friend Fixed8 operator-(Fixed8 first, Fixed8 second) noexcept { return first -= second; }
    template<class Number> requires std::is_arithmetic_v<Number>
    friend Fixed8 operator+(Fixed8 first, Number second) { return first += Fixed8(second); }
    template<class Number> requires std::is_arithmetic_v<Number>
    friend Fixed8 operator+(Number first, Fixed8 second) { return Fixed8(first) += second; }
    template<class Number> requires std::is_arithmetic_v<Number>
    friend Fixed8 operator-(Fixed8 first, Number second) { return first -= Fixed8(second); }
    template<class Number> requires std::is_arithmetic_v<Number>
    friend Fixed8 operator-(Number first, Fixed8 second) { return Fixed8(first) -= second; }
};
}
