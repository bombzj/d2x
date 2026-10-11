#include "chat_command.hpp"
#include <algorithm>
#include <charconv>

namespace d2x::net {
namespace {
std::string_view trim(std::string_view text) {
    const auto first=text.find_first_not_of(' ');
    if(first==text.npos) return {};
    return text.substr(first,text.find_last_not_of(' ')-first+1);
}
std::string lower(std::string_view text) {
    std::string result(text);
    for(auto &c:result) if(c>='A' && c<='Z') c=char(c-'A'+'a');
    return result;
}
}
ChatCommand parseChatCommand(std::string_view input) {
    ChatCommand result;
    const auto invalid=[&](std::string error) { result.route=ChatRoute::Invalid;result.error=std::move(error);return result; };
    if(input.size()>255 || !std::all_of(input.begin(),input.end(),[](unsigned char c){return c>=32 && c<127;}))
        return invalid("Chat requires at most 255 printable ASCII bytes.");
    input=trim(input);
    if(input.empty()) return invalid("Enter a message or command.");
    const auto space=input.find(' ');
    const auto name=lower(input.substr(0,space));
    auto arguments=space==input.npos?std::string_view{}:trim(input.substr(space+1));
    if(name=="/nopickup") {
        if(!arguments.empty()) return invalid("Usage: /nopickup");
        result.route=ChatRoute::NoPickup;return result;
    }
    if(name=="/players") {
        if(arguments.empty()) return invalid("Usage: /players N (1-8)");
        unsigned count{};
        const auto parsed=std::from_chars(arguments.data(),arguments.data()+arguments.size(),count);
        if(parsed.ec!=std::errc{} || parsed.ptr!=arguments.data()+arguments.size() || count<1 || count>8)
            return invalid("Usage: /players N (1-8)");
        result.route=ChatRoute::Players;result.players=count;return result;
    }
    if(name=="/fps" || name=="/framerate" || name=="soundchaosdebug")
        return invalid("This client diagnostic command is not implemented.");
    if(input.front()=='!') {
        result.route=ChatRoute::Overhead;result.text=std::string(trim(input.substr(1)));
        if(result.text.empty()) return invalid("Usage: !message");
        return result;
    }
    if(input.front()=='@' || name=="/w" || name=="/m" || name=="/msg" || name=="/whisper") {
        const bool room=input.front()=='@';
        if(room) arguments=input.substr(1);
        const auto separator=arguments.find(' ');
        result.receiver=std::string(arguments.substr(0,separator));
        result.text=separator==arguments.npos?std::string{}:std::string(trim(arguments.substr(separator+1)));
        if(result.receiver.empty() || result.text.empty()) return invalid(room?"Usage: @character message":"Usage: /w character-or-*account message");
        if(room && result.receiver.size()>15) return invalid("A room character name is limited to 15 bytes.");
        result.route=room?ChatRoute::Room:ChatRoute::Whisper;return result;
    }
    result.route=input.front()=='/'?ChatRoute::BattleNet:ChatRoute::Room;
    result.text=std::string(input);return result;
}
}
