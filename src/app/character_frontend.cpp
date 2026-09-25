#include "character_frontend.hpp"
#include "input.hpp"
#include "persistence/save_file.hpp"
#include "presentation/graphics.hpp"
#include "presentation/primitives.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <rlgl.h>
#include <string_view>

namespace d2x {
namespace {
constexpr int screenWidth = 800, screenHeight = 600;
constexpr int offsetX = (W - screenWidth) / 2, offsetY = (H - screenHeight) / 2;
constexpr Color labelColor{213, 188, 137, 255};
struct HeroArt {
    const char *name, *folder, *idle, *selected;
    Rectangle hit;
    Vector2 position;
};
constexpr std::array<HeroArt, 7> heroes{{
    {"Amazon", "amazon", "amnu1", "amnu3", {70, 220, 55, 200}, {100, 339}},
    {"Assassin", "assassin", "asnu1", "asnu3", {175, 235, 50, 180}, {231, 365}},
    {"Necromancer", "necromancer", "nenu1", "nenu3", {265, 220, 55, 175}, {300, 335}},
    {"Barbarian", "barbarian", "banu1", "banu3", {364, 201, 90, 170}, {400, 330}},
    {"Paladin", "paladin", "panu1", "panu3", {490, 210, 65, 180}, {521, 338}},
    {"Sorceress", "sorceress", "sonu1", "sonu3", {580, 240, 65, 160}, {626, 352}},
    {"Druid", "druid", "dznu1", "dznu3", {680, 220, 70, 195}, {720, 370}},
}};
struct Slot {
    std::filesystem::path path;
    std::string name, characterClass;
    int level = 1;
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
                std::string_view caption, bool active = true) {
    if (const auto *cel = art.frame(0, 0))
        DrawTexturePro(cel->texture, {0, 0, float(cel->texture.width), float(cel->texture.height)},
                       bounds, {0, 0}, 0, active ? WHITE : GRAY);
    else frame(bounds);
    text.inBox(std::string(caption), bounds, 16, active ? labelColor : GRAY);
}
void centered(const UiPainter &text, const std::string &caption, int y, int size,
              Color color = labelColor) {
    text.label(caption, (screenWidth - text.measure(caption, size)) / 2, y, size, color);
}
std::vector<Slot> listCharacters(std::string &warning) {
    std::vector<Slot> slots;
    std::filesystem::create_directories("saves");
    for (const auto &file : std::filesystem::directory_iterator("saves")) {
        if (!file.is_regular_file() || file.path().extension() != ".d2xsave" ||
            file.path().stem() == "quick") continue;
        try {
            const auto save = loadSave(file.path());
            const auto &player = save.world.player;
            if (!validName(player.name) || heroIndex(player.characterClass) < 0)
                throw std::runtime_error("Unsupported expansion character");
            slots.push_back({file.path(), player.name, player.characterClass, player.level});
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
ClassicFont loadFrontFont(Graphics &graphics, Archives &archives) {
    ClassicFont font;
    const std::string path = "data/local/font/latin/font16";
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
    auto font = loadFrontFont(graphics, archives);
    UiPainter text(font);
    const auto selection = graphics.single("data/global/ui/CharSelect/characterselectscreenEXP.dc6");
    const auto creation = graphics.single("data/global/ui/FrontEnd/charactercreationscreenEXP.dc6");
    const auto button = graphics.single("data/global/ui/FrontEnd/MediumButtonBlank.dc6");
    const auto highlight = graphics.single("data/global/ui/CharSelect/charselectbox.dc6");
    if (selection.frames.empty() || creation.frames.empty() || button.frames.empty() ||
        highlight.frames.empty())
        throw std::runtime_error("Original expansion character selection art is missing");
    std::array<GpuAnimation, 7> idle, chosen;
    for (size_t i = 0; i < heroes.size(); ++i) {
        const auto base = std::string("data/global/ui/FrontEnd/") + heroes[i].folder + "/";
        idle[i] = heroGraphics.single(base + heroes[i].idle + ".dc6");
        chosen[i] = heroGraphics.single(base + heroes[i].selected + ".dc6");
        if (idle[i].frames.empty() || chosen[i].frames.empty())
            throw std::runtime_error("Original expansion hero animation is missing: " +
                                     std::string(heroes[i].name));
    }
    std::string notice;
    auto slots = listCharacters(notice);
    bool creating = false, confirmDelete = false;
    int selected = slots.empty() ? -1 : 0, page = 0, hero = -1;
    std::string name;
    const Rectangle newButton{33, 468, 155, 44}, deleteButton{433, 468, 155, 44};
    const Rectangle backButton{33, 537, 155, 44}, okayButton{625, 537, 155, 44};
    while (!WindowShouldClose()) {
        auto viewport = currentViewport();
        Vector2 mouse = GetMousePosition();
        mouse = {(mouse.x - viewport.offset.x) / viewport.scale - offsetX,
                 (mouse.y - viewport.offset.y) / viewport.scale - offsetY};
        bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                     hit({0, 0, screenWidth, screenHeight}, mouse);
        bool enter = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
        bool escape = IsKeyPressed(KEY_ESCAPE);
        if (confirmDelete) {
            if (escape || (click && hit({280, 308, 125, 40}, mouse))) confirmDelete = false;
            else if (enter || (click && hit({420, 308, 125, 40}, mouse))) {
                std::error_code error;
                std::filesystem::remove(slots[selected].path, error);
                if (error) notice = "Could not delete character: " + error.message();
                else {
                    slots = listCharacters(notice);
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
                if (click && hit(heroes[i].hit, mouse)) { hero = int(i); notice.clear(); }
            if (escape || (click && hit(backButton, mouse))) { creating = false; notice.clear(); }
            else if (enter || (click && hit(okayButton, mouse))) {
                if (hero < 0) notice = "Select a character class.";
                else if (!validName(name)) notice = "Enter a name (1-15 letters, digits or hyphens).";
                else {
                    auto duplicate = std::find_if(slots.begin(), slots.end(), [&](const Slot &slot) {
                        return std::equal(name.begin(), name.end(), slot.name.begin(), slot.name.end(),
                            [](unsigned char left, unsigned char right) {
                                return std::tolower(left) == std::tolower(right);
                            });
                    });
                    auto path = std::filesystem::path("saves") / (name + ".d2xsave");
                    if (duplicate != slots.end() || std::filesystem::exists(path))
                        notice = "That character name is already in use.";
                    else return CharacterChoice{path, heroes[hero].name, name, true};
                }
            }
        } else {
            int direction = int(IsKeyPressed(KEY_PAGE_DOWN)) - int(IsKeyPressed(KEY_PAGE_UP));
            float wheel = GetMouseWheelMove();
            direction += wheel < 0 ? 1 : wheel > 0 ? -1 : 0;
            if (direction) page = std::clamp(page + direction, 0, std::max(0, (int(slots.size()) - 1) / 8));
            for (int i = 0; i < 8; ++i) {
                int index = page * 8 + i;
                Rectangle bounds{37.f + (i % 2) * 272.f, 86.f + (i / 2) * 92.f, 272, 92};
                if (index < int(slots.size()) && click && hit(bounds, mouse)) {
                    selected = index;
                    notice.clear();
                }
            }
            if (escape || (click && hit(backButton, mouse))) return std::nullopt;
            if (click && hit(newButton, mouse)) { creating = true; name.clear(); hero = -1; notice.clear(); }
            else if (click && hit(deleteButton, mouse) && selected >= 0) confirmDelete = true;
            else if ((enter || (click && hit(okayButton, mouse))) && selected >= 0) {
                try {
                    loadSave(slots[selected].path);
                    return CharacterChoice{slots[selected].path, slots[selected].characterClass,
                                           slots[selected].name, false};
                } catch (const std::exception &error) { notice = error.what(); }
            }
        }
        BeginTextureMode(target);
        ClearBackground(BLACK);
        BeginScissorMode(offsetX, offsetY, screenWidth, screenHeight);
        rlPushMatrix();
        rlTranslatef(float(offsetX), float(offsetY), 0);
        drawBackground(creating ? creation : selection);
        if (creating) {
            for (size_t i = 0; i < heroes.size(); ++i) {
                const auto &art = hero == int(i) ? chosen[i] : idle[i];
                const auto *cel = art.frame(0, int(GetTime() * 8) % art.count);
                if (cel)
                    DrawTexture(cel->texture, int(heroes[i].position.x - cel->texture.width / 2),
                                int(heroes[i].position.y - cel->texture.height / 2), WHITE);
            }
            centered(text, "CREATE NEW CHARACTER", 20, 24);
            if (hero >= 0) centered(text, heroes[hero].name, 65, 24);
            text.label("Character Name", 320, 471, 16);
            DrawRectangle(318, 493, 165, 28, {12, 10, 8, 230});
            text.label(name + (int(GetTime() * 2) % 2 ? "|" : ""), 324, 497, 16);
            text.label("Expansion Character", 339, 526, 16);
            text.label("X", 318, 526, 16);
        } else {
            centered(text, "SELECT CHARACTER", 22, 24);
            if (slots.empty()) centered(text, "No characters yet. Create a new character.", 250, 16);
            for (int i = 0; i < 8; ++i) {
                int index = page * 8 + i;
                if (index >= int(slots.size())) break;
                int x = 37 + (i % 2) * 272, y = 86 + (i / 2) * 92;
                if (index == selected) drawAnimation(highlight, x, y);
                const auto &art = chosen[heroIndex(slots[index].characterClass)];
                if (const auto *cel = art.frame(0, 0))
                    DrawTexturePro(cel->texture,
                                   {0, 0, float(cel->texture.width), float(cel->texture.height)},
                                   {float(x + 5), float(y + 5), 68, 78}, {0, 0}, 0, WHITE);
                text.label(slots[index].name, x + 78, y + 14, 16);
                text.label("Level " + std::to_string(slots[index].level) + " " +
                           slots[index].characterClass, x + 78, y + 39, 16);
                text.label("Expansion", x + 78, y + 61, 14);
            }
            if (slots.size() > 8)
                text.label(std::to_string(page + 1) + "/" + std::to_string((slots.size() + 7) / 8),
                           602, 435, 16);
            drawButton(text, button, newButton, "Create New");
            drawButton(text, button, deleteButton, "Delete", selected >= 0);
        }
        drawButton(text, button, backButton, creating ? "Cancel" : "Exit");
        drawButton(text, button, okayButton, creating ? "Create" : "Play",
                   creating ? hero >= 0 && validName(name) : selected >= 0);
        if (!notice.empty()) centered(text, notice, 583, 14, {255, 180, 142, 255});
        if (confirmDelete) {
            frame({246, 170, 308, 210});
            text.inBox("Delete " + slots[selected].name + "?", {250, 208, 300, 42}, 16);
            drawButton(text, button, {280, 308, 125, 40}, "Cancel");
            drawButton(text, button, {420, 308, 125, 40}, "Delete");
        }
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