#pragma once
#include <string>
#include <cstdint>
namespace d2x {
struct AppOptions {
    std::string mpq = "assets/mpq2";
    std::string screenshot, pack, debugPipe;
    std::string onlineConfig = "online.local.json";
    std::string onlineCharacter, onlineCreateGame, onlineJoinGame;
    std::string onlinePlay;
    std::string load, save, characterClass;
    std::string hostLan, lan, hostSaves = "saves";
    uint16_t realmPort = 6113, gamePort = 4001;
    bool hidden = false, help = false;
    int frameLimit = 0;
};
AppOptions parseOptions(int argc, char **argv);
} // namespace d2x
