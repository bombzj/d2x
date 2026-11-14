#include "realm_portrait.hpp"
#include "resources/data_table.hpp"
#include "gameplay/items/equipment_rules.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
Bytes RealmPortraitCatalog::encode(const ActorAppearance &parts, unsigned characterClass, unsigned level, uint16_t status) const {
    if (characterClass >= 7 || level < 1 || level > 99) throw std::runtime_error("Invalid realm portrait character");
    Bytes bytes(33, 0xFF);
    bytes[0] = 0x8D; bytes[1] = 0x80;
    for (size_t part = 0; part < 11; ++part) {
        const auto &token = parts.components[part];
        if (token == "nil" || (part == 10 && token == "ne1")) continue;
        for (size_t code = 1; code < codes_.size(); ++code)
            if (!token.empty() && codes_[code].code == token) { bytes[2 + part] = uint8_t(code); break; }
    }
    bytes[13] = uint8_t(characterClass + 1); bytes[25] = uint8_t(level);
    bytes[26] = uint8_t(0x80 | (status & 0x7F)); bytes[27] = uint8_t(0x80 | ((status >> 7) & 0x7F));
    return bytes;
}
bool RealmPortraitCatalog::isType(const std::string &code, std::string_view target,
                                  std::set<std::string> &seen) const {
    if (code == target)
        return true;
    if (code.empty() || !seen.insert(code).second)
        return false;
    auto found = types_.find(code);
    if (found == types_.end())
        return false;
    for (const auto &parent : found->second)
        if (isType(parent, target, seen))
            return true;
    return false;
}
bool RealmPortraitCatalog::isType(const std::string &code, std::string_view target) const {
    std::set<std::string> seen;
    return isType(code, target, seen);
}
RealmPortraitCatalog::RealmPortraitCatalog(Archives &a) {
    const DataTable types(a.read("data/global/excel/itemtypes.txt"));
    for (size_t i = 0; i < types.rows().size(); ++i) {
        auto code = types.value(i, "Code");
        if (code.empty())
            continue;
        types_.emplace(std::string(code), std::array{std::string(types.value(i, "Equiv1")),
                                                     std::string(types.value(i, "Equiv2"))});
    }
    const DataTable armorTypes(a.read("data/global/excel/armtype.txt"));
    if (armorTypes.rows().size() < 3)
        throw std::runtime_error("Missing original armor components");
    for (size_t i = 0; i < 3; ++i)
        codes_[i + 1] = {std::string(armorTypes.value(i, "Token")), "tors", {}, {}, false};
    light_ = codes_[1].code;
    // Native 1.13c D2Common.dll, file 0x9D888: compatibility slot categories.
    // Only the original wire-slot reservation is fixed; item codes, inheritance,
    // appearances and weapon classes below are all taken from the current MPQ.
    constexpr std::string_view reserved = "...."
                                          "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAWWWWWWWWWWWWWWWWWWWWWWWWWWW"
                                          "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW.AAAA.."
                                          "AAAAAAWWWWAWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW"
                                          "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWAAAAAAAAAAAAAAAAAAAA";
    static_assert(reserved.size() == 255);
    size_t next = 4;
    for (const auto *name : {"weapons", "armor", "misc"}) {
        const DataTable items(a.read(std::string("data/global/excel/") + name + ".txt"));
        for (size_t row = 0; row < items.rows().size(); ++row) {
            std::string type(items.value(row, "type"));
            Item visual;
            auto gfx = items.value(row, "alternategfx");
            if (gfx.empty())
                gfx = items.value(row, "code");
            visual.appearance = {std::string(gfx), type, std::string(items.value(row, "wclass")),
                                 std::string(items.value(row, "2handedwclass")),
                                 items.number(row, "2handed").value_or(0) != 0,
                                 items.number(row, "1or2handed").value_or(0) != 0};
            visual.component = items.number(row, "component").value_or(-1);
            constexpr std::array columns{"rArm", "lArm", "Torso", "Legs", "rSPad", "lSPad"};
            for (size_t part = 0; part < columns.size(); ++part) {
                const auto armor = items.number(row, columns[part]);
                if (armor && *armor >= 0 && size_t(*armor) < armorTypes.rows().size())
                    visual.body[part] = armorTypes.value(size_t(*armor), "Token");
            }
            const auto itemCode = items.value(row, "code");
            if (!itemCode.empty())
                items_.emplace(std::string(itemCode), std::move(visual));
            const bool weapon = isType(type, "weap"), armor = isType(type, "armo");
            if (!weapon && !isType(type, "tors") && !isType(type, "shld") &&
                !(isType(type, "helm") && !isType(type, "circ")))
                continue;
            if (gfx.empty())
                continue;
            // Native initialization searches only the contiguous prefix. Slots
            // reserved beyond `next` do not join that search until the prefix
            // reaches them; scanning the whole array shifts later wire IDs.
            if (std::any_of(codes_.begin(), codes_.begin() + next, [&](const auto &e) { return e.code == gfx; }))
                continue;
            size_t slot = next;
            while (slot < codes_.size() && (!codes_[slot].code.empty() || (reserved[slot] == 'W' && weapon) ||
                                            (reserved[slot] == 'A' && armor)))
                ++slot;
            if (slot == codes_.size())
                slot = next;
            if (slot >= codes_.size())
                throw std::runtime_error("Original preview component table exceeds wire capacity");
            codes_[slot] = {std::string(gfx), std::move(type), std::string(items.value(row, "wclass")),
                            std::string(items.value(row, "2handedwclass")),
                            items.number(row, "2handed").value_or(0) != 0,
                            items.number(row, "1or2handed").value_or(0) != 0};
            if (slot == next)
                ++next;
        }
    }
}
std::optional<RealmPortraitParts> RealmPortraitCatalog::decode(const OnlineUnit &u,
    const OnlineWorldView &w, bool unequipped, std::optional<uint8_t> disguise, bool holyShield,
    const EquipmentColor &equipmentColor) const {
    const auto characterClass=disguise?std::optional<uint16_t>(*disguise):u.classId;
    if ((!disguise && u.key.type != 0) || !characterClass || *characterClass >= 7 || (!unequipped && !u.equipmentObserved))
        return {};
    // Share the verified native preview component resolver with live server gear.
    OnlineCharacter preview;
    preview.characterClass = uint8_t(*characterClass);
    preview.portrait.assign(33, 0xFF);
    preview.portrait[0] = 0x8D;
    preview.portrait[1] = 0x80;
    auto set = [&](size_t component, const std::string &token) {
        for (size_t id = 1; id < codes_.size(); ++id)
            if (!token.empty() && codes_[id].code == token) {
                preview.portrait[2 + component] = uint8_t(id);
                return true;
            }
        return false;
    };
    bool equipmentEffectsKnown = true,shield=false;
    std::array<std::array<int, 2>, 16> componentColors{};
    std::array<bool, 2> occupiedHands{};
    for (const auto &[id, item] : w.equipment) {
        (void)id;
        if (unequipped || item.ownerType!=u.key.type || item.owner != u.key.id || item.bodyLocation >= 11)
            continue;
        // INVENTORY_GetWieldType reads occupied slots before checking whether
        // their items can contribute visible components (BROKEN/NOEQUIP).
        if (item.bodyLocation == 4 || item.bodyLocation == 5)
            occupiedHands[item.bodyLocation - 4] = true;
        // ITEMS_CanBeEquipped excludes BROKEN and NOEQUIP from the native
        // composite. Repair/property packets rebuild this view with new flags.
        if (item.flags & (0x100u | 0x4000u))
            continue;
        const auto found = items_.find(item.code);
        if (found == items_.end())
            return {};
        const auto &visual = found->second;
        if((item.bodyLocation==4 || item.bodyLocation==5) && isType(visual.appearance.type,"shld")) shield=true;
        if (visual.component == 16)
            continue; // No visible body component.
        const auto color = equipmentColor ? equipmentColor(item) : std::nullopt;
        if ((!color && (!item.quality || *item.quality < 1 || *item.quality > 3 ||
                        item.autoAffix || (item.flags & (0x800u | 0x4000000u)))) ||
            (item.flags & 0x400000u))
            equipmentEffectsKnown = false;
        if (item.bodyLocation == 3 && visual.component == 1) {
            constexpr std::array<size_t, 6> components{3, 4, 1, 2, 8, 9};
            for (size_t part = 0; part < components.size(); ++part) {
                if (!set(components[part], visual.body[part]))
                    return {};
                if (color) componentColors[components[part]] = *color;
            }
        } else {
            // SCmd uses UNITS_GetItemComponentId for the packet header. For
            // assassins/barbarians it can label the non-primary hand item LH,
            // including a shield. INVENTORY_GetCompositItem only remaps these
            // weapon classes; shields and other gear keep their MPQ component.
            const auto &weaponClass = visual.appearance.weapon;
            const bool handWeapon = isType(visual.appearance.type, "weap") &&
                (weaponClass == "1hs" || weaponClass == "1ht" || weaponClass == "ht1");
            const int component = handWeapon ? item.component : visual.component;
            if (component > 10 || (component != 0 && component < 5) ||
                !set(component, visual.appearance.code))
                return {};
            if (color) componentColors[component] = *color;
        }
    }
    auto parts = decode(preview, occupiedHands[0] && occupiedHands[1]);
    if(parts) {
        parts->componentColors = componentColors;
        // The crossbow graphic spans both hand layers.
        if (parts->weapon == "xbw") {
            const auto color = componentColors[5][0] ? componentColors[5] : componentColors[6];
            parts->componentColors[5] = parts->componentColors[6] = color;
        }
        parts->equipmentEffectsKnown=equipmentEffectsKnown;
        // Retail D2Client 6E1E0 / 6E289: SH + HolyShield + an actually
        // equippable shield uses the original hsh component code.
        if(holyShield && shield) parts->components[7]="hsh";
    }
    return parts;
}
std::optional<RealmPortraitParts> RealmPortraitCatalog::decode(const OnlineCharacter &c) const {
    return decode(c, {});
}
std::optional<RealmPortraitParts> RealmPortraitCatalog::decode(const OnlineCharacter &c,
                                                              std::optional<bool> oppositeOccupied) const {
    if (!c.characterClass || *c.characterClass >= 7 || c.portrait.size() != 33 ||
        !(c.portrait[0] & 0x80) || !(c.portrait[1] & 0x80)) return {};
    const auto version = uint16_t((c.portrait[0] & 0x7F) | ((c.portrait[1] & 0x7F) << 7));
    // Newly created MCP characters can still use version 4 with empty equipment.
    // Component/color/class slots are shared with the saved version 10/13 layout.
    if (version != 4 && version != 10 && version != 13) return {};
    // The character selection card currently consumes the saved component codes.
    // Live units resolve dye from equipped items.
    constexpr std::array tokens{"am", "so", "ne", "pa", "ba", "dz", "ai"};
    RealmPortraitParts p;
    p.token = tokens[*c.characterClass];
    p.weapon = "hth";
    p.components.fill(light_);
    p.components[5] = p.components[6] = p.components[7] = "nil";
    if (p.token == "ne")
        p.components[10] = "ne1";
    for (size_t component = 0; component < 11; ++component) {
        const auto id = c.portrait[2 + component];
        if (id == 0xFF)
            continue;
        if (id == 0 || id >= codes_.size() || codes_[id].code.empty())
            return {};
        p.components[component] = codes_[id].code;
    }
    auto hand = [&](size_t component) -> const Entry * {
        const auto id = c.portrait[2 + component];
        if (id == 0xFF || id >= codes_.size() || !isType(codes_[id].type, "weap"))
            return nullptr;
        return &codes_[id];
    };
    const auto *right = hand(5), *left = hand(6);
    if (right && left) {
        if (right->weapon == "ht1" && left->weapon == "ht1")
            p.weapon = "ht2";
        else if (right->weapon == left->weapon && (right->weapon == "bow" || right->weapon == "xbw"))
            p.weapon = right->weapon;
        else if (right->weapon == "1hs" || right->weapon == "1ht") {
            if (left->weapon != "1hs" && left->weapon != "1ht")
                return {};
            p.weapon = right->weapon == "1ht" ? (left->weapon == "1ht" ? "1jt" : "1st")
                                              : (left->weapon == "1ht" ? "1js" : "1ss");
        } else
            return {};
    } else if (const auto *weapon = right ? right : left) {
        EquipmentDefinition equipment;
        equipment.twoHanded = weapon->twoHanded;
        equipment.oneOrTwoHanded = weapon->oneOrTwoHanded;
        // Saved portraits only carry visible components. Live units also know
        // whether an invisible/broken item occupies the opposite body slot.
        const bool occupied = oppositeOccupied.value_or(p.components[right ? 6 : 5] != "nil" || p.components[7] != "nil");
        const bool twoHands = usesTwoHandedWeapon(equipment, *c.characterClass == 4 ? "bar" : "", occupied);
        p.weapon = twoHands && !weapon->twoHandWeapon.empty() ? weapon->twoHandWeapon : weapon->weapon;
    }
    if (p.weapon.empty())
        return {};
    if (p.weapon == "xbw") {
        // INVENTORY_GetItemSaveGfxInfo/sub_6FD917B0 duplicates a crossbow's
        // component into LH. Native XBW COFs draw separate RH and LH pixels.
        const auto *weapon = right ? right : left;
        p.components[5] = p.components[6] = weapon->code;
    }
    return p;
}
} // namespace d2x
