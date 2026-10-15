#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>
namespace d2x {
enum class BasicSkillAction { None,Attack,Throw,LeftHandSwing,LeftHandThrow };
struct SkillEquipmentRule {
    bool noAmmo{};
    BasicSkillAction basicAction{};
    std::array<std::array<std::string,3>,2> includedHandTypes;
    std::array<std::array<std::string,2>,2> excludedHandTypes;
};
struct SkillHandEquipment {
    bool present=false,unusable=false,stackable=false;
    std::vector<std::string> types;
    std::string ammunitionType;
    std::optional<unsigned> quantity;
};
std::optional<bool> skillEquipmentReady(const SkillEquipmentRule &,
    const std::array<std::optional<SkillHandEquipment>,2> &);
struct SkillRestoreRule : SkillEquipmentRule {
    bool inGame{},aura{},passive{},scroll{};
};
struct SkillRestoreInput {
    bool exists{};
    int level{};
    std::optional<int> quantity;
    std::array<std::optional<SkillHandEquipment>,2> hands;
    std::optional<size_t> primaryHand;
    bool primaryHandKnown{}; // A known missing registered weapon differs from missing input.
    bool dualWield{};
    std::array<bool,2> throwable{};
};
// Inventory Update's restore gate excludes NOLEVEL/NOQUANTITY only.
// Unknown required hand/quantity facts remain explicit.
std::optional<bool> skillSelectionRestorable(const SkillRestoreRule &,const SkillRestoreInput &);
}
