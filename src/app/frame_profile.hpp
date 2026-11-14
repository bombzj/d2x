#pragma once
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <span>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace d2x {
// Opt-in, bounded presentation diagnostics. No game state or worker is owned.
class FrameProfile {
    using Clock=std::chrono::steady_clock;
    std::ofstream output_;
    std::string path_,error_;
    Clock::time_point started_=Clock::now(),sampled_{};
    nlohmann::json previousMemory_;
    std::function<nlohmann::json()> context_;
    uint64_t events_{},slow_{},bytes_{};
    double maximum_{};
    std::vector<double> intervalSums_,intervalMaxima_;
    uint64_t intervalFrames_{},intervalOverBudget_{},intervalSlow_{};
    double intervalTotal_{},intervalMaximum_{};
    static nlohmann::json processSample();
  public:
    explicit FrameProfile(const std::string &,std::function<nlohmann::json()>);
    void record(uint64_t frame,std::span<const double>,std::span<const char *const>) noexcept;
    nlohmann::json summary() const;
};
}
