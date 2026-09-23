#include "graphics.hpp"
#include <algorithm>
#include <iostream>
namespace d2x {
namespace {
Color color(Pixel p) {
    return {p.r, p.g, p.b, p.a};
}
} // namespace
Graphics::Graphics(Archives &a, const std::string &palettePath)
    : palette(decodePalette(a.read(palettePath))), archives(a) {}
Graphics::~Graphics() {
    for (auto t : textures)
        UnloadTexture(t);
}
Sprite Graphics::upload(const IndexedFrame &f) {
    if (f.width <= 0 || f.height <= 0)
        return {};
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&](uint64_t value) { hash = (hash ^ value) * 1099511628211ull; };
    mix(f.width);
    mix(f.height);
    mix(uint32_t(f.x));
    mix(uint32_t(f.y));
    for (auto pixel : f.pixels)
        mix(pixel);
    if (auto found = textureCache.find(hash); found != textureCache.end())
        return found->second;
    std::vector<Color> pixels(f.pixels.size());
    for (size_t i = 0; i < pixels.size(); i++)
        pixels[i] = color(palette[f.pixels[i]]);
    Image img{pixels.data(), f.width, f.height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    auto t = LoadTextureFromImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_POINT);
    textures.push_back(t);
    Sprite result{t, f.x, f.y};
    textureCache.emplace(hash, result);
    return result;
}
const Animation *Graphics::animation(const std::string &path) {
    auto name = normalize(path);
    auto it = decoded.find(name);
    if (it != decoded.end())
        return &it->second;
    auto b = archives.read(name, false);
    if (b.empty())
        return nullptr;
    try {
        auto a = name.ends_with(".dc6") ? decodeDc6(b) : decodeDcc(b);
        return &decoded.emplace(name, std::move(a)).first->second;
    } catch (const std::exception &e) {
        std::cerr << "Animation " << name << ": " << e.what() << '\n';
        return nullptr;
    }
}
GpuAnimation Graphics::single(const std::string &path) {
    GpuAnimation gpu;
    auto a = animation(path);
    if (!a)
        return gpu;
    gpu.directions = a->directions;
    gpu.count = a->framesPerDirection;
    for (auto &f : a->frames)
        gpu.frames.push_back(upload(f));
    return gpu;
}
GpuAnimation Graphics::composite(const std::string &type, const std::string &token, const std::string &mode,
                                 const std::string &weapon, const std::array<const char *, 16> *equipment) {
    auto base = "data/global/" + type + "/" + token + "/";
    auto bytes = archives.read(base + "cof/" + token + mode + weapon + ".cof", false);
    if (bytes.empty())
        return {};
    auto cof = decodeCof(bytes);
    std::map<int, const Animation *> parts;
    int omitted = 0;
    static const std::string codes[] = {"hd", "tr", "lg", "ra", "la", "rh", "lh", "sh",
                                        "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8"};
    for (int i = 0; i < cof.layers; i++) {
        int c = cof.components[i];
        if (c < 0 || c >= 16)
            throw std::runtime_error("Invalid COF component");
        if (equipment && std::string_view((*equipment)[c]) == "nil") {
            ++omitted;
            continue;
        }
        std::string gear = c == 5 ? "ssd" : c == 7 ? (token == "ba" ? "kit" : "buc") : "lit";
        if (token == "zm" && c == 10)
            gear = "bld";
        if (equipment && (*equipment)[c][0])
            gear = (*equipment)[c];
        auto path = base + codes[c] + "/" + token + codes[c] + gear + mode + cof.weapons[i] + ".dcc";
        auto part = animation(path);
        if (!part && c == 7 && type != "chars")
            part =
                animation(base + codes[c] + "/" + token + codes[c] + "buc" + mode + cof.weapons[i] + ".dcc");
        if (!part && c == 5 && type != "chars")
            part =
                animation(base + codes[c] + "/" + token + codes[c] + "axe" + mode + cof.weapons[i] + ".dcc");
        if (part)
            parts[c] = part;
        else
            std::cerr << "Missing component " << path << '\n';
    }
    if (parts.empty())
        return {};
    GpuAnimation gpu;
    gpu.directions = cof.directions;
    gpu.count = cof.frames;
    gpu.completeComposite = parts.size() + omitted == size_t(cof.layers);
    for (int d = 0; d < cof.directions; d++)
        for (int f = 0; f < cof.frames; f++) {
            static constexpr int order8[] = {4, 0, 5, 1, 6, 2, 7, 3};
            static constexpr int order16[] = {4, 8, 0, 9, 5, 10, 1, 11, 6, 12, 2, 13, 7, 14, 3, 15};
            int cofDirection = d;
            if (cof.directions == 8)
                cofDirection = int(std::find(std::begin(order8), std::end(order8), d) - std::begin(order8));
            else if (cof.directions == 16)
                cofDirection = int(std::find(std::begin(order16), std::end(order16), d) - std::begin(order16));
            IndexedFrame merged;
            int left = 0, top = 0, right = 0, bottom = 0;
            auto get = [&](int c) -> const IndexedFrame * {
                auto it = parts.find(c);
                if (it == parts.end())
                    return nullptr;
                auto a = it->second;
                int dir = d * a->directions / cof.directions;
                if (cof.directions == 16 && a->directions == 8)
                    dir = order8[(cofDirection + 1) / 2 % 8];
                else if (cof.directions == 8 && a->directions == 16)
                    dir = d;
                return &a->frames[dir * a->framesPerDirection + (f % a->framesPerDirection)];
            };
            for (auto [c, a] : parts) {
                auto p = get(c);
                left = std::min(left, p->x);
                top = std::min(top, p->y);
                right = std::max(right, p->x + p->width);
                bottom = std::max(bottom, p->y + p->height);
            }
            merged.width = right - left;
            merged.height = bottom - top;
            merged.x = left;
            merged.y = top;
            merged.pixels.resize(size_t(merged.width) * merged.height);
            for (int l = 0; l < cof.layers; l++) {
                auto p = get(cof.componentAt(cofDirection, f, l));
                if (!p)
                    continue;
                for (int y = 0; y < p->height; y++)
                    for (int x = 0; x < p->width; x++) {
                        auto index = p->pixels[y * p->width + x];
                        if (index)
                            merged.pixels[(y + p->y - top) * merged.width + x + p->x - left] = index;
                    }
            }
            gpu.frames.push_back(upload(merged));
        }
    std::cout << "Composite " << token << mode << weapon << ": " << parts.size() << '/' << cof.layers
              << " parts, " << gpu.frames.size() << " frames\n";
    return gpu;
}
} // namespace d2x
