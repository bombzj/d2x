#include "graphics.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
namespace d2x {
namespace {
Color color(Pixel p) {
    return {p.r, p.g, p.b, p.a};
}
IndexedFrame projectShadow(const IndexedFrame &mask) {
    IndexedFrame result;
    if (mask.width <= 0 || mask.height <= 0 ||
        std::none_of(mask.pixels.begin(), mask.pixels.end(), [](auto p) { return p != 0; }))
        return result;
    // OpenDiablo2 Animation.renderShadow: flatten vertically and shear left
    // from the unit's feet. Only COF layers marked as casting shadows enter it.
    const int top = int(std::floor(mask.y * .5f));
    const int bottom = int(std::floor((mask.y + mask.height - 1) * .5f));
    result.x = mask.x + top;
    result.y = top;
    result.width = mask.width + bottom - top;
    result.height = bottom - top + 1;
    result.pixels.resize(size_t(result.width) * result.height);
    for (int y = 0; y < mask.height; ++y) {
        const int row = int(std::floor((mask.y + y) * .5f)) - top;
        for (int x = 0; x < mask.width; ++x)
            if (mask.pixels[size_t(y) * mask.width + x])
                result.pixels[size_t(row) * result.width + x + row] = 1;
    }
    return result;
}
} // namespace
Graphics::Graphics(Archives &a, const std::string &palettePath)
    : palette(decodePalette(a.read(palettePath))), archives(a) {}
Graphics::~Graphics() {
    for (auto t : textures)
        UnloadTexture(t);
}
Sprite Graphics::upload(const IndexedFrame &f, bool translucent) {
    archives.pulseLoading();
    if (f.width <= 0 || f.height <= 0)
        return {};
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&](uint64_t value) { hash = (hash ^ value) * 1099511628211ull; };
    mix(f.width);
    mix(f.height);
    mix(uint32_t(f.x));
    mix(uint32_t(f.y));
    mix(translucent);
    for (auto pixel : f.pixels)
        mix(pixel);
    if (auto found = textureCache.find(hash); found != textureCache.end())
        return found->second;
    std::vector<Color> pixels(f.pixels.size());
    int left = f.width, top = f.height, right = -1, bottom = -1;
    for (size_t i = 0; i < pixels.size(); i++) {
        pixels[i] = color(palette[f.pixels[i]]);
        if (translucent && !pixels[i].a)
            pixels[i] = {0, 0, 0, 0};
        if (pixels[i].a) {
            int x = int(i % size_t(f.width)), y = int(i / size_t(f.width));
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    }
    Image img{pixels.data(), f.width, f.height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    auto t = LoadTextureFromImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_POINT);
    textures.push_back(t);
    Sprite result{};
    result.texture = t;
    result.x = f.x;
    result.y = f.y;
    if (translucent) {
        for (size_t i = 0; i < pixels.size(); ++i)
            pixels[i] = {f.pixels[i], 0, 0, uint8_t(f.pixels[i] ? 255 : 0)};
        Image indices{pixels.data(), f.width, f.height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        result.indexedTexture = LoadTextureFromImage(indices);
        SetTextureFilter(result.indexedTexture, TEXTURE_FILTER_POINT);
        textures.push_back(result.indexedTexture);
    }
    if (right >= left) {
        result.hitX = f.x + left;
        result.hitY = f.y + top;
        result.hitWidth = right - left + 1;
        result.hitHeight = bottom - top + 1;
    }
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
GpuAnimation Graphics::single(const std::string &path, bool translucent) {
    GpuAnimation gpu;
    auto a = animation(path);
    if (!a)
        return gpu;
    gpu.directions = a->directions;
    gpu.count = a->framesPerDirection;
    for (auto &f : a->frames)
        gpu.frames.push_back(upload(f, translucent));
    return gpu;
}
GpuAnimation Graphics::composite(const std::string &type, const std::string &token, const std::string &mode,
                                 const std::string &weapon, const std::array<const char *, 16> *equipment,
                                 const std::array<uint8_t, 256> *colorMap) {
    auto base = "data/global/" + type + "/" + token + "/";
    auto bytes = archives.read(base + "cof/" + token + mode + weapon + ".cof", false);
    if (bytes.empty())
        return {};
    auto cof = decodeCof(bytes);
    std::map<int, const Animation *> parts;
    std::array<bool, 16> castsShadow{};
    std::array<bool, 16> softAdditive{};
    int omitted = 0;
    static const std::string codes[] = {"hd", "tr", "lg", "ra", "la", "rh", "lh", "sh",
                                        "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8"};
    for (int i = 0; i < cof.layers; i++) {
        int c = cof.components[i];
        if (c < 0 || c >= 16)
            throw std::runtime_error("Invalid COF component");
        castsShadow[size_t(c)] = cof.shadows[size_t(i)] && !cof.transparent[size_t(i)];
        softAdditive[size_t(c)] = cof.transparent[size_t(i)] && cof.drawEffects[size_t(i)] == 3;
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
    const bool splitLayers = std::any_of(parts.begin(), parts.end(), [&](const auto &part) {
        return softAdditive[size_t(part.first)];
    });
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
            IndexedFrame shadowMask = merged;
            IndexedFrame segment = merged;
            std::vector<SpriteLayer> layers;
            bool segmentHasPixels = false;
            auto recolor = [&](IndexedFrame &image) {
                if (colorMap)
                    for (auto &index : image.pixels)
                        if (index) index = (*colorMap)[index];
            };
            auto flushSegment = [&] {
                if (!segmentHasPixels) return;
                recolor(segment);
                const auto uploaded = upload(segment);
                layers.push_back({uploaded.texture, uploaded.x, uploaded.y, false});
                std::fill(segment.pixels.begin(), segment.pixels.end(), 0);
                segmentHasPixels = false;
            };
            for (int l = 0; l < cof.layers; l++) {
                const int component = cof.componentAt(cofDirection, f, l);
                auto p = get(component);
                if (!p)
                    continue;
                const bool additive = splitLayers && softAdditive[size_t(component)];
                if (additive) flushSegment();
                IndexedFrame additiveFrame;
                if (additive) {
                    additiveFrame = merged;
                    std::fill(additiveFrame.pixels.begin(), additiveFrame.pixels.end(), 0);
                }
                for (int y = 0; y < p->height; y++)
                    for (int x = 0; x < p->width; x++) {
                        auto index = p->pixels[y * p->width + x];
                        if (index) {
                            const auto pixel = size_t(y + p->y - top) * merged.width + x + p->x - left;
                            merged.pixels[pixel] = index;
                            if (splitLayers) {
                                (additive ? additiveFrame : segment).pixels[pixel] = index;
                                if (!additive) segmentHasPixels = true;
                            }
                            if (castsShadow[size_t(component)]) shadowMask.pixels[pixel] = 1;
                        }
                    }
                if (additive) {
                    recolor(additiveFrame);
                    const auto uploaded = upload(additiveFrame, true);
                    if (uploaded.texture.id)
                        layers.push_back({uploaded.texture, uploaded.x, uploaded.y, true});
                }
            }
            if (splitLayers) flushSegment();
            recolor(merged);
            auto frame = upload(merged);
            frame.layers = std::move(layers);
            const auto shadow = upload(projectShadow(shadowMask));
            frame.shadowTexture = shadow.texture;
            frame.shadowX = shadow.x;
            frame.shadowY = shadow.y;
            gpu.frames.push_back(frame);
        }
    std::cout << "Composite " << token << mode << weapon << ": " << parts.size() << '/' << cof.layers
              << " parts, " << gpu.frames.size() << " frames\n";
    return gpu;
}
} // namespace d2x
