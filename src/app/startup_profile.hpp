#pragma once
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace d2x {
// Optional diagnostic timing at real lifecycle boundaries, never a game rule.
class StartupProfile {
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
    std::ofstream output_;
    std::set<std::string, std::less<>> recorded_;
  public:
    explicit StartupProfile(const std::string &path) {
        if (path.empty()) return;
        if (std::filesystem::path(path).has_parent_path())
            std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        output_.open(path);
        if (!output_) throw std::runtime_error("Cannot open startup profile");
        output_ << "elapsed_ms\tphase\n";
    }
    void mark(std::string_view phase) {
        if (!output_.is_open() || !recorded_.emplace(phase).second) return;
        output_ << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started_).count()
                << '\t' << phase << '\n';
        output_.flush();
    }
};
}
