#include "outdoor.hpp"
#include "outdoor_shrines.hpp"
#include "resources/formats.hpp"
#include <algorithm>
#include <numeric>

namespace d2x {
namespace {
class Desert {
    Archives &archives_;
    const WorldCatalog &catalog_;
    const OutdoorPosition &position_;
    Seed random_;
    MapRecipe recipe_;
    int width_, height_;
    std::vector<int> occupied_;
    bool place(int preset, int column, int row, int variant = -1) {
        const auto &record = catalog_.presets().at(preset);
        int columns = record.width / 8, rows = record.height / 8;
        if (column < 0 || row < 0 || column + columns > width_ || row + rows > height_)
            return false;
        for (int vertical = row; vertical < row + rows; ++vertical)
            for (int horizontal = column; horizontal < column + columns; ++horizontal)
                if (occupied_[vertical * width_ + horizontal])
                    return false;
        if (variant < 0)
            variant = random_.below(record.files);
        auto source = catalog_.preset(preset, 16, variant);
        recipe_.pieces.push_back({column * 8, row * 8, record.width, record.height, preset, variant,
                                 source.ds1, source.tileLibraries, source.fillBlanks, record.populate});
        for (int vertical = row; vertical < row + rows; ++vertical)
            for (int horizontal = column; horizontal < column + columns; ++horizontal)
                occupied_[vertical * width_ + horizontal] = preset;
        return true;
    }
    void randomPreset(int preset, bool required = false, int variant = -1) {
        std::vector<int> cells((width_ - 2) * (height_ - 2));
        std::iota(cells.begin(), cells.end(), 0);
        for (size_t index = 0; index < cells.size(); ++index)
            std::swap(cells[index], cells[random_.below(int(cells.size()))]);
        for (int cell : cells)
            if (place(preset, 1 + cell % (width_ - 2), 1 + cell / (width_ - 2), variant))
                return;
        if (required)
            throw std::runtime_error("No room for original desert preset " + std::to_string(preset));
    }
    void cliffs() {
        struct Piece { int preset, variant, column, row; };
        constexpr Piece layouts[8][5]{
            {{376,1,0,4},{378,0,2,4},{377,-1,4,4},{377,-1,6,4},{376,2,8,4}},
            {{376,1,0,4},{377,-1,2,4},{378,0,4,4},{377,-1,6,4},{376,2,8,4}},
            {{376,1,0,4},{377,-1,2,4},{377,-1,4,4},{378,0,6,4},{376,2,8,4}},
            {{376,2,8,4},{377,-1,6,4},{382,0,4,4},{381,0,4,6},{379,2,4,8}},
            {{376,2,8,4},{378,0,6,4},{382,0,4,4},{380,-1,4,6},{379,2,4,8}},
            {{379,1,4,0},{381,0,4,2},{380,-1,4,4},{380,-1,4,6},{379,2,4,8}},
            {{379,1,4,0},{380,-1,4,2},{381,0,4,4},{380,-1,4,6},{379,2,4,8}},
            {{379,1,4,0},{380,-1,4,2},{380,-1,4,4},{381,0,4,6},{379,2,4,8}}};
        for (const auto &piece : layouts[random_.below(8)])
            if (!place(piece.preset, piece.column, piece.row, piece.variant))
                throw std::runtime_error("Original desert cliff overlap");
    }
    void canyon() {
        struct Piece { int preset, variant, column, row; };
        constexpr Piece tombs[]{{384,0,8,0},{383,2,6,0},{383,1,4,0},{383,0,2,0},
                                {387,0,0,0},{385,0,0,2},{385,1,0,4},{385,2,0,6},{386,0,0,8},{394,0,4,4}};
        for (const auto &piece : tombs)
            if (!place(piece.preset, piece.column, piece.row, piece.variant))
                throw std::runtime_error("Original canyon tomb overlap");
    }
    void borders() {
        for (int row = 0; row < height_; ++row)
            for (int column = 0; column < width_; ++column) {
                if (occupied_[row * width_ + column])
                    continue;
                int side = row == 0 ? 2 : row == height_ - 1 ? 0 : column == 0 ? 1
                           : column == width_ - 1 ? 3 : -1;
                if (side < 0)
                    continue;
                bool open = std::any_of(position_.boundaries.begin(), position_.boundaries.end(), [&](const auto &boundary) {
                    int lateral = (side % 2 ? row : column) * 8;
                    return boundary.side == side && lateral < boundary.end && lateral + 8 > boundary.start;
                });
                if (open) {
                    occupied_[row * width_ + column] = -1;
                    continue;
                }
                constexpr int straight[]{367,368,369,370};
                int preset = straight[side];
                if (column == 0 && row == 0) preset = 372;
                else if (column == width_ - 1 && row == 0) preset = 373;
                else if (column == width_ - 1 && row == height_ - 1) preset = 374;
                else if (column == 0 && row == height_ - 1) preset = 371;
                if (!place(preset, column, row))
                    throw std::runtime_error("Original desert border overlap");
            }
    }
    void waypoint() {
        int waypoint = catalog_.level(position_.level).waypoint;
        if (waypoint < 0 || waypoint == 255 || position_.level == 46)
            return;
        for (const auto &record : catalog_.substitutions()) {
            if (record.type != 7)
                continue;
            auto pattern = decodeDs1(archives_.read(record.file));
            for (size_t groupIndex = 0; groupIndex < pattern.substitutionGroups.size(); ++groupIndex) {
                const auto &group = pattern.substitutionGroups[groupIndex];
                if (group.variants || group.width > 7 || group.height > 7)
                    continue;
                for (int row = 1; row < height_ - 1; ++row)
                    for (int column = 1; column < width_ - 1; ++column)
                        if (!occupied_[row * width_ + column]) {
                            MapPiece piece;
                            piece.x = column * 8 + 1;
                            piece.y = row * 8 + 1;
                            piece.width = group.width;
                            piece.height = group.height;
                            piece.ds1 = record.file;
                            piece.tileLibraries = catalog_.terrainLibraries(16, record.dt1Mask);
                            piece.substitutionGroup = int(groupIndex);
                            piece.populate = false;
                            recipe_.pieces.push_back(std::move(piece));
                            occupied_[row * width_ + column] = -1;
                            return;
                        }
            }
        }
        throw std::runtime_error("Missing original desert waypoint group");
    }
  public:
    Desert(Archives &archives, const WorldCatalog &catalog, const OutdoorPosition &position, uint32_t seed)
                : archives_(archives), catalog_(catalog), position_(position), random_([&] {
                            Seed world(seed);
                            return world.next() + uint32_t(position.level);
                    }()),
          width_(position.width / 8), height_(position.height / 8), occupied_(width_ * height_) {
        recipe_.preset = 368;
        recipe_.levelType = 16;
        recipe_.act = catalog.level(position.level).act;
        recipe_.width = position.width;
        recipe_.height = position.height;
        recipe_.baseFloor = 0x40102;
        recipe_.tileLibraries = catalog.terrainLibraries(16, 1573123);
        recipe_.boundaries = position.boundaries;
        recipe_.ds1 = "desert-v1/" + std::to_string(position.level) + "/" + std::to_string(seed);
    }
    MapRecipe build() {
        int level = position_.level;
        if (level == 134) {
            if (!place(389, 4, 4))
                throw std::runtime_error("Original Matron's Den entrance does not fit");
            borders();
            for (int preset : {401,402,406,407,403,392,393}) randomPreset(preset);
            placeAct1OutdoorShrines(archives_, catalog_, catalog_.level(level), occupied_, recipe_, random_);
            return std::move(recipe_);
        }
        if (level == 41) {
            bool north = position_.direction == 2;
            if (!place(north ? 363 : 362, north ? 0 : width_ - 1, north ? height_ - 1 : 0))
                throw std::runtime_error("Original desert town transition overlap");
        }
        if (level >= 42 && level <= 44) cliffs();
        if (level == 46) canyon();
        borders();
        if (level != 46)
            randomPreset(level == 43 ? 390 : level == 44 ? 412 : level == 45 ? 389 : 388, true);
        if (level == 43) { randomPreset(396); randomPreset(397); }
        if (level == 44)
            for (int preset : {413,408,409,410}) randomPreset(preset);
        waypoint();
        placeAct1OutdoorShrines(archives_, catalog_, catalog_.level(level), occupied_, recipe_, random_);
        if (level != 45)
            for (int preset : level == 46 ? std::vector<int>{401,402,406,407,403,392,393}
                                          : std::vector<int>{395,411,399,398,403,400,401,402})
                randomPreset(preset);
        if (level == 42 || level == 44)
            for (int preset : {404,405,406,407})
                for (int variant = 0; variant < catalog_.presets().at(preset).files; ++variant)
                    randomPreset(preset, false, variant);
        return std::move(recipe_);
    }
};
}
MapRecipe generateDesert(Archives &archives, const WorldCatalog &catalog, const OutdoorPosition &position, uint32_t seed) {
    return Desert(archives, catalog, position, seed).build();
}
}