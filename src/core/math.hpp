#pragma once
#include <cmath>
namespace d2x {
struct Vec {
    float x = 0, y = 0;
    Vec operator+(Vec b) const { return {x + b.x, y + b.y}; }
    Vec operator-(Vec b) const { return {x - b.x, y - b.y}; }
    Vec operator*(float s) const { return {x * s, y * s}; }
    float length() const { return std::sqrt(x * x + y * y); }
    Vec unit() const {
        auto l = length();
        return l > 0.0001f ? *this * (1 / l) : Vec{};
    }
};
inline Vec project(Vec p) {
    return {(p.x - p.y) * 16, (p.x + p.y) * 8};
}
inline Vec unproject(Vec p) {
    return {p.x / 32 + p.y / 16, p.y / 16 - p.x / 32};
}
} // namespace d2x
