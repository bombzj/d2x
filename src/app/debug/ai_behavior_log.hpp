#pragma once
#include "contracts/online_scene.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace d2x {
class AiBehaviorLog {
    using Clock=std::chrono::steady_clock;
    std::ofstream output_;
    Clock::time_point start_,phaseStart_;
    std::string phase_,why_;
    size_t phaseGoal_{};
    std::optional<uint16_t> area_;
    std::optional<OnlinePoint> local_,host_;
  public:
    std::string path,error;
    void open(const std::filesystem::path &directory,uint64_t game);
    void observe(std::optional<uint16_t> area,std::optional<OnlinePoint> local,std::optional<OnlinePoint> host);
    void event(size_t goal,std::string_view name,std::string_view detail,uint64_t sequence=0);
    void phase(size_t goal,std::string_view name,std::string_view why);
    void endPhase();
    void close();
};
}
