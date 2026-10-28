#include "native_save_data.hpp"
#include "content/classic_data.hpp"
#include <stdexcept>

namespace d2x {
NativeItemSaveRules prepareNativeItemSaveRules(const ClassicData &content) {
    NativeItemSaveRules result;result.pd2Host=content.pd2Host;
    for(const auto &[code,definition]:content.items.entries()) {
        const auto &table=content.tables.at(definition.base.sourceTable);
        const auto row=definition.base.sourceRow;
        result.bases.emplace(code,NativeItemSaveRules::Base{definition,
            table.number(row,"compactsave").value_or(0)!=0,
            table.number(row,"quest").value_or(0)!=0 && table.number(row,"questdiffcheck").value_or(0)!=0,
            table.number(row,"stackable").value_or(0)!=0,table.number(row,"auto prefix").value_or(0)});
    }
    const auto &stats=content.tables.at("itemstatcost");
    constexpr std::array<const char *,3> bits{"Save Bits","Save Bits S12","Save Bits S13"};
    constexpr std::array<const char *,3> params{"Save Param Bits","Save Param Bits S12","Save Param Bits S13"};
    constexpr std::array<const char *,3> adds{"Save Add","Save Add S12","Save Adds S13"};
    for(size_t row=0;row<stats.rows().size();++row) {
        const auto id=stats.number(row,"ID");if(!id || *id<0) continue;
        for(size_t profile=0;profile<(content.pd2Host?3u:1u);++profile) {
            const int width=stats.number(row,bits[profile]).value_or(0),parameter=stats.number(row,params[profile]).value_or(0);
            result.stats[profile].emplace(unsigned(*id),NativeItemSaveRules::StatFormat{unsigned(width),unsigned(parameter),stats.number(row,adds[profile]).value_or(0)});
        }
    }
    const auto &automatic=content.tables.at("automagic");
    for(size_t row=0;row<automatic.rows().size();++row) result.autoAffixGroups.push_back(automatic.number(row,"group").value_or(0));
    result.rareSuffixCount=content.tables.at("raresuffix").rows().size();
    for(const auto &word:content.runewords) result.runewordIds.emplace(int(word.row),unsigned(word.stringId));
    return result;
}
}