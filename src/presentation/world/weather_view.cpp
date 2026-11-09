#include "weather_view.hpp"
#include "palette_blend_view.hpp"
#include "presentation/graphics/primitives.hpp"
#include "content/world/world_catalog.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <random>
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
    random_ = initialRandom(seed ^ uint32_t(std::random_device{}()));
    surfaceRandom_ = initialRandom(rollRandom(random_));
    act_ = level_ = -1; density_ = 0;
    stage_=cycle_.stage;remaining_=cycle_.remaining;duration_=cycle_.duration;peak_=cycle_.peak;
    randomizeCycle_=!cycle_.initialized;
    if (stage_==2) density_=peak_;
    else if (duration_>0 && stage_==1) density_=(duration_-remaining_)*peak_/duration_;
    else if (duration_>0 && stage_==3) density_=peak_*remaining_/duration_;
    tick_ = surfaceFrame_ = lastRainFrame_ = lastMudFrame_ = 0;
    remainder_ = 0; cameraKnown_ = false;
    enabled_ = mud_ = snow_ = largeSnow_ = forcedSnow_ = false;
    minWind_=42;windRange_=170;
    lightning_=secondFlash_=flashPending_=flash_=thunderSoundEnabled_=thunderReady_=false;
    lightningDelay_=-1; thunderSoundTicks_=-1;
    frameSampleStart_=-1;
    frameSampleCount_=framesPerSecond_=0;
    debugMode_ = WeatherDebugMode::Auto; debugDensity_ = 255;
    drops_.clear(); splashes_.clear();
}
void WeatherView::reposition(const LevelRecord *setupLevel) {
    splashes_.clear();
    if (setupLevel && debugMode_ == WeatherDebugMode::Auto)
        configurePrecipitation(setupLevel->act, setupLevel->id, true);
}
void WeatherView::lightning(float delay) {
    if (!lightning_ && lightningDelay_<0) lightningDelay_=std::max(0.f,delay);
}
void WeatherView::presentLightning(double now) {
    // 5A9D8/806C2: use the completed one-second render count for the flash.
    if (frameSampleStart_ < 0) frameSampleStart_ = now;
    if (now - frameSampleStart_ > 1.0) {
        frameSampleStart_ = now;
        framesPerSecond_ = frameSampleCount_;
        frameSampleCount_ = 0;
    } else ++frameSampleCount_;
    flash_=false;
    if (!lightning_ || !flashPending_) return;
    flashPending_=false; flash_=framesPerSecond_>=10;
    if (!secondFlash_) {thunderTicks_=3;secondFlash_=true;}
    else {
        thunderTicks_=random(1500,500);secondFlash_=false;
        if (thunderSoundEnabled_) {
            thunderSoundTicks_=random(50,25);
            random(400,-200);random(400,-200);
        } else thunderSoundEnabled_=true;
    }
}
bool WeatherView::takeThunder() {
    const bool ready=thunderReady_;thunderReady_=false;return ready;
}
WeatherDebugState WeatherView::debugState() const {
    WeatherDebugState state;
    state.mode=debugMode_; state.level=level_; state.stage=stage_;
    state.density=enabled_?density_:0; state.remainingFrames=remaining_;
    state.requestedDensity=debugDensity_;
    state.precipitationAllowed=enabled_;
    state.rainAllowed=enabled_ && (act_!=4 || (level_>=109 && level_<=112));
    state.snowAllowed=enabled_ && act_==4; state.snow=snow_;
    state.lightning=lightning_;state.thunderSoundFrames=thunderSoundTicks_;
    state.particles=drops_.size(); state.splashes=splashes_.size();
    return state;
}
void WeatherView::applyDebugWeather() {
    if(debugMode_==WeatherDebugMode::Auto) return;
    const auto state=debugState();
    const bool raining=debugMode_==WeatherDebugMode::Rain && state.rainAllowed;
    const bool snowing=debugMode_==WeatherDebugMode::Snow && state.snowAllowed;
    density_=peak_=(raining || snowing)?debugDensity_:0;
    stage_=density_?2:0; remaining_=duration_=0;
    if(raining || snowing) snow_=snowing;
    if(!density_) {
        drops_.clear();
        std::erase_if(splashes_,[](const Splash &s) {return !s.bubble;});
    }
}
void WeatherView::debugWeather(WeatherDebugMode mode, int density, const LevelRecord &level) {
    if(level_!=level.id) throw std::invalid_argument("Weather requires the currently rendered level");
    const auto state=debugState();
    if(mode==WeatherDebugMode::Rain && !state.rainAllowed)
        throw std::invalid_argument("Rain is unavailable in this level (MPQ Rain flag / Act V snow rules)");
    if(mode==WeatherDebugMode::Snow && !state.snowAllowed)
        throw std::invalid_argument("Snow requires an Act V level with the MPQ Rain flag");
    if(density<1 || density>255) throw std::invalid_argument("Weather density must be an integer from 1 to 255");
    debugMode_=mode; debugDensity_=density;
    stage_=remaining_=duration_=peak_=density_=0;
    randomizeCycle_=mode==WeatherDebugMode::Auto;
    lightning_=secondFlash_=flashPending_=flash_=thunderSoundEnabled_=thunderReady_=false;
    lightningDelay_=-1;thunderSoundTicks_=-1;
    drops_.clear(); splashes_.clear();
    // Re-enter using the original level setup. Auto starts a fresh natural
    // cycle; an override never changes the MPQ record or a gameplay seed.
    enterLevel(level, true);
}
void WeatherView::wind() {
    // D2Client 1.13c 80BE0. These are engine ranges, not MPQ parameters.
    windTicks_ = random(375,125);
    thunderTicks_ = random(1500,500);
    wind_ = targetWind_ = random(71,92);
}
void WeatherView::initializeCycle() {
    const std::array<int,4> durations{
        random(7500,7500),random(250,250),
        random(3000,fullDurationBase_),random(50,125)};
    int offset=random(durations[0]+durations[1]+durations[2]+durations[3]);
    stage_=0;
    while (stage_<3 && offset>=durations[size_t(stage_)]) {
        offset-=durations[size_t(stage_)];
        ++stage_;
    }
    duration_=durations[size_t(stage_)];
    remaining_=duration_-offset;
    peak_=random(224,32);
    density_=stage_==0?0:stage_==1?offset*peak_/duration_:
        stage_==2?peak_:remaining_*peak_/duration_;
    randomizeCycle_=false;
}
void WeatherView::enterLevel(const LevelRecord &level, bool initialize) {
    if (!initialize && level_ == level.id) return;
    level_ = level.id; act_ = level.act;
    cameraKnown_ = false;
    if (initialize) splashes_.clear();
    if (initialize || debugMode_ != WeatherDebugMode::Auto) {
        const bool randomized = randomizeCycle_;
        if (randomized) initializeCycle();
        configurePrecipitation(act_, level_, true);
        if (randomized && stage_ == 2 && !forcedSnow_) sustainPrecipitation(level_);
        if (!cycle_.windInitialized) {
            wind();
            cycle_.windInitialized = true;
        }
    }
    if (!level.rain) drops_.clear();
    if (!level.rain || !level.mud)
        std::erase_if(splashes_, [&](const Splash &s) { return s.bubble ? !level.mud : !level.rain; });
    enabled_ = level.rain; mud_ = level.mud;
    applyDebugWeather();
    if (debugMode_==WeatherDebugMode::Auto) cycle_={stage_,remaining_,duration_,peak_,cycle_.windInitialized,true};
}
void WeatherView::configurePrecipitation(int act, int level, bool forceSnow) {
    if (act!=4) {
        forcedSnow_=false;
        if (snow_) drops_.clear();
        snow_=false;
        wind();
        return;
    }
    wind();
    forcedSnow_=level==120 || level==121;
    if (level>=109 && level<=112) largeSnow_=false;
    else if (level==117 || forcedSnow_) largeSnow_=true;
    const int choice=random(100);
    const bool snow=forceSnow || level<109 || level>112 || choice>=25;
    if (forcedSnow_) {
        stage_=2;
        sustainPrecipitation(level);
    }
    if (snow) lightning_=false;
    if (snow!=snow_ || (snow && forceSnow)) drops_.clear();
    snow_=snow;
}
void WeatherView::sustainPrecipitation(int level) {
    if (level>=109 && level<=112) {
        minWind_=42;windRange_=170;
        peak_=random(56,32);
    } else if (level==117) {
        minWind_=170;windRange_=56;
        peak_=random(112,32);
    } else if (level==120 || level==121) {
        minWind_=windRange_=28;wind_=targetWind_=28;
        peak_=256;
    } else {
        if (!snow_) random(100);
        lightning_=false;
    }
    density_=peak_;
}
WeatherView::Drop WeatherView::spawn(int width, int height, int period, const PaletteBlendView &palette) {
    Drop drop;
    drop.x = random(width);
    // 80753: full screen height minus 47 at the bottom and 40 at the top.
    // The caller already excludes the 47-pixel strip.
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
        const uint8_t c = uint8_t(120+index*80/12);
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
    if (debugMode_==WeatherDebugMode::Auto) {
        int phase=stage_;
        const bool forceSustain=forcedSnow_ && phase!=2;
        if (forceSustain) {
            stage_=2;
            sustainPrecipitation(level_);
        } else {
            if (remaining_ == 0) {
                phase = (phase+1)%4;
                constexpr std::array<int,4> ranges{7500,250,3000,50};
                const std::array<int,4> minima{7500,250,fullDurationBase_,125};
                remaining_ = duration_ = random(ranges[size_t(phase)],minima[size_t(phase)]);
                if (!forcedSnow_ || phase==2) {
                    stage_=phase;
                    if (phase==0) density_=0;
                    else if (phase==1) {
                        configurePrecipitation(act_,level_,false);
                        peak_=random(224,32);
                    } else if (phase==2) sustainPrecipitation(level_);
                    else if (phase==3) lightning_=false;
                }
            }
            --remaining_;
            if (phase==1) density_=(duration_-remaining_)*peak_/duration_;
            else if (phase==3) density_=peak_*remaining_/duration_;
        }
    }
    while (int(drops_.size()) < density_) drops_.push_back(spawn(width,height,period,palette));
    const int turn = snow_ ? 1 : 2;
    wind_ += std::clamp(targetWind_-wind_,-turn,turn);
    if (--windTicks_ == 0) {
        windTicks_ = snow_ ? random(875,250) : random(375,125);
        targetWind_ = snow_ ? random(windRange_,minWind_) : random(71,92);
    }
    if (lightning_ && --thunderTicks_==0) flashPending_=true;
    if (debugMode_==WeatherDebugMode::Auto) cycle_={stage_,remaining_,duration_,peak_,cycle_.windInitialized,true};
}
void WeatherView::advance(float elapsed, const LevelRecord &level, std::optional<int> loadedAct, Vec cameraPixels,
    int width, int worldHeight, int period, const PaletteBlendView &palette) {
    if (loadedAct) fullDurationBase_ = *loadedAct == 2 || *loadedAct == 4 ? 9000 : 3000;
    enterLevel(level, loadedAct.has_value() || level_<0);
    if (width<=0 || worldHeight<=40) return;
    if (cameraKnown_) {
        const int dx=int(cameraPixels.x)-int(camera_.x), dy=int(cameraPixels.y)-int(camera_.y);
        for (auto &drop:drops_) { drop.x-=dx; drop.y-=dy; }
    }
    camera_=cameraPixels; cameraKnown_=true;
    remainder_ += std::max(0.f,elapsed)*25.f;
    const int ticks=int(remainder_); remainder_-=float(ticks);
    for (int i=0;i<ticks;++i) {
        if (lightningDelay_>=0) {
            lightningDelay_-=1.f/25.f;
            if (lightningDelay_<=0) {
                lightningDelay_=-1;lightning_=true;flashPending_=true;
                secondFlash_=false;thunderSoundEnabled_=false;
            }
        }
        if (thunderSoundTicks_>=0 && --thunderSoundTicks_<=0) {
            thunderSoundTicks_=-1;thunderReady_=true;
        }
        for (auto &s:splashes_) ++s.ticks;
        std::erase_if(splashes_,[&](const Splash &s) {
            const auto &art=s.bubble?bubbleArt_[size_t(s.kind)]:rainArt_[size_t(s.kind)];
            return s.ticks/2>=art.count;
        });
        if (enabled_) step(width,worldHeight,std::clamp(period,0,3),palette);
        ++surfaceFrame_;
    }
}
void WeatherView::beginWaterTiles() {
    // 14050 checks elapsed client frames once per floor pass, even after catch-up.
    splashChance_ = 0;
    if (surfaceFrame_ - lastRainFrame_ > 3) {
        lastRainFrame_ = surfaceFrame_;
        if (enabled_) splashChance_ = density_ * 1000 / 256;
    }
    bubblePass_ = mud_ && surfaceFrame_ - lastMudFrame_ > 25;
    if (bubblePass_) lastMudFrame_ = surfaceFrame_;
}
void WeatherView::waterTile(Vec position) {
    if (!splashChance_ && !bubblePass_) return;
    const auto surfaceRandom = [&](int range) { return int(limitedRandom(surfaceRandom_,uint32_t(range))); };
    const int chance = surfaceRandom(1000);
    const auto spot = [&](int rise) {
        const int y = surfaceRandom(80);
        const int half = 80 - 2 * std::abs(y - 40);
        const int x = surfaceRandom(half * 2) - half;
        return position + unproject({float(x), float(y - rise)});
    };
    if (chance < splashChance_) {
        const auto at = spot(0);
        splashes_.push_back({at,surfaceRandom(2)+2,0,false});
    }
    if (bubblePass_ && chance < 100) splashes_.push_back({spot(30),2,0,true});
}
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
void WeatherView::drawSplashes(const PaletteBlendView &palette, const std::function<Vec(Vec)> &screen, Rectangle viewport) const {
    for (const auto &s:splashes_) {
        const auto &art=s.bubble?bubbleArt_[size_t(s.kind)]:rainArt_[size_t(s.kind)];
        if (s.ticks/2>=art.count) continue;
        const auto at = screen(s.position);
        if (CheckCollisionPointRec(rv(at),viewport)) palette.draw(art.frame(0,s.ticks/2),at);
    }
}
void WeatherView::draw(const PaletteBlendView &palette, Rectangle viewport) const {
    if (viewport.width<=0 || viewport.height<=0) return;
    if (flash_) {DrawRectangleRec(viewport,palette.paletteByte(255));return;}
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
            if (!drop.landed && drop.bottom-drop.y<dy && dy) { dx=(drop.bottom-drop.y)*dx/dy;dy=drop.bottom-drop.y; }
            line(drop.x,drop.y,drop.x+dx,drop.y+dy);
        }
    }
}
} // namespace d2x
