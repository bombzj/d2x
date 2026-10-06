#pragma once
#include <filesystem>
#include <string>
#include <utility>

namespace d2x {
// Application-owned login preference. Never part of the protocol view or character save.
class OnlineLoginMemory {
    std::filesystem::path configuration_;
  public:
    explicit OnlineLoginMemory(std::filesystem::path configuration) : configuration_(std::move(configuration)) {}
    bool read(std::string &account, std::string &password) const;
    bool write(const std::string &account, const std::string &password) const;
};
}
