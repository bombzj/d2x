#include "app/account_login.hpp"
#include "app/frontend_config.hpp"
#include "app/online_login_memory.hpp"
#include "content/language.hpp"
#include "presentation/frontend/realm_frontend.hpp"
#include <utility>

namespace d2x {
void submitAccountLogin(const std::filesystem::path &path, net::protocol::ClientProfile profile,
                        const Language &language, net::RealmSession &session, RealmFrontend &ui,
                        OnlineLoginMemory &memory, FrontendIntent &action, std::string &gateway,
                        std::string &notice, std::string_view profileDigest) {
    try {
        auto config = configuration(path, profile, profileDigest);
        struct Wipe { net::LoginOptions &login; ~Wipe(){
            net::protocol::erase_secret(login.password);
            net::protocol::erase_auth_secrets(login.originalClient);
        }} wipe{config.login};
        gateway = std::move(config.gateway);
        const bool remembered = memory.write(action.name, action.password);
        ui.setLogin(action.name, action.password);
        config.login.account = std::move(action.name);
        config.login.password = std::move(action.password);
        config.login.localeId = language.localeId;
        config.login.languageCode = language.languageCode;
        config.login.gameLocale = language.gameLocale;
        notice.clear();
        if (action.command == FrontendCommand::Register) session.register_account(std::move(config.login));
        else session.login(std::move(config.login));
        if (!remembered) notice = "Login submitted, but login information could not be remembered.";
    } catch (...) {
        net::protocol::erase_secret(action.password);
        throw;
    }
}
}
