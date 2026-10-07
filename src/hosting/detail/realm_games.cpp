#include "native_realm_service.hpp"
#include <random>

namespace d2x::hosting {
using namespace net::protocol;
void NativeRealmService::createGame(net::protocol::Reader &in) {
    const auto request = in.u16(); const auto flags = in.u32(); const auto templateId = in.u8(), levelDifference = in.u8(); const auto players = in.u8();
    auto name = in.string(15); auto password = in.string(15); const auto description = in.string(31); in.finish();
    uint32_t code = 0;
    try {
        if (!selected || !lease || binding || players != 1) throw std::runtime_error("Embedded host requires one selected player");
        if ((flags & ~0x3004u) || !(flags & 4) || templateId != 1 || levelDifference > 99 || name.empty())
            throw std::runtime_error("Unsupported native game creation options");
        // Description/level limit carry no eligibility effect for this private one-player host.
        (void)description;
        gameDifficulty = (flags >> 12) & 3;
        if (gameDifficulty > 2) throw std::runtime_error("Invalid difficulty");
        const auto progression = (find(selectedName).nativeStatus >> 8) & 31;
        if (gameDifficulty > unsigned(progression >= 10 ? 2 : progression >= 5 ? 1 : 0))
            throw std::runtime_error("Character has not unlocked this difficulty");
        const auto nextHash = uint32_t(std::random_device{}());
        const auto nextToken = uint16_t(std::random_device{}());
        // Admission is transactional, and all serializers finish before
        // the MCP success authorizes the client to connect to D2GS.
        if (reload) {
            if (reload->name != selectedName || reload->difficulty != gameDifficulty || reload->expectedFile != lease->expected)
                throw std::runtime_error("Prepared reload no longer matches the selected save or difficulty");
            binding = reload->game.binding; terrain = std::move(reload->game.terrain); admission = std::move(reload->game.admission);
            reload.reset();
        } else {
            auto saved = *selected; saved.difficulty = int(gameDifficulty);
            auto prepared = prepareGame(std::move(saved));
            binding = prepared.binding; terrain = std::move(prepared.terrain); admission = std::move(prepared.admission);
        }
        host.pause(binding->game, true); gameName = std::move(name); gamePassword = std::move(password); ticket = true;
        hash = nextHash; token = nextToken;
    } catch (const std::exception &e) { failure = e.what(); code = 0x1F; }
    Writer out; out.u16(request); out.u16(0); out.u16(0); out.u32(code); sendRealm(3, std::move(out));
}
void NativeRealmService::joinGame(net::protocol::Reader &in) {
    const auto request = in.u16(); const auto name = in.string(15); const auto password = in.string(15); in.finish();
    const bool valid = ticket && binding && name == gameName && password == gamePassword;
    Writer out; out.u16(request); out.u16(token); out.u16(0); out.u8(127); out.u8(0); out.u8(0); out.u8(1); out.u32(hash); out.u32(valid ? 0 : 0x2A);
    sendRealm(4, std::move(out));
}
void NativeRealmService::listGames(net::protocol::Reader &in) {
    const auto request = in.u16(); in.u32(); in.finish();
    Writer out; out.u16(request); out.u32(0); out.u8(0); out.u32(0); out.string(""); out.string(""); sendRealm(5, std::move(out));
}
}
