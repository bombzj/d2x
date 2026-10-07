#include "ui_input.hpp"
#include "presentation/graphics/primitives.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace d2x {
std::vector<FrameInput> parseDebugInput(const nlohmann::json &request) {
    using Json = nlohmann::json;
    auto parseFrame = [](const Json &request) {
        if (!request.is_object()) throw std::invalid_argument("UI frame must be an object");
        FrameInput frame;
        frame.focused = request.value("focused", true);
        frame.screenshot = request.value("screenshot", false);
        frame.showLoot = request.value("showLoot", false);
        frame.mouse = {request.value("x", 0.f), request.value("y", 0.f)};
        if (!std::isfinite(frame.mouse.x) || !std::isfinite(frame.mouse.y) ||
            frame.mouse.x < 0 || frame.mouse.x >= W || frame.mouse.y < 0 || frame.mouse.y >= H)
            throw std::invalid_argument("UI coordinates must be inside the logical viewport");
        frame.insideViewport = true;
        const auto button = request.value("button", std::string{});
        if (button == "left") frame.leftPressed = frame.leftHeld = true;
        else if (button == "right") frame.rightPressed = frame.rightHeld = true;
        else if (!button.empty()) throw std::invalid_argument("button must be left or right");
        frame.leftHeld = request.value("leftHeld", frame.leftHeld);
        frame.leftReleased = request.value("leftReleased", false);
        frame.rightHeld = request.value("rightHeld", frame.rightHeld);
        frame.shift = request.value("shift", false);
        frame.control = request.value("control", false);
        frame.backspace = request.value("backspace", false);
        frame.text = request.value("text", std::string{});
        frame.entryText = request.value("entryText", std::string{});
        frame.entryUnsupported = request.value("entryUnsupported", false);
        if (frame.entryText.size() > 255 || !std::ranges::all_of(frame.entryText, [](unsigned char c) { return c >= 32 && c < 127; }))
            throw std::invalid_argument("UI entry text must contain at most 255 printable ASCII bytes");
        frame.wheel = request.value("wheel", 0.f);
        if (!std::isfinite(frame.wheel) || std::abs(frame.wheel) > 32)
            throw std::invalid_argument("UI wheel delta is outside the supported range");
        frame.quantityDelta = int(frame.wheel);
        if (frame.text.size() > 10 || !std::ranges::all_of(frame.text, [](char c) { return c >= '0' && c <= '9'; }))
            throw std::invalid_argument("UI text must contain at most 10 decimal digits");
        const auto key = request.value("key", std::string{});
        if (key == "escape") frame.escape = true;
        else if (key == "enter") frame.enter = true;
        else if (key == "message-log" || key == "m") frame.messageLog = true;
        else if (key == "home") frame.entryHome = frame.automapCenter = true;
        else if (key == "end") frame.entryEnd = true;
        else if (key == "delete") frame.entryDelete = true;
        else if (key == "page-up") frame.pageDelta = -1;
        else if (key == "page-down") frame.pageDelta = 1;
        else if (key == "tab") frame.tab = frame.automap = true;
        else if (key == "inventory") frame.inventory = true;
        else if (key == "character") frame.character = true;
        else if (key == "quests") frame.quests = true;
        else if (key == "skill-tree") frame.skillTree = true;
        else if (key == "f11") { frame.load = frame.control; frame.save = !frame.control; }
        else if (key.size() == 1 && key[0] >= '1' && key[0] <= '4') frame.belt[size_t(key[0] - '1')] = true;
        else if (key.size() == 2 && key[0] == 'f' && key[1] >= '1' && key[1] <= '8')
            frame.skills[size_t(key[1] - '1')] = true;
        else if (key == "hireling" || key == "o") frame.hireling = true;
        else if (key == "weapon-swap") frame.weaponSwap = true;
        else if (key == "automap") frame.automap = true;
        else if (key == "automap-side") frame.minimapSide = true;
        else if (key == "automap-center") frame.automapCenter = true;
        else if (key == "automap-names") frame.automapNames = true;
        else if (key == "up") { frame.movement.y = -1; frame.menuDelta = -1; }
        else if (key == "down") { frame.movement.y = 1; frame.menuDelta = 1; }
        else if (key == "left") { frame.movement.x = -1; frame.entryStep = -1; }
        else if (key == "right") { frame.movement.x = 1; frame.entryStep = 1; }
        else if (key == "run" || key == "r") frame.run = true;
        else if (!key.empty()) throw std::invalid_argument("Unsupported UI key");
        return frame;
    };
    std::vector<FrameInput> frames;
    if (request.contains("frames")) {
        const auto &batch = request.at("frames");
        if (!batch.is_array() || batch.empty() || batch.size() > 32)
            throw std::invalid_argument("UI frames must be an array of 1 to 32 entries");
        for (const auto &entry : batch) frames.push_back(parseFrame(entry));
    } else {
        frames.push_back(parseFrame(request));
    }
    return frames;
}
}
