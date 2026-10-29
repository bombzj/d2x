#include "asset_save_recipe.hpp"
#include "content/items/cube_data.hpp"
#include "content/items/item_magic_loot.hpp"
#include "content/items/item_properties.hpp"
#include "content/items/item_quality.hpp"
#include "content/items/socket_data.hpp"
#include "hosting/item_content.hpp"
#include "gameplay/quest/catalog.hpp"
#include "gameplay/items/gold_limits.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include "persistence/initial_character.hpp"
#include "persistence/save_codec.hpp"
#include "persistence/save_file.hpp"
#include "resources/archive.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace d2x {
namespace {
using Json = nlohmann::json;
void require(bool valid, const std::string &message) {
    if (!valid) throw std::runtime_error("Save recipe: " + message);
}
uint32_t recipeSeed(const Json &value) {
    require(value.is_number_integer() && value>=0 && value<=UINT32_MAX,"seed must be an unsigned 32-bit integer");
    return value.get<uint32_t>();
}
ItemQuality quality(const std::string &name) {
    const std::array names{"normal", "magic", "rare", "set", "unique", "superior", "inferior", "crafted"};
    const auto found = std::find(names.begin(), names.end(), name);
    require(found != names.end(), "unknown quality " + name);
    return ItemQuality(found - names.begin());
}
Json stats(const std::vector<ItemInstance::SavedStat> &values) {
    Json result = Json::array();
    for (const auto &value : values) result.push_back({{"id",value.id},{"parameter",value.parameter},{"value",value.value}});
    return result;
}
Json describe(const ClassicData &, const ItemInstance &, int level);
Json describeSave(const ClassicData &data, const PersistentCharacter &save) {
    const auto &player=save.player;
    Json report{{"name",player.name},{"class",player.characterClass},{"level",player.level},
        {"scope","Decoded save/assembly diagnostics; item descriptions are D2X-derived, not original expected values."},
        {"mapSeed",save.mapSeed},{"difficulty",save.difficulty},{"weaponSet",player.weaponSet},
        {"experience",player.experience},{"gold",player.gold},{"bankGold",player.bankGold},{"unspentAttributes",player.unspentAttributes},
        {"allocated",{player.allocated.strength,player.allocated.dexterity,player.allocated.vitality,player.allocated.energy}},
        {"unspentSkills",player.unspentSkills},{"skills",player.skillRanks},{"selectedSkills",player.selectedSkills},
        {"items",Json::array()},{"containers",Json::object()},{"quests",Json::array()}};
    for(const auto &difficulty:player.quests) {
        Json quests=Json::array();
        for(const auto &quest:difficulty) quests.push_back({{"stage",quest.stage},{"flags",quest.flags}});
        report["quests"].push_back(std::move(quests));
    }
    for(const auto &[id,container]:save.inventory.containers)
        report["containers"][std::to_string(id.value)]={{"kind",int(container.spec.kind)},{"columns",container.spec.columns},{"rows",container.spec.rows}};
    for(const auto id:save.inventory.itemOrder)
        report["items"].push_back(describe(data,save.inventory.items.at(id),player.level));
    return report;
}
Json describe(const ClassicData &data, const ItemInstance &item, int level) {
    const auto nativeQuality=std::find(nativeItemQualities.begin()+1,nativeItemQualities.end(),item.quality)-nativeItemQualities.begin();
    Json result{{"code",item.definition},{"quality",nativeQuality},
        {"level",item.level},{"seed",item.nativeSeed},{"flags",item.nativeFlags},
        {"specialRow",item.specialRow},{"gradeRow",item.gradeRow},{"autoAffix",item.nativeAutoAffix},
        {"requiredLevel",item.requiredLevel},{"identified",item.identified},{"defense",item.defense},
        {"durability",item.durability},{"maxDurability",item.nativeMaxDurability},{"quantity",item.quantity},
        {"format",item.nativeFormat},{"graphic",item.nativeGraphic},{"hasGraphic",item.nativeHasGraphic},
        {"rarePrefixRow",item.rarePrefixRow},{"rareSuffixRow",item.rareSuffixRow},
        {"sockets",item.sockets},{"runewordRow",item.runewordRow},{"stats",stats(item.savedStats)},
        {"runewordStats",stats(item.runewordStats)},{"affixes",Json::array()},
        {"setStats",Json::array()},{"children",Json::array()}};
    for (const auto &affix : item.affixes) result["affixes"].push_back({{"prefix",affix.prefix},{"row",affix.row}});
    for (const auto &layer : item.savedSetStats) result["setStats"].push_back(stats(layer));
    for (const auto &child : item.socketedItems) result["children"].push_back(describe(data,child,level));
    result["description"] = describeItemStats(data,item,level);
    if (const auto *location = std::get_if<ContainerLocation>(&item.location))
        result["location"] = {{"container",location->container.value},{"x",location->cell.x},{"y",location->cell.y}};
    return result;
}
int skillId(const ClassicData &data, const std::string &name) {
    for (const auto &[id, skill] : data.skills.skills) if (skill.sourceName == name) return id;
    throw std::runtime_error("Unknown MPQ skill " + name);
}
struct Assembler {
    const ClassicData &data;
    PersistentCharacter &save;
    ItemInstance item(const Json &spec) {
        const auto code = spec.at("code").get<std::string>();
        const auto *base = data.items.find(code);
        require(base != nullptr, "unknown base " + code);
        const auto q = quality(spec.value("quality",std::string("normal")));
        const int level = spec.value("level",80);
        require(level >= 1 && level <= 127,"item level");
        uint64_t random = initialRandom(recipeSeed(spec.at("seed")));
        LootDrop drop{code,1,{},unsigned(level),{}};
        if (q == ItemQuality::Unique || q == ItemQuality::Set) {
            const auto &records = q == ItemQuality::Unique ? data.uniqueItems : data.setItems;
            const auto name = spec.at("special").get<std::string>();
            const auto record = std::find_if(records.begin(),records.end(),[&](const auto &r){return r.name == name && r.code == code;});
            require(record != records.end(),"special identity " + name);
            require(!record->ladder,"special rejected by the current ladder selection boundary: " + name);
            const auto selected = rollSpecialItem(records,code,level,random,{},false,record->row);
            require(selected.row == record->row,"special not eligible: " + name);
            const auto rolled = rollItemProperties(data,record->properties,selected.randomState,level);
            drop.generation.quality = q;
            drop.generation.specialRow = int(record->row);
            drop.generation.requiredLevel = record->requiredLevel;
            drop.generation.propertyRolls = rolled.values;
            drop.generation.operationRolls = rolled.operations;
            random = rolled.randomState;
        } else if (spec.contains("properties")) {
            std::vector<ItemPropertyTarget> targets;
            for (const auto &p : spec.at("properties")) {
                ItemPropertyTarget target;
                target.code=p.at("code"); target.parameter=p.value("parameter",std::string{});
                if(p.contains("value")) target.value=p.at("value");
                if(p.contains("min")) target.minimum=p.at("min");
                if(p.contains("max")) target.maximum=p.at("max");
                targets.push_back(std::move(target));
            }
            const auto rolled = generatePropertyItem(data,*base,q,level,random,targets);
            drop.generation=rolled.generation;random=rolled.randomState;
        } else if (q == ItemQuality::Crafted) {
            const auto rolled = rollAffixItem(data,*base,q,level,random,{});
            require(rolled.deferred.empty(),rolled.deferred);
            drop.generation=rolled.generation;random=rolled.randomState;
        } else if (base->opensCube && q == ItemQuality::Normal) {
            // The cube is a container prepared explicitly, not a generic quest drop.
            require(code==data.cubeCode,"unknown cube identity");
        } else {
            const auto plan = planSelectedItem(data,code,level,random,{}, {},nativeDropQuality(q));
            require(plan.deferred.empty() && plan.drops.size()==1,"generation " + code + ": " + plan.deferred);
            drop=plan.drops.front();random=plan.randomState;
            require(drop.generation.quality==q,"quality fallback " + code);
        }
        drop.generation.rollNaturalEthereal=false;
        drop.generation.suppressNaturalSockets=true;
        drop.generation.forceEthereal=spec.value("ethereal",false);
        auto result=prepareItem(data,drop,random,save.difficulty);
        result.identified=true;
        if (q == ItemQuality::Crafted) {
            const size_t row=spec.at("cubeRecipe");
            const auto recipe=std::find_if(data.cubeRecipes.begin(),data.cubeRecipes.end(),[&](const auto &r){return r.row==row;});
            require(recipe!=data.cubeRecipes.end() && !recipe->ladder && recipe->outputs.size()==1,"craft recipe unavailable");
            const auto &out=recipe->outputs.front();
            require(out.quality==q && (out.kind==CubeOutputKind::UseItem || out.kind==CubeOutputKind::UseType) && !out.properties.empty(),"not a crafted item recipe");
            require(recipe->version<=100 && !recipe->operation && recipe->minimumDifficulty<=save.difficulty,
                "craft recipe conditions are not supported");
            const auto actor=std::find_if(data.characters.begin(),data.characters.end(),[&](const auto &c){return c.name==save.player.characterClass;});
            require(actor!=data.characters.end() && (recipe->characterClass.empty() || recipe->characterClass==actor->code),"craft class requirement");
            auto input=result;input.quality=ItemQuality::Magic;
            require(!recipe->inputs.empty() && matchesCubeInput(recipe->inputs.front(),input,*base,data.cubeBases.at(code)),"craft base not eligible");
            const int inputLevel=spec.at("inputLevel");
            require(inputLevel>=1 && inputLevel<=127,"craft input item level");
            const int expected=out.level?out.level:save.player.level*out.playerPercent/100+inputLevel*out.itemPercent/100;
            require(level==std::clamp(expected,1,99),"craft output item level mismatch");
            applyCubeProperties(data,out,result,random);
        }
        result.id=EntityId{save.nextEntityId++};
        result.quantity=spec.value("quantity",result.quantity);
        require(result.quantity>0 && result.quantity<=base->maxStack,"stack quantity");
        if(spec.contains("children")) {
            require(q==ItemQuality::Normal || q==ItemQuality::Superior,"explicit sockets require a normal or superior base");
            const auto &children=spec.at("children");
            const int limit=std::min({base->base.sockets.value_or(0),base->base.socketsByLevel[level<=25?0:level<=40?1:2],base->width*base->height,6});
            require(!children.empty() && int(children.size())<=limit,"socket capacity " + code);
            result.sockets=unsigned(children.size());result.nativeFlags|=0x800;
            for(const auto &childSpec:children) {
                auto child=item(childSpec);
                require(data.socketGems.contains(child.definition) || data.items.find(child.definition)->equipment.isType("jewl"),"invalid socket filler");
                child.location=SocketLocation{result.id,unsigned(result.socketedItems.size())};
                result.socketedItems.push_back(std::move(child));
            }
            const auto *word=matchRuneword(data,result);
            require(!word || !word->server,"ladder-only runeword");
            prepareSocketedItem(data,result,random);
        }
        if(spec.contains("runeword")) {
            const auto *word=matchRuneword(data,result);
            require(word && word->name==spec.at("runeword").get<std::string>(),"runeword identity mismatch for " + code + ": expected " + spec.at("runeword").get<std::string>() + ", actual " + (word?word->name:"none"));
        }
        updateCubeRequiredLevel(data,result);
        result.durability=itemMaximumDurability(data,result,resolveItemStats(data,result,save.player.level));
        result.identified=spec.value("identified",true);
        if(spec.value("broken",false)) {require(result.nativeMaxDurability>0,"cannot break non-durable item");result.durability=0;}
        return result;
    }
    void place(ItemInstance value, const Json &spec) {
        const auto destination=spec.value("location",std::string("stash"));
        EntityId container;
        Cell cell;
        if(destination=="equipment") {
            auto slot=equipmentSlotFromCode(spec.at("slot").get<std::string>());
            require(slot.has_value(),"unknown body slot");
            const unsigned weaponSet=spec.value("weaponSet",0u);
            require(weaponSet<=1,"invalid equipment weapon set");
            if(weaponSet) {
                require(*slot==EquipmentSlot::RightHand || *slot==EquipmentSlot::LeftHand,"weapon set on non-hand slot");
                slot=weaponHandSlot(*slot==EquipmentSlot::LeftHand,weaponSet);
            }
            const auto &base=*data.items.find(value.definition);
            require(base.equipment.fits(*slot),"base does not fit body slot " + value.definition);
            const auto &actor=*std::find_if(data.characters.begin(),data.characters.end(),[&](const auto &c){return c.name==save.player.characterClass;});
            require(base.equipment.requiredClass.empty() || base.equipment.requiredClass==actor.code,"wrong class equipment");
            require(value.requiredLevel<=save.player.level && value.socketRequiredLevel<=save.player.level,"equipment level requirement");
            container=*slot==EquipmentSlot::Belt?save.containers.beltEquipment:save.containers.equipment;
            cell={*slot==EquipmentSlot::Belt?0:int(*slot),0};
            if(*slot==EquipmentSlot::Belt) save.inventory.containers.at(save.containers.belt).spec.rows=base.beltRows;
        } else {
            if(destination=="stash") container=save.containers.stash;
            else if(destination=="backpack") container=save.containers.backpack;
            else if(destination=="cube") container=save.containers.cube;
            else throw std::runtime_error("Unknown recipe container " + destination);
            const auto &grid=save.inventory.containers.at(container).spec;
            const auto &base=*data.items.find(value.definition);
            bool found=false;
            for(int y=0;y<=grid.rows-base.height && !found;++y) for(int x=0;x<=grid.columns-base.width && !found;++x) {
                bool occupied=false;
                for(const auto &[id,other]:save.inventory.items) if(const auto *at=std::get_if<ContainerLocation>(&other.location);at && at->container==container) {
                    const auto &size=*data.items.find(other.definition);
                    occupied|=x<at->cell.x+size.width && x+base.width>at->cell.x && y<at->cell.y+size.height && y+base.height>at->cell.y;
                }
                if(!occupied) {cell={x,y};found=true;}
            }
            require(found,"container full: " + destination + " for " + value.definition);
        }
        for(const auto &[id,other]:save.inventory.items)
            require(other.location!=ItemLocation{ContainerLocation{container,cell}},"occupied destination");
        value.location=ContainerLocation{container,cell};
        save.inventory.itemOrder.push_back(value.id);
        save.inventory.items.emplace(value.id,std::move(value));
    }
};
}
void generateSaveRecipes(const ClassicData &data, const std::filesystem::path &recipePath,
                         const std::filesystem::path &destination) {
    require(!data.pd2Host,"only original LoD recipes are supported");
    std::ifstream input(recipePath);
    const auto recipe=Json::parse(input);
    require(recipe.at("schema")==1,"unsupported recipe schema");
    require(!std::filesystem::exists(destination),"destination already exists");
    std::vector<std::pair<std::string,Bytes>> outputs;
    Json manifest{{"schema",1},{"recipe",recipe},{"characters",Json::array()}};
    std::set<std::string> names;
    std::set<std::string> ids;
    for(const auto &entry:recipe.at("characters")) {
        const std::string name=entry.at("name"),className=entry.at("class");
        try {
            require(!entry.at("id").get<std::string>().empty() && ids.insert(entry.at("id").get<std::string>()).second,"empty or duplicate fixture ID");
            require(name.size()>=2 && name.size()<=15 && std::all_of(name.begin(),name.end(),[](unsigned char c){return (c>='A' && c<='Z') || (c>='a' && c<='z');}),"ASCII letter name required");
            std::string lower=name;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
            require(names.insert(lower).second,"duplicate character name");
            const auto actor=std::find_if(data.characters.begin(),data.characters.end(),[&](const auto &c){return c.name==className;});
            require(actor!=data.characters.end(),"unknown class");
            auto save=createCharacter(data,name,unsigned(actor-data.characters.begin()),recipeSeed(entry.at("mapSeed")));
            save.inventory.items.clear();save.inventory.itemOrder.clear();save.player.selectedSkills.fill(-1);
            auto &player=save.player;player.level=entry.value("level",80);
            save.difficulty=entry.value("difficulty",0);
            require(save.difficulty>=0 && save.difficulty<=2,"difficulty");
            for(int difficulty=0;difficulty<save.difficulty;++difficulty) {
                for(const auto id:{QuestId::SistersToTheSlaughter,QuestId::SevenTombs,QuestId::Guardian,QuestId::TerrorsEnd,QuestId::EveOfDestruction})
                    player.quests[size_t(difficulty)][questIndex(id)].stage=questCompletionStage(id);
                player.completedActs[size_t(difficulty)].fill(true);
            }
            for(const auto &difficulty:entry.value("resistanceRewards",Json::array())) {
                const int index=difficulty;
                require(index>=0 && index<=2,"resistance reward difficulty");
                auto &quest=player.quests[size_t(index)][questIndex(QuestId::PrisonOfIce)];
                quest.stage=questCompletionStage(QuestId::PrisonOfIce);
                quest.flags=iceScrollGranted|iceScrollUsed|iceRareGranted;
            }
            require(player.level>=1 && player.level<=99,"character level");
            const auto gold=entry.value("gold",Json(player.gold));const auto bankGold=entry.value("bankGold",Json(player.bankGold));
            require(gold.is_number_integer() && gold>=0 && gold<=uint64_t(player.level)*10000,"carried gold budget");
            require(bankGold.is_number_integer() && bankGold>=0 && bankGold<=stashGoldLimit(unsigned(player.level)),"stash gold budget");
            player.gold=gold.get<unsigned>();player.bankGold=bankGold.get<unsigned>();
            player.experience=data.experienceByClass.at(className).at(size_t(player.level));
            const auto &a=entry.at("allocated");
            player.allocated={a.at(0),a.at(1),a.at(2),a.at(3)};
            player.unspentAttributes=(player.level-1)*actor->statPerLevel-int(allocatedPoints(player.allocated));
            for(const auto &[skill,rank]:entry.at("skills").items()) player.skillRanks.emplace(skillId(data,skill),rank.get<int>());
            player.unspentSkills=player.level-1;
            for(const auto &[id,rank]:player.skillRanks) player.unspentSkills-=rank;
            const auto attributes=deriveCharacterAttributes(*actor,player.level,player.allocated);
            player.hp=attributes.maxLifeFixed;player.mana=attributes.maxManaFixed;player.stamina=attributes.maxStaminaFixed;
            if(entry.contains("leftSkill")) player.selectedSkills[0]=skillId(data,entry.at("leftSkill"));
            if(entry.contains("rightSkill")) player.selectedSkills[1]=skillId(data,entry.at("rightSkill"));
            Assembler assembler{data,save};
            for(const auto &spec:entry.at("items")) assembler.place(assembler.item(spec),spec);
            // The existing codec enforces character budgets and native inventory boundaries.
            const auto bytes=encodeSave(save,data);
            const auto decoded=decodeSave(bytes,data);
            require(decoded.player.name==name && decoded.inventory.items.size()==save.inventory.items.size(),"round-trip identity/count");
            auto report=describeSave(data,decoded);
            report["id"]=entry.at("id");report["bytes"]=bytes.size();
            manifest["characters"].push_back(std::move(report));
            outputs.emplace_back(name+".d2s",bytes);
        } catch(const std::exception &error) {throw std::runtime_error(name+": "+error.what());}
    }
    // Prepare every character before publishing any file; never overwrite an existing batch.
    std::filesystem::create_directories(destination);
    for(const auto &[name,bytes]:outputs) writeFile(destination/name,bytes);
    std::ofstream manifestFile(destination/"manifest.json");manifestFile<<manifest.dump(2)<<'\n';
    require(bool(manifestFile),"manifest write failed");
    std::cout<<"Generated "<<outputs.size()<<" original v96 saves in "<<destination.string()<<'\n';
}
void exportSaveManifest(const ClassicData &data, const std::filesystem::path &savePath,
                        const std::filesystem::path &destination) {
    require(!std::filesystem::exists(destination),"destination already exists");
    const auto save=loadSave(savePath,data);
    const auto report=describeSave(data,save);
    std::ofstream output(destination);
    output<<report.dump(2)<<'\n';
    require(bool(output),"save manifest write failed");
}
}
