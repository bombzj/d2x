#pragma once
#include "app/frontend_config.hpp"
#include "app/options.hpp"
#include "content/language.hpp"
#include "app/online_login_memory.hpp"
#include "hosting/embedded_realm.hpp"
#include "network/realm_session.hpp"
#include "presentation/frontend/realm_frontend.hpp"
#include "presentation/input.hpp"
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <string>

namespace d2x {
// Mutable frontend state shared between the frame loop and command dispatch.
struct FrontendDispatchState {
    EmbeddedRealm *embedded;
    net::RealmSession &session;
    RealmFrontend &ui;
    OnlineLoginMemory &loginMemory;
    const AppOptions &options;
    const Language &lang;
    std::filesystem::path configPath;
    bool &localConnection;
    bool &enterLocalGame;
    bool &lanConnection;
    bool &lanListening;
    bool &lanJoiningGame;
    bool &tcpIpSaveFailed;
    bool &quit;
    bool &manualRealm;
    std::string &lanJoinAddress;
    std::string &reloadCharacter;
    std::string &notice;
    std::string &gateway;
    uint8_t &localDifficulty;
    bool &localDifficultyChosen;
    std::deque<FrameInput> &debugInputs;
    FrontendPage &page;
    uint64_t &dismissedErrorSequence;
    const std::function<std::string()> &prepareGame;
};
void dispatchFrontendCommand(FrontendDispatchState &state, FrontendIntent &action);
} // namespace d2x
