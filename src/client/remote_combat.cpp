#include "remote_combat.hpp"
#include "content/skills/skill_eligibility.hpp"
#include "network/protocol/bits.hpp"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace d2x {
RemoteCombat::RemoteCombat(Archives &archives, RemoteTown &scene, net::RealmSession &session)
    : scene_(scene), session_(session) {
    for (const char *name : {"skills", "playerclass", "charstats", "monstats", "monstats2", "states", "itemstatcost"})
        tables_.emplace(name, DataTable(archives.read("data/global/excel/" + std::string(name) + ".txt")));
    for (const auto &[name, column] : {std::pair{"skills", "Id"}, {"itemstatcost", "ID"}}) {
        const auto &table = tables_.at(name);
        auto &rows = std::string_view(name) == "skills" ? skills_ : stats_;
        for (size_t row = 0; row < table.rows().size(); ++row)
            if (auto id = table.number(row, column); id && *id >= 0 && *id <= UINT16_MAX) rows.emplace(uint16_t(*id), row);
    }
    const auto &states = tables_.at("states");
    for (const auto &[id, row] : skills_)
        skillMetadata_.emplace(id, loadSkillEligibilityMetadata(tables_.at("skills"), row));
    for (size_t row = 0; row < states.rows().size(); ++row)
        if (auto id = states.number(row, "Id"); id && *id >= 0 && *id < 255) states_.emplace(uint8_t(*id), row);
    const auto &monsters = tables_.at("monstats");
    for (size_t row = 0; row < monsters.rows().size(); ++row)
        if (auto id = monsters.number(row, "hcIdx"); id && *id >= 0 && *id <= UINT16_MAX) monsters_.emplace(uint16_t(*id), row);
    const auto &classes=tables_.at("playerclass"), &characters=tables_.at("charstats");
    for (size_t row=0; row<classes.rows().size(); ++row) {
        const auto code=classes.value(row,"Code");
        if (code.empty()) continue;
        auto &innate=innateSkills_[std::string(code)];
        for (size_t character=0; character<characters.rows().size(); ++character) {
            if (characters.value(character,"class")!=classes.value(row,"Player Class")) continue;
            for (const int id : loadInnateSkillIds(tables_.at("skills"), characters, character))
                if (id <= UINT16_MAX) innate.insert(uint16_t(id));
        }
    }
}
bool RemoteCombat::reject(std::string reason) { reason_ = std::move(reason); return false; }
std::string_view RemoteCombat::classCode() const {
    const auto &view = session_.read();
    for (const auto &character : view.characters)
        if (character.name == view.selectedCharacter && character.characterClass)
            {
                const auto &classes = tables_.at("playerclass");
                size_t ordinal = 0;
                for (size_t row = 0; row < classes.rows().size(); ++row) {
                    if (classes.value(row,"Code").empty()) continue; // Expansion separator is not a class.
                    if (ordinal++ == *character.characterClass) return classes.value(row,"Code");
                }
            }
    return {};
}
bool RemoteCombat::innateSkill(uint16_t id) const {
    const auto found=innateSkills_.find(classCode());
    return found!=innateSkills_.end() && found->second.contains(id);
}
bool RemoteCombat::hostile(const OnlineUnit &unit) const {
    if (unit.key.type != 1 || !unit.classId) return false;
    const auto monster = monsters_.find(*unit.classId);
    if (monster == monsters_.end()) return false;
    const auto &table = tables_.at("monstats");
    if (table.number(monster->second, "npc").value_or(0) ||
        table.number(monster->second, "interact").value_or(0) ||
        table.number(monster->second, "Align").value_or(0) ||
        !table.number(monster->second, "killable").value_or(0)) return false;
    if (const auto snapshot = unitStates_.find(unit.key); snapshot != unitStates_.end()) {
        if (!snapshot->second.decoded) return false;
        for (const auto &[id, state] : snapshot->second.states) {
            (void)id;
            for (const auto &stat : state.stats)
                if (const auto row = stats_.find(stat.id); row != stats_.end() &&
                    tables_.at("itemstatcost").value(row->second, "Stat") == "alignment" && stat.value != 0)
                    return false;
        }
    }
    return true;
}
bool RemoteCombat::monsterTargetEligible(const OnlineUnit &unit, bool targetCorpse) const {
    return hostile(unit) && onlineMonsterCorpse(unit) == targetCorpse &&
        (!targetCorpse || corpseSelectable(unit));
}
bool RemoteCombat::corpseSelectable(const OnlineUnit &unit) const {
    const auto monster=monsters_.find(unit.classId.value_or(UINT16_MAX));
    if (unit.key.type!=1 || monster==monsters_.end()) return false;
    const auto &extras=tables_.at("monstats2");
    const auto identity=tables_.at("monstats").value(monster->second,"MonStatsEx");
    bool selectable=false;
    for (size_t row=0; row<extras.rows().size(); ++row)
        if (extras.value(row,"Id")==identity) { selectable=extras.number(row,"corpseSel").value_or(0)!=0; break; }
    if (!selectable) return false;
    if (const auto snapshot=unitStates_.find(unit.key); snapshot!=unitStates_.end()) {
        if (!snapshot->second.decoded) return false;
        for (const auto &[id,state]:snapshot->second.states) {
            (void)state;
            if (const auto row=states_.find(id); row!=states_.end())
                if (tables_.at("states").number(row->second,"hide").value_or(0) ||
                    tables_.at("states").number(row->second,"udead").value_or(0)) return false;
        }
    }
    return true;
}
bool RemoteCombat::submit(OnlineCombatCommand command) {
    if (!command.context) command.context = onlineIntentContext(session_.read());
    if (!onlineWorldMatches(*command.context, session_.read()))
        return reject("Combat intent belongs to a previous game or area");
    scene_.update(session_.read());
    update();
    const auto &world = session_.read().world;
    const auto &table = tables_.at("skills");
    using Action = OnlineCombatCommand::Action;
    if (command.action == Action::BindHotkey && command.hotkeySlot >= world.skillHotkeys.size())
        return reject("Hotkey slot is outside the native range");
    const auto attribute = [&](uint8_t id) { const auto it = world.playerAttributes.find(id); return it == world.playerAttributes.end() ? 0u : it->second; };
    if (command.action == Action::SpendAttribute) {
        if (command.attribute > 3 || !command.count || command.count > 100 || attribute(4) < command.count)
            return reject("Insufficient server attribute points or invalid count");
    } else if (command.action != Action::Stop) {
        if (command.action == Action::Cast) {
            const auto selected = command.hand == OnlineSkillHand::Left ? world.leftSkill : world.rightSkill;
            if (!selected || selected->owner != UINT32_MAX) return reject("No confirmed normal skill is selected for this hand");
            command.skill = selected->skill;
        }
        const auto row = skills_.find(command.skill);
        if (row == skills_.end()) return reject("Skill is absent from current MPQ");
        const auto n = [&](std::string_view key) { return table.number(row->second, key).value_or(0); };
        const auto &metadata = skillMetadata_.at(command.skill);
        SkillEligibilityInput facts;
        facts.classCode = classCode(); facts.innate = innateSkill(command.skill);
        facts.dead = onlinePlayerDead(world); facts.town = scene_.read().town;
        const auto knownAttribute = [&](uint8_t id) -> std::optional<int> {
            const auto value = world.playerAttributes.find(id);
            return value == world.playerAttributes.end() ? std::nullopt : std::optional{int(value->second)};
        };
        facts.level = knownAttribute(12); facts.skillPoints = knownAttribute(5);
        constexpr std::array<uint8_t, 4> attributeIds{0, 2, 3, 1};
        for (size_t i = 0; i < attributeIds.size(); ++i) facts.attributes[i] = knownAttribute(attributeIds[i]);
        if (const auto rank = world.playerBaseSkills.find(command.skill); rank != world.playerBaseSkills.end())
            facts.baseRank = rank->second;
        else if (world.playerBaseSkillsAssigned) facts.baseRank = 0;
        if (const auto rank = world.playerSkills.find(command.skill); rank != world.playerSkills.end())
            facts.effectiveRank = rank->second;
        for (const int required : metadata.prerequisites) {
            if (required > UINT16_MAX) continue;
            if (const auto rank = world.playerBaseSkills.find(uint16_t(required)); rank != world.playerBaseSkills.end())
                facts.prerequisiteRanks.emplace(required, rank->second);
        }
        const auto eligibility = evaluateSkillEligibility(metadata, facts);
        if (command.action == Action::LearnSkill) {
            if (!eligibility.canAllocate)
                return reject("Known server ranks, points and attributes or MPQ skill requirements do not permit learning");
        } else {
            // SKILLS_InitSkillList adds Attack and current CharStats.Skill 1..10;
            // native 0x94 need not list these innate skills. Equipment and item
            // quantities still belong to the server, not an invented skill rank.
            if (!eligibility.canSelect(command.hand == OnlineSkillHand::Left, metadata))
                return reject("Server has not reported an available active skill for this hand");
            if (command.action == Action::Cast) {
                const auto &binding = scene_.read();
                if (!binding.nativeMapReady || !binding.movementAvailable || !world.playerPosition || !eligibility.usableNow)
                    return reject("Skill cannot be cast in the current loaded area");
                if (command.point.has_value() == command.target.has_value()) return reject("Specify exactly one point or unit target");
                auto point = command.point;
                if (command.target) {
                    const auto target = world.units.find(*command.target);
                    if (target == world.units.end() || !target->second.position || command.target->type != 1 || !target->second.classId)
                        return reject("Only assigned PvE monster targets are supported in this batch");
                    if (!monsterTargetEligible(target->second, n("TargetCorpse") != 0))
                        return reject("MPQ monster identity, native alignment or corpse state does not permit this target");
                    point = target->second.position;
                }
                const auto player = *world.playerPosition;
                if (std::abs(int(player.x) - point->x) > 50 || std::abs(int(player.y) - point->y) > 50)
                    return reject("Cast exceeds the native 50-subtile request range");
                if (!binding.origin || point->x < binding.origin->x || point->y < binding.origin->y ||
                    int(point->x - binding.origin->x) >= binding.width || int(point->y - binding.origin->y) >= binding.height)
                    return reject("Cast target is outside the current map binding");
            }
        }
    }
    if (!session_.submit_combat(command)) return reject(session_.read().error ? session_.read().error->message : "Combat request is unavailable");
    reason_.clear(); return true;
}
void RemoteCombat::update() {
    const auto &view = session_.read();
    const auto &world = view.world;
    if (game_ != view.gameGeneration || area_ != world.areaGeneration) {
        game_ = view.gameGeneration; area_ = world.areaGeneration; unitStates_.clear();
    }
    catalog_.clear();
    const auto &skills = tables_.at("skills");
    for (const auto &[id, row] : skills_) {
        const auto &metadata = skillMetadata_.at(id);
        const bool classSkill = !classCode().empty() && metadata.classCode == classCode();
        const bool innate = innateSkill(id);
        const auto level = world.playerSkills.find(id);
        if (!classSkill && !innate && (level == world.playerSkills.end() || !level->second)) continue;
        OnlineCombatSkillView entry; entry.id = id; entry.name = std::string(skills.value(row, "skill"));
        if (const auto it = world.playerBaseSkills.find(id); it != world.playerBaseSkills.end()) entry.base = it->second;
        if (const auto it = world.playerBonusSkills.find(id); it != world.playerBonusSkills.end()) entry.bonus = it->second;
        if (level != world.playerSkills.end()) entry.level = level->second;
        entry.innate = innate;
        entry.left = metadata.leftAllowed;
        entry.passive = metadata.passive;
        entry.inTown = metadata.allowedInTown; entry.classSkill = classSkill;
        catalog_.push_back(std::move(entry));
    }
    std::erase_if(unitStates_, [&](const auto &entry) { return !world.units.contains(entry.first); });
    for (const auto &[key, unit] : world.units) {
        if (!unit.stateSequence) continue;
        auto &output = unitStates_[key];
        if (output.sequence == unit.stateSequence) continue;
        const auto previous = output.sequence;
        output.sequence = unit.stateSequence;
        try {
            if (!previous) output.decoded = true;
            auto stats = [&](net::protocol::BitReader &bits) {
                std::vector<OnlineItemStat> values;
                for (;;) {
                    const auto id = uint16_t(bits.read(9)); if (id == 511) return values;
                    const auto found = stats_.find(id);
                    if (found == stats_.end()) throw std::runtime_error("Unknown state stat in current MPQ");
                    const auto &table = tables_.at("itemstatcost"); const auto row = found->second;
                    const int width = table.number(row, "Send Bits").value_or(0), param = table.number(row, "Send Param Bits").value_or(0);
                    if (width <= 0 || width > 32 || param < 0 || param > 32) throw std::runtime_error("Invalid MPQ state stat width");
                    OnlineItemStat stat; stat.id = id; stat.parameter = bits.read(unsigned(param));
                    const auto raw = bits.read(unsigned(width)); stat.value = raw;
                    if (table.number(row, "Signed").value_or(0) && (raw & (uint32_t{1} << (width - 1)))) stat.value -= int64_t{1} << width;
                    values.push_back(stat);
                }
            };
            uint64_t expected = previous + 1;
            for (const auto &message : unit.stateMessages) {
                if (message.sequence <= previous) continue;
                if (message.kind == OnlineStateMessage::Kind::Snapshot) {
                    output.states.clear(); output.decoded = true; output.reason.clear();
                } else if (message.sequence != expected) {
                    output.states.clear(); output.decoded = false; output.reason = "Native state history prefix was discarded";
                }
                expected = message.sequence + 1;
                if (!output.decoded) continue;
                auto state = [&](uint8_t id, std::vector<OnlineItemStat> values) {
                    const auto row = states_.find(id);
                    if (row == states_.end()) throw std::runtime_error("Unknown state in current MPQ");
                    output.states[id] = {id, std::string(tables_.at("states").value(row->second, "state")), std::move(values)};
                };
                if (message.kind == OnlineStateMessage::Kind::Disable) output.states.erase(message.state);
                else if (message.kind == OnlineStateMessage::Kind::Snapshot) {
                    net::protocol::BitReader bits(message.packed);
                    for (;;) { const auto id = uint8_t(bits.read(8)); if (id == 255) break;
                        state(id, bits.read(1) ? stats(bits) : std::vector<OnlineItemStat>{}); }
                } else {
                    if (message.packed.empty()) {
                        if (!output.states.contains(message.state)) state(message.state, {});
                    } else { net::protocol::BitReader bits(message.packed); state(message.state, stats(bits)); }
                }
            }
        } catch (const std::exception &error) { output.states.clear(); output.decoded = false; output.reason = error.what(); }
    }
}
}
