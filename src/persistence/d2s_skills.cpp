#include "d2s_skills.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace d2x {
namespace {
std::vector<int> classSkills(uint8_t characterClass, uint8_t skillCount, const DataTable &skills) {
    constexpr std::array<const char *, 7> classes = {"ama", "sor", "nec", "pal", "bar", "dru", "ass"};
    if (characterClass >= classes.size() || skillCount == 0)
        throw std::runtime_error("Invalid Diablo II skill class or count");
    std::array<std::vector<int>, 7> ids;
    for (size_t row = 0; row < skills.rows().size(); ++row) {
        auto found = std::find_if(classes.begin(), classes.end(), [&](const char *code) {
            return skills.value(row, "charclass") == code;
        });
        if (found == classes.end()) continue;
        auto id = skills.number(row, "Id");
        if (!id || *id < 0 || *id > UINT16_MAX || size_t(*id) != row)
            throw std::runtime_error("Diablo II skill ID disagrees with MPQ row index");
        ids[size_t(found - classes.begin())].push_back(*id);
    }
    size_t highest = 0;
    for (const auto &classIds : ids) highest = std::max(highest, classIds.size());
    if (highest != skillCount || ids[characterClass].empty())
        throw std::runtime_error("Diablo II skill count disagrees with MPQ Skills.txt");
    return ids[characterClass];
}
} // namespace
D2sSkills readD2sSkills(std::span<const uint8_t> bytes, uint8_t characterClass,
                       uint8_t skillCount, const DataTable &skills) {
    const auto ids = classSkills(characterClass, skillCount, skills);
    if (bytes.size() < size_t(skillCount) + 2 || bytes[0] != 'i' || bytes[1] != 'f')
        throw std::runtime_error("Invalid Diablo II skills section");
    D2sSkills result;
    result.bytesRead = size_t(skillCount) + 2;
    for (size_t index = 0; index < ids.size(); ++index)
        if (bytes[index + 2]) result.ranks.emplace(ids[index], bytes[index + 2]);
    return result;
}
void writeD2sSkills(Bytes &bytes, uint8_t characterClass, uint8_t skillCount,
                    const std::map<int, uint8_t> &ranks, const DataTable &skills) {
    const auto ids = classSkills(characterClass, skillCount, skills);
    for (const auto &[id, rank] : ranks)
        if (rank && std::find(ids.begin(), ids.end(), id) == ids.end())
            throw std::runtime_error("Diablo II skill belongs to a different class");
    bytes.push_back('i');
    bytes.push_back('f');
    for (auto id : ids) {
        const auto found = ranks.find(id);
        bytes.push_back(found == ranks.end() ? 0 : found->second);
    }
    bytes.insert(bytes.end(), skillCount - ids.size(), 0);
}
} // namespace d2x