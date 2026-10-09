#include "character_rules.hpp"
#include "content/classic_data.hpp"
#include "core/fingerprint.hpp"

namespace d2x {
uint64_t characterRulesFingerprint(const ClassicData &content) {
    Fingerprint hash;
    hash.add("d2x-character-admission-v28/native-wire113c/d2s96/act1-hireling");
    hash.add(content.profile);
    for (const auto &[name, table] : content.tables) {
        hash.add(name);
        for (const auto &column : table.columns()) hash.add(column);
        for (const auto &row : table.rows()) {
            hash.add("row");
            for (const auto &cell : row) hash.add(cell);
        }
    }
    return hash.value();
}
} // namespace d2x
