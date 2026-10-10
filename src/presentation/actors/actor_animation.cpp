#include "actor_animation.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "gameplay/skills/weapon_volley.hpp"
#include "content/monsters/monster_animation.hpp"
#include "content/world/object_mode.hpp"
#include "gameplay/skills/cast_timing.hpp"
#include "gameplay/skills/amazon_sequence.hpp"
#include "presentation/graphics/primitives.hpp"
#include "resources/monster_palshift.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace d2x {
namespace {
std::string lower(std::string value) {
    for (auto &ch : value) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}
std::string keyFor(int palette, const ActorAnimationRequest &request) {
    std::string key = std::to_string(palette) + ":" + request.category + ":" +
        request.appearance.token + ":" + request.mode + ":" + request.appearance.weapon + ":" +
        std::to_string(request.paletteTransform) + ":" + std::to_string(request.randomTransform) + ":" +
        std::to_string(request.finalFrame) + ":" + std::to_string(request.shadow) + ":" +
        std::to_string(request.playerSequence) + ":" + (request.fasterCast ? std::to_string(*request.fasterCast) : "unknown");
    if(request.attackTiming) key+=":"+std::to_string(request.attackTiming->itemIAS)+":"+std::to_string(request.attackTiming->weaponSpeed)+":"+std::to_string(request.attackTiming->skillRate);
    key+=":"+std::to_string(request.repeatCount)+":"+std::to_string(request.rollbackPercent);
    for (const auto &part : request.appearance.components) key += ":" + part;
    return key;
}
} // namespace
bool ActorAnimation::ready() const {
    return animation.directions > 0 && frameCount > 0 && start >= 0 &&
        start <= animation.count && frameCount <= animation.count - start &&
        animation.frames.size() >= size_t(animation.directions) * size_t(animation.count);
}
float ActorAnimation::duration() const { return durationFrames ? float(*durationFrames)/25.f : fps > 0 ? frameCount / fps : 0; }
bool ActorAnimation::finished(float elapsed) const {
    return !cycle && holdFrame < 0 && fps > 0 && elapsed >= duration();
}
const Sprite *ActorAnimation::sample(float clock, float elapsed, Vec look) const {
    return sampleFacing(clock, elapsed, direction(look, animation.directions));
}
const Sprite *ActorAnimation::sampleFacing(float clock, float elapsed, int facing) const {
    if (!ready()) return nullptr;
    // Avoid GPU frame()'s implicit wrapping for one-shot and static death poses.
    const double advance = std::max(0.0, double(cycle ? clock : elapsed)) * std::max(0.f, fps);
    const int index = holdFrame >= 0 ? int(std::min(advance, double(holdFrame))) : cycle ? int(std::fmod(advance, double(frameCount)))
        : int(std::min(advance, double(frameCount - 1)));
    return animation.frame(facing, start + index);
}
void ActorAnimationState::observe(std::optional<uint8_t> currentMode, uint64_t actionRevision,
                                 float now, float receivedAge, bool frozen) {
    if (mode != currentMode || revision != actionRevision) {
        mode = currentMode; revision = actionRevision;
        startedAt = now - std::max(0.f, receivedAge); frozenAt.reset();
    }
    if (frozen && !frozenAt) frozenAt = now;
    if (!frozen && frozenAt) { startedAt += now - *frozenAt; frozenAt.reset(); }
}
float ActorAnimationState::elapsed(float now) const { return std::max(0.f, clock(now) - startedAt); }
ActorAnimationCatalog::ActorAnimationCatalog(Archives &archives)
    : archives_(archives), timing_(archives.read("data/global/animdata.d2")),
      objects_(archives.read("data/global/excel/objects.txt")),
      sequences_(archives.read("data/global/excel/monseq.txt")) {
    for (size_t row = 0; row < objects_.rows().size(); ++row)
        if (auto id = objects_.number(row, "Id")) objectRows_.emplace(*id, row);
    for (size_t row = 0; row < sequences_.rows().size(); ++row)
        if (!sequences_.value(row, "sequence").empty())
            sequenceRows_[std::string(sequences_.value(row, "sequence"))].push_back(row);
}
ActorAnimation ActorAnimationCatalog::composite(Graphics &graphics, const ActorAnimationRequest &request) {
    ActorAnimation result;
    const auto &parts = request.appearance;
    const auto base = "data/global/" + request.category + "/" + parts.token + "/";
    std::array<const char *, 16> components;
    for (size_t index = 0; index < components.size(); ++index) components[index] = parts.components[index].c_str();
    std::optional<std::array<uint8_t, 256>> colors;
    if (request.paletteTransform >= 0) {
        const auto path = base + "cof/palshift.dat";
        if (request.randomTransform) {
            if (request.paletteTransform < 8) return result;
            const auto data = archives_.read("data/global/monsters/randtransforms.dat");
            const auto offset = size_t(request.paletteTransform - 8) * 256;
            if (offset + 256 > data.size()) return result;
            colors.emplace(); std::copy_n(data.begin() + offset, 256, colors->begin());
            if ((*colors)[0] != 0) return result;
        } else if (archives_.contains(path)) colors = monsterPalshift(archives_.read(path), request.paletteTransform);
        else if (request.paletteTransform != 0) return result;
    }
    auto mode = request.mode;
    // The original corpse can lack a DD COF. Only its own DT final frame is allowed.
    bool finalFrame = request.finalFrame;
    if (mode == "dd" && !archives_.contains(base + "cof/" + parts.token + mode + parts.weapon + ".cof")) {
        mode = "dt"; finalFrame = true;
    }
    result.animation = graphics.composite(request.category, parts.token, mode, parts.weapon, &components,
        colors ? &*colors : nullptr);
    if (!result.animation.completeComposite) { result.animation = {}; return result; }
    result.shadow = request.shadow;
    std::string timingKey = parts.token + mode + parts.weapon;
    for (auto &ch : timingKey) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    const auto *record=timing_.find(timingKey);
    if (record && record->speed > 0) {
        result.fps = float(record->speed) * 25.f / 256.f;
        for (size_t frame = 0; frame < std::min(size_t(record->frames), record->frameFlags.size()); ++frame)
            if (record->frameFlags[frame] == 1 || record->frameFlags[frame] == 2) {
                result.releaseTime = float(frame) / result.fps; break;
            }
        if (request.category=="chars" && mode=="sc" && request.fasterCast) {
            const auto animation=prepareCastAnimationTiming(int(record->frames),record->speed,record->frameFlags);
            if (!animation) return {};
            const auto timing=normalCastTiming(*animation,*request.fasterCast);
            result.fps=float(timing.speed)*25.f/256.f;
            result.releaseTime=float(timing.impact)/25.f;
            result.durationFrames=timing.duration;
        }
        if(request.category=="chars" && request.attackTiming && (mode=="a1" || mode=="a2" || mode=="th" || mode=="s1")) {
            const auto &known=*request.attackTiming;
            int action=0;
            for(size_t frame=0;frame<record->frameFlags.size();++frame) if(record->frameFlags[frame]==1 || record->frameFlags[frame]==2) {action=int(frame);break;}
            const auto start=attackStartingFrame(parts.token=="am"?"ama":parts.token=="so"?"sor":"",parts.weapon,mode);
            const WeaponAttackTiming timing{mode,int(record->frames),effectiveAttackSpeed(record->speed,known.itemIAS,known.weaponSpeed,known.skillRate),action,start};
            result.fps=float(timing.speed)*25.f/256.f;result.start=start;
            result.releaseTime=float(timing.actionTick())/25.f;result.durationFrames=timing.durationTicks();
        }
    }
    result.frameCount = result.animation.count;
    result.frameCount-=result.start;
    if(request.repeatCount>1 && (mode=="a1" || mode=="a2")) {
        const int speed=int(std::lround(result.fps*256.f/25.f));
        if(!record) return {};
        int action=-1;for(size_t frame=0;frame<record->frameFlags.size();++frame) if(record->frameFlags[frame]==1 || record->frameFlags[frame]==2) {action=int(frame);break;}
        if(action<0) return {};
        const auto volley=weaponVolley({mode,result.animation.count,speed,action,result.start},request.repeatCount,request.rollbackPercent);
        const auto original=result.animation;result.animation.frames.clear();result.animation.count=int(volley.frames.size());
        for(int direction=0;direction<original.directions;++direction) for(int frame:volley.frames) result.animation.frames.push_back(*original.frame(direction,frame));
        result.start=0;result.frameCount=result.animation.count;result.fps=25.f;result.durationFrames=result.frameCount;result.releaseTimes.clear();
        for(int tick:volley.hits) result.releaseTimes.push_back(float(tick)/25.f);
        if(!result.releaseTimes.empty()) result.releaseTime=result.releaseTimes.front();
    }
    result.cycle = mode == "nu" || mode == "tn" || mode == "wl" || mode == "tw" || mode == "rn";
    if (finalFrame) {
        result.start = std::max(0, result.animation.count - 1); result.frameCount = 1;
        result.fps = 0; result.cycle = false;
    }
    return result;
}
ActorAnimation ActorAnimationCatalog::sequence(Graphics &graphics, int palette, const ActorAnimationRequest &request) {
    ActorAnimation result;
    auto baseRequest = request; baseRequest.playerSequence = -1;
    if(request.category=="chars" && request.playerSequence==4) {
        baseRequest.mode="a1";const auto *attack=resolve(graphics,palette,baseRequest);
        if(!attack || attack->animation.count<13) return {};
        constexpr int attackFrames[]{1,4,5,6,8,10,12};
        const int speed=request.attackTiming?effectiveAttackSpeed(256,request.attackTiming->itemIAS,request.attackTiming->weaponSpeed,request.attackTiming->skillRate):256;
        result=*attack;result.animation.frames.clear();result.animation.count=7;result.frameCount=7;result.start=0;result.fps=float(speed)*25.f/256.f;result.durationFrames=std::max(1,(7*256+speed-1)/speed);result.releaseTime=float(std::max(1,(3*256+speed-1)/speed))/25.f;result.cycle=false;
        for(int facing=0;facing<attack->animation.directions;++facing) {
            for(const int frame:attackFrames) result.animation.frames.push_back(*attack->animation.frame(facing,frame));
        }
        return result;
    }
    if(request.category=="chars" && (request.playerSequence==1 || request.playerSequence==8)) {
        const auto sequence=amazonWeaponSequence(request.playerSequence,request.appearance.weapon);
        if(sequence.frames.empty()) return result;
        baseRequest.mode="a1";const auto *first=resolve(graphics,palette,baseRequest);
        baseRequest.mode="a2";const auto *second=resolve(graphics,palette,baseRequest);
        if(!first || !second || first->animation.directions!=second->animation.directions) return result;
        result=*first;result.animation.frames.clear();result.animation.count=int(sequence.frames.size());
        const auto speed=request.attackTiming?effectiveAttackSpeed(256,request.attackTiming->itemIAS,request.attackTiming->weaponSpeed,request.attackTiming->skillRate-30):256;
        result.frameCount=result.animation.count;result.fps=float(speed)*25.f/256.f;result.releaseTimes.clear();result.cycle=false;
        for(int facing=0;facing<first->animation.directions;++facing) for(const auto &sample:sequence.frames) {
            const auto *base=sample.secondAttack?second:first;
            if(sample.frame<0 || sample.frame>=base->animation.count) return {};
            result.animation.frames.push_back(*base->animation.frame(facing,sample.frame));
        }
        for(size_t i=0;i<sequence.frames.size();++i) if(sequence.frames[i].hit) result.releaseTimes.push_back(float(weaponSequenceTick(int(i),speed))/25.f);
        result.releaseTime=result.releaseTimes.front();result.durationFrames=WeaponAttackTiming{"sq",int(sequence.frames.size()),speed,0,0}.durationTicks();return result;
    }
    if (const auto sequence=playerCastSequence(request.playerSequence); sequence && request.category == "chars") {
        baseRequest.mode = "sc";
        const auto *base = resolve(graphics, palette, baseRequest);
        if (!base || base->animation.count <= *std::max_element(sequence->frames.begin(),sequence->frames.end()) ||
            (!sequence->fixedSpeed && base->fps <= 0)) return result;
        result = *base; result.animation.frames.clear(); result.animation.count = int(sequence->frames.size());
        for (int facing = 0; facing < base->animation.directions; ++facing)
            for (const int frame : sequence->frames) result.animation.frames.push_back(*base->animation.frame(facing, frame));
        result.frameCount = result.animation.count; result.cycle = false;
        if (sequence->fixedSpeed) result.fps=float(sequence->fixedSpeed)*25.f/256.f;
        result.holdFrame=sequence->holdFrame;
        result.releaseTime = float(sequence->releaseFrame) / result.fps; result.releaseTimes = {result.releaseTime};
        result.durationFrames.reset();
        if (request.fasterCast || sequence->fixedSpeed) {
            // Base SC has already evaluated FCR. Sequence lengths/release steps are common.
            const int speed=sequence->fixedSpeed ? sequence->fixedSpeed : int(std::lround(result.fps*256.f/25.f));
            const auto timing=playerCastSequenceTiming(*sequence,speed);
            result.durationFrames=timing.duration;
            result.releaseTime=float(timing.impact)/25.f;
            result.releaseTimes={result.releaseTime};
        }
        return result;
    }
    if (request.playerSequence >= 0 || request.category != "monsters") return result;
    const auto rows = sequenceRows_.find(request.mode);
    if (rows == sequenceRows_.end() || rows->second.empty()) return result;
    std::vector<std::pair<const ActorAnimation *, int>> samples;
    for (const auto row : rows->second) {
        baseRequest.mode = lower(std::string(sequences_.value(row, "mode")));
        if (baseRequest.mode.starts_with("seq_") || baseRequest.mode == "sq") return result;
        baseRequest.appearance.weapon = monsterModeWeapon(archives_, request.appearance.token, baseRequest.mode,
            request.appearance.weapon);
        if (baseRequest.appearance.weapon.empty() || sequences_.number(row, "dir").value_or(0)) return result;
        const auto *base = resolve(graphics, palette, baseRequest);
        const auto frame = sequences_.number(row, "frame");
        if (!base || !frame || *frame < 0 || *frame >= base->animation.count || base->fps <= 0) return result;
        samples.emplace_back(base, *frame);
    }
    const auto *first = samples.front().first;
    result = *first; result.animation.frames.clear(); result.animation.count = int(samples.size());
    result.frameCount = result.animation.count; result.cycle = false; result.releaseTime = -1; result.releaseTimes.clear();
    for (int facing = 0; facing < first->animation.directions; ++facing)
        for (const auto &[base, frame] : samples) {
            if (base->animation.directions != first->animation.directions) return {};
            result.animation.frames.push_back(*base->animation.frame(facing, frame));
        }
    for (size_t index = 0; index < rows->second.size(); ++index) {
        const auto event = sequences_.number(rows->second[index], "event").value_or(0);
        if (event == 1 || event == 2 || event == 4) result.releaseTimes.push_back(float(index) / result.fps);
    }
    if (!result.releaseTimes.empty()) result.releaseTime = result.releaseTimes.front();
    return result;
}
const ActorAnimation *ActorAnimationCatalog::resolve(Graphics &graphics, int palette, const ActorAnimationRequest &request) {
    const auto key = keyFor(palette, request);
    if (const auto found = animations_.find(key); found != animations_.end())
        return found->second.ready() ? &found->second : nullptr;
    auto result = request.playerSequence >= 0 || request.mode.starts_with("seq_")
        ? sequence(graphics, palette, request) : composite(graphics, request);
    const auto entry = animations_.emplace(key, std::move(result)).first;
    graphics.releaseDecoded();
    return entry->second.ready() ? &entry->second : nullptr;
}
std::string ActorAnimationCatalog::sequenceMode(std::string_view name) const {
    const auto rows = sequenceRows_.find(name);
    return rows == sequenceRows_.end() || rows->second.empty() ? std::string{}
        : lower(std::string(sequences_.value(rows->second.front(), "mode")));
}
ObjectPresentation ActorAnimationCatalog::objectPresentation(int identity, int serverMode, float elapsed) const {
    const auto row = objectRows_.find(identity);
    return row == objectRows_.end() ? ObjectPresentation{serverMode, elapsed, true}
        : d2x::objectPresentation(objects_, row->second, serverMode, elapsed);
}
const ActorAnimation *ActorAnimationCatalog::object(Graphics &graphics, int palette, int identity, int mode) {
    constexpr std::array modes{"nu","op","on","s1","s2","s3","s4","s5"};
    if (mode < 0 || size_t(mode) >= modes.size()) return nullptr;
    const auto row = objectRows_.find(identity);
    if (row == objectRows_.end() || !objects_.number(row->second, "Draw").value_or(0)) return nullptr;
    const auto suffix = std::to_string(mode);
    if (!objects_.number(row->second, "Mode" + suffix).value_or(0)) return nullptr;
    const auto key = "object:" + std::to_string(palette) + ":" + std::to_string(identity) + ":" + suffix;
    if (const auto found = animations_.find(key); found != animations_.end())
        return found->second.ready() ? &found->second : nullptr;
    ActorAnimationRequest request; request.category = "objects"; request.mode = modes[size_t(mode)];
    // The shared compositor already masks shadows using the original COF layers.
    request.shadow = true;
    request.appearance.token = lower(std::string(objects_.value(row->second, "Token")));
    request.appearance.weapon = "hth"; request.appearance.components.fill("lit");
    const auto base = "data/global/objects/" + request.appearance.token + "/";
    const auto cof = base + "cof/" + request.appearance.token + request.mode + "hth.cof";
    ActorAnimation result;
    if (archives_.contains(cof)) result = composite(graphics, request);
    else {
        const auto path = base + "tr/" + request.appearance.token + "trlit" + request.mode + "hth";
        if (archives_.contains(path + ".dcc")) result.animation = graphics.single(path + ".dcc");
        else if (archives_.contains(path + ".dc6")) result.animation = graphics.single(path + ".dc6");
    }
    result.fps = float(objects_.number(row->second, "FrameDelta" + suffix).value_or(0)) * 25.f / 256.f;
    result.start = std::max(0, objects_.number(row->second, "Start" + suffix).value_or(0));
    const int declared = objects_.number(row->second, "FrameCnt" + suffix).value_or(0);
    result.frameCount = std::max(0, result.animation.count - result.start);
    if (declared > 0) result.frameCount = std::min(result.frameCount, declared);
    result.cycle = objects_.number(row->second, "CycleAnim" + suffix).value_or(0) != 0;
    result.offset = {float(objects_.number(row->second, "Xoffset").value_or(0)),
                     float(objects_.number(row->second, "Yoffset").value_or(0))};
    result.order = objects_.number(row->second, "DrawUnder").value_or(0) ? 1
        : objects_.number(row->second, "OrderFlag" + suffix).value_or(0);
    const auto entry = animations_.emplace(key, std::move(result)).first;
    graphics.releaseDecoded();
    return entry->second.ready() ? &entry->second : nullptr;
}
} // namespace d2x
