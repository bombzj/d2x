#include "online_login_memory.hpp"
#include "network/protocol/auth.hpp"
#include <algorithm>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#include <windows.h>
#include <wincred.h>
#endif

namespace d2x {
namespace {
bool valid(std::string_view text) {
    return text.size() <= 15 && std::all_of(text.begin(), text.end(), [](unsigned char ch) { return ch >= 32 && ch <= 126; });
}
#ifdef _WIN32
std::wstring target(const std::filesystem::path &configuration) {
    return L"D2X/Online/" + std::filesystem::weakly_canonical(configuration).wstring();
}
#else
std::filesystem::path privateFile(const std::filesystem::path &configuration) {
    auto file = configuration; file += ".credentials.local"; return file;
}
#endif
}
bool OnlineLoginMemory::read(std::string &account, std::string &password) const {
    net::protocol::erase_secret(password); account.clear();
    try {
#ifdef _WIN32
        PCREDENTIALW record = nullptr;
        const auto key = target(configuration_);
        if (!CredReadW(key.c_str(), CRED_TYPE_GENERIC, 0, &record)) return false;
        auto freeRecord = [](CREDENTIALW *value) {
            if (value->CredentialBlob) SecureZeroMemory(value->CredentialBlob, value->CredentialBlobSize);
            CredFree(value);
        };
        const std::unique_ptr<CREDENTIALW, decltype(freeRecord)> guard(record, freeRecord);
        if (record->UserName) for (const wchar_t *ch = record->UserName; *ch; ++ch) account.push_back(char(*ch));
        if (record->CredentialBlobSize && record->CredentialBlob)
            password.assign(reinterpret_cast<const char *>(record->CredentialBlob), record->CredentialBlobSize);
#else
        const auto file = privateFile(configuration_);
        if (std::filesystem::is_symlink(file) || std::filesystem::file_size(file) > 4096) return false;
        std::error_code error;
        const auto permissions = std::filesystem::status(file, error).permissions();
        if (error || (permissions & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) != std::filesystem::perms::none)
            return false;
        std::ifstream input(file);
        auto json = nlohmann::json::parse(input);
        account = json.at("account").get<std::string>(); password = json.at("password").get<std::string>();
        net::protocol::erase_secret(json["password"].get_ref<std::string &>());
#endif
        if (valid(account) && valid(password)) return true;
    } catch (...) {}
    account.clear(); net::protocol::erase_secret(password); return false;
}
bool OnlineLoginMemory::write(const std::string &account, const std::string &password) const {
    if (!valid(account) || !valid(password)) return false;
    try {
#ifdef _WIN32
        const auto key = target(configuration_);
        if (account.empty()) return CredDeleteW(key.c_str(), CRED_TYPE_GENERIC, 0) != 0 || GetLastError() == ERROR_NOT_FOUND;
        const std::wstring user(account.begin(), account.end());
        CREDENTIALW record{};
        record.Type = CRED_TYPE_GENERIC; record.TargetName = const_cast<wchar_t *>(key.c_str());
        record.UserName = const_cast<wchar_t *>(user.c_str()); record.Persist = CRED_PERSIST_LOCAL_MACHINE;
        record.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(password.data()));
        record.CredentialBlobSize = DWORD(password.size());
        return CredWriteW(&record, 0) != 0;
#else
        const auto file = privateFile(configuration_);
        auto temporary = file; temporary += ".tmp";
        if (std::filesystem::is_symlink(temporary)) return false;
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        std::filesystem::permissions(temporary, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace);
        auto bytes = nlohmann::json{{"account", account}, {"password", password}}.dump();
        output.write(bytes.data(), std::streamsize(bytes.size())); net::protocol::erase_secret(bytes);
        output.close(); if (!output) return false;
        std::filesystem::rename(temporary, file); return true;
#endif
    } catch (...) { return false; }
}
}
