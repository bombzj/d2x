#include "character_frontend.hpp"
#include "input.hpp"
#include "content/string_table.hpp"
#include "content/classic_data.hpp"
#include "persistence/save_file.hpp"
#include "presentation/graphics.hpp"
#include "presentation/primitives.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <rlgl.h>
#include <sstream>
#include <string_view>

namespace d2x {
namespace {
constexpr int screenWidth = 800, screenHeight = 600;
constexpr float frontScale = float(H) / screenHeight;
constexpr float offsetX = (W - screenWidth * frontScale) / 2, offsetY = 0;
constexpr Color labelColor{213, 188, 137, 255};
struct HeroArt {
    const char *name, *folder, *idle, *selected;
    Rectangle hit;
    Vector2 position;
    float idleSeconds, forwardSeconds, backSeconds;
};
constexpr std::array<HeroArt, 7> heroes{{
    {"Amazon", "amazon", "amnu1", "amnu3", {70, 220, 55, 200}, {100, 339}, 2.5f, 2.2f, 1.5f},
    {"Assassin", "assassin", "asnu1", "asnu3", {175, 235, 50, 180}, {231, 365}, 2.5f, 3.8f, 1.5f},
    {"Necromancer", "necromancer", "nenu1", "nenu3", {265, 220, 55, 175}, {300, 335}, 1.2f, 2.f, 1.5f},
    {"Barbarian", "barbarian", "banu1", "banu3", {364, 201, 90, 170}, {400, 330}, 0.f, 2.5f, 1.f},
    {"Paladin", "paladin", "panu1", "panu3", {490, 210, 65, 180}, {521, 338}, 2.5f, 3.4f, 1.3f},
    {"Sorceress", "sorceress", "sonu1", "sonu3", {580, 240, 65, 160}, {626, 352}, 2.5f, 2.3f, 1.2f},
    {"Druid", "druid", "dznu1", "dznu3", {680, 220, 70, 195}, {720, 370}, 1.5f, 4.8f, 1.5f},
}};
enum class HeroPose { Idle, Forward, Selected, Back };
struct HeroPreview {
    GpuAnimation idle, hover, forward, selected, back;
    GpuAnimation forwardOverlay, selectedOverlay, backOverlay;
    HeroPose pose = HeroPose::Idle;
    float elapsed = 0;
};
void drawHero(const GpuAnimation &animation, Vector2 position, float elapsed, float duration, bool loop) {
    if (animation.frames.empty()) return;
    const int progress = int(elapsed * (duration > 0 ? animation.count / duration : 25.f));
    const int index = loop ? progress % animation.count : std::min(progress, animation.count - 1);
    const auto *cel = animation.frame(0, index);
    if (cel)
        sprite(cel, {position.x, position.y - cel->texture.height});
}
struct Slot {
    std::filesystem::path path;
    std::string name, characterClass;
    int level = 1;
    GpuAnimation portrait;
};
bool validName(std::string_view name) {
    return !name.empty() && name.size() <= 15 &&
           std::all_of(name.begin(), name.end(), [](unsigned char letter) {
               return (letter >= 'A' && letter <= 'Z') ||
                      (letter >= 'a' && letter <= 'z') ||
                      (letter >= '0' && letter <= '9') || letter == '-';
           });
}
int heroIndex(std::string_view characterClass) {
    for (size_t i = 0; i < heroes.size(); ++i)
        if (characterClass == heroes[i].name) return int(i);
    return -1;
}
bool hit(Rectangle bounds, Vector2 mouse) { return CheckCollisionPointRec(mouse, bounds); }
void drawAnimation(const GpuAnimation &animation, int x, int y, int frame = 0) {
    const auto *cel = animation.frame(0, frame);
    if (cel) DrawTexture(cel->texture, x, y, WHITE);
}
void drawBackground(const GpuAnimation &background) {
    if (background.frames.empty()) return;
    // Original frontend screens are stored as 256x256 DC6 tiles in scanline order.
    for (int i = 0; i < background.count; ++i)
        drawAnimation(background, (i % 4) * 256, (i / 4) * 256, i);
}
void drawButton(const UiPainter &text, const GpuAnimation &art, Rectangle bounds,
                std::string_view caption, bool active = true, bool pressed = false) {
    if (const auto *cel = art.frame(0, pressed && active ? 1 : 0))
        DrawTexturePro(cel->texture, {0, 0, float(cel->texture.width), float(cel->texture.height)},
                       bounds, {0, 0}, 0, active ? WHITE : GRAY);
    std::istringstream lines{std::string(caption)};
    std::string line;
    const int count = 1 + int(std::count(caption.begin(), caption.end(), '\n'));
    int top = int(bounds.y + (bounds.height - count * 16) / 2) + (pressed ? 1 : 0);
    while (std::getline(lines, line)) {
        text.label(line, int(bounds.x + (bounds.width - text.measure(line, 16)) / 2) + (pressed ? 1 : 0),
                   top, 16, active ? BLACK : Color{65, 65, 65, 255});
        top += 16;
    }
}
void centered(const UiPainter &text, const std::string &caption, int y, int size,
              Color color = labelColor) {
    text.label(caption, (screenWidth - text.measure(caption, size)) / 2, y, size, color);
}
GpuAnimation loadPortrait(Graphics &graphics, const ClassicData &content, const CharacterSaveData &save) {
    const auto character = std::find_if(content.characters.begin(), content.characters.end(), [&](const auto &entry) {
        return entry.name == save.player.characterClass;
    });
    if (character == content.characters.end() || content.armorTypes.empty())
        throw std::runtime_error("Character portrait definition unavailable");
    std::array<std::string, 16> parts;
    parts.fill(content.armorTypes.front());
    parts[5] = parts[6] = parts[7] = "nil";
    if (character->appearance == "ne") parts[10] = "ne1";
    auto equipped = [&](EquipmentSlot slot) -> const ItemDefinition * {
        for (const auto &[id, item] : save.inventory.items)
            if (auto location = std::get_if<ContainerLocation>(&item.location);
                location && location->container == save.containers.equipment &&
                location->cell == Cell{int(slot), 0}) return content.items.find(item.definition);
        return nullptr;
    };
    if (const auto *head = equipped(EquipmentSlot::Head); head && !head->appearance.token.empty())
        parts[0] = head->appearance.token;
    if (const auto *torso = equipped(EquipmentSlot::Torso)) {
        constexpr std::array<size_t, 6> components{3, 4, 1, 2, 8, 9};
        for (size_t index = 0; index < components.size(); ++index)
            if (!torso->appearance.body[index].empty()) parts[components[index]] = torso->appearance.body[index];
    }
    std::string weapon = "hth";
    std::vector<std::string> weaponClasses;
    const auto weaponSet = save.player.weaponSet;
    for (bool left : {false, true}) {
        const auto *item = equipped(weaponHandSlot(left, weaponSet));
        if (!item || item->appearance.token.empty()) continue;
        int component = item->appearance.component;
        if (item->equipment.isType("weap")) {
            weaponClasses.push_back(item->base.weaponClass);
            weapon = item->base.weaponClass;
            component = left ? 6 : 5;
            if (item->equipment.twoHanded &&
                (!item->equipment.oneOrTwoHanded || !equipped(weaponHandSlot(!left, weaponSet))))
                weapon = item->equipment.twoHandWeaponClass;
        }
        if (component >= 5 && component <= 7) parts[component] = item->appearance.token;
    }
    if (weaponClasses.size() == 2)
        weapon = weaponClasses[0] == "1ht" ? (weaponClasses[1] == "1ht" ? "1jt" : "1st")
                                          : (weaponClasses[1] == "1ht" ? "1js" : "1ss");
    std::array<const char *, 16> equipment;
    for (size_t index = 0; index < parts.size(); ++index) equipment[index] = parts[index].c_str();
    auto portrait = graphics.composite("chars", character->appearance, "tn", weapon, &equipment);
    if (portrait.frames.empty() || !portrait.completeComposite)
        portrait = graphics.composite("chars", character->appearance, "nu", weapon, &equipment);
    return portrait;
}
std::vector<Slot> listCharacters(std::string &warning, Graphics &graphics, const ClassicData &content) {
    std::vector<Slot> slots;
    std::filesystem::create_directories("saves");
    for (const auto &file : std::filesystem::directory_iterator("saves")) {
        auto extension = file.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char letter) { return char(std::tolower(letter)); });
        if (!file.is_regular_file() || extension != ".d2s" ||
            file.path().stem() == "quick") continue;
        try {
            const auto save = loadSave(file.path(), content);
            const auto &player = save.player;
            if (!validName(player.name) || heroIndex(player.characterClass) < 0)
                throw std::runtime_error("Unsupported expansion character");
            slots.push_back({file.path(), player.name, player.characterClass, player.level,
                             loadPortrait(graphics, content, save)});
        } catch (const std::exception &error) {
            warning = "Some saves could not be listed; check the console.";
            TraceLog(LOG_WARNING, "Character %s: %s", file.path().string().c_str(), error.what());
        }
    }
    std::sort(slots.begin(), slots.end(), [](const Slot &left, const Slot &right) {
        return left.name < right.name;
    });
    return slots;
}
ClassicFont loadFrontFont(Graphics &graphics, Archives &archives, std::string_view name = "font16") {
    ClassicFont font;
    const std::string path = "data/local/font/latin/" + std::string(name);
    font.glyphs = graphics.single(path + ".dc6");
    auto table = archives.read(path + ".tbl", false);
    if (table.size() < 3596 || font.glyphs.frames.empty())
        throw std::runtime_error("Original character selection font is missing");
    for (int i = 0; i < 256; ++i) {
        font.widths[i] = table[12 + i * 14 + 3];
        font.indices[i] = table[12 + i * 14 + 8];
    }
    font.ready = true;
    return font;
}
} // namespace

std::optional<CharacterChoice> chooseCharacter(Archives &archives, RenderTexture2D target) {
    Graphics graphics(archives, "data/global/palette/sky/pal.dat");
    Graphics heroGraphics(archives, "data/global/palette/fechar/pal.dat");
    Graphics unitsGraphics(archives, "data/global/palette/units/pal.dat");
    Graphics characterGraphics(archives);
    const auto content = loadClassicData(archives);
    ClassicStrings strings(archives);
    auto localized = [&](int index) {
        auto value = strings.find(index);
        if (value.empty()) throw std::runtime_error("Original frontend string missing: " + std::to_string(index));
        return std::string(value);
    };
    auto font = loadFrontFont(unitsGraphics, archives);
    auto titleFont = loadFrontFont(unitsGraphics, archives, "font30");
    auto buttonFont = loadFrontFont(unitsGraphics, archives, "fontexocet10");
    auto nameFont = loadFrontFont(unitsGraphics, archives, "fontformal11");
    UiPainter text(font);
    UiPainter title(titleFont), buttonText(buttonFont), inputText(nameFont);
    const auto selection = graphics.single("data/global/ui/CharSelect/characterselectscreenEXP.dc6");
    const auto creation = heroGraphics.single("data/global/ui/FrontEnd/charactercreationscreenEXP.dc6");
    const auto campfire = heroGraphics.single("data/global/ui/FrontEnd/fire.dc6");
    const auto button = unitsGraphics.single("data/global/ui/FrontEnd/MediumButtonBlank.dc6");
    const auto tallButton = unitsGraphics.single("data/global/ui/CharSelect/TallButtonBlank.dc6");
    const auto textbox = unitsGraphics.single("data/global/ui/FrontEnd/textbox2.dc6");
    const auto checkbox = heroGraphics.single("data/global/ui/FrontEnd/clickbox.dc6");
    const auto popup = unitsGraphics.single("data/global/ui/FrontEnd/PopUpOkCancel2.dc6");
    const auto cancelButton = unitsGraphics.single("data/global/ui/FrontEnd/CancelButtonBlank.dc6");
    const auto cursor = unitsGraphics.single("data/global/ui/CURSOR/ohand.dc6");
    const auto highlight = graphics.single("data/global/ui/CharSelect/charselectbox.dc6");
    if (selection.frames.empty() || creation.frames.empty() || button.frames.empty() ||
        highlight.frames.empty() || tallButton.frames.empty() || textbox.frames.empty() ||
        checkbox.frames.size() < 2 || campfire.frames.empty() || popup.frames.empty() ||
        cancelButton.frames.empty() || cursor.frames.empty())
        throw std::runtime_error("Original expansion character selection art is missing");
    std::array<HeroPreview, 7> previews;
    for (size_t i = 0; i < heroes.size(); ++i) {
        const auto base = std::string("data/global/ui/FrontEnd/") + heroes[i].folder + "/";
        const auto token = std::string(heroes[i].idle, 2);
        auto &preview = previews[i];
        preview.idle = heroGraphics.single(base + token + "nu1.dc6");
        preview.hover = heroGraphics.single(base + token + "nu2.dc6");
        preview.forward = heroGraphics.single(base + token + "fw.dc6");
        preview.selected = heroGraphics.single(base + token + "nu3.dc6");
        preview.back = heroGraphics.single(base + token + "bw.dc6");
        if (i == 2 || i == 3 || i == 4 || i == 5)
            preview.forwardOverlay = heroGraphics.single(base + token + "fws.dc6");
        if (i == 2 || i == 5) {
            preview.selectedOverlay = heroGraphics.single(base + token + "nu3s.dc6");
            preview.backOverlay = heroGraphics.single(base + token + "bws.dc6");
        }
        if (preview.idle.frames.empty() || preview.hover.frames.empty() || preview.forward.frames.empty() ||
            preview.selected.frames.empty() || preview.back.frames.empty())
            throw std::runtime_error("Original expansion hero animation is missing: " +
                                     std::string(heroes[i].name));
    }
    std::string notice;
    auto slots = listCharacters(notice, characterGraphics, content);
    bool creating = false, confirmDelete = false;
    int selected = slots.empty() ? -1 : 0, page = 0, hero = -1;
    std::string name;
    const auto &medium = button.frames.front().texture;
    const auto &tall = tallButton.frames.front().texture;
    const Rectangle newButton{34, 467, float(tall.width), float(tall.height)};
    const Rectangle convertButton{234, 467, float(tall.width), float(tall.height)};
    const Rectangle deleteButton{434, 467, float(tall.width), float(tall.height)};
    const Rectangle backButton{33, 538, float(medium.width), float(medium.height)};
    const Rectangle okayButton{628, 538, float(medium.width), float(medium.height)};
    const std::array<int, 7> descriptionIds{5128, 22519, 5129, 5130, 5132, 5131, 22518};
    HideCursor();
    bool awaitRelease = true;
    int lastClickedSlot = -1;
    double lastClickTime = 0;
    while (!WindowShouldClose()) {
        const float elapsed = std::min(GetFrameTime(), .1f);
        bool transitioning = false;
        for (size_t index = 0; index < previews.size(); ++index) {
            auto &preview = previews[index];
            preview.elapsed += elapsed;
            if (preview.pose == HeroPose::Forward && preview.elapsed >= heroes[index].forwardSeconds) {
                preview.pose = HeroPose::Selected;
                preview.elapsed = 0;
            } else if (preview.pose == HeroPose::Back && preview.elapsed >= heroes[index].backSeconds) {
                preview.pose = HeroPose::Idle;
                preview.elapsed = 0;
            }
            transitioning |= preview.pose == HeroPose::Forward || preview.pose == HeroPose::Back;
        }
        auto viewport = currentViewport();
        Vector2 mouse = GetMousePosition();
        mouse = {((mouse.x - viewport.offset.x) / viewport.scale - offsetX) / frontScale,
             ((mouse.y - viewport.offset.y) / viewport.scale - offsetY) / frontScale};
        if (awaitRelease && !IsKeyDown(KEY_ESCAPE) && !IsKeyDown(KEY_ENTER) &&
            !IsKeyDown(KEY_KP_ENTER) && !IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
            !IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) awaitRelease = false;
        bool click = !awaitRelease && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                     hit({0, 0, screenWidth, screenHeight}, mouse);
        bool enter = !awaitRelease && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER));
        bool escape = !awaitRelease && IsKeyPressed(KEY_ESCAPE);
        if (confirmDelete) {
            if (escape || (click && hit({280, 340, 96, 32}, mouse))) confirmDelete = false;
            else if (enter || (click && hit({420, 340, 96, 32}, mouse))) {
                std::error_code error;
                std::filesystem::remove(slots[selected].path, error);
                if (error) notice = "Could not delete character: " + error.message();
                else {
                    slots = listCharacters(notice, characterGraphics, content);
                    selected = slots.empty() ? -1 : std::min(selected, int(slots.size()) - 1);
                    page = std::min(page, std::max(0, (int(slots.size()) - 1) / 8));
                    notice.clear();
                }
                confirmDelete = false;
            }
        } else if (creating) {
            for (int letter = GetCharPressed(); letter; letter = GetCharPressed())
                if (letter < 128 && (std::isalnum(static_cast<unsigned char>(letter)) || letter == '-') &&
                    name.size() < 15) name += char(letter);
            if (IsKeyPressed(KEY_BACKSPACE) && !name.empty()) name.pop_back();
            for (size_t i = 0; i < heroes.size(); ++i)
                if (!transitioning && hero != int(i) && click && hit(heroes[i].hit, mouse)) {
                    if (hero >= 0) {
                        previews[hero].pose = HeroPose::Back;
                        previews[hero].elapsed = 0;
                    }
                    hero = int(i);
                    previews[i].pose = HeroPose::Forward;
                    previews[i].elapsed = 0;
                    notice.clear();
                }
            if (escape || (click && hit(backButton, mouse))) { creating = false; notice.clear(); }
            else if (!transitioning && (enter || (click && hit(okayButton, mouse)))) {
                if (hero < 0) notice = "Select a character class.";
                else if (!validName(name)) notice = "Enter a name (1-15 letters, digits or hyphens).";
                else {
                    auto duplicate = std::find_if(slots.begin(), slots.end(), [&](const Slot &slot) {
                        return std::equal(name.begin(), name.end(), slot.name.begin(), slot.name.end(),
                            [](unsigned char left, unsigned char right) {
                                return std::tolower(left) == std::tolower(right);
                            });
                    });
                    auto path = std::filesystem::path("saves") / (name + ".d2s");
                    if (duplicate != slots.end() || std::filesystem::exists(path))
                        notice = "That character name is already in use.";
                    else return CharacterChoice{path, heroes[hero].name, name, true};
                }
            }
        } else {
            int direction = int(IsKeyPressed(KEY_PAGE_DOWN)) - int(IsKeyPressed(KEY_PAGE_UP));
            float wheel = GetMouseWheelMove();
            direction += wheel < 0 ? 1 : wheel > 0 ? -1 : 0;
            if (direction) {
                page = std::clamp(page + direction, 0, std::max(0, (int(slots.size()) - 1) / 8));
                lastClickedSlot = -1;
            }
            bool openSelected = enter || (click && hit(okayButton, mouse));
            bool clickedSlot = false;
            for (int i = 0; i < 8; ++i) {
                int index = page * 8 + i;
                Rectangle bounds{37.f + (i % 2) * 272.f, 86.f + (i / 2) * 92.f, 272, 92};
                if (index < int(slots.size()) && click && hit(bounds, mouse)) {
                    const double now = GetTime();
                    openSelected |= index == lastClickedSlot && now - lastClickTime < 1.25;
                    selected = index;
                    notice.clear();
                    lastClickedSlot = index;
                    lastClickTime = now;
                    clickedSlot = true;
                }
            }
            if (click && !clickedSlot) lastClickedSlot = -1;
            if (escape || (click && hit(backButton, mouse))) return std::nullopt;
            if (click && hit(newButton, mouse)) {
                creating = true;
                name.clear();
                hero = -1;
                notice.clear();
                for (auto &preview : previews) { preview.pose = HeroPose::Idle; preview.elapsed = 0; }
            }
            else if (click && hit(deleteButton, mouse) && selected >= 0) confirmDelete = true;
            else if (openSelected && selected >= 0) {
                try {
                    loadSave(slots[selected].path, content);
                    return CharacterChoice{slots[selected].path, slots[selected].characterClass,
                                           slots[selected].name, false};
                } catch (const std::exception &error) { notice = error.what(); }
            }
        }
        BeginTextureMode(target);
        ClearBackground(BLACK);
        BeginScissorMode(int(offsetX), int(offsetY), int(screenWidth * frontScale), int(screenHeight * frontScale));
        rlPushMatrix();
        rlTranslatef(offsetX, offsetY, 0);
        rlScalef(frontScale, frontScale, 1);
        drawBackground(creating ? creation : selection);
        if (creating) {
            for (bool foreground : {false, true})
                for (size_t index = 0; index < previews.size(); ++index) {
                    const auto &preview = previews[index];
                    const auto &config = heroes[index];
                    if ((preview.pose != HeroPose::Idle) != foreground) continue;
                    const bool forward = preview.pose == HeroPose::Forward;
                    const bool back = preview.pose == HeroPose::Back;
                    const auto &art = forward ? preview.forward : back ? preview.back
                        : preview.pose == HeroPose::Selected ? preview.selected
                        : !transitioning && hit(config.hit, mouse) ? preview.hover : preview.idle;
                    const float duration = forward ? config.forwardSeconds : back ? config.backSeconds : config.idleSeconds;
                    drawHero(art, config.position, preview.elapsed, duration, !forward && !back);
                    const auto *overlay = forward ? &preview.forwardOverlay : back ? &preview.backOverlay
                        : preview.pose == HeroPose::Selected ? &preview.selectedOverlay : nullptr;
                    const bool additive = !forward || index == 2 || index == 5;
                    if (additive) BeginBlendMode(BLEND_ADDITIVE);
                    if (overlay) drawHero(*overlay, config.position, preview.elapsed, duration, !forward && !back);
                    if (additive) EndBlendMode();
                }
            BeginBlendMode(BLEND_ADDITIVE);
            drawHero(campfire, {380, 335}, float(GetTime()), 0, true);
            EndBlendMode();
            centered(title, localized(5127), 17, 16, WHITE);
            int described = hero;
            if (described < 0)
                for (size_t index = 0; index < heroes.size(); ++index)
                    if (hit(heroes[index].hit, mouse)) described = int(index);
            if (described >= 0) {
                centered(title, heroes[described].name, 65, 16, WHITE);
                std::istringstream words(localized(descriptionIds[described]));
                std::string word, line;
                int top = 104;
                while (words >> word) {
                    const auto next = line.empty() ? word : line + " " + word;
                    if (!line.empty() && text.measure(next, 16) > 290) {
                        centered(text, line, top, 16, WHITE);
                        top += 14;
                        line = word;
                    } else line = next;
                }
                if (!line.empty()) centered(text, line, top, 16, WHITE);
            }
            if (hero >= 0) {
                text.label("Character Name", 321, 475, 16, labelColor);
                drawAnimation(textbox, 318, 493);
                inputText.label(name + (int(GetTime() * 2) % 2 ? "_" : ""), 325, 500, 16, WHITE);
                drawAnimation(checkbox, 318, 526, 1);
                text.label(localized(22731), 339, 526, 16, labelColor);
                drawAnimation(checkbox, 318, 548, 0);
                text.label("Hardcore", 339, 548, 16, GRAY);
            }
        } else {
            if (selected >= 0)
                title.label(slots[selected].name, 317 - title.measure(slots[selected].name, 16) / 2, 33, 16, WHITE);
            for (int i = 0; i < 8; ++i) {
                int index = page * 8 + i;
                if (index >= int(slots.size())) break;
                int x = 37 + (i % 2) * 272, y = 86 + (i / 2) * 92;
                if (index == selected) {
                    int left = x;
                    for (int segment = 0; segment < highlight.count; ++segment) {
                        drawAnimation(highlight, left, y, segment);
                        left += highlight.frame(0, segment)->texture.width;
                    }
                }
                const auto &portrait = slots[index].portrait;
                if (const auto *cel = portrait.frame(0, int(GetTime() * 12)))
                    sprite(cel, {float(x + 30), float(y + 82)});
                text.label(slots[index].name, x + 76, y + 14, 16, labelColor);
                text.label("Level " + std::to_string(slots[index].level) + " " +
                           slots[index].characterClass, x + 76, y + 28, 16, WHITE);
                text.label(localized(22731), x + 76, y + 42, 16, {0, 255, 0, 255});
            }
            if (slots.size() > 8)
                text.label(std::to_string(page + 1) + "/" + std::to_string((slots.size() + 7) / 8),
                           602, 435, 16);
            drawButton(buttonText, tallButton, newButton, "CREATE NEW\nCHARACTER", true,
                       hit(newButton, mouse) && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
            drawButton(buttonText, tallButton, convertButton, "CONVERT TO\nEXPANSION", false);
            drawButton(buttonText, tallButton, deleteButton, "DELETE\nCHARACTER", selected >= 0,
                       hit(deleteButton, mouse) && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
        }
        drawButton(buttonText, button, backButton, localized(5101), true,
                   hit(backButton, mouse) && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
        drawButton(buttonText, button, okayButton, localized(5102),
                   creating ? hero >= 0 && validName(name) && !transitioning : selected >= 0,
                   hit(okayButton, mouse) && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
        if (!notice.empty()) centered(text, notice, 583, 14, {255, 180, 142, 255});
        if (confirmDelete) {
            DrawRectangle(0, 0, screenWidth, screenHeight, {0, 0, 0, 150});
            int left = 268;
            for (int segment = 0; segment < popup.count; ++segment) {
                drawAnimation(popup, left, 212, segment);
                left += popup.frame(0, segment)->texture.width;
            }
            text.inBox("Delete " + slots[selected].name + "?", {278, 244, 244, 42}, 16);
            drawButton(buttonText, cancelButton, {280, 340, 96, 32}, localized(5167));
            drawButton(buttonText, cancelButton, {420, 340, 96, 32}, localized(5166));
        }
        if (const auto *pointer = cursor.frame(0, 0))
            cursorSprite(pointer, {mouse.x, mouse.y}, handCursorHotspot(pointer));
        rlPopMatrix();
        EndScissorMode();
        EndTextureMode();
        if (IsKeyPressed(KEY_F12)) {
            std::filesystem::create_directories("artifacts");
            Image capture = LoadImageFromTexture(target.texture);
            ImageFlipVertical(&capture);
            ExportImage(capture, "artifacts/d2x-capture.png");
            UnloadImage(capture);
        }
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(target.texture, {0, 0, float(W), -float(H)},
                       {viewport.offset.x, viewport.offset.y, W * viewport.scale, H * viewport.scale},
                       {0, 0}, 0, WHITE);
        EndDrawing();
    }
    return std::nullopt;
}
} // namespace d2x
