#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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
    std::optional<bool> magicArrow=false;
};
std::optional<bool> skillEquipmentReady(const SkillEquipmentRule &,
    const std::array<std::optional<SkillHandEquipment>,2> &);
struct SkillRestoreRule : SkillEquipmentRule {
    bool inGame{},aura{},passive{},scroll{},attackNoMana{};
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
std::optional<bool> nativeSkillEquipmentReady(const SkillRestoreRule &,const SkillRestoreInput &);

enum class NativeSkillUseState : uint8_t {
    Ready, NoMana, NoQuantity, Disabled, ShapeRestricted, Passive, Aura, NoLevel, Delayed
};
struct NativeSkillUseChecks {
    bool inGame{},aura{},passive{};
    std::optional<int> level;
    std::optional<bool> equipmentReady=true,manaReady=true,shapeReady=true,startManaReady=true,
        itemChargesReady=true,delayReady=true;
};
// Unobserved facts leave the first native result unknown.
std::optional<NativeSkillUseState> nativeSkillUseState(const NativeSkillUseChecks &);
bool nativeSkillFallsBackToAttack(NativeSkillUseState,bool attackNoMana);

struct NativeAttackArrow {
    enum class Kind { Ordinary,Magic,Explosive };
    Kind kind{};
    int rank{};
    std::string_view missileName() const;
};
NativeAttackArrow nativeAttackArrow(int magicArrow,int explosiveArrow);
}
