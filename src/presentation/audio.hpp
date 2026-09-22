#pragma once
#include "resources/archive.hpp"
#include <map>
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
};
} // namespace d2x
