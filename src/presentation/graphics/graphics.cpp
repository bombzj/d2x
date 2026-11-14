#include "graphics.hpp"
#include "world_art.hpp"
#include "graphics_diagnostics.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
namespace d2x {
namespace {
auto &graphicsReaders() {
    static std::map<const Graphics *,std::function<GraphicsDiagnostics()>> readers;
    return readers;
}
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
    : palette(decodePalette(a.read(palettePath))), archives(a) {
    graphicsReaders().emplace(this,[this] {
        GraphicsDiagnostics result=world_?world_->usage():GraphicsDiagnostics{};result.textureCounts=textureCounts;result.textureBytes=textureBytes;result.owners=1;result.textures=textures.size();
        for(const auto &texture:textures) result.logicalRgbaBytes+=uint64_t(std::max(0,texture.width))*std::max(0,texture.height)*4;
        std::erase_if(decodedSources,[](const auto &source) {return source.expired();});
        for(const auto &source:decodedSources) if(const auto animation=source.lock()) {
            ++result.decodedAnimations;result.decodedFrames+=animation->frames.size();
            for(const auto &frame:animation->frames) result.decodedPixelBytes+=frame.pixels.capacity();
        }
        return result;
    });
}
Graphics::~Graphics() {
    graphicsReaders().erase(this);
    for (auto t : textures)
        UnloadTexture(t);
}
GraphicsDiagnostics graphicsDiagnostics() {
    GraphicsDiagnostics result;
    for(const auto &[owner,read]:graphicsReaders()) {
        (void)owner;const auto sample=read();
        for(size_t index=0;index<result.textureCounts.size();++index) {result.textureCounts[index]+=sample.textureCounts[index];result.textureBytes[index]+=sample.textureBytes[index];}
        for (size_t i=0;i<result.atlasBytesByUse.size();++i) result.atlasBytesByUse[i]+=sample.atlasBytesByUse[i];
        result.atlasBytes+=sample.atlasBytes;result.atlasPages+=sample.atlasPages;result.atlasPeakBytes+=sample.atlasPeakBytes;
        result.atlasUploads+=sample.atlasUploads;result.atlasUploadBytes+=sample.atlasUploadBytes;
        result.atlasEvictions+=sample.atlasEvictions;result.directionBytes+=sample.directionBytes;result.frameRecords+=sample.frameRecords;
        result.owners+=sample.owners;result.textures+=sample.textures;
        result.logicalRgbaBytes+=sample.logicalRgbaBytes;result.decodedAnimations+=sample.decodedAnimations;
        result.decodedFrames+=sample.decodedFrames;result.decodedPixelBytes+=sample.decodedPixelBytes;
    }
    return result;
}
Sprite Graphics::upload(const IndexedFrame &f, bool translucent) {
    return uploadAsset(f, translucent, AssetKind::Direct);
}
Sprite Graphics::uploadAsset(const IndexedFrame &f, bool translucent, AssetKind kind) {
    archives.pulseLoading();
    if (f.width <= 0 || f.height <= 0)
        return {};
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&](uint64_t value) { hash = (hash ^ value) * 1099511628211ull; };
    mix(f.width);
    mix(f.height);
    mix(uint32_t(f.x));
    mix(uint32_t(f.y));
    mix(f.cellOffsetY.has_value());
    if (f.cellOffsetY) mix(uint32_t(*f.cellOffsetY));
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
    if (f.cellOffsetY)
        result.selectionFrameBounds = Rectangle{float(f.x), float(*f.cellOffsetY - f.height),
            float(f.width), float(f.height)};
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
    textureCounts[size_t(kind)] += translucent ? 2 : 1;
    textureBytes[size_t(kind)] += uint64_t(f.width) * f.height * 4 * (translucent ? 2 : 1);
    textureCache.emplace(hash, result);
    return result;
}
const Animation *Graphics::animation(const std::string &path) {
    return animationSource(path).get();
}
std::shared_ptr<const Animation> Graphics::animationSource(const std::string &path) {
    auto name = normalize(path);
    auto it = decoded.find(name);
    if (it != decoded.end())
        return it->second;
    auto b = archives.read(name, false);
    if (b.empty())
        return nullptr;
    try {
        auto a = name.ends_with(".dc6") ? decodeDc6(b) : decodeDcc(b);
        auto source=std::make_shared<const Animation>(std::move(a));
        decodedSources.emplace_back(source);
        return decoded.emplace(name, std::move(source)).first->second;
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
        gpu.frames.push_back(uploadAsset(f, translucent, AssetKind::Single));
    return gpu;
}
WorldArt &Graphics::worldArt() {
    if (!world_)
        world_ = std::make_unique<WorldArt>(archives);
    return *world_;
}
Sprite Graphics::terrain(const IndexedFrame &f) {
    Sprite result;
    result.cell =
        worldArt().upload(f, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, f.pixels.data(), WorldArt::Use::Terrain);
    result.indexedTexture = result.cell.page;
    result.texture = result.cell.page;
    result.texture.width = f.width;
    result.texture.height = f.height;
    result.x = f.x;
    result.y = f.y;
    return result;
}
GpuAnimation Graphics::worldSingle(const std::string &path, WorldSingleArt kind, const uint8_t *itemShift) {
    const bool keepColor = kind != WorldSingleArt::Object;
    const bool ground = kind == WorldSingleArt::GroundItem;
    const bool item = ground || kind == WorldSingleArt::ItemIcon;
    const bool paired = item || kind == WorldSingleArt::BlendedEffect || kind == WorldSingleArt::BlendedOverlay;
    auto &art = worldArt();
    auto asset = art.asset(path);
    if (!asset)
        return {};
    // Native overlay cells share the unit baseline. DC6 stores that baseline;
    // DCC already has a decoded top edge. Other standalone/UI art keeps its API.
    const bool baseline = ground || (asset->dc6 &&
        (kind==WorldSingleArt::Overlay || kind==WorldSingleArt::BlendedOverlay));
    std::optional<std::array<uint8_t, 256>> shift;
    if (itemShift)
        std::copy_n(itemShift, 256, shift.emplace().begin());
    const auto use = item ? WorldArt::Use::Item : keepColor ? WorldArt::Use::Effect : WorldArt::Use::Actor;
    auto animation = art.animation(
        asset->directions, asset->count, [this, asset, keepColor, paired, shift, baseline, use](size_t offset) {
            auto &cache = worldArt();
            const int direction = int(offset / asset->count), frame = int(offset % asset->count);
            const auto pixels = cache.pixels(*asset, direction, frame);
            Sprite result;
            const auto *indices = &pixels->pixels;
            Bytes shifted;
            if (shift) {
                shifted = pixels->pixels;
                for (auto &index : shifted)
                    if (index)
                        index = (*shift)[index];
                indices = &shifted;
            }
            if (paired && shift) {
                result.cell = cache.upload(*pixels, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, indices->data(), use);
                result.indexedTexture = result.cell.page;
            } else if (!keepColor || paired)
                result = cache.component(*asset, direction, frame, *pixels, use);
            if (keepColor) {
                std::vector<Color> rgba;
                rgba.reserve(pixels->pixels.size());
                for (uint8_t index : *indices)
                    rgba.push_back(color(palette[index]));
                result.colorCell = cache.upload(*pixels, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, rgba.data(), use);
                result.texture = result.colorCell.page;
                result.texture.width = pixels->width;
                result.texture.height = pixels->height;
                result.x = pixels->x;
                result.y = pixels->y - (baseline ? pixels->height : 0);
                if (pixels->cellOffsetY)
                    result.selectionFrameBounds =
                        Rectangle{float(pixels->x), float(*pixels->cellOffsetY - pixels->height),
                                  float(pixels->width), float(pixels->height)};
            }
            return result;
        });
    animation.bounds = baseline ? asset->compositeBounds : asset->bounds;
    return animation;
}
GpuAnimation Graphics::composite(const std::string &type, const std::string &token, const std::string &mode,
                                 const std::string &weapon, const std::array<const char *, 16> *equipment,
                                 const std::array<uint8_t, 256> *colorMap, bool menuPortrait,
                                 const std::array<const uint8_t *, 16> &componentColors) {
    auto base = "data/global/" + type + "/" + token + "/";
    const auto cofPath=base + "cof/" + token + mode + weapon + ".cof";
    auto &art=worldArt();
    std::string recipe = cofPath;
    recipe.push_back(menuPortrait ? 'P' : 'W');
    recipe.push_back(equipment ? '1' : '0');
    if (equipment) for (auto part : *equipment) { recipe.push_back('\0'); recipe.append(part); }
    recipe.push_back('\0');
    recipe.push_back(colorMap ? '1' : '0');
    if (colorMap) recipe.append(reinterpret_cast<const char *>(colorMap->data()),colorMap->size());
    for (const auto *color : componentColors) {
        recipe.push_back(color ? '1' : '0');
        if (color) recipe.append(reinterpret_cast<const char *>(color), 256);
    }
    if (auto found=art.recipes.find(recipe); found!=art.recipes.end()) return found->second;
    auto bytes = archives.read(cofPath, false);
    if (bytes.empty())
        return {};
    Cof cof;
    try {cof=decodeCof(bytes);}
    catch(const std::exception &error) {throw std::runtime_error(cofPath+": "+error.what());}
    if(!cof.layers) return {};
    std::map<int, WorldArt::AssetPtr> parts;
    std::array<bool, 16> castsShadow{};
    std::array<bool, 16> selectable{};
    std::array<bool, 16> softAdditive{};
    std::array<uint8_t,16> drawModes{};
    drawModes.fill(5);
    std::array<bool, 16> dc6Component{};
    int omitted = 0;
    const bool monsterDeath = type == "monsters" && (mode == "dt" || mode == "dd");
    static const std::string codes[] = {"hd", "tr", "lg", "ra", "la", "rh", "lh", "sh",
                                        "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8"};
    for (int i = 0; i < cof.layers; i++) {
        int c = cof.components[i];
        if (c < 0 || c >= 16)
            throw std::runtime_error("Invalid COF component");
        // Client 6BE75 -> 6D740 reads only the COF shadow byte. A
        // component's color draw mode does not suppress its shadow mask.
        castsShadow[size_t(c)] = cof.shadows[size_t(i)];
        selectable[size_t(c)] = cof.selectable[size_t(i)];
        softAdditive[size_t(c)] = cof.transparent[size_t(i)] && cof.drawEffects[size_t(i)] == 3;
        if(cof.transparent[size_t(i)]) drawModes[size_t(c)]=cof.drawEffects[size_t(i)];
        if (equipment && std::string_view((*equipment)[c]) == "nil") {
            ++omitted;
            continue;
        }
        if (type == "chars" && equipment && !(*equipment)[c][0]) return {};
        std::string gear = c == 5 ? "ssd" : c == 7 ? (token == "ba" ? "kit" : "buc") : "lit";
        if (token == "zm" && c == 10)
            gear = "bld";
        if (equipment && (*equipment)[c][0])
            gear = (*equipment)[c];
        auto path = base + codes[c] + "/" + token + codes[c] + gear + mode + cof.weapons[i] + ".dcc";
        auto part = art.asset(path);
        if (!part && !archives.contains(path)) {
            part = art.asset(path.substr(0, path.size() - 4) + ".dc6");
            dc6Component[size_t(c)]=part!=nullptr;
        }
        // Retail D2CMP 1.13c E1D0 returns no cell for an absent component;
        // D2Gdi 65EC/65F3 skips its draw and D2Client 6CACA continues the COF.
        // Keep the missing identity in diagnostics; undecodable files still fail.
        if (!part && (type == "chars" || monsterDeath) &&
            !archives.contains(path) && !archives.contains(path.substr(0, path.size() - 4) + ".dc6")) {
            std::cerr << "Missing COF component (omitted) " << path << '\n';
            ++omitted;
            continue;
        }
        const bool explicitComponent = equipment && (*equipment)[c][0];
        if (!part && c == 7 && type != "chars" && !explicitComponent)
            part =
                art.asset(base + codes[c] + "/" + token + codes[c] + "buc" + mode + cof.weapons[i] + ".dcc");
        if (!part && c == 5 && type != "chars" && !explicitComponent)
            part =
                art.asset(base + codes[c] + "/" + token + codes[c] + "axe" + mode + cof.weapons[i] + ".dcc");
        if (part)
            parts[c] = part;
        else
            std::cerr << "Missing component " << path << '\n';
    }
    if (parts.empty())
        return {};
    // DC6 stores a cell baseline, while DCC frames already carry a top edge.
    // World components share the COF origin. Using the raw DC6 offset here
    // puts Tyrael's 90px body below his wings, outside the COF bounds.
    const auto componentY = [dc6Component](int component, const IndexedFrame &frame) {
        return dc6Component[size_t(component)] && frame.cellOffsetY ? *frame.cellOffsetY - frame.height
                                                                    : frame.y;
    };
    const bool complete = parts.size() + omitted == size_t(cof.layers);
    const int directions = cof.directions, frames = cof.frames;
    std::array<std::shared_ptr<const std::array<uint8_t, 256>>, 16> componentTranslations{};
    for (size_t component = 0; component < componentColors.size(); ++component) {
        if (const auto *color = componentColors[component]) {
            auto translation = std::make_shared<std::array<uint8_t, 256>>();
            std::copy_n(color, translation->size(), translation->begin());
            (*translation)[0] = 0;
            componentTranslations[component] = std::move(translation);
        }
    }
    auto gpu = art.animation(
        directions, frames,
        [this, cof, parts, componentY, castsShadow, selectable, softAdditive, drawModes, menuPortrait, monsterDeath, componentTranslations,
         colors = colorMap ? std::make_shared<const std::array<uint8_t, 256>>(*colorMap)
                           : nullptr](size_t offset) {
            const int d = int(offset / cof.frames), f = int(offset % cof.frames);
            static constexpr int order8[] = {4, 0, 5, 1, 6, 2, 7, 3};
            static constexpr int order16[] = {4, 8, 0, 9, 5, 10, 1, 11, 6, 12, 2, 13, 7, 14, 3, 15};
            int cofDirection = d;
            if (cof.directions == 8)
                cofDirection = int(std::find(std::begin(order8), std::end(order8), d) - std::begin(order8));
            else if (cof.directions == 16)
                cofDirection =
                    int(std::find(std::begin(order16), std::end(order16), d) - std::begin(order16));
            auto &cache = worldArt();
            struct Part {
                std::shared_ptr<const IndexedFrame> pixels;
                int direction, frame;
            };
            std::array<Part, 16> selected{};
            int left = 0, top = 0, right = 0, bottom = 0;
            Sprite result;
            result.translation = colors;
            for (const auto &[c, asset] : parts) {
                // D2CMP D140 returns no cell beyond the component's frames.
                if (monsterDeath && f >= asset->count) continue;
                int dir = d * asset->directions / cof.directions;
                if (cof.directions == 16 && asset->directions == 8)
                    dir = order8[(cofDirection + 1) / 2 % 8];
                else if (cof.directions == 8 && asset->directions == 16)
                    dir = d;
                const int frame = f % asset->count;
                auto pixels = cache.pixels(*asset, dir, frame);
                const auto &p = *pixels;
                const int y = componentY(c, p);
                left = std::min(left, p.x);
                right = std::max(right, p.x + p.width);
                top = std::min(top, y);
                bottom = std::max(bottom, y + p.height);
                if (c == 14)
                    result.riderAnchor = Vec{float(p.x), float(p.y)};
                result.componentBounds.push_back(
                    {{float(p.x), float(p.cellOffsetY ? *p.cellOffsetY - p.height : p.y), float(p.width),
                      float(p.height)},
                     selectable[c],
                     c});
                selected[c] = {std::move(pixels), dir, frame};
            }
            IndexedFrame mask;
            mask.width = right - left;
            mask.height = bottom - top;
            mask.x = left;
            mask.y = top;
            mask.pixels.resize(size_t(mask.width) * mask.height);
            // Menu portraits use the ordinary color renderer. Merge opaque runs so the
            // dead-character tint is applied once, including where components overlap.
            bool segmentUsed=false;
            auto flushPortrait = [&](bool additive) {
                if (!segmentUsed) return;
                const auto image=uploadAsset(mask,false,AssetKind::Composite);
                result.layers.push_back({image.texture,left,top,additive,{},{}});
                std::fill(mask.pixels.begin(),mask.pixels.end(),0);
                segmentUsed=false;
            };
            for (int layer = 0; layer < cof.layers; ++layer) {
                const int c = cof.componentAt(cofDirection, f, layer);
                if (c < 0 || c >= 16 || !selected[c].pixels)
                    continue;
                const auto &part = selected[c];
                const auto &p = *part.pixels;
                const int y = componentY(c, p);
                const auto &translation = componentTranslations[c] ? componentTranslations[c] : colors;
                if (menuPortrait) {
                    if (softAdditive[c]) flushPortrait(false);
                    for (int row=0;row<p.height;++row)
                        for (int x=0;x<p.width;++x)
                            if (auto index=p.pixels[size_t(row)*p.width+x]) {
                                mask.pixels[size_t(row+y-top)*mask.width+p.x-left+x]=translation?(*translation)[index]:index;
                                segmentUsed=true;
                            }
                    if (softAdditive[c]) flushPortrait(true);
                    continue;
                }
                const auto cell = cache.component(*parts.at(c), part.direction, part.frame, p);
                result.layers.push_back(
                    {cell.texture, p.x, y, softAdditive[c], cell.indexedTexture, cell.cell, drawModes[c], componentTranslations[c]});
                if (castsShadow[c]) {
                    for (int row = 0; row < p.height; ++row)
                        for (int x = 0; x < p.width; ++x)
                            if (p.pixels[size_t(row) * p.width + x])
                                mask.pixels[size_t(row + y - top) * mask.width + p.x - left + x] = 1;
                }
            }
            if (menuPortrait) flushPortrait(false);
            if (!result.layers.empty())
                result.texture = result.layers.front().texture;
            result.x = left;
            result.y = top;
            result.texture.width = mask.width;
            result.texture.height = mask.height;
            result.selectionCoarseBounds =
                Rectangle{float(cof.minX) - 16, float(cof.minY) - 16, float(cof.maxX - cof.minX) + 32,
                          float(cof.maxY - cof.minY) + 32};
            if (menuPortrait) return result;
            const auto shadow = projectShadow(mask);
            if (shadow.width) {
                Bytes alpha(shadow.pixels.size() * 2);
                for (size_t i = 0; i < shadow.pixels.size(); ++i)
                    alpha[2 * i + 1] = shadow.pixels[i] ? 255 : 0;
                result.shadowCell = cache.upload(shadow, PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA, alpha.data(),
                                                 WorldArt::Use::Actor);
                result.shadowTexture = result.shadowCell.page;
                result.shadowX = shadow.x;
                result.shadowY = shadow.y;
            }
            return result;
        });
    float left = 0, top = 0, right = 0, bottom = 0;
    for (const auto &[component, asset] : parts) {
        (void)component;
        const auto &box = asset->compositeBounds;
        left = std::min(left, box.x);
        top = std::min(top, box.y);
        right = std::max(right, box.x + box.width);
        bottom = std::max(bottom, box.y + box.height);
    }
    gpu.bounds = Rectangle{left, top, right - left, bottom - top};
    gpu.completeComposite = complete;
    art.recipes.emplace(std::move(recipe), gpu);
    return gpu;
}
} // namespace d2x
