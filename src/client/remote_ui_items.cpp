#include "remote_ui_clients_impl.hpp"
#include "content/pd2_stash.hpp"

namespace d2x {
ItemInstance RemoteUiClients::Impl::itemInstance(const OnlineItem &native, const OnlineDecodedItem &di, ItemLocation location) const {
        const auto *definition=data.items.find(native.code);
            ItemInstance item; item.id=itemId(native.id); item.revision=native.revision; item.definition=native.code; item.location=location;
            item.quantity=di.gold.value_or(di.quantity.value_or(1)); item.charges=definition->bookCapacity?item.quantity:0; item.durability=di.durability.value_or(0);
            item.quality=itemQualityFromNative(di.quality).value(); item.identified=di.identified; item.level=di.level; item.defense=int(di.defense.value_or(0));
            item.nativeProperties=true; item.nativeFlags=native.flags; item.nativeMaxDurability=di.maxDurability.value_or(0);
            item.nativeFormat=di.format; item.nativeGraphic=di.graphic; item.nativeHasGraphic=di.hasGraphic; item.personalizedName=di.personalizedName;
            item.nativeAutoAffix=di.autoAffix;
            if (native.flags & 0x10000u) item.ear=ItemEarIdentity{di.earClass,di.earLevel,di.earName};
            if (item.quality==ItemQuality::Normal && definition->equipment.isType("body") &&
                !definition->equipment.isType("play")) item.nativeBodyPart=di.fileIndex;
            if(di.autoAffix) for(const auto &record:data.autoMagic) if(record.row==unsigned(di.autoAffix-1))
                item.requiredLevel=std::max(item.requiredLevel,record.requiredLevel);
            item.sockets=di.sockets; item.runewordRow=-1;
            if (native.flags & 0x4000000u)
                for (const auto &word:data.runewords) if (word.stringId > 0 && word.stringId==di.runeword) item.runewordRow=word.row;
            // Unidentified native special items omit their file index. Zero is
            // not evidence for the first UniqueItems/SetItems row.
            if(di.identified && (item.quality==ItemQuality::Set || item.quality==ItemQuality::Unique)) item.specialRow=di.fileIndex;
            if(item.quality==ItemQuality::Superior || item.quality==ItemQuality::Inferior) item.gradeRow=di.fileIndex;
            if (item.specialRow>=0) {
                const auto &records=item.quality==ItemQuality::Unique?data.uniqueItems:data.setItems;
                for (const auto &record:records) if(int32_t(record.row)==item.specialRow) item.requiredLevel=record.requiredLevel;
            }
            for (bool prefix:{true,false}) for (auto index:prefix?di.prefixes:di.suffixes) {
                if (!index) continue;
                item.affixes.push_back({prefix,int32_t(index-1),{}});
                const auto &records=prefix?data.magicPrefixes:data.magicSuffixes;
                for (const auto &record:records) if(record.row==unsigned(index-1)) {
                    item.requiredLevel=std::max(item.requiredLevel,record.requiredLevel);
                }
            }
            if(item.quality==ItemQuality::Rare || item.quality==ItemQuality::Crafted || item.quality==ItemQuality::Tempered) {
                item.rarePrefixRow=int(di.rarePrefix)-int(data.tables.at("raresuffix").rows().size())-1;
                item.rareSuffixRow=int(di.rareSuffix)-1;
            }
            for(const auto &s:di.stats) item.savedStats.push_back({s.id,int(s.parameter),int(s.value)});
            for(const auto &s:di.runewordStats) item.runewordStats.push_back({s.id,int(s.parameter),int(s.value)});
            for(size_t index=0;index<di.setStats.size();++index) for(const auto &s:di.setStats[index]) item.savedSetStats[index].push_back({s.id,int(s.parameter),int(s.value)});
            return item;
    }
std::optional<ItemInstance> RemoteUiClients::Impl::completeItem(const OnlineItem &native, const OnlineDecodedItem &detail, ItemLocation location) const {
        auto item = itemInstance(native, detail, location);
        const auto &world = session.read().world;
        std::vector<const OnlineItem *> children;
        for (const auto &[id, child] : world.items) {
            if (child.ownerType != 4 || child.owner != native.id) continue;
            children.push_back(&child);
        }
        std::sort(children.begin(), children.end(), [](const auto *first, const auto *second) { return first->socketAssignmentRevision < second->socketAssignmentRevision; });
        for (const auto *entry : children) {
            const auto &child = *entry;
            const auto id = child.id;
            const auto decoded = items.read().items.find(id);
            if (!child.socketAssignmentRevision || child.mode != 6 || decoded == items.read().items.end() || !decoded->second.decoded ||
                decoded->second.revision != child.revision || !data.items.find(child.code)) return {};
            item.socketedItems.push_back(itemInstance(child, decoded->second, SocketLocation{item.id, unsigned(item.socketedItems.size())}));
        }
        if (item.socketedItems.size() != detail.filledSockets) return {};
        const auto *host = data.items.find(native.code);
        for (const auto &child : item.socketedItems) {
            const auto gem = data.socketGems.find(child.definition);
            if (gem == data.socketGems.end() || !child.savedStats.empty()) continue;
            if (!host || host->gemApplyType < 0 || host->gemApplyType > 2) return {};
            for (const auto &property : gem->second.properties[size_t(host->gemApplyType)])
                if (property.directRoll && property.minimum != property.maximum) return {};
        }
        return item;
    }
std::optional<CombatDisplayEquipment> RemoteUiClients::Impl::observedCombatEquipment(OnlineUnitKey key,EquipmentActor actor,
        const std::map<int,int> &ranks,int defense,int rating,int minimum,int maximum) const {
        const auto &online=session.read();
        const auto unit=online.world.units.find(key);
        if(unit==online.world.units.end()) return {};
        const CombatEquipmentKey cacheKey{online.gameGeneration,online.world.itemRevision,items.read().revision,
            unit->second.appearanceRevision,unit->second.stateSequence,actor.characterClass,actor.strength,actor.dexterity,actor.level,
            actor.blockFactor,actor.weaponSet,actor.hireling,defense,rating,minimum,maximum,ranks};
        const auto cached=combatEquipment.find(key);
        if(cached!=combatEquipment.end() && cached->second.key==cacheKey) return cached->second.value;
        auto value=calculateCombatEquipment(key,std::move(actor),ranks,defense,rating,minimum,maximum);
        combatEquipment.insert_or_assign(key,CachedCombatEquipment{cacheKey,value});return value;
    }
std::optional<CombatDisplayEquipment> RemoteUiClients::Impl::calculateCombatEquipment(OnlineUnitKey key,EquipmentActor actor,
        const std::map<int,int> &ranks,int defense,int rating,int minimum,int maximum) const {
        if (data.profile != "lod-named-txt-v1") return {};
        const auto &world=session.read().world;
        const auto unit=world.units.find(key),end=world.units.end();
        const auto observed=combat.states().find(key);
        if(unit==end || !unit->second.equipmentObserved || !unit->second.stateSequence ||
            observed==combat.states().end() || !observed->second.decoded || observed->second.sequence!=unit->second.stateSequence) return {};
        std::vector<CombatDisplayItem> equipped;std::vector<ItemInstance> charms;
        for(const auto &[id,wire]:world.items) {
            if(wire.ownerType!=key.type || wire.owner!=key.id || (wire.flags&0x4100u)) continue;
            const auto *base=data.items.find(wire.code);
            const bool worn=wire.mode==1 && wire.body>=1 && wire.body<=10;
            const bool charm=key.type==0 && wire.mode==0 && wire.page==1 && base && base->equipment.isType("char");
            if(!worn && !charm) continue;
            const auto decoded=items.read().items.find(id);
            if(!base || decoded==items.read().items.end() || !decoded->second.decoded || decoded->second.revision!=wire.revision) return {};
            auto item=completeItem(wire,decoded->second,SocketLocation{{},0});if(!item) return {};
            if(worn) {
                auto slot=EquipmentSlot(wire.body-1);
                if(actor.weaponSet && slot==EquipmentSlot::RightHand) slot=EquipmentSlot::AlternateRightHand;
                if(actor.weaponSet && slot==EquipmentSlot::LeftHand) slot=EquipmentSlot::AlternateLeftHand;
                equipped.push_back({slot,std::move(*item)});
            } else charms.push_back(std::move(*item));
        }
        std::vector<ResolvedItemStat> stateStats;std::set<int> states,accounted;
        for(const auto &[id,list]:observed->second.states) {
            states.insert(id);
            for(const auto &entry:list.stats) {
                const auto cost=std::find_if(data.itemStats.begin(),data.itemStats.end(),[&](const auto &record){return record.id==entry.id;});
                if(cost==data.itemStats.end() || entry.value<INT32_MIN || entry.value>INT32_MAX) return {};
                stateStats.push_back({cost->name,cost->name,int(entry.value),int(entry.parameter),int(entry.value)});
            }
            if(data.skills.descriptions) for(const auto &[skill,source]:data.skills.descriptions->skills) {
                if(source.reference("passivestate")!=list.name) continue;
                bool carried=false;
                for(unsigned slot=1;slot<=5;++slot) {
                    const auto name=source.reference("passivestat"+std::to_string(slot));
                    const auto cost=std::find_if(data.itemStats.begin(),data.itemStats.end(),[&](const auto &record){return record.name==name;});
                    if(cost!=data.itemStats.end() && std::any_of(list.stats.begin(),list.stats.end(),[&](const auto &entry){return cost->id==entry.id;})) carried=true;
                }
                if(carried) accounted.insert(skill);
            }
        }
        std::map<int,int> baseRanks;
        if(!actor.hireling) for(const auto &[id,rank]:session.read().world.playerBaseSkills) baseRanks.emplace(id,rank);
        int concentration=0;
        int smiteMinimum=0,smiteMaximum=0;
        for(const auto &[id,list]:observed->second.states) {
            (void)id;
            if(list.name=="concentration") for(const auto &entry:list.stats) for(const auto &cost:data.itemStats)
                if(cost.id==entry.id && cost.name=="damagepercent" && !entry.parameter) concentration=int(entry.value);
            if(list.name=="holyshield" && data.skills.descriptions) {
                int skill=-1,rank=0;
                for(const auto &entry:list.stats) for(const auto &cost:data.itemStats) if(cost.id==entry.id) {
                    if(cost.name=="modifierlist_skill") skill=int(entry.value);
                    if(cost.name=="modifierlist_level") rank=int(entry.value);
                }
                const auto source=data.skills.descriptions->skills.find(skill);
                if(rank>0 && source!=data.skills.descriptions->skills.end() && std::any_of(equipped.begin(),equipped.end(),[&](const auto &entry){
                    const auto *definition=data.items.find(entry.item.definition);return definition && definition->equipment.isType("shld");
                })) {
                    const std::map<std::string,double,std::less<>> values;
                    SkillDescriptionInput input{skill,rank,baseRanks,ranks,values};input.baseRanksComplete=true;
                    const auto bonus=SkillDescriptionValues(*data.skills.descriptions,input,source->second).parameter("clc1");
                    if(!bonus) return {};
                    stateStats.push_back({"item_armor_percent","item_armor_percent",*bonus,0,*bonus});
                    const auto smite=SkillDescriptionValues(*data.skills.descriptions,input,source->second).physicalDamage();
                    if(!smite.minimum || !smite.maximum) return {};
                    smiteMinimum=*smite.minimum;smiteMaximum=*smite.maximum;
                }
            }
        }
        auto result=projectCombatEquipment(data,std::move(actor),equipped,charms,stateStats,ranks,baseRanks,states,accounted,defense,rating,minimum,maximum);
        result.concentrationDamagePercent=concentration;
        result.modifiers.combat.smiteMinimum+=smiteMinimum;result.modifiers.combat.smiteMaximum+=smiteMaximum;
        return result;
    }
std::optional<unsigned> RemoteUiClients::Impl::itemQuote(uint32_t id, OnlineItemAction action) const {
        const auto &online = session.read();
        const auto &world = online.world;
        const bool repair = action == OnlineItemAction::Repair;
        const bool sale = action == OnlineItemAction::Sell;
        if ((!repair && !sale && action != OnlineItemAction::Buy) ||
            !onlineInteractionMatches(context, online) || !world.npcConversation ||
            world.npcRequested != world.npcConversation->source || !online.load.difficulty) return {};
        const auto vendor = data.vendors.find(npcIdentity());
        const auto wire = world.items.find(id);
        const auto decoded = items.read().items.find(id);
        if (wire == world.items.end() || decoded == items.read().items.end() ||
            !decoded->second.decoded || decoded->second.revision != wire->second.revision) return {};
        const auto &native = wire->second;
        const auto &detail = decoded->second;
        if (!detail.gamble && vendor == data.vendors.end()) return {};
        const auto *definition = data.items.find(native.code);
        if (!definition || (detail.gamble && (sale || repair || !world.shopGamble))) return {};
        if (sale || repair) {
            if (native.ownerType != 0 || native.owner != online.load.playerUnitId ||
                !((native.mode == 0 && native.page == 1) || native.mode == 1 || (sale && native.mode == 4))) return {};
            if (sale && (definition->questTag || (native.flags & 0x1000u))) return {};
            if (repair && !shopView.repairAvailable) return {};
        } else if (world.shopSource != world.npcRequested || native.ownerType != 1 ||
            native.owner != world.shopSource || native.action != 11 || native.mode != 0) return {};
        if (!repair && (native.flags & 0x20000u)) return 1;
        if (detail.gamble && !detail.format) return itemGamblePrice(data, native.code, 0, detail.format, 0);
        std::optional<int64_t> bodyCost;
        std::vector<int> factors;
        std::vector<int> repairFactors;
        if (!detail.gamble) for (const auto &price : vendor->second.questPrices) {
            if (price.flag < 0 || !world.quests.playerFlags ||
                size_t(price.flag) >= world.quests.playerFlags->size()) return {};
            if ((*world.quests.playerFlags)[size_t(price.flag)] & 3u) {
                factors.push_back(repair ? price.repair : sale ? price.buy : price.sell);
                repairFactors.push_back(price.repair);
            }
        }
        int reduce = 0;
        if (!sale) {
            if (const auto value = stat("item_reducedprices")) reduce = int(*value);
            else {
                const auto known=knownEquipmentStat("item_reducedprices",characterView.level);
                if (!known) return {};
                reduce=*known;
            }
        }
        if (detail.gamble) {
            const auto level = stat("level");
            return itemGamblePrice(data, native.code, int(level.value_or(0)), detail.format, reduce);
        }
        if ((definition->maxStack > 1 || definition->bookCapacity) && !detail.quantity) return {};
        if (definition->family == ItemFamily::Armor && !detail.defense) return {};
        if (definition->family != ItemFamily::Misc && (!detail.maxDurability || (*detail.maxDurability && !detail.durability))) return {};
        const auto item = completeItem(native, detail, SocketLocation{{}, 0});
        if (!item) return {};
        return itemTradePrice(data, *item, vendor->second, repair, factors, reduce, sale, *online.load.difficulty, detail.autoAffix, bodyCost, repairFactors,
            definition->bookCapacity ? std::optional<unsigned>(detail.book) : std::nullopt);
    }
InventoryItemView RemoteUiClients::Impl::projectItem(const OnlineItem &native, const OnlineDecodedItem &di, ItemLocation location) const {
            const auto *definition=data.items.find(native.code);
            const auto complete = completeItem(native, di, location);
            auto item = complete.value_or(itemInstance(native, di, location));
            const auto stats = resolveItemStats(data, item, characterView.level);
            const auto maximum = itemMaximumDurability(data, item, stats);
            const auto &online = session.read();
            std::vector<int32_t> equippedSets;
            bool equipmentKnown = false;
            if (item.quality == ItemQuality::Set && online.load.playerUnitId) {
                const auto actor = online.world.units.find({0, *online.load.playerUnitId});
                equipmentKnown = actor != online.world.units.end() && actor->second.equipmentObserved;
                for (const auto &[id, equipped] : online.world.items) {
                    if (equipped.ownerType != 0 || equipped.owner != online.load.playerUnitId || equipped.mode != 1 || equipped.body > 10 || (equipped.flags & 0x4100u)) continue;
                    const auto decoded = items.read().items.find(id);
                    if (decoded == items.read().items.end() || !decoded->second.decoded || decoded->second.revision != equipped.revision) { equipmentKnown = false; continue; }
                    const auto piece = itemInstance(equipped, decoded->second, SocketLocation{{}, 0});
                    if (piece.identified && piece.quality == ItemQuality::Set && piece.specialRow >= 0 &&
                        std::find(equippedSets.begin(), equippedSets.end(), piece.specialRow) == equippedSets.end())
                        equippedSets.push_back(piece.specialRow);
                }
            }
            auto display=describeInventoryItem(data,data.items,item,{characterView.level,characterView.attributes[0],characterView.attributes[1],maximum,online.world.quests.cainStones.value_or(std::array<int,5>{}),characterView.classCode,equippedSets,equipmentKnown});
            if (!complete) display.tooltip.push_back({"Socket properties are not fully assigned by the server",ItemTextTone::Error});
            std::string groundArt=definition->groundAnimation, inventoryArt=di.artKey;
            if(item.specialRow>=0) {
                const auto &records=item.quality==ItemQuality::Unique?data.uniqueItems:data.setItems;
                for(const auto &record:records) if(int32_t(record.row)==item.specialRow) {
                    if(!record.groundAnimation.empty()) groundArt=record.groundAnimation;
                    if(item.identified && !record.icon.empty()) inventoryArt=record.icon;
                }
            }
            InventoryItemView projection{item.id,item.revision,item.definition,std::move(inventoryArt),std::move(display.name),location,item.quality,item.identified,item.quantity,item.durability,item.charges,std::move(display.tooltip),native.flags,item.sockets,item.runewordRow>=0,std::move(groundArt)};
            if (complete && di.revision == native.revision && definition->maxStack > 1 && !definition->bookCapacity) {
                InventoryStackView stack;
                stack.fileIndex = di.fileIndex;
                constexpr std::array<std::string_view,6> names{"mindamage","maxdamage","secondary_mindamage",
                    "secondary_maxdamage","item_throw_mindamage","item_throw_maxdamage"};
                for (const auto &stat : stats) if (!stat.layer)
                    for (size_t index=0;index<names.size();++index)
                        if (stat.name == names[index]) {
                            if (stat.replace) stack.damage[index] = stat.rawValue;
                            else stack.damage[index] += stat.rawValue;
                        }
                projection.stack = stack;
            }
            projection.specialRow = item.specialRow;
            projection.iconCells = {di.width, di.height};
            projection.colorTransform = inventoryColorTransform(data, data.items, item);
            projection.groundColorTransform = groundColorTransform(data, data.items, item);
            if (definition->equipment.isType("gold"))
                projection.artFrame = item.quantity < 100 ? 0 : item.quantity < 500 ? 1 : item.quantity < 5000 ? 2 : 3;
            projection.socketContentsKnown = complete.has_value();
            if (complete)
                for (const auto &child : item.socketedItems) {
                    const auto detail = items.read().items.find(guid(child.id));
                    if (detail == items.read().items.end()) { projection.socketContentsKnown = false; break; }
                    auto art = detail->second.artKey;
                    if (child.identified && child.specialRow >= 0) {
                        const auto &records = child.quality == ItemQuality::Unique ? data.uniqueItems : data.setItems;
                        for (const auto &record : records)
                            if (int32_t(record.row) == child.specialRow && !record.icon.empty()) art = record.icon;
                    }
                    projection.socketItems.push_back({std::move(art), inventoryColorTransform(data, data.items, child), child.nativeFlags});
                }
            if (data.profile == "lod-named-txt-v1" && complete && di.revision == native.revision)
                projection.cellTone = display.requirementsMet && item.identified && !(native.flags & 0x4104u) &&
                    (!definition->bookCapacity || item.charges) ? InventoryCellTone::Usable : InventoryCellTone::Unusable;
            const auto &source = data.tables.at(definition->base.sourceTable);
            int64_t indestructible = 0;
            for (const auto &stat : stats)
                if (stat.name == "item_indesctructible" && !stat.layer) indestructible += stat.rawValue;
            // D2Client 1.13c RVA 0x28680: HasDurability, non-stackable,
            // then current * 100 / resolved maximum, with integer truncation.
            if (complete && di.revision == native.revision && di.maxDurability && di.durability && maximum &&
                definition->maxDurability && !source.number(definition->base.sourceRow, "nodurability").value_or(0) &&
                !source.number(definition->base.sourceRow, "stackable").value_or(0) && indestructible <= 0)
                projection.durabilityPercentage = unsigned(std::min(uint64_t(*di.durability) * 100 / maximum, uint64_t{100}));
            return projection;
    }
std::optional<unsigned> RemoteUiClients::Impl::repairAllQuote() const {
        const auto &online = session.read();
        if (!shopView.repairAvailable || !online.load.playerUnitId || !onlineInteractionMatches(context, online)) return {};
        const auto actor = online.world.units.find({0, *online.load.playerUnitId});
        if (actor == online.world.units.end() || !actor->second.equipmentObserved) return {};
        uint64_t total = 0;
        for (const auto &[id, item] : online.world.items) {
            if (item.ownerType != 0 || item.owner != online.load.playerUnitId || item.mode != 1 || !item.body || item.body > 12) continue;
            if (!item.flags || !(item.flags & 0x10u) || (item.flags & 0x400000u)) continue;
            const auto decoded = items.read().items.find(id);
            const auto *definition = data.items.find(item.code);
            if (decoded == items.read().items.end() || !decoded->second.decoded || decoded->second.revision != item.revision || !definition) return {};
            const auto instance = completeItem(item, decoded->second, SocketLocation{{}, 0});
            if (!instance) return {};
            const auto stats = resolveItemStats(data, *instance, characterView.level);
            auto value = [&](std::string_view name) {
                int64_t totalValue = 0;
                for (const auto &stat : stats) if (stat.name == name) totalValue += stat.rawValue;
                return totalValue;
            };
            const auto maximumDurability = itemMaximumDurability(data, *instance, stats);
            const auto &source = data.tables.at(definition->base.sourceTable);
            bool needed = definition->equipment.repairable && definition->maxDurability && maximumDurability &&
                !source.number(definition->base.sourceRow, "nodurability").value_or(0) &&
                !value("item_indesctructible") && decoded->second.durability.value_or(0) != maximumDurability;
            if (definition->equipment.repairable && definition->equipment.throwable && definition->maxStack > 1) {
                const auto maximum = std::clamp(int64_t(definition->maxStack) + value("item_extra_stack"), int64_t(1), int64_t(511));
                if (!decoded->second.quantity) return {};
                needed |= *decoded->second.quantity < maximum;
            }
            for (const auto &stat : stats) {
                if (stat.name != "item_charged_skill") continue;
                if (stat.rawValue < 0 || stat.rawValue > 65535 || (unsigned(stat.rawValue) & 0xff) > (unsigned(stat.rawValue) >> 8)) return {};
                needed |= (unsigned(stat.rawValue) & 0xff) < (unsigned(stat.rawValue) >> 8);
            }
            if (!needed) continue;
            const auto price = itemQuote(id, OnlineItemAction::Repair);
            if (!price || total + *price > UINT32_MAX) return {};
            total += *price;
        }
        return unsigned(total);
    }
void RemoteUiClients::Impl::itemNotice(const InventoryItemView &item, unsigned quantity, std::string prefix, bool gold) {
        if (!quantity) return;
        std::string name = item.name;
        const auto *definition = data.items.find(item.definition);
        if (gold) name = std::to_string(quantity) + " " + name;
        else if (quantity > 1)
            name += definition && definition->bookCapacity ? " (" + std::to_string(quantity) + " pages)" :
                " x" + std::to_string(quantity);
        itemNotices.push_back(std::move(prefix) + name);
        while (itemNotices.size() > 32) itemNotices.pop_front();
    }
void RemoteUiClients::Impl::projectItemFeedback(const InventoryView &current) {
        const auto &w = session.read().world;
        if (inventoryView.gameGeneration != current.gameGeneration || inventoryView.areaGeneration != current.areaGeneration) {
            transferFeedback.reset(); feedbackSequence = 0; itemNotices.clear();
            return; // Initial snapshots are not transfers.
        }
        const auto now = std::chrono::steady_clock::now();
        const auto &request = w.itemRequest;
        if (request && request->sequence != feedbackSequence) {
            feedbackSequence = request->sequence; transferFeedback.reset();
            const auto action = request->command.action;
            const auto *source = inventoryView.item(itemId(request->command.item));
            const bool pickup = action == OnlineItemAction::Pickup;
            const auto *container = source ? std::get_if<ContainerLocation>(&source->location) : nullptr;
            if (source && source->revision == request->command.itemRevision && source->quantity &&
                ((pickup && std::holds_alternative<GroundLocation>(source->location)) ||
                 ((action == OnlineItemAction::Stack || action == OnlineItemAction::Book) &&
                  container && (container->container == inventoryView.containers.cursor ||
                                container->container == inventoryView.containers.backpack)))) {
                TransferFeedback feedback;
                feedback.action = action; feedback.sequence = request->sequence; feedback.source = *source;
                feedback.gold = inventoryView.gold; feedback.goldKnown = inventoryView.goldKnown;
                const auto *definition = data.items.find(source->definition);
                feedback.isGold = definition && definition->equipment.isType("gold");
                for (const auto &[id, item] : inventoryView.items) {
                    if (id == source->id || !std::holds_alternative<ContainerLocation>(item.location)) continue;
                    if (!pickup && id != itemId(request->command.target)) continue;
                    if (!pickup && item.revision != request->command.targetRevision) continue;
                    const auto *destination = data.items.find(item.definition);
                    if (destination && (item.definition == source->definition || destination->bookScroll == source->definition))
                        feedback.quantities.emplace(id,item.quantity);
                }
                transferFeedback = std::move(feedback);
            }
        }
        if (transferFeedback && request && request->state == OnlineItemRequest::State::Updated &&
            transferFeedback->expires == std::chrono::steady_clock::time_point::max())
            transferFeedback->expires = now + std::chrono::seconds(5); // Allow ordered follow-up packets after a related update.
        if (transferFeedback && (!request || request->sequence != transferFeedback->sequence ||
            request->state == OnlineItemRequest::State::Rejected || request->state == OnlineItemRequest::State::Interrupted ||
            request->state == OnlineItemRequest::State::TimedOut || current.dead || now >= transferFeedback->expires))
            transferFeedback.reset();
        EntityId tracked;
        if (transferFeedback) {
            auto &feedback = *transferFeedback;
            tracked = feedback.source.id;
            const auto *source = current.item(tracked);
            const bool removed = !w.items.contains(guid(tracked)); // A decode failure is not removal.
            const bool assigned = source && std::holds_alternative<ContainerLocation>(source->location);
            uint64_t received = 0;
            bool book = false;
            for (const auto &[id, before] : feedback.quantities) {
                const auto *destination = current.item(id);
                if (!destination || !std::holds_alternative<ContainerLocation>(destination->location) || destination->quantity <= before) continue;
                received += destination->quantity - before;
                if (const auto *definition = data.items.find(destination->definition)) book |= definition->bookCapacity != 0;
            }
            bool complete = false, confirmed = false;
            if (feedback.isGold) {
                // Gold pickup removes the pile (partial pickup creates a new pile).
                // Neither an unrelated gold stat nor disappearance alone is success.
                if (removed && feedback.goldKnown && current.goldKnown && current.gold > feedback.gold) {
                    received = current.gold - feedback.gold;
                    complete = received <= feedback.source.quantity;
                    confirmed = complete;
                } else received = 0;
            } else if (feedback.action == OnlineItemAction::Pickup && assigned) {
                received += source->quantity; // Include the remainder of an automatic stack pickup.
                complete = received == feedback.source.quantity;
                confirmed = complete;
            } else if (removed) {
                complete = received == feedback.source.quantity;
                confirmed = complete;
            } else if (source && source->quantity <= feedback.source.quantity &&
                       received == feedback.source.quantity - source->quantity) {
                // Partial manual/ground stacking requires both sides of the transfer.
                complete = source->quantity == 0;
                confirmed = true;
            } else received = 0;
            if (received > feedback.reported && received <= feedback.source.quantity &&
                confirmed) {
                const auto &display = source && assigned ? *source : feedback.source;
                const auto prefix = feedback.action == OnlineItemAction::Pickup ? "Picked up: " :
                    book ? "Added to tome: " : "Stacked: ";
                itemNotice(display,unsigned(received - feedback.reported),prefix,feedback.isGold);
                feedback.reported = unsigned(received);
            }
            if (complete) transferFeedback.reset();
        }
        // Preserve unsolicited, explicit ground-to-owned assignment feedback.
        for (const auto &[id, item] : current.items) {
            if (id == tracked || !std::holds_alternative<ContainerLocation>(item.location)) continue;
            const auto *previous = inventoryView.item(id);
            if (previous && std::holds_alternative<GroundLocation>(previous->location))
                itemNotice(item,item.quantity,"Picked up: ");
        }
    }
void RemoteUiClients::Impl::projectInventory() {
        const auto &online=session.read(); const auto &w=online.world; const auto &decoded=items.read();
        InventoryView v; v.revision=revision; v.gameGeneration=online.gameGeneration; v.areaGeneration=w.areaGeneration;
        v.personalItemActionsOnly = online.remoteLobbyOnly;
        v.containers={containerId(1),containerId(2),containerId(3),containerId(4),containerId(5),containerId(6),containerId(7),containerId(8)};
        const auto &owned=v.containers;
        for (const auto &[id,kind,columns,rows]:std::vector<std::tuple<EntityId,ContainerKind,int,int>>{
            {owned.backpack,ContainerKind::Backpack,decoded.columns,decoded.rows},{owned.belt,ContainerKind::Belt,4,std::max(1,decoded.beltSlots/4)},
            {owned.stash,ContainerKind::Stash,decoded.stashColumns,decoded.stashRows},{owned.cube,ContainerKind::Cube,decoded.cubeColumns,decoded.cubeRows},
            {owned.equipment,ContainerKind::Equipment,int(EquipmentSlot::Count),1},{owned.beltEquipment,ContainerKind::BeltEquipment,1,1},
            {owned.cursor,ContainerKind::Cursor,1,1},{owned.hirelingEquipment,ContainerKind::Equipment,int(EquipmentSlot::Count),1}})
            v.containerViews.emplace(id,InventoryContainerView{id,kind,columns,rows,
                v.readOnly || (v.personalItemActionsOnly && id == owned.hirelingEquipment)});
        auto layout=[](const auto &x){return InventoryLayoutView{x.columns,x.rows,x.left,x.top,x.cellSize,x.expansion};};
        if (const auto playerLayout = data.playerInventoryLayouts.find(characterView.classCode);
            playerLayout != data.playerInventoryLayouts.end()) {
            v.backpackLayout = layout(playerLayout->second.grid);
            v.equipmentSlots = playerLayout->second.slots;
            v.itemCellSize = {v.backpackLayout.cellSize, v.backpackLayout.cellSize};
            v.panelTop = playerLayout->second.top;
            v.panelHeight = playerLayout->second.height;
        }
        v.stashLayout=layout(data.stashLayout); v.cubeLayout=layout(data.cubeLayout); v.hirelingSlots=data.hirelingLayout.slots;
        v.storagePanelTop = data.stashLayout.panelTop; v.storagePanelHeight = data.stashLayout.panelHeight;
        v.storagePage = w.stashPage; v.storagePagePending = w.requestedStashPage.has_value();
        v.storageShared = w.sharedStash; v.storageCnConfirmed = w.cnStashConfirmed && w.stashSynchronized;
        v.storageMaterialsAvailable = v.personalItemActionsOnly && w.cnMaterialsAvailable && w.cnMaterialsSynchronized;
        v.storageMaterialsOpen = v.storageMaterialsAvailable && w.cnMaterialsOpen;
        if (v.storageMaterialsAvailable) for (const auto &slot : pd2CnMaterialSlots) {
            const auto *definition = data.items.find(slot.code);
            if (!definition || definition->icon.empty()) continue;
            const auto key = onlineCnMaterialKey(slot.code);
            const auto count = key ? w.cnMaterialCounts.find(*key) : w.cnMaterialCounts.end();
            v.storageMaterials.push_back({std::string(slot.code), definition->name, definition->icon,
                slot.x, slot.y, definition->width, definition->height,
                count == w.cnMaterialCounts.end() ? 0 : count->second});
        }
        v.storagePagingAvailable = onlinePd2ItemAction(OnlineItemAction::StoragePage) && w.stashInitialized &&
            !w.requestedStashPage && !(w.cnStashPlayerFlags & 8);
        if (w.playerTrade.phase == OnlinePlayerTrade::Phase::Open) {
            const auto &table = data.tables.at("inventory");
            size_t peerRow = table.rows().size(), ownRow = peerRow;
            for (size_t row = 0; row < table.rows().size(); ++row) {
                if (table.value(row,"class") == "Trade Page 1-2") peerRow = row;
                if (table.value(row,"class") == "Trade Page 2-2") ownRow = row;
            }
            auto tradeLayout = [&](size_t row) {
                InventoryLayoutView l;
                if (row >= table.rows().size() || peerRow >= table.rows().size()) return l;
                l.columns = table.number(row,"gridX").value_or(0); l.rows = table.number(row,"gridY").value_or(0);
                l.left = table.number(row,"gridLeft").value_or(0) - table.number(peerRow,"invLeft").value_or(0);
                l.top = table.number(row,"gridTop").value_or(0) - table.number(peerRow,"invTop").value_or(0);
                l.cellSize = table.number(row,"gridBoxWidth").value_or(0);
                if (l.columns < 1 || l.rows < 1 || l.cellSize < 1 || l.left < 0 || l.top < 0 ||
                    l.cellSize != table.number(row,"gridBoxHeight").value_or(0) ||
                    l.left + l.columns*l.cellSize > 320 || l.top + l.rows*l.cellSize > 432) return InventoryLayoutView{};
                return l;
            };
            v.ownTradeLayout = tradeLayout(ownRow); v.peerTradeLayout = tradeLayout(peerRow);
            if (v.ownTradeLayout.cellSize && v.peerTradeLayout.cellSize) {
                v.ownTrade = containerId(9); v.peerTrade = containerId(10);
                v.containerViews.emplace(v.ownTrade, InventoryContainerView{v.ownTrade,ContainerKind::Backpack,v.ownTradeLayout.columns,v.ownTradeLayout.rows});
                v.containerViews.emplace(v.peerTrade, InventoryContainerView{v.peerTrade,ContainerKind::Backpack,v.peerTradeLayout.columns,v.peerTradeLayout.rows,true});
            }
        }
        v.cubeCode=data.cubeCode; v.staffRecipeOutput=data.staffRecipe.output;
        v.weaponSet=w.weaponSet; v.gold=unsigned(stat("gold").value_or(0)); v.bankGold=unsigned(stat("goldbank").value_or(0));
        v.goldKnown = stat("gold").has_value();
        v.bankGoldKnown = stat("goldbank").has_value();
        v.storagePageName = w.stashPageName;
        v.walletLimit=unsigned(characterView.level)*10000; v.groundGoldLimit=v.walletLimit;
        v.bankGoldLimit=stashGoldLimit(unsigned(characterView.level));
        for (const auto &[code,def]:data.items.entries()) if(def.equipment.isType("gold")) { v.groundGoldLimit=def.maxStack; break; }
        v.dead=characterView.dead;
        if(w.storage.kind==OnlineStorageKind::Stash) v.storage=owned.stash;
        if(w.playerPosition) v.dropLocation=GroundLocation{mapView.region,mapView.observer};
        for(const auto &[id,native]:w.items) {
            if (v.personalItemActionsOnly && (native.flags & 0x40000000u)) continue; // Native preview inventory, not this player's items.
            if (v.personalItemActionsOnly && (!native.positionKnown ||
                (native.mode == 0 && native.page == 5 && (v.storageMaterialsOpen || !w.stashSynchronized ||
                    native.stashPage != w.stashPage || native.sharedStash != w.sharedStash)))) continue;
            const bool ground=native.mode==3 || native.mode==5;
            const bool mercenary=w.hireling && native.ownerType==1 && native.owner==w.hireling->id && native.mode==1;
            if(!ground && !mercenary && (native.ownerType!=0 || native.owner!=online.load.playerUnitId)) continue;
            const auto d=decoded.items.find(id); const auto *definition=data.items.find(native.code);
            if(d==decoded.items.end() || !d->second.decoded || !definition) continue;
            const auto &di=d->second; ItemLocation location;
            if(ground && scene && scene->origin) location=GroundLocation{mapView.region,
                {float(int(native.groundX)-scene->origin->x),float(int(native.groundY)-scene->origin->y)}};
            else if(mercenary && (native.body==1 || native.body==3 || native.body==4 || native.body==5)) location=ContainerLocation{owned.hirelingEquipment,{int(native.body)-1,0}};
            else if(native.mode==4) location=ContainerLocation{owned.cursor,{}};
            else if(native.mode==2) location=ContainerLocation{owned.belt,{native.x%4,native.x/4}};
            else if(native.mode==1 && native.body>=1 && native.body<=12) {
                int slot=native.body-1;
                if(native.body==4 || native.body==5) slot=int(weaponHandSlot(native.body==5,w.weaponSet));
                else if(native.body==11 || native.body==12) slot=int(weaponHandSlot(native.body==12,1-w.weaponSet));
                location = native.body==8 ? ItemLocation{ContainerLocation{owned.beltEquipment,{}}} : ItemLocation{ContainerLocation{owned.equipment,{slot,0}}};
            } else if(native.mode==0 && (native.page==1 || native.page==4 || native.page==5))
                location=ContainerLocation{native.page==1?owned.backpack:native.page==4?owned.cube:owned.stash,{native.x,native.y}};
            else if(native.mode==0 && ((native.page==3 && v.ownTrade) || (native.page==2 && v.peerTrade)))
                location=ContainerLocation{native.page==3?v.ownTrade:v.peerTrade,{native.x,native.y}};
            else continue;
            if (const auto *at = std::get_if<ContainerLocation>(&location)) {
                const auto *container = v.container(at->container);
                if (!container) continue;
                const bool grid = container->kind == ContainerKind::Backpack ||
                    container->kind == ContainerKind::Stash || container->kind == ContainerKind::Cube;
                if (grid && (di.width < 1 || di.height < 1 || at->cell.x < 0 || at->cell.y < 0 ||
                    at->cell.x + di.width > container->columns || at->cell.y + di.height > container->rows)) continue;
            }
            InventoryDefinitionView def; def.targetCursor=definition->targetCursor; def.code=native.code; def.name=definition->name; def.bookScroll=definition->bookScroll;
            def.width=di.width; def.height=di.height; def.beltRows=definition->beltRows; def.maxStack=definition->maxStack; def.bookCapacity=definition->bookCapacity;
            def.beltAllowed=definition->beltAllowed; def.opensCube=definition->opensCube; def.twoHanded=definition->equipment.twoHanded;
            def.autoBelt=definition->autoBelt;
            def.beltPotionType=beltPotionFamily(native.code);
            def.shield=definition->equipment.isType("shld"); def.quiver=!definition->equipment.quiver.empty();
            def.socketFiller=definition->equipment.isType("sock"); def.identifySource=data.isIdentifyScroll(native.code)||data.isIdentifyScroll(def.bookScroll);
            def.usable=data.tables.at(definition->base.sourceTable).number(definition->base.sourceRow,"useable").value_or(0)!=0;
            for(size_t slot=0;slot<def.slots.size();++slot) def.slots[slot]=definition->equipment.fits(EquipmentSlot(slot));
            v.definitions.emplace(native.code,std::move(def));
            auto projection=projectItem(native,di,location);
            if (ground) {
                projection.groundPickupAllowed = !v.readOnly && native.mode == 3;
                projection.groundAnimationRevision = native.groundAnimationRevision;
                const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                if (native.groundAnimationRevision && now >= native.groundAnimationReceivedMilliseconds)
                    projection.groundAnimationAge = std::min(4.f, float(now-native.groundAnimationReceivedMilliseconds)/1000.f);
            }
            v.items.emplace(itemId(id), std::move(projection));
        }
        if(w.itemRequest && w.itemRequest->command.action==OnlineItemAction::Pickup && w.itemRequest->state==OnlineItemRequest::State::Pending)
            v.pickupTarget=itemId(w.itemRequest->command.item);
        projectItemFeedback(v);
        v.targetingRevision=w.itemTargetingRevision;
        v.staffRevision=w.staffRevision;v.staffResult=w.staffResult;
        if(w.staffSource) {
            const auto object=w.units.find({2,*w.staffSource});
            if(object!=w.units.end() && object->second.classId) {
                const auto &table=data.tables.at("objects");
                for(size_t row=0;row<table.rows().size();++row) if(table.number(row,"Id")==*object->second.classId && table.number(row,"OperateFn")==25) v.staffSource=EntityId{(uint64_t{2}<<32)+*w.staffSource+1};
            }
        }
        v.targetingReady=!w.itemTargetingSource;
        if(w.itemTargetingSource) {
            const auto source=v.items.find(itemId(*w.itemTargetingSource));
            if(source!=v.items.end()) {
                v.targetingReady=true;
                const auto definition=v.definitions.find(source->second.definition);
                if(definition!=v.definitions.end() && definition->second.identifySource) v.targetingSource=source->second.handle();
            }
        }
        inventoryView=std::move(v);
    }
}
