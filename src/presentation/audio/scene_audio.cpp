#include <exception>
#include "scene_audio.hpp"
#include "scene_streams.hpp"
#include "core/random.hpp"
#include "contracts/online_world.hpp"
#include "gameplay/quest/catalog.hpp"
#include <algorithm>
#include <random>

namespace d2x {
const std::set<std::string, std::less<>> &SceneAudio::limitations() const { return limitations_; }
SceneAudio::SceneAudio(Archives &archives, const SoundCatalog &catalog, SoundBank &bank,SceneStreams &streams)
        : archives_(archives), catalog_(catalog), bank_(bank),streams_(streams),
            greetingRandom_(initialRandom(std::random_device{}())) {
    lastNarrativeAt_=GetTime();
    // Stable UI semantics select original Sounds identities, never loose WAVs.
    for (const auto &[key, sound] : {std::pair{"drink","item_potion_drink"},
        std::pair{"belt","item_belt"}, std::pair{"quest_done","cursor_questdone"},
        std::pair{"item_flip","item_flippy"}})
        registerSound(sound,key);
    for (const auto *sound : {"cursor_pass", "cursor_select", "cursor_switch", "cursor_button_click",
                              "cursor_point_drop", "cursor_identify_ready", "cursor_identify_item"})
        registerSound(sound,"original:" + std::string(sound));
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
    if (const auto found = aliases_.find(key); found != aliases_.end())
        bank_.play(found->second,frame,-1,key.starts_with("missile-") || key=="item_flip" || key=="monster-shatter");
}
bool SceneAudio::play(std::string_view sound, uint64_t frame, float volume, bool transient) {
    const std::string key = "original:" + std::string(sound);
    if (!registerSound(sound,key)) return false;
    bank_.play(key,frame,volume,transient); return true;
}
bool SceneAudio::playNativeSound(int row,uint64_t frame) {
    if(row<=0 || size_t(row)>=catalog_.sounds().rows().size()) return false;
    return play(catalog_.sounds().value(size_t(row),"Sound"),frame);
}
void SceneAudio::enqueue(const SoundRule &rule, const SoundActorView &source, float age, float extraDelay, bool transient) {
    if (rule.sound.empty() || rule.volume == 0 || rule.probability <= 0) return;
    if (rule.probability < 100 && limitedRandom(random_,100) >= unsigned(rule.probability)) return;
    pending_.push_back({rule,source.id,rule.interruptible ? source.actionRevision : 0,clock_ + rule.delay + extraDelay - age,transient});
    while (pending_.size() > 256) pending_.pop_front();
}
bool SceneAudio::npcReturnGreeting(int identity) {
    const auto *rule=catalog_.npcReturnGreeting(identity);
    if(!rule) return false;
    play(rule->sound,uint64_t(std::max(0.f,clock_)*25.f));
    return true; // Missing WAV is reported by registerSound; the native greeting is still acknowledged.
}
bool SceneAudio::npcGreeting(int identity, int mode, int period) {
    const auto sound=catalog_.npcGreeting(identity,mode,period,greetingRandom_);
    return !sound.empty() && play(sound,uint64_t(GetTime()*25));
}
void SceneAudio::playerNarrative(int identity, int event) {
    const auto voice=catalog_.playerNarrative(identity,event);
    if(voice.empty()) return;
    lastNarrativeAt_=GetTime();
    struct Cue {int event;const char *music;int start,end,voiceAt,resume;};
    static constexpr Cue cues[]{
        {33,"music_quest_andariel",0,475,425,0},
        {34,"music_quest_bloodraven",475,800,525,1},
        {35,"music_quest_den",0,300,250,1},
        {37,"",0,1500,0,0},
        {50,"music_quest_radament",475,1100,525,1},
        {52,"music_quest_tainted",0,425,375,1},
        {66,"music_quest_mephisto",0,425,375,0},
        {75,"music_quest_diablo",0,475,325,0},
        {80,"music_quest_shenk",0,500,475,1},
        {82,"music_quest_nihlathak",0,550,400,1},
        {83,"music_quest_baal",0,525,425,1}
    };
    for(const auto &cue:cues) if(cue.event==event) {
        streams_.questCue(cue.music,cue.start,cue.end,voice,cue.voiceAt,cue.resume);
        return;
    }
    if(!voice.empty()) play(voice,uint64_t(GetTime()*25));
}
void SceneAudio::enterLevel(uint64_t game, int identity, int level, const OnlineQuestState &quests) {
    if(narrativeGame_!=game) {
        narrativeGame_=game;enteredNarrativeGroups_.clear();narrativeLevel_=-1;lastNarrativeAt_=GetTime();
    }
    if(!quests.availability || !quests.playerFlags || !quests.gameFlags) return;
    if(narrativeLevel_==level) return;
    narrativeLevel_=level;
    struct Entry {std::array<int,10> levels;int quest,event;};
    static constexpr Entry entries[]{
        {{17},2,38},{{34,35,36,37},6,40},{{8},1,41},{{29,30,31},6,42},{{26,27},3,43},
        {{20,21,22,23,24,25},5,44},{{38},6,46},{{2,3,4,5,6,7},1,47},{{74},12,55},
        {{58},13,56},{{110},31,76},{{120},35,78},{{121},34,77},{{132},36,79}
    };
    for(size_t group=0;group<std::size(entries);++group) {
        const auto &entry=entries[group];
        if(std::find(entry.levels.begin(),entry.levels.end(),level)==entry.levels.end()) continue;
        if(!enteredNarrativeGroups_.insert(int(group)).second) return;
        if(!(*quests.availability)[size_t(entry.quest)]) return;
        const auto slot=nativeQuestInitializationSlots[size_t(entry.quest)];
        if(((*quests.gameFlags)[slot]&(1u<<13)) || ((*quests.playerFlags)[slot]&((1u<<1)|1u|(1u<<14)))) return;
        if(entry.quest==1 && (!quests.statuses[slot] || *quests.statuses[slot]>=5)) return;
        const auto speech=streams_.speechFinished();
        if(GetTime()-lastNarrativeAt_<=62.0/25.0 || (speech && !*speech)) return;
        playerNarrative(identity,entry.event);
        return;
    }
}
bool SceneAudio::itemDrop(const ItemDropSoundEvent &event) {
    if (event.kind == ItemDropSoundEvent::Kind::Flip) {
        if (event.audible && event.age <= .25f) playRegistered("item_flip");
        return true;
    }
    const auto definition = catalog_.item(event.code,event.quality,event.specialRow);
    if (!definition) {
        limitations_.insert("Original item sound definition unavailable: " + event.code);
        return true;
    }
    if (definition->drop.empty()) return true; // Intentional silent item.
    if (definition->dropFrame < 0) {
        limitations_.insert("Original item drop sound frame unavailable: " + event.code);
        return true;
    }
    const float due = definition->dropFrame / 25.f;
    if (event.age < due) return false;
    if (event.audible && event.age - due <= .25f)
        play(definition->drop,uint64_t(std::max(0.f,clock_) * 25.f),-1,true);
    return true;
}
void SceneAudio::itemPlacement(std::string_view code, ItemQuality quality, int specialRow) {
    if (const auto definition = catalog_.item(code,quality,specialRow); definition && !definition->drop.empty())
        play(definition->drop,uint64_t(GetTime()*25));
}
void SceneAudio::itemUse(std::string_view code, ItemQuality quality, int specialRow) {
    if (const auto definition=catalog_.item(code,quality,specialRow); definition && !definition->use.empty())
        play(definition->use,uint64_t(GetTime()*25));
}
void SceneAudio::playerFootstep(int identity, bool running, int material, int level, uint64_t frame) {
    const auto sound=catalog_.playerFootstep(identity,running,material,level);
    if (!sound.empty()) play(sound,frame,-1,true);
}
void SceneAudio::reset() {
    pending_.clear(); cadence_.clear(); clock_ = -1;
    bank_.resetEmitters();
}
void SceneAudio::update(float clock, std::span<const SoundActorView> actors, std::span<const PresentationSoundEvent> events) {
    for (auto &failure : bank_.takeFailures()) limitations_.insert("Original sound unavailable: " + failure);
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
        auto submit = [&](const SoundRule &rule, float delay = 0, bool transient = true) {
            auto stamped = *source; stamped.actionRevision = event.actionRevision;
            enqueue(rule,stamped,event.age,delay,transient);
        };
        using Kind = PresentationSoundEvent::Kind;
        if (event.kind == Kind::Original) {
            const auto name=event.nativeSound>=0 && size_t(event.nativeSound)<catalog_.sounds().rows().size()?
                catalog_.sounds().value(size_t(event.nativeSound),"Sound"):std::string_view(event.sound);
            submit(SoundRule{std::string(name)},0,false);
        }
        else if(event.kind==Kind::IzualComplete) {
            // 53070 -> 45570: sound-table music slot, immediate start,
            // 250 client frames, no voice and environment resume Block0.
            if(event.nativeSound>=0 && size_t(event.nativeSound)<catalog_.sounds().rows().size())
                streams_.questCue(catalog_.sounds().value(size_t(event.nativeSound),"Sound"),0,250,{},0,0);
        }
        else if (event.kind == Kind::ObjectMode) {
            if (const auto *sound = catalog_.object(source->identity,event.mode)) submit(*sound);
        }
        else if (event.kind == Kind::Cast) {
            if (const auto *skill = catalog_.skill(event.skill)) {
                submit(skill->start);
                if (event.releaseTime >= 0) submit(skill->active,event.releaseTime);
            }
        } else if (event.kind == Kind::LevelUp) submit(SoundRule{"cursor_level_up"});
        else if (source->kind == SoundActorKind::Player) {
            if (const auto *profile = catalog_.player(source->identity)) {
                if (event.kind == Kind::Hit) submit(profile->hit);
                else if (event.kind == Kind::Death) submit(profile->death);
                else if (event.kind == Kind::NeedKey) submit(profile->needKey);
                else if (event.kind == Kind::DenComplete)
                    streams_.questCue("music_quest_den",0,300,profile->denComplete.sound,250,1);
                else if (event.kind == Kind::NihlathakComplete)
                    streams_.questCue("music_quest_nihlathak",0,550,profile->nihlathakComplete.sound,400,1);
                else if (event.kind == Kind::Voice && event.mode>=25 && event.mode<=32) submit(profile->chat[size_t(event.mode-25)]);
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
        if (!profile) continue;
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
        if (clock - sound.due < .25f && source->audible) play(sound.rule.sound,uint64_t(std::max(0.f,clock) * 25.f),sound.rule.volume,sound.transient);
        return true;
    });
}
} // namespace d2x
