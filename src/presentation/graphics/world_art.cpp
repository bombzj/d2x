#include "world_art.hpp"
#include "resources/animation_cells.hpp"
#include <algorithm>
#include <rlgl.h>
namespace d2x {
namespace {
uint64_t frameEpoch = 1;
constexpr size_t atlasBudget = 128u << 20;
constexpr size_t decodeBudget = 32u << 20;
std::vector<WorldArt *> &worldCaches() {
    static std::vector<WorldArt *> caches;
    return caches;
}
} // namespace
WorldArt::WorldArt(Archives &archives) : archives_(archives) {
    worldCaches().push_back(this);
}
void beginGraphicsFrame() {
    ++frameEpoch;
    if (frameEpoch % 120 == 0)
        for (auto *cache : worldCaches())
            cache->retireColdPages();
}
void WorldArt::retireColdPages() {
    const auto retired = std::erase_if(pages_, [&](const Page &page) {
        if (frameEpoch - *page.stamp < 600)
            return false;
        UnloadTexture(page.texture);
        allocated_ -= page.bytes;
        ++evictions_;
        return true;
    });
    if (retired)
        std::erase_if(components_, [](const auto &entry) { return entry.second.stamp.expired(); });
}
bool AtlasCell::touch() const {
    if (!page.id)
        return true;
    if (const auto current = stamp.lock()) {
        *current = frameEpoch;
        return true;
    }
    return false;
}
bool Sprite::touch() const {
    if (!cell.touch() || !colorCell.touch() || !shadowCell.touch())
        return false;
    for (const auto &layer : layers)
        if (!layer.cell.touch())
            return false;
    return true;
}
bool GpuAnimation::intersects(Vec p, Rectangle viewport) const {
    return !bounds || (p.x + bounds->x < viewport.x + viewport.width &&
                       p.y + bounds->y < viewport.y + viewport.height &&
                       p.x + bounds->x + bounds->width > viewport.x &&
                       p.y + bounds->y + bounds->height > viewport.y);
}
const Sprite *GpuAnimation::frame(int direction, int index) const {
    if (directions <= 0 || count <= 0 || direction < 0 || index < 0)
        return nullptr;
    const size_t offset = size_t(direction % directions) * count + index % count;
    if (source)
        return source->resolve(offset);
    if (!sequence.empty()) {
        const auto &sample = sequence.at(offset);
        return sample.source->resolve(sample.offset);
    }
    return offset < frames.size() ? &frames[offset] : nullptr;
}
void GpuAnimation::appendSample(const GpuAnimation &from, int facing, int index) {
    const size_t offset = size_t(facing) * from.count + index;
    if (from.bounds) {
        if (!bounds)
            bounds = from.bounds;
        else {
            const float x = std::min(bounds->x, from.bounds->x), y = std::min(bounds->y, from.bounds->y);
            const float right = std::max(bounds->x + bounds->width, from.bounds->x + from.bounds->width);
            const float bottom = std::max(bounds->y + bounds->height, from.bounds->y + from.bounds->height);
            bounds = Rectangle{x, y, right - x, bottom - y};
        }
    }
    if (!from.sequence.empty())
        sequence.push_back(from.sequence.at(offset));
    else if (from.source)
        sequence.push_back({from.source, offset});
    else
        frames.push_back(from.frames.at(offset));
}
WorldArt::~WorldArt() {
    std::erase(worldCaches(), this);
    for (auto &page : pages_)
        UnloadTexture(page.texture);
}
WorldArt::AssetPtr WorldArt::asset(const std::string &path) {
    const auto name = normalize(path);
    if (auto found = assets_.find(name); found != assets_.end())
        return found->second;
    auto bytes = archives_.read(name, false);
    if (bytes.empty())
        return {};
    const bool dc6 = name.ends_with(".dc6");
    const auto headers = dc6 ? describeDc6(bytes) : describeDcc(bytes);
    Asset info{name, headers.directions, headers.framesPerDirection, dc6, ++nextId_};
    const auto extents = [&](bool component) {
        int left = 0, top = 0, right = 0, bottom = 0;
        for (const auto &f : headers.frames) {
            const int y = component && dc6 ? f.y - f.height : f.y;
            left = std::min(left, f.x);
            top = std::min(top, y);
            right = std::max(right, f.x + f.width);
            bottom = std::max(bottom, y + f.height);
        }
        return Rectangle{float(left), float(top), float(right - left), float(bottom - top)};
    };
    info.bounds = extents(false);
    info.compositeBounds = extents(true);
    auto value = std::make_shared<const Asset>(std::move(info));
    assets_.emplace(name, value);
    return value;
}
std::shared_ptr<const IndexedFrame> WorldArt::pixels(const Asset &asset, int direction, int frame) {
    const int cell = direction * asset.count + frame;
    const Key key{asset.id, size_t(asset.dc6 ? cell : direction)};
    auto found = decodedLookup_.find(key);
    if (found == decodedLookup_.end()) {
        const auto bytes = archives_.read(asset.path);
        auto group = std::make_shared<Animation>(asset.dc6 ? decodeDc6Cell(bytes, cell)
                                                           : decodeDccDirection(bytes, direction));
        size_t size = group->frames.capacity() * sizeof(IndexedFrame);
        for (const auto &f : group->frames)
            size += f.pixels.capacity();
        while (!decoded_.empty() && decodedBytes_ + size > decodeBudget) {
            const auto &last = decoded_.back();
            decodedBytes_ -= last.bytes;
            decodedLookup_.erase(last.key);
            decoded_.pop_back();
        }
        decodedBytes_ += size;
        decoded_.push_front({key, std::move(group), size});
        found = decodedLookup_.emplace(key, decoded_.begin()).first;
    } else
        decoded_.splice(decoded_.begin(), decoded_, found->second);
    const auto &group = found->second->animation;
    return {group, &group->frames.at(asset.dc6 ? 0 : frame)};
}
GpuAnimation WorldArt::animation(int directions, int count, std::function<Sprite(size_t)> load) {
    GpuAnimation result;
    result.directions = directions;
    result.count = count;
    result.source = std::make_shared<GpuAnimation::Source>(
        GpuAnimation::Source{[this, id = ++nextId_, load = std::move(load)](size_t offset) {
            return resolve({id, offset}, load);
        }});
    return result;
}
const Sprite *WorldArt::resolve(Key key, const std::function<Sprite(size_t)> &load) {
    auto found = frameLookup_.find(key);
    if (found != frameLookup_.end()) {
        auto entry = found->second;
        if (!entry->sprite.touch())
            entry->sprite = load(key.second);
        entry->used = frameEpoch;
        frames_.splice(frames_.begin(), frames_, entry);
    } else {
        // References in the current draw list remain valid until its batch has finished.
        while (frames_.size() >= 4096 && frames_.back().used != frameEpoch) {
            frameLookup_.erase(frames_.back().key);
            frames_.pop_back();
        }
        auto sprite = load(key.second);
        frames_.push_front({key, std::move(sprite), frameEpoch});
        frameLookup_.emplace(key, frames_.begin());
    }
    return &frames_.front().sprite;
}
AtlasCell WorldArt::upload(const IndexedFrame &f, int format, const void *pixels, Use use) {
    const int w = f.width, h = f.height;
    if (!w || !h)
        return {};
    Page *slot = nullptr;
    for (auto &page : pages_) {
        if (page.use != use || page.texture.format != format || w > page.texture.width)
            continue;
        const bool next = page.x + w > page.texture.width;
        if (page.y + (next ? page.row : 0) + h > page.texture.height)
            continue;
        if (next) {
            page.x = 0;
            page.y += page.row;
            page.row = 0;
        }
        slot = &page;
        break;
    }
    if (!slot) {
        const int width = std::max(1024, w), height = std::max(1024, h);
        const size_t bytes = GetPixelDataSize(width, height, format);
        while (allocated_ + bytes > atlasBudget && !pages_.empty()) {
            const auto oldest =
                std::min_element(pages_.begin(), pages_.end(),
                                 [](const Page &a, const Page &b) { return *a.stamp < *b.stamp; });
            if (*oldest->stamp == frameEpoch)
                break;
            UnloadTexture(oldest->texture);
            allocated_ -= oldest->bytes;
            pages_.erase(oldest);
            ++evictions_;
            std::erase_if(components_, [](const auto &entry) { return entry.second.stamp.expired(); });
        }
        Texture2D texture{rlLoadTexture(nullptr, width, height, format, 1), width, height, 1, format};
        if (!texture.id)
            throw std::runtime_error("World art atlas allocation failed");
        SetTextureFilter(texture, TEXTURE_FILTER_POINT);
        pages_.push_back({texture, use, std::make_shared<uint64_t>(frameEpoch), 0, 0, 0, bytes});
        slot = &pages_.back();
        allocated_ += bytes;
        peak_ = std::max(peak_, allocated_);
    }
    Rectangle rect{float(slot->x), float(slot->y), float(w), float(h)};
    UpdateTextureRec(slot->texture, rect, pixels);
    *slot->stamp = frameEpoch;
    slot->x += w;
    slot->row = std::max(slot->row, h);
    ++uploads_;
    uploadBytes_ += GetPixelDataSize(w, h, format);
    return {slot->texture, rect, slot->stamp};
}
Sprite WorldArt::component(const Asset &asset, int direction, int frame, const IndexedFrame &f, Use use) {
    const auto key = std::tuple{asset.id, direction, frame};
    const auto it = components_.find(key);
    if (it == components_.end() || !it->second.touch()) {
        auto cell = upload(f, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, f.pixels.data(), use);
        components_.insert_or_assign(key, std::move(cell));
    }
    Sprite sprite;
    sprite.cell = components_.at(key);
    sprite.indexedTexture = sprite.cell.page;
    sprite.texture = sprite.cell.page;
    sprite.texture.width = f.width;
    sprite.texture.height = f.height;
    sprite.x = f.x;
    sprite.y = f.y;
    if (f.cellOffsetY)
        sprite.selectionFrameBounds =
            Rectangle{float(f.x), float(*f.cellOffsetY - f.height), float(f.width), float(f.height)};
    return sprite;
}
GraphicsDiagnostics WorldArt::usage() const {
    GraphicsDiagnostics d;
    for (const auto &page : pages_)
        d.atlasBytesByUse[size_t(page.use)] += page.bytes;
    d.atlasBytes = allocated_;
    d.atlasPages = pages_.size();
    d.atlasPeakBytes = peak_;
    d.atlasEvictions = evictions_;
    d.atlasUploads = uploads_;
    d.atlasUploadBytes = uploadBytes_;
    d.directionBytes = decodedBytes_;
    d.frameRecords = frames_.size();
    return d;
}
} // namespace d2x
