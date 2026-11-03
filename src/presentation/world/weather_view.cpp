#include "weather_view.hpp"
#include "palette_blend_view.hpp"
#include "presentation/graphics/primitives.hpp"
#include "content/world/world_catalog.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x {
namespace {
// Fog #10083/#10084: 512 angular steps, float LUT entries; x86 ftol truncates.
float cosine(int angle) { return float(std::cos(double(angle & 511) * 3.14159265358979323846 / 256.0)); }
float sine(int angle) { return float(std::sin(double(angle & 511) * 3.14159265358979323846 / 256.0)); }
void weatherLine(int x, int y, int endX, int endY, Color color, Rectangle viewport) {
    // Retail GDI 77B0 / DDraw 65C0: indexed writes, including the first
    // pixel and zero-length lines. These backends ignore the alpha argument.
    // The minor-axis accumulator starts at zero and advances on strict >.
    color.a=255;
    const int dx=std::abs(endX-x),dy=std::abs(endY-y);
    const int sx=endX<x?-1:1,sy=endY<y?-1:1;
    const auto pixel=[&] {
        if (CheckCollisionPointRec({float(x),float(y)},viewport)) DrawPixel(x,y,color);
    };
    pixel();
    int error=0;
    for (int i=0;i<std::max(dx,dy);++i) {
        if (dx>=dy) {
            x+=sx;error+=dy;
            if (error>dx) {y+=sy;error-=dx;}
        } else {
            y+=sy;error+=dx;
            if (error>dy) {x+=sx;error-=dy;}
        }
        pixel();
    }
}
}
int WeatherView::random(int range, int minimum) {
    return minimum + int(limitedRandom(random_, uint32_t(std::max(0, range))));
}
void WeatherView::reset(uint32_t seed) {
    random_ = initialRandom(seed);
    surfaceRandom_ = initialRandom(seed);
    act_ = level_ = -1; stage_ = remaining_ = duration_ = peak_ = density_ = 0;
    tick_ = surfaceFrame_ = lastMudFrame_ = 0; remainder_ = 0; cameraKnown_ = false;
    enabled_ = mud_ = snow_ = largeSnow_ = false;
    drops_.clear(); splashes_.clear(); surfaceSteps_.clear();
    wind();
}
void WeatherView::wind() {
    // D2Client 1.13c 80BE0. These are engine ranges, not MPQ parameters.
    windTicks_ = random(375,125);
    random(1500,500); // Original thunder countdown shares this client stream.
    wind_ = targetWind_ = random(71,92);
}
void WeatherView::enterLevel(const LevelRecord &level) {
    if (level_ == level.id) return;
    const bool changedAct = act_ != level.act;
    level_ = level.id; act_ = level.act;
    cameraKnown_ = false;
    const bool snow = level.act == 4;
    if (snow != snow_ || changedAct) { drops_.clear(); splashes_.clear(); cameraKnown_ = false; }
    snow_ = snow;
    largeSnow_ = level.id == 117 || level.id == 120 || level.id == 121;
    if (snow_) {
        // 814F0 forces snow on entry; only 117/120/121 force phase 2.
        // 109..112 may select rain (25/100) when the next build-up starts.
        wind(); random(100);
        minWind_ = 42; windRange_ = 170;
        if (level.id == 117) { minWind_ = 170; windRange_ = 56; peak_ = random(112,32); }
        else if (level.id == 120 || level.id == 121) {
            minWind_ = windRange_ = 28; wind_ = targetWind_ = 28; peak_ = 256;
        }
        if (largeSnow_) {stage_=2;density_=peak_;}
    }
    if (!level.rain) drops_.clear();
    if (!level.rain || !level.mud)
        std::erase_if(splashes_, [&](const Splash &s) { return s.bubble ? !level.mud : !level.rain; });
    enabled_ = level.rain; mud_ = level.mud;
}
WeatherView::Drop WeatherView::spawn(int width, int height, int period, const PaletteBlendView &palette) {
    Drop drop;
    drop.x = random(width);
    // Retail reserves 47 pixels for its HUD and another 40 at the top.
    // Our caller supplies the actual world height, keeping the 40-pixel range.
    const int span = std::max(1,height-40);
    drop.bottom = random(span,40);
    drop.y = random(drop.bottom+20,-20);
    drop.phase = random(512);
    const float depth = float(drop.bottom-40)/float(span);
    if (snow_) {
        random(100);
        drop.speed = 8-int(depth*-28.f);
        drop.size = (drop.bottom-40)*7/span + int(largeSnow_);
        drop.size = std::clamp(drop.size,0,7);
        drop.rest = 1;
        const int index = (drop.size*12/8) / (period==2 ? 4 : period==1 || period==3 ? 2 : 1);
        const uint8_t c = uint8_t(120-index*4);
        drop.color = palette.paletteColor({c,c,c,255});
    } else {
        const int shade = random(12);
        const uint8_t c = uint8_t(period==0 ? 98-shade*80/12 : period==2 ? 25-shade*24/12 : 45-shade*40/12);
        const uint8_t g = uint8_t(c+(period==0 ? 25 : period==2 ? 5 : 10));
        drop.color = palette.paletteColor({c,g,c,255});
        drop.length = 4-int(depth*-8.f);
        drop.speed = 15-int(depth*-15.f);
        drop.rest = 3;
    }
    return drop;
}
void WeatherView::step(int width, int height, int period, const PaletteBlendView &palette) {
    // 80F60 precedes 811F0/80E30. Existing particles use the previous density
    // and wind; refilling and steering happen only after their update.
    const float speed = snow_ ? std::max(.25f,std::abs(cosine(wind_))) : .85f+.15f*float(density_)/256.f;
    for (auto &drop : drops_) {
        const float distance = float(int(float(drop.speed)*speed));
        int dx = int(cosine(wind_)*distance);
        drop.y += int(sine(wind_)*distance);
        if (drop.y > drop.bottom) drop.landed = true;
        if (drop.landed && drop.rest > 0) { --drop.rest; drop.speed = 0; dx = 0; }
        else if (drop.landed) { drop.rest = -1; continue; }
        if (snow_ && !drop.landed) dx += int(sine(int((tick_*512u/100u+uint32_t(drop.phase))&511u))*2.f);
        drop.x = (drop.x + dx) % width;
        if (drop.x < 0) drop.x += width;
    }
    std::erase_if(drops_, [](const Drop &drop) { return drop.rest < 0; });
    ++tick_;
    if (!largeSnow_) {
        if (remaining_ == 0) {
            stage_ = (stage_+1)%4;
            constexpr std::array<int,4> ranges{7500,250,3000,50};
            const std::array<int,4> minima{7500,250,act_==2 || act_==4 ? 9000 : 3000,125};
            remaining_ = duration_ = random(ranges[size_t(stage_)],minima[size_t(stage_)]);
            if (stage_==0) density_=0;
            else if (stage_==1) {
                wind();
                const bool snow=act_==4 && (random(100)>=25 || level_<109 || level_>112);
                if (snow!=snow_) drops_.clear();
                snow_=snow;peak_=random(224,32);
            } else if (stage_==2) {
                if (level_>=109 && level_<=112) peak_=random(56,32);
                else if (!snow_) random(100);
                density_=peak_;
            }
        }
        --remaining_;
        if (stage_==1) density_=(duration_-remaining_)*peak_/duration_;
        else if (stage_==3) density_=peak_*remaining_/duration_;
    }
    while (int(drops_.size()) < density_) drops_.push_back(spawn(width,height,period,palette));
    const int turn = snow_ ? 1 : 2;
    wind_ += std::clamp(targetWind_-wind_,-turn,turn);
    if (--windTicks_ == 0) {
        windTicks_ = snow_ ? random(875,250) : random(375,125);
        targetWind_ = snow_ ? random(windRange_,minWind_) : random(71,92);
    }
}
void WeatherView::advance(float elapsed, const LevelRecord &level, Vec cameraPixels,
    int width, int height, int period, const PaletteBlendView &palette) {
    enterLevel(level);
    if (width<=0 || height<=40) return;
    if (cameraKnown_) {
        const int dx=int(cameraPixels.x)-int(camera_.x), dy=int(cameraPixels.y)-int(camera_.y);
        for (auto &drop:drops_) { drop.x-=dx; drop.y-=dy; }
    }
    camera_=cameraPixels; cameraKnown_=true;
    remainder_ += std::max(0.f,elapsed)*25.f;
    const int ticks=int(remainder_); remainder_-=float(ticks);
    for (int i=0;i<ticks;++i) {
        for (auto &s:splashes_) ++s.ticks;
        std::erase_if(splashes_,[&](const Splash &s) {
            const auto &art=s.bubble?bubbleArt_[size_t(s.kind)]:rainArt_[size_t(s.kind)];
            return s.ticks/2>=art.count;
        });
        if (enabled_) step(width,height,std::clamp(period,0,3),palette);
        ++surfaceFrame_;
        const int rainChance=enabled_ && !snow_ && surfaceFrame_%4==0 ? density_*1000/256 : 0;
        // 14050 uses a separate >25-frame gate for Mud, not every update.
        const bool bubble= mud_ && surfaceFrame_-lastMudFrame_>25;
        if (bubble) lastMudFrame_=surfaceFrame_;
        if (rainChance || bubble) surfaceSteps_.push_back({rainChance,ticks-1-i,bubble});
    }
}
void WeatherView::waterTile(Vec position, Vec at, Rectangle viewport) {
    if (surfaceSteps_.empty() || !CheckCollisionPointRec(rv(at),viewport)) return;
    // 13E82/14050: only WATER material. Rain3/4 use the weather density
    // every fourth client frame; Mud uses 100/1000 every 26 client frames.
    for (const auto &step:surfaceSteps_) {
        const auto surfaceRandom=[&](int range,int minimum=0) {return minimum+int(limitedRandom(surfaceRandom_,uint32_t(range)));};
        const int chance=surfaceRandom(1000);
        if (chance<step.rainChance) {
            const int y=surfaceRandom(80),half=80-2*std::abs(y-40),x=surfaceRandom(half*2,-half);
            // Inverse isometric pixel offset (D2's tile diamond is 160x80).
            splashes_.push_back({position+unproject({float(x),float(y)}),surfaceRandom(2,2),step.age,false});
        }
        if (step.mud && chance<100) {
            const int y=surfaceRandom(80),half=80-2*std::abs(y-40),x=surfaceRandom(half*2,-half);
            splashes_.push_back({position+unproject({float(x),float(y-30)}),2,step.age,true});
        }
    }
}
void WeatherView::finishWaterTiles() { surfaceSteps_.clear(); }
void WeatherView::loadArt(Archives &archives, Graphics &graphics, int palette) {
    if (artPalette_==palette) return;
    for (size_t i=0;i<4;++i) {
        const auto load=[&](const char *name) {
            const auto path="data/global/uncompoverlays/"+std::string(name)+std::to_string(i+1)+".dc6";
            if (!archives.contains(path)) throw std::runtime_error("Original weather resource unavailable: "+path);
            // 7FC70 draws DC6 cells on their baseline, like unit overlays.
            return graphics.worldSingle(path,WorldSingleArt::BlendedOverlay);
        };
        rainArt_[i]=load("rain"); bubbleArt_[i]=load("bubble");
    }
    artPalette_=palette;
}
void WeatherView::drawSplashes(const PaletteBlendView &palette, const std::function<Vec(Vec)> &screen) const {
    for (const auto &s:splashes_) {
        const auto &art=s.bubble?bubbleArt_[size_t(s.kind)]:rainArt_[size_t(s.kind)];
        if (s.ticks/2>=art.count) continue;
        palette.draw(art.frame(0,s.ticks/2),screen(s.position));
    }
}
void WeatherView::draw(const PaletteBlendView &, Rectangle viewport) const {
    if (!enabled_) return;
    // 7FE80: software rain is a clipped palette line; snow is two short
    // segments selected by its original eight-size table, not a new sprite.
    constexpr std::array<std::array<int,4>,9> snowLines{{
        {0,0,0,1},{-1,-1,0,0},{0,-1,0,1},{1,0,-1,1},
        {1,0,0,1},{2,-1,-1,1},{1,1,-2,-1},{2,1,-2,-1},{2,-1,-2,2}}};
    for (const auto &drop:drops_) {
        const auto line=[&](int x,int y,int x2,int y2) {
            weatherLine(x,y,x2,y2,drop.color,viewport);
        };
        if (snow_) {
            if (!CheckCollisionPointRec({float(drop.x),float(drop.y)},viewport)) continue;
            for (int i=0;i<2;++i) {
                const auto &v=snowLines[size_t(drop.size+i)];
                line(drop.x+v[0],drop.y+v[1],drop.x+v[2],drop.y+v[3]);
            }
        } else {
            int dx=drop.landed?0:int(cosine(wind_)*float(drop.length));
            int dy=drop.landed?0:int(sine(wind_)*float(drop.length));
            if (drop.bottom-drop.y<dy && dy) { dx=(drop.bottom-drop.y)*dx/dy;dy=drop.bottom-drop.y; }
            line(drop.x,drop.y,drop.x+dx,drop.y+dy);
        }
    }
}
} // namespace d2x
