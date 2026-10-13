#pragma once
#include "game_content.hpp"
#include <memory>

namespace d2x::hosting {
// Owns independent MPQ handles/caches. Only immutable requests/results cross
// the worker boundary; entity IDs and gameplay admission stay on the host.
class AreaPreparation {
    struct Impl;
    std::unique_ptr<Impl> impl_;
  public:
    struct Result { std::optional<PreparedWorldArea> area; std::string error; };
    AreaPreparation(const Archives &, std::shared_ptr<const ClassicData>);
    ~AreaPreparation();
    uint64_t submit(AreaGenerationRequest);
    std::optional<Result> take(uint64_t);
    void cancel(uint64_t);
};
}
