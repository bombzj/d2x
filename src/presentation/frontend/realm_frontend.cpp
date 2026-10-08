#include "realm_frontend.hpp"
#include "content/character/realm_portrait.hpp"
#include "content/string_table.hpp"
#include "presentation/graphics/primitives.hpp"
#include "resources/data_table.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <limits>

namespace d2x {
namespace {
void wipe(std::string &s) {
    for (size_t i = 0; i < s.size(); ++i)
        reinterpret_cast<volatile char *>(s.data())[i] = 0;
    s.clear();
}
ClassicFont font(Graphics &g, Archives &a, const std::string &name, int color = 0) {
    ClassicFont f;
    const auto base = "data/local/font/latin/" + name;
    f.glyphs = g.single(base + ".dc6");
    if (color) {
        constexpr size_t shifts = 0x6B600 + 13 * 3;
        const auto palette = a.read("data/global/palette/sky/pal.pl2");
        if (color < 0 || color >= 13 || palette.size() < shifts + 13 * 256)
            throw std::runtime_error("Missing original frontend font color transforms");
        const auto *source = g.animation(base + ".dc6");
        if (!source) throw std::runtime_error("Missing original frontend font");
        f.glyphs.frames.clear();
        f.glyphs.directions = source->directions;
        f.glyphs.count = source->framesPerDirection;
        for (auto glyph : source->frames) {
            for (auto &pixel : glyph.pixels)
                if (pixel) pixel = palette[shifts + size_t(color) * 256 + pixel];
            f.glyphs.frames.push_back(g.upload(glyph));
        }
    }
    const auto table = a.read(base + ".tbl");
    if (table.size() < 3596 || f.glyphs.frames.empty())
        throw std::runtime_error("Missing original frontend font");
    for (int i = 0; i < 256; ++i) {
        f.widths[i] = table[12 + i * 14 + 3];
        f.indices[i] = table[12 + i * 14 + 8];
    }
    f.ready = true;
    return f;
}
bool playable(const OnlineCharacter &c) {
    return c.characterClass && c.expansion.value_or(false) && !c.ladder.value_or(true) &&
           !(c.hardcore.value_or(true) && c.dead.value_or(true));
}
bool busy(OnlineStage s) {
    return s != OnlineStage::Idle && s != OnlineStage::Cancelled && s != OnlineStage::Failed &&
           s != OnlineStage::RealmSelection && s != OnlineStage::CharacterSelection &&
           s != OnlineStage::Lobby && s != OnlineStage::ListingGames && s != OnlineStage::ProtocolReady;
}
bool validHostAddress(std::string_view address) {
    for (unsigned part = 0; part < 4; ++part) {
        const auto dot = address.find('.');
        const auto digits = address.substr(0, dot);
        unsigned value{};
        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
        if (digits.empty() || digits.size() > 3 || (digits.size() > 1 && digits.front() == '0') ||
            error != std::errc{} || end != digits.data() + digits.size() || value > 255 ||
            (part == 0 && (value == 0 || value >= 224)) || (part < 3) != (dot != address.npos)) return false;
        if (dot != address.npos) address.remove_prefix(dot + 1);
    }
    return true;
}
struct PortraitKeyLess {
    bool operator()(const Bytes &left, const Bytes &right) const {
        const auto common = std::min(left.size(), right.size());
        for (size_t i = 0; i < common; ++i) {
            if (left[i] != right[i]) return left[i] < right[i];
        }
        return left.size() < right.size();
    }
};
} // namespace
struct RealmFrontend::Impl {
    FrameInput input;
    Graphics sky, units, lobbyControls, checkboxControls;
    std::unique_ptr<Graphics> heroGraphics;
    struct Hero {
        GpuAnimation idle, hover, forward, chosen, back, forwardOverlay, chosenOverlay, backOverlay;
        int pose{};
        float elapsed{};
    };
    std::array<Hero, 7> heroes;
    int hero{-1}, difficulty{}, gameOffset{}, gameInfoOffset{}, realmSelected{};
    enum class LobbyPanel { Notice, Create, Join };
    LobbyPanel lobbyPanel{LobbyPanel::Notice};
    bool hardcore{}, confirmHardcore{};
    std::string verifyPassword, characterName, deleteName;
    uint64_t lobbyGeneration{};
    Archives &archives;
    RealmPortraitCatalog portraitCatalog;
    std::unique_ptr<Graphics> characterGraphics;
    std::map<Bytes, GpuAnimation, PortraitKeyLess> portraits;
    ClassicStrings strings;
    ClassicFont normal, buttonFont, inputFont, titleFont, lobbyFont, roomGoldFont, roomGrayFont, lanTitleFont, lanTipFont;
    UiPainter text, buttonText, inputText, title, lobbyText, roomGoldText, roomGrayText, lanTitle, lanTip;
    std::map<std::string, GpuAnimation> art;
    std::vector<std::string> classes;
    std::string account, password, gameName, gamePassword, description;
    std::string lanAddresses, hostAddress = "127.0.0.1";
    bool replaceHostAddress{}, lanArtReady{};
    int focus{}, selected{}, page{}, players{8}, difference{4};
    int lastClickedCharacter{-1};
    double lastCharacterClick{};
    std::string lastClickedGame;
    uint32_t lastClickedGameIndex{};
    uint64_t lastGameConnection{}, lastGameGeneration{};
    double lastGameClick{};
    int maximumCharacterLevel{};
    bool restrictLevels{false};
    uint64_t characterGeneration{};
    std::vector<std::string> characterNames;
    std::vector<Bytes> characterPortraits;
    FrontendPage previous{FrontendPage::Main};
    Vector2 mouse{};
    bool clicked{}, enabled{};
    FrontendIntent intent;
    Impl(Archives &a)
        : sky(a, "data/global/palette/sky/pal.dat"), units(a, "data/global/palette/units/pal.dat"),
          lobbyControls(a, "data/global/palette/act1/pal.dat"),
          checkboxControls(a, "data/global/palette/fechar/pal.dat"),
          archives(a), portraitCatalog(a), strings(a),
          normal(font(units, a, "font16")), buttonFont(font(units, a, "fontexocet10")),
          inputFont(font(units, a, "fontformal11")), titleFont(font(units, a, "font30")),
          lobbyFont(font(units, a, "fontridiculous")),
          roomGoldFont(font(units, a, "font8", 4)), roomGrayFont(font(units, a, "font8", 5)),
          text(normal),
          buttonText(buttonFont), inputText(inputFont), title(titleFont), lobbyText(lobbyFont),
          roomGoldText(roomGoldFont, 0), roomGrayText(roomGrayFont, 0), lanTitle(lanTitleFont), lanTip(lanTipFont) {
        auto load = [&](Graphics &g, const char *id, const char *path) {
            auto value = g.single(std::string("data/global/ui/") + path + ".dc6");
            if (value.frames.empty())
                throw std::runtime_error(std::string("Missing original frontend art: ") + path);
            art.emplace(id, std::move(value));
        };
        load(sky, "main", "FrontEnd/gameselectscreenEXP");
        load(sky, "characters", "CharSelect/characterselectscreenEXP");
        load(sky, "highlight", "CharSelect/charselectbox");
        load(units, "realms", "FrontEnd/realmbckg");
        load(units, "join", "bigmenu/joingamebckg");
        for (auto [id, path] : {std::pair{"fireL", "FrontEnd/D2LogoFireLeft"},
                                {"fireR", "FrontEnd/D2LogoFireRight"},
                                {"blackL", "FrontEnd/D2LogoBlackLeft"},
                                {"blackR", "FrontEnd/D2LogoBlackRight"},
                                {"wide", "FrontEnd/3widebuttonblank"},
                                {"battle", "FrontEnd/WideButtonBlank02"},
                                {"thin", "FrontEnd/NarrowButtonBlank"},
                                {"medium", "FrontEnd/MediumButtonBlank"},
                                {"tall", "CharSelect/TallButtonBlank"},
                                {"textbox", "FrontEnd/textbox2"},
                                {"realm", "CharSelect/realmselect"},
                                {"realmButton", "CharSelect/realmselectbuttonthin"},
                                {"lobby", "bigmenu/bnet"},
                                {"create", "bigmenu/creategamebckg"},
                                {"arrows", "bigmenu/numberarrows"},
                                {"radio", "bigmenu/radiobutton"},
                                {"popup", "FrontEnd/PopUpLarge"},
                                {"cursor", "CURSOR/ohand"}})
            load(units, id, path);
        // clickbox's unchecked/checked frames use the original Fechar palette.
        load(checkboxControls, "check", "FrontEnd/clickbox");
        for (auto [id, path] : {std::pair{"createButton", "bigmenu/creategamebutton"},
                                {"gameButton", "bigmenu/gamebuttonblank"},
                                {"cancel", "bigmenu/cancelbuttonblank"},
                                {"tabs", "bigmenu/chatrighttopbuttons"},
                                {"chatButton", "bigmenu/chatrightbuttons"},
                                {"leftButton", "bigmenu/chatleftbuttons"}})
            load(lobbyControls, id, path);
        DataTable stats(a.read("data/global/excel/charstats.txt"));
        for (size_t i = 0; i < stats.rows().size(); ++i) {
            auto name = stats.value(i, "class");
            if (!name.empty() && name != "Expansion")
                classes.emplace_back(name);
        }
        DataTable experience(a.read("data/global/excel/experience.txt"));
        for (size_t i = 0; i < experience.rows().size(); ++i) {
            if (experience.value(i, "Level") != "MaxLvl") continue;
            for (const auto &name : classes) {
                const auto level = experience.number(i, name);
                if (level && *level > 0 && *level <= 255)
                    maximumCharacterLevel = std::max(maximumCharacterLevel, *level);
            }
            break;
        }
    }
    ~Impl() {
        wipe(password);
        wipe(verifyPassword);
        wipe(gamePassword);
    }
    std::string s(int id) const {
        const auto value = strings.find(id);
        if (value.empty())
            throw std::runtime_error("Missing original frontend string: " + std::to_string(id));
        return std::string(value);
    }
    void image(const char *id, int x, int y, int frame = 0, Color tint = WHITE) {
        const auto *cel = art.at(id).frame(0, frame);
        DrawTexture(cel->texture, x, y, tint);
    }
    // DC6 screen and control tiles retain their native dimensions; no stretched tiles.
    void tiles(const char *id, int x, int y, int columns, int first = 0, int count = -1, Color tint = WHITE) {
        const auto &a = art.at(id);
        if (count < 0)
            count = a.count - first;
        int dx = x, dy = y, rowHeight = 0;
        for (int i = 0; i < count; ++i) {
            const auto *cel = a.frame(0, first + i);
            image(id, dx, dy, first + i, tint);
            dx += cel->texture.width;
            rowHeight = std::max(rowHeight, cel->texture.height);
            if ((i + 1) % columns == 0) {
                dx = x;
                dy += rowHeight;
                rowHeight = 0;
            }
        }
    }
    void emit(FrontendCommand command) {
        if (intent.command == FrontendCommand::None)
            intent.command = command;
    }
    bool button(const char *id, int x, int y, std::string label, bool active = true, int segments = 1,
                int base = 0, const UiPainter *caption = nullptr) {
        const auto &a = art.at(id);
        int width = 0;
        for (int i = 0; i < segments; ++i)
            width += a.frame(0, base + i)->texture.width;
        Rectangle r{float(x), float(y), float(width), float(a.frame(0, base)->texture.height)};
        active = active && enabled;
        bool down = active && CheckCollisionPointRec(mouse, r) && input.leftHeld;
        int first = base + (down ? segments : 0);
        Color tint = active ? WHITE : GRAY;
        if (!active && base + segments * 3 <= a.count) {
            first = base + segments * 2;
            tint = WHITE;
        }
        tiles(id, x, y, segments, first, segments, tint);
        std::istringstream lines(label);
        std::string line;
        int count = 1 + int(std::count(label.begin(), label.end(), '\n')),
            top = y + (int(r.height) - count * 16) / 2;
        // Keep the original glyph mask, but render button captions in solid
        // black so the grayscale glyph pixels do not form a bright rim.
        const UiPainter &painter = caption && caption != &lobbyText ? *caption :
            r.height >= 35 ? buttonText : lobbyText;
        while (std::getline(lines, line)) {
            painter.inBox(
                line, {float(x + (down ? 1 : 0)), float(top + (down ? 1 : 0)), r.width, 16}, 16,
                BLACK);
            top += 16;
        }
        return active && clicked && CheckCollisionPointRec(mouse, r);
    }
    void label(const std::string &v, int x, int y, Color c = parchment) { text.label(v, x, y, 16, c); }
    void centered(const std::string &v, int y) { label(v, (800 - text.measure(v, 16)) / 2, y); }
    void wrap(const std::string &v, int x, int y, int width, bool center = false,
              int bottom = std::numeric_limits<int>::max(), const UiPainter *font = nullptr) {
        const auto &painter = font ? *font : text;
        std::istringstream words(v);
        std::string word, line;
        auto draw = [&]() {
            if (y + 18 > bottom) { line.clear(); return; }
            painter.label(line, center ? x + (width - painter.measure(line, 16)) / 2 : x, y, 16);
            y += 18;
            line.clear();
        };
        while (words >> word) {
            if (y + 18 > bottom) break;
            while (!word.empty() && painter.measure(word, 16) > width) word.pop_back();
            auto next = line.empty() ? word : line + " " + word;
            if (!line.empty() && painter.measure(next, 16) > width)
                draw();
            if (!line.empty())
                line += ' ';
            line += word;
        }
        if (!line.empty())
            draw();
    }
    void field(std::string &value, Rectangle r, int index, size_t limit, bool secret = false,
               bool drawBox = true) {
        if (enabled && clicked && CheckCollisionPointRec(mouse, r))
            focus = index;
        if (drawBox)
            image("textbox", int(r.x), int(r.y));
        if (enabled && focus == index) {
            for (unsigned char c : input.entryText)
                if (c >= 32 && c <= 126 && value.size() < limit)
                    value.push_back(char(c));
            input.entryText.clear();
            if (input.backspace) {
                if (!value.empty())
                    value.pop_back();
                input.backspace = false;
            }
        }
        auto shown = secret ? std::string(value.size(), '*') : value;
        if (enabled && focus == index && int(GetTime() * 2) % 2 == 0)
            shown += '|';
        while (!shown.empty() && inputText.measure(shown, 16) > r.width - 12)
            shown.erase(shown.begin());
        inputText.label(shown, int(r.x + 8), int(r.y + 4), 16, WHITE);
    }
    void logo() {
        for (const auto *id : {"blackL", "blackR", "fireL", "fireR"}) {
            const auto *cel = art.at(id).frame(0, int(GetTime() * 25));
            const Vec at{400.f, 120.f - float(cel->texture.height)};
            if (id[0] == 'f')
                softAdditiveSprite(cel, at);
            else
                sprite(cel, at);
        }
    }
    void portrait(const OnlineCharacter &c, int x, int y) {
        auto found = portraits.find(c.portrait);
        if (found == portraits.end()) {
            GpuAnimation animation;
            if (const auto parts = portraitCatalog.decode(c)) {
                std::array<const char *, 16> equipment;
                for (size_t i = 0; i < 16; ++i)
                    equipment[i] = parts->components[i].c_str();
                animation =
                    characterGraphics->composite("chars", parts->token, "tn", parts->weapon, &equipment);
                if (!animation.completeComposite)
                    animation = {};
            }
            found = portraits.emplace(c.portrait, std::move(animation)).first;
        }
        if (const auto *cel = found->second.frame(0, int(GetTime() * 25)))
            sprite(cel, {float(x), float(y)});
    }
    void main(std::string_view gateway) {
        tiles("main", 0, 0, 4);
        logo();
        if (button("wide", 265, 290, s(5106), true, 2))
            emit(FrontendCommand::SinglePlayer);
        if (button("battle", 265, 332, s(5107), true, 2))
            emit(FrontendCommand::Online);
        auto gatewayLabel = s(11049);
        const auto placeholder = gatewayLabel.find("%s");
        if (placeholder != std::string::npos)
            gatewayLabel.replace(placeholder, 2, gateway);
        button("thin", 265, 366, gatewayLabel, false, 2);
        if (button("wide", 265, 400, s(5116), true, 2))
            emit(FrontendCommand::TcpIp);
        button("medium", 265, 495, s(5110), false);
        button("medium", 410, 495, s(5111), false);
        if (button("wide", 265, 535, s(5109), true, 2))
            emit(FrontendCommand::Exit);
        label("D2X", 30, 565, WHITE);
    }
    void prepareLanArt() {
        if (lanArtReady) return;
        // LAN-only assets must not become prerequisites for the original-server UI.
        lanTitleFont = font(units, archives, "font42");
        lanTipFont = font(units, archives, "fontformal12");
        auto load = [&](Graphics &graphics, const char *id, const char *path) {
            auto value = graphics.single(std::string("data/global/ui/") + path + ".dc6");
            if (value.frames.empty())
                throw std::runtime_error(std::string("Missing original TCP/IP art: ") + path);
            art.insert_or_assign(id, std::move(value));
        };
        load(units, "tcpip", "FrontEnd/TCPIPscreen");
        load(units, "joinHostPopup", "FrontEnd/PopUpOkCancel2");
        load(lobbyControls, "joinHostButton", "FrontEnd/CancelButtonBlank");
        lanArtReady = true;
    }
    void tcpIp(bool joining) {
        prepareLanArt();
        tiles("tcpip", 0, 0, 4);
        lanTitle.inBox(s(5117), {0, 35, 800, 50}, 16, parchment);
        centered(s(5121), 110);
        wrap(lanAddresses.empty() ? s(5124) : lanAddresses, 180, 130, 440, true, 168);
        const bool available = enabled;
        if (joining) enabled = false;
        if (button("wide", 265, 170, s(5118), true, 2)) emit(FrontendCommand::HostLan);
        if (button("wide", 265, 230, s(5119), true, 2)) emit(FrontendCommand::OpenJoinHost);
        if (button("medium", 40, 535, s(5103))) emit(FrontendCommand::Back);
        if (!joining) {
            const int width = art.at("wide").frame(0, 0)->texture.width + art.at("wide").frame(0, 1)->texture.width;
            const int height = art.at("wide").frame(0, 0)->texture.height;
            const bool overJoin = CheckCollisionPointRec(mouse, {265, 230, float(width), float(height)});
            wrap(s(overJoin ? 5123 : 5122), 265, 310, width, true, 510, &lanTip);
            return;
        }
        // Original 264 x 176 panel and 96 x 32 buttons. The textbox extends
        // its original middle pixels; borders and glyphs retain native size.
        tiles("joinHostPopup", 268, 160, 2);
        wrap(s(5120), 303, 182, 194, true, 224);
        const auto &box = art.at("textbox").frame(0, 0)->texture;
        constexpr int x = 291, y = 230, width = 218, edge = 8;
        DrawTextureRec(box, {0, 0, edge, float(box.height)}, {x, y}, WHITE);
        for (int offset = edge; offset < width - edge;) {
            const int count = std::min(box.width - 2 * edge, width - edge - offset);
            DrawTextureRec(box, {edge, 0, float(count), float(box.height)}, {float(x + offset), y}, WHITE);
            offset += count;
        }
        DrawTextureRec(box, {float(box.width - edge), 0, edge, float(box.height)}, {x + width - edge, y}, WHITE);
        enabled = available;
        if (enabled && replaceHostAddress && (!input.entryText.empty() || input.backspace || input.entryDelete)) {
            hostAddress.clear(); replaceHostAddress = false;
        }
        if (enabled && clicked && CheckCollisionPointRec(mouse, {x, y, width, 26})) replaceHostAddress = true;
        if (replaceHostAddress)
            DrawRectangle(x + 7, y + 3, inputText.measure(hostAddress, 16) + 2, 20, {35, 44, 75, 220});
        std::erase_if(input.entryText, [](unsigned char c) { return c != '.' && (c < '0' || c > '9'); });
        field(hostAddress, {x, y, width, 26}, 0, 15, false, false);
        const bool valid = validHostAddress(hostAddress);
        if (button("joinHostButton", 284, 290, s(5103))) emit(FrontendCommand::Back);
        if (button("joinHostButton", 420, 290, s(5102), valid) || (enabled && input.enter && valid)) {
            emit(FrontendCommand::JoinLan);
            intent.name = hostAddress;
        }
    }
    void login() {
        tiles("main", 0, 0, 4);
        logo();
        wrap(s(5205), 180, 233, 440, true);
        label(s(5224), 300, 289);
        const auto oldAccount = account;
        field(account, {300, 307, 169, 26}, 0, 15);
        if (account != oldAccount) {
            wipe(password); wipe(verifyPassword); intent.editedAccount = account;
        }
        label(s(5225), 300, 345);
        field(password, {300, 363, 169, 26}, 1, 15, true);
        if (button("wide", 245, 452, s(5288), !account.empty() && !password.empty(), 2) ||
            (enabled && input.enter && !account.empty() && !password.empty())) {
            emit(FrontendCommand::Login);
            intent.name = account;
            intent.password = password;
        }
        button("wide", 245, 498, s(11108), false, 2);
        if (button("wide", 245, 542, s(5221), true, 2))
            emit(FrontendCommand::OpenRegister);
        if (button("medium", 12, 542, s(5101)))
            emit(FrontendCommand::Back);
    }
    void registration() {
        tiles("main", 0, 0, 4);
        logo();
        wrap(s(5223), 180, 218, 440, true);
        label(s(5224), 300, 276);
        const auto oldAccount = account;
        field(account, {300, 294, 169, 26}, 0, 15);
        if (account != oldAccount) {
            wipe(password); wipe(verifyPassword); intent.editedAccount = account;
        }
        label(s(5225), 300, 332);
        field(password, {300, 350, 169, 26}, 1, 15, true);
        label(s(5226), 300, 388);
        field(verifyPassword, {300, 406, 169, 26}, 2, 15, true);
        const bool valid = account.size() >= 2 && password.size() >= 2 && password == verifyPassword;
        if (button("wide", 245, 485, s(5221), valid, 2) || (enabled && valid && input.enter)) {
            emit(FrontendCommand::Register);
            intent.name = account;
            intent.password = std::move(password);
            wipe(password);
            wipe(verifyPassword);
        }
        if (button("medium", 12, 542, s(5101)))
            emit(FrontendCommand::Back);
    }
    void realms(const OnlineView &v) {
        tiles("realms", 0, 0, 4);
        title.inBox(s(5289), {0, 30, 800, 40}, 16, parchment);
        wrap(s(5291), 180, 95, 440, true);
        realmSelected = std::clamp(realmSelected, 0, std::max(0, int(v.realms.size()) - 1));
        const int start = realmSelected / 12 * 12;
        for (int i = start; i < int(v.realms.size()) && i < start + 12; ++i) {
            const int y = 192 + (i - start) * 22;
            if (enabled && clicked && CheckCollisionPointRec(mouse, {60, float(y), 330, 22}))
                realmSelected = i;
            label(v.realms[size_t(i)].name, 70, y, i == realmSelected ? WHITE : parchment);
        }
        if (v.realms.empty())
            label(s(5290), 70, 220);
        else
            wrap(v.realms[size_t(realmSelected)].description, 462, 330, 287);
        if (v.realms.size() > 12) {
            if (button("medium", 60, 466, "<", start > 0))
                realmSelected = start - 1;
            if (button("medium", 230, 466, ">", start + 12 < int(v.realms.size())))
                realmSelected = start + 12;
        }
        if (button("medium", 34, 538, s(5101)))
            emit(FrontendCommand::Back);
        if (button("medium", 628, 538, s(5102), !v.realms.empty()) ||
            (enabled && !v.realms.empty() && input.enter)) {
            emit(FrontendCommand::SelectRealm);
            intent.name = v.realms[size_t(realmSelected)].name;
        }
    }
    void creation() {
        // Same native expansion hero positions and animation timing as the offline selector.
        constexpr std::array folders{"amazon",  "assassin",  "necromancer", "barbarian",
                                     "paladin", "sorceress", "druid"};
        constexpr std::array tokens{"am", "as", "ne", "ba", "pa", "so", "dz"};
        constexpr std::array<int, 7> ids{0, 6, 2, 4, 3, 1, 5},
            descriptions{5128, 22519, 5129, 5130, 5132, 5131, 22518};
        constexpr std::array<Rectangle, 7> hits{{{70, 220, 55, 200},
                                                 {175, 235, 50, 180},
                                                 {265, 220, 55, 175},
                                                 {364, 201, 90, 170},
                                                 {490, 210, 65, 180},
                                                 {580, 240, 65, 160},
                                                 {680, 220, 70, 195}}};
        constexpr std::array<Vector2, 7> positions{
            {{100, 339}, {231, 365}, {300, 335}, {400, 330}, {521, 338}, {626, 352}, {720, 370}}};
        constexpr std::array<float, 7> idleTimes{2.5f, 2.5f, 1.2f, 0, 2.5f, 2.5f, 1.5f},
            forwardTimes{2.2f, 3.8f, 2, 2.5f, 3.4f, 2.3f, 4.8f},
            backTimes{1.5f, 1.5f, 1.5f, 1, 1.3f, 1.2f, 1.5f};
        if (!heroGraphics) {
            heroGraphics = std::make_unique<Graphics>(archives, "data/global/palette/fechar/pal.dat");
            auto load = [&](const std::string &path) {
                auto a = heroGraphics->single("data/global/ui/FrontEnd/" + path + ".dc6");
                if (a.frames.empty())
                    throw std::runtime_error("Missing original hero art: " + path);
                return a;
            };
            art.emplace("creation", load("charactercreationscreenEXP"));
            art.emplace("campfire", load("fire"));
            for (size_t i = 0; i < heroes.size(); ++i) {
                const auto base = std::string(folders[i]) + "/" + tokens[i];
                auto &h = heroes[i];
                h.idle = load(base + "nu1");
                h.hover = load(base + "nu2");
                h.forward = load(base + "fw");
                h.chosen = load(base + "nu3");
                h.back = load(base + "bw");
                if (i >= 2 && i <= 5)
                    h.forwardOverlay = load(base + "fws");
                if (i == 2 || i == 5) {
                    h.chosenOverlay = load(base + "nu3s");
                    h.backOverlay = load(base + "bws");
                }
            }
        }
        tiles("creation", 0, 0, 4);
        title.inBox(s(5127), {0, 17, 800, 36}, 16, parchment);
        if (hero >= 0) {
            centered(classes[size_t(ids[size_t(hero)])], 65);
            wrap(s(descriptions[size_t(hero)]), 255, 104, 290, true);
        }
        for (size_t i = 0; i < heroes.size(); ++i) {
            auto &h = heroes[i];
            if (enabled && clicked && CheckCollisionPointRec(mouse, hits[i]) && hero != int(i)) {
                if (hero >= 0) {
                    heroes[size_t(hero)].pose = 3;
                    heroes[size_t(hero)].elapsed = 0;
                }
                hero = int(i);
                h.pose = 1;
                h.elapsed = 0;
            }
        }
        auto drawHero = [&](size_t i) {
            auto &h = heroes[i];
            h.elapsed += std::min(GetFrameTime(), .1f);
            if (h.pose == 1 && h.elapsed >= forwardTimes[i]) {
                h.pose = 2;
                h.elapsed = 0;
            }
            if (h.pose == 3 && h.elapsed >= backTimes[i]) {
                h.pose = 0;
                h.elapsed = 0;
            }
            const auto &animation = h.pose == 1                                         ? h.forward
                                    : h.pose == 2                                       ? h.chosen
                                    : h.pose == 3                                       ? h.back
                                    : enabled && CheckCollisionPointRec(mouse, hits[i]) ? h.hover
                                                                                        : h.idle;
            const float duration = h.pose == 1   ? forwardTimes[i]
                                   : h.pose == 3 ? backTimes[i]
                                   : h.pose == 0 ? idleTimes[i]
                                                 : 0;
            const int progress = int(h.elapsed * (duration > 0 ? animation.count / duration : 25));
            const int frame = (h.pose == 1 || h.pose == 3) ? std::min(progress, animation.count - 1)
                                                           : progress % animation.count;
            const auto *cel = animation.frame(0, frame);
            sprite(cel, {positions[i].x, positions[i].y - cel->texture.height});
            const auto &overlay = h.pose == 1   ? h.forwardOverlay
                                  : h.pose == 2 ? h.chosenOverlay
                                                : h.backOverlay;
            if (h.pose && !overlay.frames.empty()) {
                const auto *extra = overlay.frame(0, frame);
                const Vec at{positions[i].x, positions[i].y - extra->texture.height};
                if (h.pose != 1 || i == 2 || i == 5)
                    softAdditiveSprite(extra, at);
                else
                    sprite(extra, at);
            }
        };
        for (size_t i = 0; i < heroes.size(); ++i)
            if (int(i) != hero)
                drawHero(i);
        if (hero >= 0)
            drawHero(size_t(hero));
        if (const auto *cel = art.at("campfire").frame(0, int(GetTime() * 25)))
            softAdditiveSprite(cel, {380, 335});
        label(s(5125), 321, 475);
        field(characterName, {318, 493, 169, 26}, 0, 15);
        image("check", 318, 526, 1);
        label(s(22731), 339, 526, GREEN);
        image("check", 318, 548, hardcore ? 1 : 0);
        label(s(5126), 339, 548);
        if (enabled && clicked && CheckCollisionPointRec(mouse, {318, 548, 20, 20})) {
            if (hardcore)
                hardcore = false;
            else
                confirmHardcore = true;
        }
        if (button("medium", 34, 538, s(5101)))
            emit(FrontendCommand::Back);
        const bool valid = hero >= 0 && !characterName.empty();
        if (button("medium", 628, 538, s(5102), valid) || (enabled && valid && input.enter)) {
            emit(FrontendCommand::CreateCharacter);
            intent.name = characterName;
            intent.characterClass = uint8_t(ids[size_t(hero)]);
            intent.hardcore = hardcore;
        }
    }
    void characters(const OnlineView &v) {
        tiles("characters", 0, 0, 4);
        image("realm", 608, 8);
        text.inBox(s(11058), {608, 10, 182, 22}, 16, parchment);
        text.inBox(v.selectedRealm, {608, 42, 182, 27}, 16, parchment);
        if (button("realmButton", 608, 81, s(11057), true, 1, 0, &text))
            emit(FrontendCommand::ChangeRealm);
        std::vector<std::string> names;
        std::vector<Bytes> previews;
        for (const auto &c : v.characters) {
            names.push_back(c.name);
            previews.push_back(c.portrait);
        }
        if (characterGeneration != v.connectionGeneration || names != characterNames ||
            previews != characterPortraits) {
            characterGeneration = v.connectionGeneration;
            characterNames = std::move(names);
            characterPortraits = std::move(previews);
            selected = 0;
            page = 0;
            lastClickedCharacter = -1;
            portraits.clear();
            characterGraphics = std::make_unique<Graphics>(archives);
            for (size_t i = 0; i < v.characters.size(); ++i)
                if (playable(v.characters[i])) {
                    selected = int(i);
                    page = selected / 8;
                    break;
                }
        }
        auto changePage = [&](int direction) {
            const int next = std::clamp(page + direction, 0, std::max(0, (int(v.characters.size()) - 1) / 8));
            if (next != page) { page = next; lastClickedCharacter = -1; }
        };
        if (enabled)
            changePage(input.pageDelta + (input.wheel < 0 ? 1 : input.wheel > 0 ? -1 : 0));
        else lastClickedCharacter = -1;
        const auto &box = art.at("highlight");
        const int width = box.frame(0, 0)->texture.width + box.frame(0, 1)->texture.width;
        const int height = std::max(box.frame(0, 0)->texture.height, box.frame(0, 1)->texture.height);
        if (v.characters.size() > 8) {
            // Keep paging controls in the gutter, outside the fourth character row.
            const int x = 37 + width * 2 + 8;
            int y = 419;
            for (int direction : {-1, 1}) {
                const int first = direction < 0 ? 0 : 2;
                const auto *cel = art.at("arrows").frame(0, first);
                const Rectangle bounds{float(x), float(y), float(cel->texture.width), float(cel->texture.height)};
                const bool active = enabled && (direction < 0 ? page > 0 : (page + 1) * 8 < int(v.characters.size()));
                const bool down = active && input.leftHeld && CheckCollisionPointRec(mouse, bounds);
                image("arrows", x, y, first + int(down), active ? WHITE : GRAY);
                if (active && clicked && CheckCollisionPointRec(mouse, bounds)) changePage(direction);
                y += cel->texture.height;
            }
        }
        bool doubleClick = false, clickedCharacter = false;
        for (int i = page * 8; i < int(v.characters.size()) && i < page * 8 + 8; ++i) {
            int slot = i - page * 8, x = 37 + (slot % 2) * width, y = 86 + (slot / 2) * height;
            Rectangle r{float(x), float(y), float(width), float(height)};
            if (enabled && clicked && CheckCollisionPointRec(mouse, r)) {
                const double now = GetTime();
                // Same timing as the former picker and OpenDiablo2's original UI adaptation.
                doubleClick = lastClickedCharacter == i && now - lastCharacterClick < 1.25;
                selected = i;
                lastClickedCharacter = i; lastCharacterClick = now; clickedCharacter = true;
            }
            if (selected == i)
                tiles("highlight", x, y, 2);
            const auto &c = v.characters[size_t(i)];
            portrait(c, x + 36, y + 82);
            label(c.name, x + 76, y + 14, playable(c) ? WHITE : GRAY);
            if (c.characterClass && *c.characterClass < classes.size() && c.level) {
                auto line = s(5017);
                auto replace = [&](const char *token, const std::string &value) {
                    const auto pos = line.find(token);
                    if (pos != std::string::npos)
                        line.replace(pos, 2, value);
                };
                replace("%d", std::to_string(*c.level));
                line += " " + classes[*c.characterClass];
                label(line, x + 76, y + 28, WHITE);
            }
            if (c.expansion.value_or(false))
                label(s(22731), x + 76, y + 42, GREEN);
            if (c.ladder.value_or(false))
                label(s(5315), x + 76, y + 56, GRAY);
        }
        if (enabled && clicked && !clickedCharacter) lastClickedCharacter = -1;
        if (!v.characters.empty())
            title.inBox(v.characters[size_t(selected)].name, {34, 20, 564, 47}, 16, parchment);
        if (v.characters.size() > 8) {
            const auto indicator = std::to_string(page + 1) + "/" + std::to_string((v.characters.size() + 7) / 8);
            label(indicator, 37 + width * 2 + (27 - text.measure(indicator, 16)) / 2, 447, WHITE);
        }
        auto tallCaption = [&](int id) {
            auto value = s(id);
            const auto at = value.rfind(' ');
            if (at != std::string::npos)
                value[at] = '\n';
            return value;
        };
        if (button("tall", 34, 467, tallCaption(22743)))
            emit(FrontendCommand::OpenCreateCharacter);
        button("tall", 234, 467, tallCaption(22742), false);
        if (button("tall", 434, 467, tallCaption(22744), !v.characters.empty()))
            deleteName = v.characters[size_t(selected)].name;
        if (button("medium", 34, 538, s(5101)))
            emit(FrontendCommand::Back);
        bool canSelect = !v.characters.empty() && playable(v.characters[size_t(selected)]);
        if (button("medium", 628, 538, s(5102), canSelect) ||
            (enabled && canSelect && (input.enter || doubleClick))) {
            emit(FrontendCommand::SelectCharacter);
            intent.name = v.characters[size_t(selected)].name;
        }
    }
    void spinner(int x, int y, int &value, int low, int high) {
        label(std::to_string(value), x + 8, y + 5, WHITE);
        image("arrows", x + 33, y, 0);
        image("arrows", x + 33, y + 12, 2);
        if (enabled && clicked) {
            if (CheckCollisionPointRec(mouse, {float(x + 33), float(y), 15, 12}))
                value = std::min(high, value + 1);
            if (CheckCollisionPointRec(mouse, {float(x + 33), float(y + 12), 15, 12}))
                value = std::max(low, value - 1);
        }
    }
    void lobby(const OnlineView &v) {
        tiles("lobby", 0, 0, 4);
        if (lobbyGeneration != v.gameGeneration) {
            lobbyGeneration = v.gameGeneration;
            difficulty = 0;
        }
        if (lobbyPanel != LobbyPanel::Notice)
            tiles(lobbyPanel == LobbyPanel::Join ? "join" : "create", 418, 72, 2);
        text.inBox(s(11123), {19, 72, 357, 20}, 16, Color{100, 100, 220, 255});
        const bool listing = v.stage == OnlineStage::ListingGames;
        const bool canSubmit = v.stage == OnlineStage::Lobby && !gameName.empty();
        if (lobbyPanel == LobbyPanel::Join) {
            bool doubleClickGame = false, clickedGame = false;
            if (!enabled || input.wheel || input.tab || input.backspace || input.entryDelete ||
                !input.entryText.empty()) lastClickedGame.clear();
            title.inBox(s(5151), {418, 73, 373, 38}, 16, parchment);
            label(s(5274), 428, 101);
            field(gameName, {428, 121, 166, 26}, 0, 15, false, false);
            label(s(5256), 603, 101);
            field(gamePassword, {603, 121, 168, 26}, 1, 15, true, false);
            label(s(5275), 428, 190);
            const int count = int(v.games.size());
            if (enabled && CheckCollisionPointRec(mouse, {428, 208, 166, 186}))
                gameOffset -= int(input.wheel);
            gameOffset = std::clamp(gameOffset, 0, std::max(0, count - 10));
            for (int i = gameOffset; i < count && i < gameOffset + 10; ++i) {
                const auto &game = v.games[size_t(i)];
                const int y = 212 + (i - gameOffset) * 18;
                if (enabled && clicked && CheckCollisionPointRec(mouse, {428, float(y), 166, 18})) {
                    const auto now = GetTime();
                    doubleClickGame = lastClickedGame == game.name && lastClickedGameIndex == game.index &&
                        lastGameConnection == v.connectionGeneration && lastGameGeneration == v.gameGeneration &&
                        now - lastGameClick < 1.25;
                    lastClickedGame = game.name;
                    lastClickedGameIndex = game.index;
                    lastGameConnection = v.connectionGeneration;
                    lastGameGeneration = v.gameGeneration;
                    lastGameClick = now;
                    clickedGame = true;
                    gameName = game.name;
                    gameInfoOffset = 0;
                    if (!doubleClickGame) {
                        emit(FrontendCommand::QueryGame);
                        intent.name = gameName;
                    }
                }
                auto name = game.name;
                while (!name.empty() && inputText.measure(name, 16) > 130)
                    name.pop_back();
                inputText.label(name, 432, y, 16, game.name == gameName ? WHITE : parchment);
                inputText.label(std::to_string(game.players), 574, y, 16, WHITE);
            }
            if (enabled && clicked && !clickedGame) lastClickedGame.clear();
            const auto chosenGame = std::find_if(v.games.begin(), v.games.end(),
                [&](const auto &game) { return game.name == gameName; });
            if (v.gameInfo && v.gameInfo->name == gameName) {
                const auto &info = *v.gameInfo;
                if (info.state == OnlineGameInfo::State::Ready) {
                    const auto detailLine = [](const UiPainter &painter, std::string value, int y) {
                        while (!value.empty() && painter.measure(value, 16) > 142) value.pop_back();
                        painter.inBox(value, {612, float(y), 142, 16}, 16, WHITE);
                    };
                    std::ostringstream elapsed;
                    elapsed << info.uptimeSeconds / 3600 << ':' << std::setfill('0')
                            << std::setw(2) << (info.uptimeSeconds / 60) % 60 << ':'
                            << std::setw(2) << info.uptimeSeconds % 60;
                    // Caption and layout follow the supplied original-game screenshot;
                    // MCP supplies the elapsed time, creator level and level difference.
                    detailLine(roomGoldText, "Elapsed Time: " + elapsed.str(), 198);
                    auto level = s(5017);
                    const auto placeholder = level.find("%d");
                    std::string range = "? to ?";
                    if (info.creatorLevel > 0 && info.creatorLevel <= maximumCharacterLevel) {
                        range = std::to_string(std::max(1, int(info.creatorLevel) - info.levelDifference)) + " to " +
                            std::to_string(std::min(maximumCharacterLevel, int(info.creatorLevel) + info.levelDifference));
                    }
                    if (placeholder != std::string::npos) level.replace(placeholder, 2, range);
                    detailLine(roomGoldText, level, 216);
                    const int playerCount = int(info.players.size());
                    constexpr int firstPlayer = 248, playerStep = 44, visiblePlayers = 3;
                    if (enabled && CheckCollisionPointRec(mouse, {612, firstPlayer, 142, 146}))
                        gameInfoOffset -= int(input.wheel);
                    gameInfoOffset = std::clamp(gameInfoOffset, 0, std::max(0, playerCount - visiblePlayers));
                    for (int i = gameInfoOffset; i < playerCount && i < gameInfoOffset + visiblePlayers; ++i) {
                        const auto &player = info.players[size_t(i)];
                        const int y = firstPlayer + (i - gameInfoOffset) * playerStep;
                        detailLine(roomGoldText, player.name, y);
                        auto playerLevel = s(5017);
                        const auto at = playerLevel.find("%d");
                        if (at != std::string::npos) playerLevel.replace(at, 2, std::to_string(player.level));
                        const auto characterClass = player.characterClass < classes.size() ? classes[player.characterClass] : "?";
                        detailLine(roomGrayText, playerLevel + " " + characterClass, y + 18);
                    }
                } else wrap(info.state == OnlineGameInfo::State::Pending ? "Retrieving room details..."
                    : "Room details unavailable. Refresh or join by name.", 608, 212, 155, false, 394);
            } else if (chosenGame != v.games.end())
                wrap(chosenGame->description, 608, 212, 155, false, 394);
            if (!v.gameListComplete)
                inputText.label(listing ? "Retrieving room list..." : "Incomplete list. Join by name or refresh.",
                     428, 165, 16, parchment);
            if (button("cancel", 434, 404, s(5103), true, 1, 0, &lobbyText)) {
                lastClickedGame.clear();
                lobbyPanel = LobbyPanel::Notice;
                if (listing) emit(FrontendCommand::CancelList);
                wipe(gamePassword);
            }
            if (button("gameButton", 599, 404, s(5151), !gameName.empty(), 1, 0, &lobbyText) ||
                (enabled && !gameName.empty() && (input.enter || doubleClickGame))) {
                lastClickedGame.clear();
                emit(FrontendCommand::JoinGame);
                intent.name = gameName;
                intent.password = std::move(gamePassword);
                wipe(gamePassword);
            }
        } else if (lobbyPanel == LobbyPanel::Create) {
            title.inBox(s(5150), {418, 73, 373, 38}, 16, parchment);
            label(s(5274), 426, 114);
            field(gameName, {429, 135, 182, 26}, 0, 15, false, false);
            label(s(5256), 426, 169);
            field(gamePassword, {429, 189, 182, 26}, 1, 15, true, false);
            label(s(5257), 426, 224);
            field(description, {429, 243, 341, 26}, 2, 31, false, false);
            label(s(5258), 436, 290);
            spinner(650, 283, players, 1, 8);
            image("check", 430, 324, restrictLevels ? 1 : 0);
            if (enabled && clicked && CheckCollisionPointRec(mouse, {430, 324, 20, 20}))
                restrictLevels = !restrictLevels;
            label(s(5259), 460, 325);
            spinner(650, 319, difference, 0, 99);
            label(s(5260), 700, 325);
            const auto chosen = std::find_if(v.characters.begin(), v.characters.end(),
                                             [&](const auto &c) { return c.name == v.selectedCharacter; });
            const auto progress = chosen != v.characters.end() ? chosen->progression.value_or(0) : 0;
            const int unlocked = progress >= 10 ? 2 : progress >= 5 ? 1 : 0;
            difficulty = std::min(difficulty, unlocked);
            for (int i = 0; i < 3; ++i) {
                int x = 430 + i * 140;
                image("radio", x, 366, i == difficulty ? 1 : 0, i <= unlocked ? WHITE : GRAY);
                label(s(5156 - i), x + 26, 366, i <= unlocked ? parchment : GRAY);
                if (enabled && clicked && i <= unlocked &&
                    CheckCollisionPointRec(mouse, {float(x), 366, 130, 22}))
                    difficulty = i;
            }
            if (button("cancel", 434, 404, s(5103), true, 1, 0, &lobbyText)) {
                lobbyPanel = LobbyPanel::Notice;
                gameName.clear();
                wipe(gamePassword);
                description.clear();
            }
            // The original create-game control already contains its caption.
            if (button("createButton", 599, 404, "", canSubmit) ||
                (enabled && input.enter && canSubmit)) {
                emit(FrontendCommand::CreateGame);
                intent.name = gameName;
                intent.password = std::move(gamePassword);
                wipe(gamePassword);
                intent.description = description;
                intent.difficulty = uint8_t(difficulty);
                intent.maximumPlayers = uint8_t(players);
                intent.levelDifference = uint8_t(restrictLevels ? difference : 99);
            }
        } else {
            // Only server announcements populate the initial lobby panel.
            int y = 145;
            for (const auto &announcement : v.lobbyNotices) {
                std::istringstream words(announcement.text);
                std::string word, line;
                auto drawLine = [&]() {
                    inputText.inBox(line, {428, float(y), 343, 18}, 16,
                                    announcement.error ? RED : parchment);
                    y += 18;
                    line.clear();
                };
                while (words >> word) {
                    while (!word.empty() && inputText.measure(word, 16) > 343)
                        word.pop_back();
                    const auto next = line.empty() ? word : line + ' ' + word;
                    if (!line.empty() && inputText.measure(next, 16) > 343)
                        drawLine();
                    if (y > 380) break;
                    line = line.empty() ? word : line + ' ' + word;
                }
                if (y > 380) break;
                if (!line.empty()) drawLine();
            }
        }
        if (button("tabs", 534, 449, s(5312), true, 1, 0, &lobbyText)) {
            lastClickedGame.clear();
            lobbyPanel = LobbyPanel::Create;
            if (listing) emit(FrontendCommand::CancelList);
            focus = 0;
        }
        if (button("tabs", 654, 449, s(5313), true, 1, 0, &lobbyText)) {
            lastClickedGame.clear();
            lobbyPanel = LobbyPanel::Join;
            gameOffset = 0;
            focus = 0;
            emit(FrontendCommand::ListGames);
        }
        button("chatButton", 534, 469, s(5254), false, 1, 0, &lobbyText);
        button("chatButton", 614, 469, s(5315), false, 1, 0, &lobbyText);
        if (button("chatButton", 694, 469, s(5316), true, 1, 0, &lobbyText))
            emit(FrontendCommand::Back);
        tiles("leftButton", 19, 461, 3, 0, 3, GRAY);
        lobbyText.inBox(s(11126), {19, 461, 120, 20}, 16, BLACK);
        lobbyText.inBox(s(5308), {139, 461, 120, 20}, 16, BLACK);
        label(v.selectedCharacter, 139, 511, WHITE);
        auto c = std::find_if(v.characters.begin(), v.characters.end(),
                              [&](const auto &entry) { return entry.name == v.selectedCharacter; });
        if (c != v.characters.end() && c->characterClass && *c->characterClass < classes.size() && c->level) {
            auto level = s(5017);
            const auto at = level.find("%d");
            if (at != std::string::npos)
                level.replace(at, 2, std::to_string(*c->level));
            label(level + " " + classes[*c->characterClass], 139, 531, WHITE);
        }
        if (c != v.characters.end() && characterGraphics)
            portrait(*c, 66, 586);
        if (c != v.characters.end() && c->progression) {
            label(s(11124), 139, 552, WHITE);
            const auto progress = *c->progression;
            label(s(progress >= 15   ? 5154
                    : progress >= 10 ? 5155
                    : progress >= 5  ? 5156
                                     : 3762),
                  139, 572, WHITE);
        }
    }
    FrontendIntent draw(FrontendPage current, const OnlineView &v, std::string_view gateway,
                        std::string_view notice, Vector2 at, const FrameInput &frameInput, std::string_view worldNotice) {
        input = frameInput.focused ? frameInput : FrameInput{};
        if (!frameInput.focused) lastClickedGame.clear();
        mouse = at;
        clicked = input.leftPressed;
        intent = {};
        if (current != previous) {
            focus = 0;
            deleteName.clear();
            confirmHardcore = false;
            lastClickedCharacter = -1;
            lastClickedGame.clear();
            if (current == FrontendPage::CreateCharacter) {
                characterName.clear();
                hardcore = false;
            }
            if (current == FrontendPage::JoinHost) {
                replaceHostAddress = true;
            }
            if (current == FrontendPage::Lobby) {
                lobbyPanel = LobbyPanel::Notice;
                gameOffset = 0;
                gameName.clear();
                wipe(gamePassword);
                description.clear();
            }
            previous = current;
        }
        bool confirmation = !deleteName.empty() || confirmHardcore;
        bool modal = !notice.empty() || busy(v.stage) || confirmation;
        enabled = !modal;
        if (enabled && input.tab)
            focus = (focus + 1) %
                    (current == FrontendPage::Register ||
                     (current == FrontendPage::Lobby && lobbyPanel == LobbyPanel::Create) ? 3
                     : current == FrontendPage::CreateCharacter || current == FrontendPage::JoinHost ? 1 : 2);
        if (current == FrontendPage::Main)
            main(gateway);
        else if (current == FrontendPage::TcpIp || current == FrontendPage::JoinHost)
            tcpIp(current == FrontendPage::JoinHost);
        else if (current == FrontendPage::Login)
            login();
        else if (current == FrontendPage::Register)
            registration();
        else if (current == FrontendPage::Realms)
            realms(v);
        else if (current == FrontendPage::CreateCharacter)
            creation();
        else if (current == FrontendPage::Characters)
            characters(v);
        else if (current == FrontendPage::Lobby)
            lobby(v);
        else {
            tiles("main", 0, 0, 4);
            logo();
            centered(v.stage == OnlineStage::ProtocolReady ? "Connected to game server" : s(5243), 315);
            if (v.stage == OnlineStage::ProtocolReady) {
                wrap(worldNotice.empty() ? "Waiting for server world data." : std::string(worldNotice),
                     220, 350, 360, true);
                if (button("medium", 330, 405, s(5101)))
                    emit(FrontendCommand::LeaveGame);
            }
        }
        if (modal) {
            tiles("popup", 230, 130, 2);
            std::string message = !notice.empty()       ? std::string(notice)
                                  : !deleteName.empty() ? s(5163) + "\n" + deleteName
                                  : confirmHardcore     ? s(5303)
                                                        : s(5243);
            if (v.gameQueuePosition && notice.empty() && !confirmation)
                message += "\nQueue: " + std::to_string(*v.gameQueuePosition);
            wrap(message, 254, 175, 292, true);
            enabled = true;
            if (confirmation && notice.empty()) {
                if (button("medium", 280, 411, s(5102))) {
                    if (!deleteName.empty()) {
                        emit(FrontendCommand::DeleteCharacter);
                        intent.name = std::move(deleteName);
                        deleteName.clear();
                    } else
                        hardcore = true;
                    confirmHardcore = false;
                }
                if (button("medium", 450, 411, s(5103)) || input.escape) {
                    deleteName.clear();
                    confirmHardcore = false;
                }
            } else if (button("medium", 330, 411, notice.empty() ? s(5103) : s(5102)) ||
                       input.enter || input.escape)
                emit(!notice.empty()                        ? FrontendCommand::Dismiss
                     : v.stage == OnlineStage::ListingGames ? FrontendCommand::CancelList
                     : v.stage == OnlineStage::CreatingGame || v.stage == OnlineStage::JoiningGame ||
                       v.stage == OnlineStage::ConnectingGame || v.stage == OnlineStage::GameHandshake ||
                       v.stage == OnlineStage::LoadingGame || v.stage == OnlineStage::LeavingGame
                         ? FrontendCommand::LeaveGame : FrontendCommand::Back);
        } else if (input.escape && current == FrontendPage::Lobby && lobbyPanel != LobbyPanel::Notice) {
            lastClickedGame.clear();
            lobbyPanel = LobbyPanel::Notice;
            wipe(gamePassword);
            if (v.stage == OnlineStage::ListingGames) emit(FrontendCommand::CancelList);
        } else if (input.escape)
            emit(current == FrontendPage::Main      ? FrontendCommand::Exit
                 : current == FrontendPage::Loading ? FrontendCommand::LeaveGame
                                                    : FrontendCommand::Back);
        const auto *cursor = art.at("cursor").frame(0, 0);
        cursorSprite(cursor, {mouse.x, mouse.y}, handCursorHotspot(cursor));
        return std::move(intent);
    }
};
RealmFrontend::RealmFrontend(Archives &a) : impl_(std::make_unique<Impl>(a)) {}
RealmFrontend::~RealmFrontend() = default;
FrontendIntent RealmFrontend::frame(FrontendPage p, const OnlineView &v, std::string_view gateway,
                                    std::string_view notice, Vector2 mouse, const FrameInput &input, std::string_view worldNotice) {
    return impl_->draw(p, v, gateway, notice, mouse, input, worldNotice);
}
void RealmFrontend::clearPassword() {
    wipe(impl_->password);
    clearTransientPasswords();
}
void RealmFrontend::clearTransientPasswords() {
    wipe(impl_->verifyPassword);
    wipe(impl_->gamePassword);
}
void RealmFrontend::setLogin(std::string account, std::string password) {
    wipe(impl_->password); wipe(impl_->verifyPassword);
    impl_->account = std::move(account); impl_->password = std::move(password);
}
void RealmFrontend::setLanAddresses(std::string addresses) {
    impl_->lanAddresses = std::move(addresses);
}
} // namespace d2x
