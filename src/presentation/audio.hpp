#pragma once
#include "resources/archive.hpp"
#include <map>
#include <string>
#include <string_view>
#include <raylib.h>
namespace d2x {
class SoundBank {
    std::map<std::string, Sound> sounds;
    bool enabled = false;

  public:
    bool muted = false;
    explicit SoundBank(Archives &archives);
    ~SoundBank();
    SoundBank(const SoundBank &) = delete;
    SoundBank &operator=(const SoundBank &) = delete;
    void play(const std::string &name);
    void registerOriginal(Archives &archives, std::string key, std::string_view path);
};
} // namespace d2x
