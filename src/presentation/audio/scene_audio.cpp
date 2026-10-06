#include "scene_audio.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x {
SceneAudio::SceneAudio(Archives &archives, const SoundCatalog &catalog, SoundBank &bank)
    : archives_(archives), catalog_(catalog), bank_(bank) {
    // Stable UI semantics select original Sounds identities, never loose WAVs.
    for (const auto &[key, sound] : {std::pair{"drink","item_potion_drink"},
        std::pair{"belt","item_belt"}, std::pair{"quest_done","cursor_questdone"}})
        registerSound(sound,key);
}
bool SceneAudio::registerSound(std::string_view name, std::string key, bool travel) {
    if (name.empty()) return true;
    if (aliases_.contains(key)) return true;
    const auto row = catalog_.soundRow(name);
    if (!row) { limitations_.insert("Original Sounds definition unavailable: " + std::string(name)); return false; }
    const bool loop = travel && catalog_.sounds().number(*row,"Loop") == 1;
    const std::string canonical = loop ? key : "original:" + std::string(name);
    if (unavailable_.contains(canonical)) return false;
    if (registered_.contains(canonical)) { aliases_.emplace(std::move(key),canonical); return true; }
    try {
        if (loop) bank_.registerTravelGroup(archives_,canonical,catalog_.sounds(),*row);
        else bank_.registerOriginalGroup(archives_,canonical,catalog_.sounds(),*row);
        registered_.insert(canonical); aliases_.emplace(std::move(key),canonical);
        return true;
    } catch (const std::exception &error) {
        unavailable_.insert(canonical);
        limitations_.insert("Original sound unavailable: " + std::string(name) + " (" + error.what() + ")");
        return false;
    }
}
void SceneAudio::playRegistered(const std::string &key, uint64_t frame) {
    if (frame == UINT64_MAX) frame = uint64_t(std::max(0.f,clock_) * 25.f);
    if (const auto found = aliases_.find(key); found != aliases_.end()) bank_.play(found->second,frame);
}
bool SceneAudio::play(std::string_view sound, uint64_t frame, float volume) {
    const std::string key = "original:" + std::string(sound);
    if (!registerSound(sound,key)) return false;
    bank_.play(key,frame,volume); return true;
}
void SceneAudio::enqueue(const SoundRule &rule, const SoundActorView &source, float age, float extraDelay) {
    if (rule.sound.empty() || rule.volume == 0 || rule.probability <= 0) return;
    if (rule.probability < 100 && limitedRandom(random_,100) >= unsigned(rule.probability)) return;
    pending_.push_back({rule,source.id,rule.interruptible ? source.actionRevision : 0,clock_ + rule.delay + extraDelay - age});
    while (pending_.size() > 256) pending_.pop_front();
}
void SceneAudio::reset() {
    pending_.clear(); cadence_.clear(); clock_ = -1;
    bank_.resetEmitters();
}
void SceneAudio::update(float clock, std::span<const SoundActorView> actors, std::span<const PresentationSoundEvent> events) {
    if (clock_ >= 0 && (clock < clock_ || clock - clock_ > .25f)) {
        pending_.clear(); cadence_.clear();
    }
    clock_ = clock;
    const auto actor = [&](EntityId id) -> const SoundActorView * {
        const auto found = std::find_if(actors.begin(),actors.end(),[&](const auto &value) { return value.id == id; });
        return found == actors.end() ? nullptr : &*found;
    };
    for (const auto &event : events) {
        const auto *source = actor(event.source);
        if (!source || event.age > .25f) continue;
        auto submit = [&](const SoundRule &rule, float delay = 0) {
            auto stamped = *source; stamped.actionRevision = event.actionRevision;
            enqueue(rule,stamped,event.age,delay);
        };
        using Kind = PresentationSoundEvent::Kind;
        if (event.kind == Kind::Cast) {
            if (const auto *skill = catalog_.skill(event.skill)) {
                submit(skill->start);
                if (event.releaseTime >= 0) submit(skill->active,event.releaseTime);
            }
        } else if (event.kind == Kind::LevelUp) submit(SoundRule{"cursor_level_up"});
        else if (source->kind == SoundActorKind::Player) {
            if (const auto *profile = catalog_.player(source->identity)) {
                if (event.kind == Kind::Hit) submit(profile->hit);
                else if (event.kind == Kind::Death) submit(profile->death);
            }
        } else if (const auto *profile = catalog_.monster(source->identity)) {
            if (event.kind == Kind::Attack1 || event.kind == Kind::Attack2) {
                const size_t hand = event.kind == Kind::Attack2 ? 1 : 0;
                submit(profile->attacks[hand]); submit(profile->weapons[hand]);
            } else if (event.kind == Kind::Hit) submit(profile->hit);
            else if (event.kind == Kind::Death) submit(profile->death);
            else if (event.kind >= Kind::Skill1 && event.kind <= Kind::Skill4)
                submit(profile->skills[size_t(event.kind) - size_t(Kind::Skill1)]);
        } else limitations_.insert("Original MonSounds definition unavailable for identity " + std::to_string(source->identity));
    }
    std::erase_if(cadence_,[&](const auto &entry) { return !actor(entry.first); });
    for (const auto &source : actors) {
        auto &cadence = cadence_[source.id];
        if (!source.alive || source.frozen || !source.audible) { cadence = {}; continue; }
        const auto *profile = source.kind == SoundActorKind::Monster ? catalog_.monster(source.identity) : nullptr;
        if (!profile) continue; // Player material/armor footstep choice has not been established.
        if (source.moving && source.movementCycle > 0 && profile->footstepCount > 0) {
            if (profile->footstepOffset != 0) {
                if (!profile->footstep.sound.empty() || !profile->footstepLayer.sound.empty())
                    limitations_.insert("Original footstep FsOff is not established for identity " + std::to_string(source.identity));
            } else {
                if (!cadence.moving || cadence.cycle != source.movementCycle) cadence.nextStep = clock;
                if (clock >= cadence.nextStep) {
                    // Layer and ground share one footstep probability decision.
                    auto step = profile->footstep, layer = profile->footstepLayer;
                    if (step.probability > 0 && (step.probability == 100 || limitedRandom(random_,100) < unsigned(step.probability))) {
                        step.probability = layer.probability = 100;
                        enqueue(step,source); enqueue(layer,source);
                    }
                    cadence.nextStep = clock + source.movementCycle / profile->footstepCount;
                }
            }
        } else if (source.neutral && profile->neutralInterval > 0) {
            if (!cadence.neutral) cadence.nextNeutral = clock + profile->neutralInterval;
            if (clock >= cadence.nextNeutral) {
                enqueue(profile->neutral,source);
                cadence.nextNeutral = clock + profile->neutralInterval;
            }
        }
        cadence.moving = source.moving; cadence.neutral = source.neutral; cadence.cycle = source.movementCycle;
    }
    std::erase_if(pending_,[&](const Pending &sound) {
        const auto *source = actor(sound.source);
        if (!source || (sound.rule.interruptible && source->frozen) ||
            (sound.actionRevision && sound.actionRevision != source->actionRevision)) return true;
        if (clock < sound.due) return false;
        if (clock - sound.due < .25f && source->audible) play(sound.rule.sound,uint64_t(std::max(0.f,clock) * 25.f),sound.rule.volume);
        return true;
    });
}
} // namespace d2x
