#pragma once
#include "server/player_identity.hpp"
#include "gameplay/character/persistent_character.hpp"
#include "gameplay/quest/acts/act_two_runtime.hpp"
#include "gameplay/quest/id.hpp"
#include <array>
#include <optional>
#include <cstddef>
#include <cstdint>

namespace d2x::server {
enum class QuestPublication {Transition,Query,Private,Global};
struct QuestFact { PlayerId player; CharacterRecord record; int difficulty{}; unsigned remaining{}; std::array<int,5> stones{}; std::optional<uint16_t> tombOffset{}; std::optional<bool> eclipse{}; std::optional<uint8_t> staffStatus{}; std::optional<uint8_t> khalimStatus{}; unsigned rescued{}; std::array<uint16_t,48> gameFlags{}; std::array<bool,size_t(QuestId::Count)> unavailable{}, credited{}, entitled{}; bool fullStatus{}; std::optional<uint16_t> zooMonster{}; bool zooOnly{}; bool denEffect{}; QuestPublication publication{};std::array<ActTwoQuestRuntime,6> actTwoRuntime{};bool nativeActTwo{};std::optional<uint8_t> iceStatus{},baalStatus{}; bool figurineDropped{}; };
}
