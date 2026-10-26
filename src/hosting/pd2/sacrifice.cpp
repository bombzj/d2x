#include "sacrifice.hpp"
#include "hosting/item_content.hpp"
#include "content/pd2/cube.hpp"
#include "content/items/item_properties.hpp"
#include "server/systems/crafting/system.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include <stdexcept>

namespace d2x {
void preparePd2Sacrifice(const ClassicData &data,server::crafting::Prepared &result) {
    const auto &source=result.source;
    const auto &request=std::get<server::crafting::Pd2SacrificeItem>(source.pending.request.intent);
    const auto found=source.character.inventory.items.find(request.input.id);
    if(found==source.character.inventory.items.end() || found->second.revision!=request.input.revision ||
       !pd2SacrificeEligible(data,found->second)) throw std::runtime_error("Native sacrifice requires an identified, corrupted amulet without desecration or a mirror");
    auto item=found->second;auto random=item.nativePropertyRandom.value_or(initialRandom(item.nativeSeed));
    auto program=source.pd2Random;auto game=source.pending.seed;
    const bool brick=int(pd2CrtRandom(program)%100)>=50+(source.pd2BossDifficulty?10:0);
    if(brick) {
        item=prepareNativeItem(data,"amu",99,source.difficulty,game,DropQuality::Rare,{},NativeItemUse::Vendor,&random);
        item.identified=true;applyPd2AutomaticCorruption(data,item,random,program);
        result.consumed.push_back(request.input);
    }
    applyPd2AutomaticCorruption(data,item,random,program,true);
    item.nativePropertyRandom=random;item.id=request.input.id;
    item.location=ContainerLocation{source.character.containers.backpack,{}};
    result.outputs.push_back({std::move(item),brick?std::nullopt:std::optional{request.input},source.character.containers.backpack});
    result.pd2Random=program;result.nativeGameRandom=game;
}
}
