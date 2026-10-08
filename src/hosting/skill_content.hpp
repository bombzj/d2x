#pragma once
namespace d2x {
struct ClassicData;
struct CharacterDefinition;
namespace server { struct PreparedRules; }
void prepareSkillRules(server::PreparedRules &, const ClassicData &, const CharacterDefinition &);
}
