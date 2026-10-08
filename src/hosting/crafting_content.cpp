#include "crafting_content.hpp"
#include "character_content.hpp"
#include "item_content.hpp"
#include "content/items/cube_data.hpp"
#include "content/items/socket_data.hpp"
#include "content/items/item_magic_loot.hpp"
#include "content/items/item_properties.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace d2x {
namespace {
using namespace server::crafting;
void require(bool valid, const char *reason) { if (!valid) throw std::runtime_error(reason); }
bool carriedCube(const ClassicData &data, const Preparation &s) {
    return std::any_of(s.character.inventory.items.begin(), s.character.inventory.items.end(), [&](const auto &entry) {
        const auto *at = std::get_if<ContainerLocation>(&entry.second.location);
        return at && at->container == s.character.containers.backpack && entry.second.definition == data.cubeCode;
    });
}
const ItemInstance &resolve(const Preparation &s, ItemHandle h) {
    const auto found = s.character.inventory.items.find(h.id);
    require(found != s.character.inventory.items.end() && found->second.revision == h.revision, "Crafting input expired");
    return found->second;
}
void sockets(const ClassicData &data, Prepared &result, const SocketItem &request) {
    const auto &s = result.source; const auto &c = s.character.containers;
    auto host = resolve(s, request.host); auto filler = resolve(s, request.filler);
    const auto *base = data.items.find(host.definition), *gem = data.items.find(filler.definition);
    const auto *at = std::get_if<ContainerLocation>(&host.location);
    require(base && gem && at && request.host.id != request.filler.id &&
        filler.location == ItemLocation{ContainerLocation{c.cursor,{}}}, "Socket filler must be the owned cursor item");
    require(at->container == c.backpack || at->container == c.equipment || at->container == c.beltEquipment ||
        (at->container == c.stash && s.storage) || (at->container == c.cube && s.cube && carriedCube(data,s)), "Socket host is not accessible");
    require(host.identified && filler.identified && host.quantity == 1 && filler.quantity == 1 &&
        gem->equipment.isType("sock") && filler.socketedItems.empty() && base->maxStack == 1 &&
        base->base.sockets.value_or(0) > 0 && base->gemApplyType >= 0 && base->gemApplyType <= 2 &&
        host.sockets && host.socketedItems.size() < host.sockets, "Item cannot accept this socket filler");
    filler.location = SocketLocation{host.id, unsigned(host.socketedItems.size())}; ++filler.revision;
    host.socketedItems.push_back(std::move(filler)); auto random = s.pending.seed;
    prepareSocketedItem(data,host,random);
    const auto container=at->container;result.consumed.push_back(request.filler); result.outputs.push_back({std::move(host),request.host,container});
}
void cube(const ClassicData &data, Prepared &result) {
    const auto &s = result.source; const auto &c = s.character.containers;
    require(s.cube && carriedCube(data,s), "Cube must be open and carried in the backpack");
    std::vector<const ItemInstance *> inputs;
    for (const auto &[id,item] : s.character.inventory.items) {
        (void)id; const auto *at = std::get_if<ContainerLocation>(&item.location);
        require(!at || at->container != c.cursor, "Cursor must be empty before transmutation");
        if (at && at->container == c.cube) inputs.push_back(&item);
    }
    const CubeRecipe *matched = nullptr; std::vector<const ItemInstance *> ordered;
    for (const auto &recipe : data.cubeRecipes) {
        if (recipe.version > 100 || recipe.minimumDifficulty > s.difficulty ||
            (!recipe.characterClass.empty() && recipe.characterClass != s.classCode) || recipe.inputCount != inputs.size()) continue;
        std::vector<bool> used(inputs.size()); ordered.clear();
        std::function<bool(size_t,unsigned)> match = [&](size_t slot, unsigned count) {
            if (slot == recipe.inputs.size()) return ordered.size() == inputs.size();
            const auto &wanted = recipe.inputs[slot];
            if (count == wanted.quantity) return match(slot+1,0);
            for (size_t i=0;i<inputs.size();++i) {
                const auto &item = *inputs[i]; const auto *base = data.items.find(item.definition);
                if (used[i] || !base || !matchesCubeInput(wanted,item,*base,data.cubeBases.at(item.definition)) ||
                    (recipe.operation == 28 && base->questTag && item.nativeQuestDifficulty < unsigned(s.difficulty))) continue;
                used[i]=true; ordered.push_back(&item);
                if (match(slot,count+1)) return true;
                ordered.pop_back(); used[i]=false;
            }
            return false;
        };
        if (match(0,0)) { matched=&recipe; break; }
    }
    require(matched, "No original CubeMain recipe matches these inputs");
    const auto &original = *ordered.front(); auto random = s.pending.seed;
    for (const auto *input : ordered) result.consumed.push_back(input->handle());
    // Travel and quest rewards belong to their domains. Do not consume materials
    // until those domains can validate the corresponding native quest/portal.
    require(matched->operation==0, "Quest assembly requires the quest reward transaction");
    uint64_t temporary=1;
    for(const auto &[id,item] : s.character.inventory.items) { (void)item; temporary=std::max(temporary,id.value+1); }
    for (const auto &output : matched->outputs) {
        const bool preserve=output.kind==CubeOutputKind::UseItem || output.copy;
        require(output.kind!=CubeOutputKind::CowPortal && output.kind!=CubeOutputKind::UberPortal && output.kind!=CubeOutputKind::UberFinale,
            "Special portal recipe requires the world and quest qualification transaction");
        std::string code = output.kind==CubeOutputKind::UseType || output.kind==CubeOutputKind::UseItem ? original.definition : output.code;
        if (output.tier) { const auto &tiers=data.cubeBases.at(original.definition); code=output.tier==2?tiers.exceptional:tiers.elite; }
        const int level = std::clamp(output.level ? output.level : output.playerPercent*s.character.player.level/100+output.itemPercent*int(original.level)/100,1,99);
        if (output.kind==CubeOutputKind::Type) {
            std::vector<std::string> eligible;
            for (const auto &[key,base] : data.items.entries())
                if (base.equipment.isType(code) && base.base.spawnable.value_or(0) && base.base.level.value_or(100)<=level) eligible.push_back(key);
            require(!eligible.empty(), "No eligible original cube output base"); code=eligible[limitedRandom(random,unsigned(eligible.size()))];
        }
        const auto *base=data.items.find(code); require(base && base->artAvailable,"Original cube output is unavailable");
        ItemInstance item;
        if (preserve) {
            item=original; item.identified=true; freezeCubeItem(data,item);
            if (item.definition!=code) {
                item.definition=code;
                item.nativeMaxDurability=base->maxDurability;
                if(item.nativeMaxDurability && (item.nativeFlags&0x400000u)) item.nativeMaxDurability=item.nativeMaxDurability/2+1;
                if (base->family==ItemFamily::Armor) {
                    item.defense=base->base.minDefense.value()+int(limitedRandom(random,unsigned(base->base.maxDefense.value()-base->base.minDefense.value()+1)));
                    if(item.nativeFlags&0x400000u) item.defense=item.defense*3/2;
                }
                if(base->maxDurability) { const unsigned half=base->maxDurability/2; item.durability=half+limitedRandom(random,half); } else item.durability=0;
            }
        } else {
            ItemGeneration generation; const auto quality=output.quality.value_or(ItemQuality::Normal);
            if (quality==ItemQuality::Magic || quality==ItemQuality::Rare || quality==ItemQuality::Crafted) {
                auto roll=rollAffixItem(data,*base,quality,level,random,s.classCode,output.prefix,output.suffix);
                require(roll.deferred.empty(),roll.deferred.c_str()); random=roll.randomState; generation=std::move(roll.generation);
            } else require(quality==ItemQuality::Normal,"Unverified cube output quality");
            const auto &stack=data.cubeBases.at(code); unsigned quantity=1;
            if(base->maxStack>1) quantity=std::max(1u,stack.minimumStack+limitedRandom(random,stack.spawnStack>stack.minimumStack?stack.spawnStack-stack.minimumStack:1u));
            if(output.quantity) quantity=std::min(base->maxStack,output.quantity);
            item=prepareItem(data,{code,quality==ItemQuality::Normal?quantity:1u,{},unsigned(level),std::move(generation)},random,s.difficulty);
            item.id=EntityId{temporary++}; item.location=ContainerLocation{c.cube,{}}; item.identified=true;
        }
        if (output.unsocket) { item.socketedItems.clear(); item.socketRequiredLevel=0; item.runewordRow=-1; item.runewordStats.clear(); item.nativeFlags&=~0x4000000u; }
        applyCubeProperties(data,output,item,random);
        if (output.socketCount && !item.sockets) {
            int cap=std::max(0,std::min({base->base.sockets.value_or(0),base->base.socketsByLevel[item.level<=25?0:item.level<=40?1:2],base->width*base->height,output.socketCount,6}));
            if(item.quality==ItemQuality::Magic) cap=std::min(cap,3);
            else if(item.quality==ItemQuality::Rare || item.quality==ItemQuality::Crafted || item.quality==ItemQuality::Set || item.quality==ItemQuality::Unique) cap=std::min(cap,1);
            item.sockets=unsigned(cap); if(cap) item.nativeFlags|=0x800u;
        }
        if(preserve && output.quantity) (base->bookCapacity?item.charges:item.quantity)=std::min(base->maxStack,output.quantity);
        if(output.repair) { auto stats=resolveItemStats(data,item,s.character.player.level); item.durability=itemMaximumDurability(data,item,stats); item.nativeFlags&=~0x100u; }
        if(output.recharge) for(auto *list:{&item.savedStats,&item.runewordStats}) for(auto &stat:*list)
            if(stat.id==204) stat.value=(stat.value&~255)|((unsigned(stat.value)>>8)&255);
        if(output.ethereal && !(item.nativeFlags&0x400000u)) { item.nativeFlags|=0x400000u; if(base->family==ItemFamily::Armor) item.defense=item.defense*3/2; }
        prepareSocketedItem(data,item,random); updateCubeRequiredLevel(data,item);
        item.id=EntityId{temporary++};item.location=ContainerLocation{c.cube,{}};
        result.outputs.push_back({std::move(item),{},c.cube});
    }
}
Prepared prepare(const ClassicData &data, Preparation source) {
    Prepared result; result.source=std::move(source);
    if (const auto *socket=std::get_if<SocketItem>(&result.source.pending.request.intent)) sockets(data,result,*socket);
    else cube(data,result);
    auto projection=result.source.character; projection.inventory.items.clear();
    for(const auto &out:result.outputs) projection.inventory.items.emplace(out.item.id,out.item);
    result.equipment=prepareEquipmentRules(data,projection); return result;
}
}
void prepareCrafting(GameHost &host, GameHandle game, const ClassicData &data) {
    for(auto &source:host.pendingCrafting(game)) {
        Prepared result;
        try { result=prepare(data,source); }
        catch(const std::exception &error) { result.source=std::move(source); result.deferred=error.what(); }
        host.installCrafting(game,std::move(result));
    }
}
}
