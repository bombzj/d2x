#include "chat_command.hpp"
#include "network/chat_text.hpp"
#include <algorithm>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace d2x {
namespace {
std::string_view trim(std::string_view text) {
    const auto first=text.find_first_not_of(" \t\r\n");
    return first==text.npos?std::string_view{}:text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);
}
std::string word(std::string_view &text) {
    const auto end=text.find_first_of(" \t\r\n");
    std::string result(text.substr(0,end));
    text=end==text.npos?std::string_view{}:trim(text.substr(end));return result;
}
}
std::optional<nlohmann::json> parseDebugChatCommand(std::string_view text) {
    using Json=nlohmann::json;
    const auto input=text;
    text=trim(text);auto name=word(text);
    for(auto &c:name) if(c>='A' && c<='Z') c=char(c-'A'+'a');
    if(!name.starts_with("/debug-")) return {};
    const auto encoded=net::encodeChatText(input);
    if(!encoded || encoded->size()>255) throw std::invalid_argument("Debug chat is limited to 255 native text bytes.");
    name.erase(0,1);
    const bool plan=name=="debug-ai-start" || name=="debug-ai-append";
    const bool walk=name=="debug-walk";
    const bool noArguments=name=="debug-help" || name=="debug-ai-status" || name=="debug-ai-pause" ||
        name=="debug-ai-resume" || name=="debug-ai-cancel" || name=="debug-walk-status" || name=="debug-walk-cancel";
    if(!plan && !walk && !noArguments) throw std::invalid_argument("Unknown local debug command. Use /debug-help.");
    if(noArguments && !text.empty()) throw std::invalid_argument("Usage: /"+name);
    Json request=Json::object();
    if(plan && !text.empty() && text.front()!='{') {
        const auto scenario=word(text);
        if(scenario=="quest") {
            if(text.empty() || text.front()=='{') throw std::invalid_argument("Usage: /"+name+" quest <quest-name> [JSON options]");
            request["goals"]=Json::array({{{"action","quest"},{"quest",word(text)}}});
        } else request["scenario"]=scenario;
    } else if(walk && !text.empty() && text.front()!='{') request["goal"]=word(text);
    if(!text.empty()) {
        const auto options=Json::parse(text.begin(),text.end(),nullptr,false);
        if(options.is_discarded() || !options.is_object()) throw std::invalid_argument("Debug options must be a JSON object with double-quoted keys and strings.");
        for(const auto &[key,value]:options.items()) {
            if(key=="command" || request.contains(key)) throw std::invalid_argument("Duplicate or reserved debug option: "+key);
            request[key]=value;
        }
    }
    if(plan && !request.contains("scenario") && !request.contains("goals"))
        throw std::invalid_argument("Usage: /"+name+" <scenario> [JSON options], quest <quest-name>, or {\"goals\":[...]}");
    request["command"]=name;return request;
}
std::vector<std::string> debugChatSummary(const nlohmann::json &reply) {
    std::vector<std::string> lines;
    if(reply.contains("ai")) {
        const auto &ai=reply.at("ai"),&goals=ai.at("goals");
        const auto index=ai.at("goalIndex").get<size_t>();
        auto line="AI "+ai.at("state").get<std::string>()+"; goal "+std::to_string(std::min(index+1,goals.size()))+"/"+std::to_string(goals.size());
        if(index<goals.size()) {
            const auto &goal=goals.at(index);
            line+="; "+goal.at("action").get<std::string>();
            const auto quest=goal.value("quest",std::string{});if(!quest.empty()) line+=" "+quest;
        }
        lines.push_back(std::move(line));
        const auto phase=ai.value("phase",std::string{});if(!phase.empty()) lines.push_back("Phase: "+phase);
        if(ai.contains("questProgress") && ai.at("questProgress").is_object()) {
            const auto &progress=ai.at("questProgress");
            const auto steps=progress.at("steps").get<size_t>();
            lines.push_back("Quest step "+std::to_string(std::min(progress.at("step").get<size_t>()+1,steps))+"/"+
                std::to_string(steps)+"; "+progress.at("action").dump());
        }
        const auto reason=ai.value("reason",std::string{});if(!reason.empty()) lines.push_back("Reason: "+reason);
    } else if(reply.contains("navigation")) {
        const auto &nav=reply.at("navigation");
        lines.push_back("Walk "+nav.at("state").get<std::string>());
        const auto reason=nav.value("reason",std::string{});if(!reason.empty()) lines.push_back("Reason: "+reason);
    }
    return lines;
}
}
