#include "app/account_lobby.hpp"
#include "app/account_login.hpp"
#include "app/frontend_config.hpp"
#include "app/debug/debug_pipe.hpp"
#include "app/debug/ui_input.hpp"
#include "app/input.hpp"
#include "app/online_login_memory.hpp"
#include "app/options.hpp"
#include "app/startup_profile.hpp"
#include "content/language.hpp"
#include "network/realm_session.hpp"
#include "presentation/frontend/realm_frontend.hpp"
#include "presentation/graphics/primitives.hpp"
#include <nlohmann/json.hpp>
#include <rlgl.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <deque>
#include <limits>
#include <stdexcept>
#include <utility>

namespace d2x {
namespace {
constexpr std::array stageNames{"Idle","ConnectingAccount","AuthChallenge","AuthenticatingVersion",
    "LoggingIn","ListingRealms","RealmSelection","ConnectingRealm","RealmStartup","ListingCharacters",
    "CharacterSelection","SelectingCharacter","Lobby","ListingGames","CreatingGame","JoiningGame",
    "ConnectingGame","GameHandshake","LoadingGame","ProtocolReady","LeavingGame","Failed","Cancelled",
    "CreatingAccount","CreatingCharacter","DeletingCharacter"};
nlohmann::json lobbyStatus(const OnlineView &view) {
    nlohmann::json result{{"stage",stageNames.at(size_t(view.stage))},{"clientProfile",view.protocolProfile},
        {"remoteLobbyOnly",view.remoteLobbyOnly},{"selectedRealm",view.selectedRealm},{"selectedCharacter",view.selectedCharacter},
        {"gameListComplete",view.gameListComplete},{"revision",view.revision},{"connectionGeneration",view.connectionGeneration},
        {"realms",nlohmann::json::array()},{"characters",nlohmann::json::array()},{"games",nlohmann::json::array()}};
    for(const auto &realm:view.realms) result["realms"].push_back({{"name",realm.name},{"description",realm.description}});
    for(const auto &character:view.characters) {
        nlohmann::json value{{"name",character.name}};
        if(character.characterClass) value["classId"]=*character.characterClass;
        if(character.level) value["level"]=*character.level;
        if(character.ladder) value["ladder"]=*character.ladder;
        result["characters"].push_back(std::move(value));
    }
    for(const auto &game:view.games) result["games"].push_back({{"name",game.name},{"players",game.players},{"flags",game.flags}});
    if(view.error) result["error"]={{"message",view.error->message},{"packet",view.error->packetId},
        {"serverCode",view.error->serverCode},{"sequence",view.error->sequence}};
    if(view.versionChallenge) result["versionChallenge"]={{"logonType",view.versionChallenge->logonType},
        {"revisionModule",view.versionChallenge->revisionModule}};
    auto counts=[](const OnlineProtocolCounters &counters) {
        nlohmann::json value{{"receivedBytes",counters.receivedBytes},{"sentBytes",counters.sentBytes}};
        for(size_t i=0;i<256;++i) if(counters.received[i] || counters.sent[i] || counters.unconsumed[i])
            value["packets"][std::to_string(i)]={{"received",counters.received[i]},{"sent",counters.sent[i]},
                {"unconsumed",counters.unconsumed[i]}};
        return value;
    };
    result["sidProtocol"]=counts(view.sidProtocol);result["mcpProtocol"]=counts(view.mcpProtocol);
    return result;
}
}
void runAccountLobby(Archives &archives, RenderTexture2D &target, const AppOptions &options, StartupProfile &profile) {
    // A remote-only composition root: no EmbeddedRealm, local character store,
    // world, item or combat adapter is constructed on the PD2 lobby path.
    RealmFrontend ui(archives, RealmPortraitSupport::Unverified);
    profile.mark("frontend-assets-ready");
    net::RealmSession session(options.protocolProfile);
    const auto language=languageOf(archives);
    const auto configPath=std::filesystem::path(options.onlineConfig);
    OnlineLoginMemory memory(configPath);
    std::string account,password;
    memory.read(account,password);ui.setLogin(std::move(account),std::move(password));
    FrontendPage page=FrontendPage::Login;
    std::string notice,gateway;
    uint64_t dismissed=std::numeric_limits<uint64_t>::max();
    bool quit=false,manualRealm=false,quickCharacter=!options.onlineCharacter.empty();
    DebugPipe pipe(options.debugPipe);
    auto submit=[&](FrontendIntent &action) {
        dismissed=std::numeric_limits<uint64_t>::max();manualRealm=false;
        submitAccountLogin(configPath,options.protocolProfile,language,session,ui,memory,action,gateway,notice,options.clientProfileDigest);
    };
    if(quickCharacter) {
        FrontendIntent action;action.command=FrontendCommand::Login;
        memory.read(action.name,action.password);
        if(action.name.empty() || action.password.empty()) {
            net::protocol::erase_secret(action.password);quickCharacter=false;
            notice="Log in once through the UI before using --online-character.";
        } else try {submit(action);} catch(const std::exception &error){notice=error.what();quickCharacter=false;}
    }
    int frames=0;
    std::deque<FrameInput> debugInputs;
    HideCursor();
    while(!quit || pipe.responsePending()) {
        if(WindowShouldClose() || (options.frameLimit && frames++>=options.frameLimit)) quit=true;
        session.tick();
        pipe.poll([&](const std::string &request) {
            nlohmann::json command;
            struct ErasePassword { nlohmann::json &json; ~ErasePassword(){
                if(json.is_object() && json.contains("password") && json["password"].is_string())
                    net::protocol::erase_secret(json["password"].get_ref<std::string &>());
            }} erasePassword{command};
            try {
                command=nlohmann::json::parse(request);
                const auto operation=command.at("command").get<std::string>();
                if(command.contains("connectionGeneration") && command.at("connectionGeneration").get<uint64_t>()!=session.read().connectionGeneration)
                    throw std::invalid_argument("Expired connection generation");
                bool accepted=true;
                if(operation=="quit") quit=true;
                else if(operation=="online-status" || operation=="status" || operation=="online-realms" ||
                        operation=="online-characters" || operation=="online-games") {}
                else if(operation=="online-login") {
                    const auto stage=session.read().stage;
                    if(stage!=OnlineStage::Idle && stage!=OnlineStage::Failed && stage!=OnlineStage::Cancelled)
                        throw std::invalid_argument("Logout before starting another account login");
                    auto config=configuration(configPath,options.protocolProfile,options.clientProfileDigest);
                    struct EraseLogin { net::LoginOptions &login; ~EraseLogin(){
                        net::protocol::erase_secret(login.password);net::protocol::erase_auth_secrets(login.originalClient);
                    }} eraseLogin{config.login};
                    config.login.account=command.at("account").get<std::string>();
                    config.login.password=command.at("password").get<std::string>();
                    config.login.localeId=language.localeId;config.login.languageCode=language.languageCode;
                    config.login.gameLocale=language.gameLocale;gateway=std::move(config.gateway);
                    manualRealm=false;notice.clear();dismissed=std::numeric_limits<uint64_t>::max();
                    session.login(std::move(config.login));accepted=session.read().stage!=OnlineStage::Failed;
                }
                else if(operation=="online-cancel" || operation=="online-logout") {
                    session.logout();page=FrontendPage::Login;ui.clearTransientPasswords();notice.clear();debugInputs.clear();
                } else if(operation=="online-select-realm") {
                    gateway=command.at("name").get<std::string>();accepted=session.choose_realm(gateway);
                } else if(operation=="online-select-character") accepted=session.select_character(command.at("name").get<std::string>());
                else if(operation=="online-list-games") accepted=session.list_games(command.value("filter",std::string{}));
                else if(operation=="online-return-characters") accepted=session.return_to_characters();
                else if(operation=="online-return-realms") {manualRealm=true;accepted=session.return_to_realms();}
                else if(operation=="online-cancel-list") accepted=session.cancel_game_list();
                else if(operation=="ui-input") {
                    auto inputs=parseDebugInput(command);
                    if(debugInputs.size()+inputs.size()>32) throw std::invalid_argument("The UI input queue exceeds 32 frames");
                    for(auto &input:inputs) debugInputs.push_back(std::move(input));
                } else if(operation=="screenshot") {
                    const auto path=std::filesystem::path(command.at("path").get<std::string>());
                    if(path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
                    auto capture=LoadImageFromTexture(target.texture);ImageFlipVertical(&capture);
                    const bool saved=ExportImage(capture,path.string().c_str());UnloadImage(capture);
                    if(!saved) throw std::runtime_error("Screenshot could not be written");
                }
                else throw std::invalid_argument("Command is unavailable in the remote account lobby");
                return nlohmann::json{{"ok",accepted},{"online",lobbyStatus(session.read())}}.dump();
            } catch(const std::runtime_error &error) {
                return nlohmann::json{{"ok",false},{"error",error.what()}}.dump();
            } catch(...) {return nlohmann::json{{"ok",false},{"error","Invalid or unavailable lobby command"}}.dump();}
        });
        if(quit && !pipe.responsePending()) break;
        auto view=session.read();
        if(view.stage==OnlineStage::RealmSelection) {
            auto match=std::find_if(view.realms.begin(),view.realms.end(),[&](const auto &realm){return realm.name==gateway;});
            if(!manualRealm && match!=view.realms.end()) session.choose_realm(match->name);
            else {manualRealm=true;page=FrontendPage::Realms;}
        }
        if(view.error) quickCharacter=false;
        if(quickCharacter && view.stage==OnlineStage::CharacterSelection) {
            quickCharacter=false;session.select_character(options.onlineCharacter);
        }
        view=session.read();
        if(view.stage==OnlineStage::CharacterSelection) page=FrontendPage::Characters;
        if(view.stage==OnlineStage::Lobby || view.stage==OnlineStage::ListingGames) page=FrontendPage::Lobby;
        if(view.error && view.error->sequence!=dismissed && notice.empty())
            notice=view.error->message+" (packet "+std::to_string(view.error->packetId)+", code "+std::to_string(view.error->serverCode)+")";
        W=std::max(1,GetScreenWidth());H=std::max(1,GetScreenHeight());
        if(target.texture.width!=W || target.texture.height!=H) {
            UnloadRenderTexture(target);target=LoadRenderTexture(W,H);
            if(!target.id) throw std::runtime_error("Unable to resize lobby render target");
            SetTextureFilter(target.texture,TEXTURE_FILTER_POINT);
        }
        auto input=pollInput(currentViewport());
        if(!debugInputs.empty()){input=std::move(debugInputs.front());debugInputs.pop_front();}
        const float sx=float(W)/800.f,sy=float(H)/600.f;
        uiScale=1;uiScaleX=sx;uiScaleY=sy;
        BeginTextureMode(target);ClearBackground(BLACK);
        uiScissor(0,0,800,600);rlPushMatrix();rlScalef(sx,sy,1);
        auto action=ui.frame(page,view,gateway,notice,{input.mouse.x/sx,input.mouse.y/sy},input,true);
        struct EraseIntent { FrontendIntent &intent; ~EraseIntent(){net::protocol::erase_secret(intent.password);} } eraseIntent{action};
        rlPopMatrix();EndScissorMode();EndTextureMode();uiScaleX=uiScaleY=1;
        BeginDrawing();ClearBackground(BLACK);
        DrawTexturePro(target.texture,{0,0,float(W),-float(H)},{0,0,float(GetScreenWidth()),float(GetScreenHeight())},{0,0},0,WHITE);
        EndDrawing();profile.mark("first-presented-frame");
        if(quit) continue;
        if(action.editedAccount && !memory.write(*action.editedAccount,{})) notice="Unable to remember the edited account name.";
        if(action.command!=FrontendCommand::None) quickCharacter=false;
        try {
            switch(action.command) {
            case FrontendCommand::None: break;
            case FrontendCommand::Exit: quit=true;break;
            case FrontendCommand::Login:
            case FrontendCommand::Register: submit(action);break;
            case FrontendCommand::OpenRegister: ui.clearPassword();notice.clear();page=FrontendPage::Register;break;
            case FrontendCommand::ChangeRealm: manualRealm=true;session.return_to_realms();break;
            case FrontendCommand::SelectRealm: gateway=action.name;session.choose_realm(std::move(action.name));break;
            case FrontendCommand::SelectCharacter: session.select_character(std::move(action.name));break;
            case FrontendCommand::ListGames: session.list_games();break;
            case FrontendCommand::CancelList: session.cancel_game_list();break;
            case FrontendCommand::Dismiss:
                notice.clear();dismissed=view.error ? view.error->sequence : std::numeric_limits<uint64_t>::max();
                if(view.stage==OnlineStage::Failed){session.logout();page=FrontendPage::Login;}break;
            case FrontendCommand::Back:
                notice.clear();ui.clearTransientPasswords();
                if(page==FrontendPage::Lobby) {
                    if(view.stage==OnlineStage::ListingGames) session.cancel_game_list();
                    session.return_to_characters();
                }
                else if(page==FrontendPage::Login && (view.stage==OnlineStage::Idle || view.stage==OnlineStage::Cancelled)) quit=true;
                else {session.logout();page=FrontendPage::Login;}
                break;
            default: notice="This PD2 CN profile currently supports login, character selection and the remote lobby.";break;
            }
        } catch(const std::exception &error){notice=error.what();}
        net::protocol::erase_secret(action.password);
    }
    ui.clearTransientPasswords();ui.clearPassword();session.logout();
}
}
