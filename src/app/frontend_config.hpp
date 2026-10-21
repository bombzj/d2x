#pragma once
#include "network/realm_session.hpp"
#include "app/client_setup.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <array>
#include <nlohmann/json.hpp>

namespace d2x {
struct OnlineConfiguration {
    net::LoginOptions login;
    std::string gateway;
};
inline OnlineConfiguration configuration(const std::filesystem::path &path,
    net::protocol::ClientProfile profile = net::protocol::ClientProfile::Lod113c,
    std::string_view profileDigest = {}) {
    // Keep keys out of argv, logs, views, saves and committed configuration.
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Online login configuration is unavailable.");
    nlohmann::json json;
    OnlineConfiguration result;
    std::string preparationError;
    try {
        if(profile == net::protocol::ClientProfile::Pd2CnNormal20261020)
            verifyClientProfileDefinition(path,profileDigest);
        file >> json;
        if(profile == net::protocol::ClientProfile::Pd2CnNormal20261020)
            verifyClientProfileDefinition(path,profileDigest);
        const auto port = json.value("accountPort", 6112), gamePort = json.value("gamePort", profile == net::protocol::ClientProfile::Lod113c ? 4000 : 4001);
        if (port < 1 || port > 65535 || gamePort < 1 || gamePort > 65535)
            throw std::runtime_error("Invalid port");
        result.login.accountServer = {json.at("accountHost").get<std::string>(), uint16_t(port)};
        result.login.gamePort = uint16_t(gamePort);
        result.gateway = json.at("gateway").get<std::string>();
        auto directory = std::filesystem::path(json.at("originalClientDirectory").get<std::string>());
        if (directory.is_relative())
            directory = path.parent_path() / directory;
        if (profile == net::protocol::ClientProfile::Pd2CnNormal20261020) {
            if(json.value("version",0)!=1 || json.value("clientProfile",std::string{})!="pd2-cn-normal-20261020" ||
               json.value("authentication",std::string{})!="pd2-cn" || result.login.accountServer.host.empty())
                throw std::runtime_error("Mismatched client profile");
            if (json.contains("gameServer") && !json.at("gameServer").is_number_integer())
                throw std::runtime_error("Invalid PD2 game server selection");
            const auto gameServer = json.value("gameServer", 1);
            if (gameServer < 1 || gameServer > net::protocol::clientCompatibility(profile).maximumGameServer)
                throw std::runtime_error("Invalid PD2 game server selection (expected GS1-GS9)");
            result.login.gameServer = uint8_t(gameServer);
            result.login.lootAllocation = json.value("lootAllocation", true);
            auto pd2=std::filesystem::path(json.at("pd2Directory").get<std::string>());
            if(pd2.is_relative()) pd2=path.parent_path()/pd2;
            if(!std::filesystem::equivalent(pd2.parent_path(),directory))
                throw std::runtime_error("Mismatched client resource roots");
            try {
                verifyPd2AuthenticationFiles(pd2);
                preparePd2Authentication(result.login.originalClient,pd2,directory);
            } catch(const std::exception &error) {preparationError=error.what();throw;}
            return result;
        }
        constexpr std::array names{"Game.exe", "Bnclient.dll", "D2Client.dll"};
        for (size_t i = 0; i < names.size(); ++i) {
            result.login.originalClient.files[i] = directory / names[i];
            if (!std::filesystem::is_regular_file(result.login.originalClient.files[i]))
                throw std::runtime_error("Missing original file");
        }
        const auto authentication = json.value("authentication", std::string("pvpgn"));
        if (authentication != "pvpgn" && authentication != "keys")
            throw std::runtime_error("Unsupported authentication mode");
        result.login.originalClient.submitKeys = authentication == "keys";
        if (result.login.originalClient.submitKeys) {
            result.login.originalClient.classicKey = json.at("classicKey").get<std::string>();
            result.login.originalClient.expansionKey = json.at("expansionKey").get<std::string>();
        }
        result.login.originalClient.owner = json.value("keyOwner", std::string("D2X"));
        if ((result.login.originalClient.submitKeys && (result.login.originalClient.classicKey.empty() ||
                                                        result.login.originalClient.expansionKey.empty())) ||
            result.gateway.empty() || result.login.accountServer.host.empty())
            throw std::runtime_error("Missing configuration");
        for (auto key : {"classicKey", "expansionKey"})
            if (json.contains(key) && json[key].is_string())
                net::protocol::erase_secret(json[key].get_ref<std::string &>());
        return result;
    } catch (...) {
        for (auto key : {"classicKey", "expansionKey"})
            if (json.contains(key) && json[key].is_string())
                net::protocol::erase_secret(json[key].get_ref<std::string &>());
        net::protocol::erase_auth_secrets(result.login.originalClient);
        throw std::runtime_error(preparationError.empty() ? "Online login configuration is incomplete or invalid." : preparationError);
    }
}
} // namespace d2x
