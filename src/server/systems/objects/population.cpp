#include "server/area_store.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace d2x::server::objects {
std::optional<GroundGenerationRequest> System::pendingGroundGeneration() const {
    for(const auto &entry:groundGeneration_) if(!entry.prepared && entry.deferred.empty()) return entry.request;
    return {};
}
DomainResult<> System::prepareGroundGeneration(uint64_t request,items::PreparedBatch batch,std::string deferred) {
    for(auto &entry:groundGeneration_) if(entry.request.request==request) {
        if(entry.prepared || !entry.deferred.empty()) return {DomainStatus::Stale,{}};
        if(!deferred.empty()) {entry.deferred=std::move(deferred);return {DomainStatus::Applied,std::monostate{}};}
        if(!batch.equipment || batch.items.size()!=1 || !batch.items.front().socketedItems.empty() ||
            batch.items.front().definition!=entry.request.code || !batch.equipment->items.contains(batch.items.front().id))
            return {DomainStatus::InvalidRequest,{}};
        entry.prepared=std::move(batch);return {DomainStatus::Applied,std::monostate{}};
    }
    return {DomainStatus::Stale,{}};
}
StepStatus System::populateRandomRoom(RegionId requestedRegion,size_t requestedRoom,TickContext tick) {
    for(const auto &[region,area]:ports_.areas.all()) {
        if(region!=requestedRegion) continue;
        if(area.definition.town) continue;
        const auto &definition=area.definition;
        auto &published=population_[region];
        if(published.generation!=area.generation) {
            published={};published.generation=area.generation;
        }
        const auto &rooms=definition.activation.rooms();
        const int populated=int(std::count_if(rooms.begin(),rooms.end(),[](const auto &room){return room.populate;}));
        for(size_t roomIndex=0;roomIndex<rooms.size();++roomIndex) {
            if(roomIndex!=requestedRoom) continue;
            if(roomIndex>=area.rooms.size() || !area.rooms[roomIndex].loaded || published.rooms.contains(roomIndex)) continue;
            const auto &bounds=rooms[roomIndex];
            if(!bounds.populate || std::any_of(definition.objects.begin(),definition.objects.end(),[&](const auto &object) {
                return object.rule.operation==23 && definition.activation.room(object.position)==&bounds;
            })) {published.rooms.insert(roomIndex);continue;}
            const RoomView room{region,(int(definition.origin.x)+bounds.x)/5,(int(definition.origin.y)+bounds.y)/5};
            auto loan=ports_.areas.roomRandom(room);if(!loan) return StepStatus::Blocked;
            const auto owner=std::tuple{region,area.generation,roomIndex,tick.tick};
            if(groundGenerationOwner_!=owner) {
                groundGeneration_.clear();groundGenerationOwner_=owner;
            }
            auto next=published;
            ++next.processed;
            auto gameRandom=ports_.random,controlRandom=state_.controlRandom.value();
            auto &roomRandom=loan.value->next;
            auto ids=ports_.ids;
            std::map<EntityId,Object> created;
            std::vector<std::shared_ptr<RoomUnitRegistration>> registrations;
            std::vector<std::shared_ptr<RoomUnitRegistration>> initializationOrder;
            std::vector<std::pair<items::PreparedBatch,GroundLocation>> groundItems;
            std::vector<EntityId> goldIds;
            monsters::NativeBirthBatch births;
            auto uniques=ports_.loot.read().uniques;
            bool capacity=false;
            const auto sample=ports_.spatial.nativeCollisionSample();
            const auto candidateSample=[&](RoomView view,int32_t column,int32_t row) {
                auto mask=sample(view,column,row);
                const auto &origin=ports_.areas.at(view.area).definition.origin;
                for(const auto &[id,object]:created) {
                    (void)id;const auto &entry=object.rule;
                    if(object.area!=view.area || !entry.collision[size_t(object.mode)]) continue;
                    const int left=int(object.position.x+origin.x)-entry.width/2,top=int(object.position.y+origin.y)-entry.height/2;
                    if(column>=left && column<left+entry.width && row>=top && row<top+entry.height) mask|=entry.collisionMask;
                }
                for(const auto &[id,actor]:births.actors) {
                    (void)id;if(actor.area!=view.area) continue;
                    mask|=nativeCollisionFootprint(actor.collisionPattern.value_or(actor.rule.collisionPattern),
                        column-int(actor.position.x+origin.x),row-int(actor.position.y+origin.y),0x100);
                }
                for(const auto &[prepared,location]:groundItems) {
                    (void)prepared;
                    if(location.region==view.area && int(location.position.x+origin.x)==column && int(location.position.y+origin.y)==row) mask|=0x200;
                }
                return mask;
            };
            const auto clear=[&](int x,int y,int width,int height,uint16_t mask) {
                const int left=x-width/2,top=y-height/2;
                for(int row=top;row<top+height;++row) for(int column=left;column<left+width;++column) {
                    const auto owner=ports_.areas.roomAtPosition(region,{float(bounds.x),float(bounds.y)},
                        column+int(definition.origin.x),row+int(definition.origin.y));
                    if(!owner || (candidateSample(*owner,column+int(definition.origin.x),row+int(definition.origin.y))&mask)) return false;
                }
                for(const auto &[id,object]:created) {
                    (void)id;const auto &rule=object.rule;
                    if(!rule.collision[size_t(object.mode)] || !(rule.collisionMask&mask)) continue;
                    const int otherLeft=int(object.position.x)-rule.width/2,otherTop=int(object.position.y)-rule.height/2;
                    if(left<otherLeft+rule.width && left+width>otherLeft && top<otherTop+rule.height && top+height>otherTop) return false;
                }
                if(mask&0x200) for(const auto &[item,location]:groundItems) {
                    (void)item;
                    if(location.region==region && location.position.x>=left && location.position.x<left+width &&
                        location.position.y>=top && location.position.y<top+height) return false;
                }
                return true;
            };
            const auto add=[&](int type,int x,int y)->Object * {
                if(state_.objects.size()+created.size()>=65536 || ids.cursor()>=UINT32_MAX) {capacity=true;return nullptr;}
                const auto rule=definition.objectRules.find(type);
                if(rule==definition.objectRules.end()) throw std::logic_error("Missing native generated object definition");
                const auto id=ids.allocate();
                Object object{id,type,region,{float(x),float(y)},1,0,rule->second};
                object.uses=2*object.rule.parameters[2];initialize(object,tick.tick,gameRandom,controlRandom);
                const auto initialized=prepareGoldPlaceholder(object,gameRandom,controlRandom,ids,groundItems,goldIds,candidateSample);
                if(!initialized) {
                    if(!isDeferred(initialized.status)) throw std::logic_error("Native gold placeholder initialization unavailable");
                    capacity=true;return nullptr;
                }
                const auto firstChild=ids.cursor();
                const auto souls=prepareSoulPlaceholder(object,tick.tick,gameRandom,controlRandom,ids,births,loan.value,candidateSample);
                if(!souls) {
                    if(!isDeferred(souls.status)) throw std::logic_error("Native soul placeholder initialization unavailable");
                    capacity=true;return nullptr;
                }
                finishInitialization(object,controlRandom);
                for(auto child=births.actors.lower_bound(EntityId{firstChild});child!=births.actors.end();++child)
                    initializationOrder.push_back(child->second.roomAdmission);
                registrations.push_back(ports_.areas.prepareRoomUnit(id,2,region,object.position));
                initializationOrder.push_back(registrations.back());
                return &created.emplace(id,std::move(object)).first->second;
            };
            const auto inside=[&](int x,int y) {
                return x+int(definition.origin.x)!=0 && y+int(definition.origin.y)!=0 &&
                    x>=bounds.x+1 && y>=bounds.y+1 && x<bounds.x+bounds.width-1 && y<bounds.y+bounds.height-1;
            };
            const auto createCommon=[&](int type)->Object * {
                const auto &rule=definition.objectRules.at(type);
                if(bounds.width<2 || bounds.height<2) return nullptr;
                for(int attempt=0;attempt<5;++attempt) {
                    const int x=int(uint16_t(int(definition.origin.x)+bounds.x+
                        int(limitedRandom(controlRandom,unsigned(bounds.width-rule.width-1)))))-int(definition.origin.x);
                    const int y=int(uint16_t(int(definition.origin.y)+bounds.y+
                        int(limitedRandom(controlRandom,unsigned(bounds.height-rule.height-1)))))-int(definition.origin.y);
                    if(inside(x,y) && clear(x,y,rule.width+6,rule.height+6,0x3f11)) return add(type,x,y);
                }
                return nullptr;
            };
            const auto healingRequired=[&] {return populated>0 && (next.processed<<7)/populated>96 && !next.healingShrines;};
            bool themed=false;
            if(definition.objectPopulation.themes) {
                int chance=12;
                if(next.processed>populated/2) chance+=5;
                if(next.processed>populated-populated/4) chance+=5;
                if(int(limitedRandom(controlRandom,100))<chance) {
                    std::vector<int> themes;
                    for(int bit=0;bit<7;++bit)
                        if(definition.objectPopulation.themes&(1u<<bit)) themes.push_back(bit+1);
                    const auto selected=limitedRandom(controlRandom,uint32_t(themes.size()));
                    if(selected<themes.size() && themes[selected]<int(themes.size())) {
                        const int theme=themes[selected];
                        if(theme==1) themed=true;
                        else if(theme==2) {
                            const auto &barrel=definition.objectRules.at(7);
                            int total=0;
                            for(int column=bounds.x;column<bounds.x+bounds.width+1 && total<32 && !capacity;column+=2)
                                for(int row=bounds.y;row<bounds.y+bounds.height+1 && total<32 && !capacity;row+=2) {
                                    const int type=limitedRandom(controlRandom,4)==0?11:7;
                                    if(bounds.width>=2 && bounds.height>=2 && column>bounds.x+2 && row>bounds.y+2 &&
                                        column<bounds.x+bounds.width-barrel.width && row<bounds.y+bounds.height-barrel.height &&
                                        clear(column,row,barrel.width+2,barrel.height+2,0x3f11)) {
                                        if(!add(type,column,row)) break;
                                        ++total;
                                    }
                                }
                            themed=true;
                        } else if(theme==5 || theme==6) {
                            const auto orientation=limitedRandom(controlRandom,2);
                            const int type=104+2*(theme==6)+int(orientation);
                            const auto &rack=definition.objectRules.at(type);
                            if(rack.width<=0 || rack.height<=0) throw std::logic_error("Invalid native rack dimensions");
                            const auto place=[&](int column,int row) {
                                if(bounds.width>=rack.width+2 && bounds.height>=rack.height+2 &&
                                    column>bounds.x+1 && row>bounds.y+1 &&
                                    column<bounds.x+bounds.width-rack.width-2 && row<bounds.y+bounds.height-rack.height-2 &&
                                    clear(column,row,rack.width+7,rack.height+7,0x0c01) &&
                                    clear(column,row,rack.width,rack.height,0x3f11)) add(type,column,row);
                            };
                            if(orientation) {
                                for(int row=bounds.y+rack.height;row<bounds.y+bounds.height-rack.height && !capacity;row+=2*rack.height)
                                    place(bounds.x+1,row);
                            } else {
                                for(int column=bounds.x+rack.width;column<bounds.x+bounds.width-rack.width && !capacity;column+=2*rack.width)
                                    place(column,bounds.y+1);
                            }
                            themed=true;
                        } else if(theme==4) {
                            const int level=std::min(std::max(definition.monsterLevel-1,1),64);
                            int attempted=0;
                            for(int column=bounds.x+1;column<bounds.x+std::min(bounds.width,12) && attempted<32;column+=2)
                                for(int row=bounds.y+1;row<bounds.y+std::min(bounds.height,12) && attempted<32;row+=2) {
                                    if(bounds.width<2 || bounds.height<2 || column<=bounds.x+2 || row<=bounds.y+2 ||
                                        column>=bounds.x+bounds.width-1 || row>=bounds.y+bounds.height-1 ||
                                        !clear(column,row,3,3,0x3f11)) continue;
                                    ++attempted;
                                    const int roll=int(limitedRandom(roomRandom,100));
                                    const int goldChance=65-level,armorChance=level/2+goldChance+5;
                                    std::string code="gld";
                                    if(roll>=goldChance) {
                                        const size_t family=roll<armorChance?0:roll<armorChance+level/2+10+(level%2)?1:2;
                                        std::vector<std::string> candidates;
                                        for(const auto &item:definition.objectPopulation.itemCandidates[family]) {
                                            if((family==2 && item.body) || !item.spawnable || item.quest || item.level>level) continue;
                                            if(limitedRandom(roomRandom,uint32_t(item.rarity-definition.objectPopulation.itemSelectionAct))==0 && candidates.size()<1023)
                                                candidates.push_back(item.code);
                                        }
                                        const auto selectedItem=limitedRandom(roomRandom,uint32_t(candidates.size()));
                                        code=candidates.empty()?definition.objectPopulation.firstItemCode:candidates[selectedItem];
                                    }
                                    const auto groundSample=[&](RoomView owner,int32_t atX,int32_t atY) {
                                        auto value=sample(owner,atX,atY);
                                        for(const auto &[item,location]:groundItems) {
                                            (void)item;const auto &origin=ports_.areas.at(location.region).definition.origin;
                                            if(location.region==owner.area && int(location.position.x+origin.x)==atX &&
                                                int(location.position.y+origin.y)==atY) value|=0x200;
                                        }
                                        return value;
                                    };
                                    const auto position=ports_.areas.nativeFreeSpace(room,column+int(definition.origin.x),
                                        row+int(definition.origin.y),1,groundSample);
                                    if(!position) continue;
                                    const ItemSeeds seeds{rollRandom(gameRandom),rollRandom(gameRandom)};
                                    const auto index=groundItems.size();
                                    if(index<groundGeneration_.size()) {
                                        const auto &previous=groundGeneration_[index].request;
                                        if(previous.code!=code || previous.level!=level || previous.difficulty!=ports_.settings.difficulty ||
                                            previous.seeds.unit!=seeds.unit || previous.seeds.properties!=seeds.properties || previous.uniques!=uniques)
                                            groundGeneration_.erase(groundGeneration_.begin()+index,groundGeneration_.end());
                                    }
                                    if(index==groundGeneration_.size()) {
                                        if(nextGroundRequest_==UINT64_MAX) return StepStatus::Blocked;
                                        groundGeneration_.push_back({{nextGroundRequest_++,code,level,ports_.settings.difficulty,seeds,uniques},{},{}});
                                        return StepStatus::Blocked;
                                    }
                                    const auto &entry=groundGeneration_[index];
                                    if(!entry.deferred.empty()) {published.deferredThemes.insert(theme);return StepStatus::NotImplemented;}
                                    if(!entry.prepared) return StepStatus::Blocked;
                                    groundItems.emplace_back(*entry.prepared,GroundLocation{position->room.area,position->position});
                                    uniques.insert(entry.prepared->limitedUniques.begin(),entry.prepared->limitedUniques.end());
                                }
                            themed=true;
                        }
                    }
                }
            }
            for(size_t slot=0;!themed && slot<definition.objectPopulation.groups.size() && !capacity;++slot) {
                const int group=definition.objectPopulation.groups[slot];
                auto probability=limitedRandom(roomRandom,100);
                const auto groupObject=definition.objectPopulation.objects.find(group);
                if(healingRequired() && groupObject!=definition.objectPopulation.objects.end() && groupObject->second.subClass) probability=100;
                if(!group || int(probability)>definition.objectPopulation.probabilities[slot]) continue;
                const auto entries=definition.objectPopulation.entries.find(group);
                if(entries==definition.objectPopulation.entries.end()) throw std::logic_error("Missing native object group");
                const int choice=int(limitedRandom(roomRandom,100));
                int accumulated=0;
                const ObjectGroupEntry *selected=nullptr;
                for(const auto &entry:entries->second) {
                    if(!entry.definition) break;
                    accumulated+=entry.probability;
                    if(choice<accumulated && definition.objectPopulation.objects.at(entry.definition).gore<=2) {selected=&entry;break;}
                }
                if(!selected) continue;
                const int type=selected->definition;
                const auto &generation=definition.objectPopulation.objects.at(type);
                const auto &rule=definition.objectRules.at(type);
                const int count=selected->density*((bounds.width*bounds.height)>>7)>>8;
                if(!generation.function) continue;
                if(generation.function==3 || generation.function==6 || generation.function==9) {
                    limitedRandom(generation.function==9?roomRandom:controlRandom,100);
                    Object *last=nullptr;
                    for(int index=0;index<count && !capacity;++index) {
                        last=createCommon(type);
                        if(last && generation.function==9) break;
                    }
                    if(last && generation.function==6 && limitedRandom(controlRandom,100)>70)
                        add(103,int(last->position.x),int(last->position.y));
                } else if(generation.function==1) {
                    limitedRandom(controlRandom,100);
                    std::vector<int> variants;
                    int attempts=12,baseStep=1,stepRange=0;
                    bool smallCluster=false;
                    if(type==3) {variants={3,28};attempts=18;baseStep=5;stepRange=5;}
                    else if(type==79 || type==1) {variants={79,53,1};baseStep=5;stepRange=5;}
                    else if(type==4) {variants={4,9,52,94,95};smallCluster=true;}
                    else if(type==89) {variants={89,284};baseStep=5;stepRange=5;}
                    else if(type==208 || type==209) {variants={208,209};smallCluster=true;}
                    else continue;
                    const auto eligible=[&](int column,int row,bool small) {
                        if(small) return bounds.width>=2 && bounds.height>=2 &&
                            column>bounds.x+2 && row>bounds.y+2 &&
                            column<bounds.x+bounds.width-rule.width && row<bounds.y+bounds.height-rule.height &&
                            clear(column,row,rule.width+2,rule.height+2,0x3f11);
                        return bounds.width>=rule.width+2 && bounds.height>=rule.height+2 &&
                            column>bounds.x+1 && row>bounds.y+1 &&
                            column<bounds.x+bounds.width-rule.width-2 && row<bounds.y+bounds.height-rule.height-2 &&
                            clear(column,row,rule.width+7,rule.height+7,0x0c01) &&
                            clear(column,row,rule.width,rule.height,0x3f11);
                    };
                    constexpr int horizontal[]{-1,0,1,-1,1,-1,0,1};
                    constexpr int vertical[]{-1,-1,-1,0,0,1,1,1};
                    int remaining=count;
                    while(remaining>0 && attempts-- >0 && !capacity) {
                        const int selectedType=variants[limitedRandom(controlRandom,uint32_t(variants.size()))];
                        int column=int(uint16_t(int(definition.origin.x)+bounds.x+
                            int(limitedRandom(controlRandom,unsigned(bounds.width-rule.width-1)))))-int(definition.origin.x);
                        int row=int(uint16_t(int(definition.origin.y)+bounds.y+
                            int(limitedRandom(controlRandom,unsigned(bounds.height-rule.height-1)))))-int(definition.origin.y);
                        if(!eligible(column,row,false)) continue;
                        if(!add(selectedType,column,row)) break;
                        int clusterSize=1;
                        bool placed=true;
                        while(limitedRandom(controlRandom,unsigned(clusterSize/2))==0 && placed && !capacity) {
                            placed=false;
                            for(int attempt=0;attempt<3*std::max(remaining,4) && !placed;++attempt) {
                                const auto direction=limitedRandom(controlRandom,8);
                                column=int(uint16_t(column+int(definition.origin.x)+
                                    2*(baseStep+int(limitedRandom(controlRandom,unsigned(stepRange))))*horizontal[direction]))-int(definition.origin.x);
                                row=int(uint16_t(row+int(definition.origin.y)+
                                    2*(baseStep+int(limitedRandom(controlRandom,unsigned(stepRange))))*vertical[direction]))-int(definition.origin.y);
                                placed=eligible(column,row,smallCluster);
                            }
                            if(placed) {
                                const int memberType=variants[limitedRandom(controlRandom,uint32_t(variants.size()))];
                                if(!add(memberType,column,row)) break;
                                ++clusterSize;
                            }
                        }
                        --remaining;
                    }
                } else if(generation.function==7) {
                    limitedRandom(controlRandom,100);
                    static constexpr std::pair<int,int> shapes[4][7]{
                        {{-4,0},{0,0},{4,0},{8,0},{0,4},{0,-4}},
                        {{-8,0},{-4,0},{0,0},{4,0},{8,0}},
                        {{0,-8},{0,-4},{0,0},{0,4},{0,8}},
                        {{-7,-5},{-5,-3},{-3,-1},{0,0},{3,-1},{5,-3},{7,-5}}
                    };
                    constexpr int lengths[]{6,5,5,7};
                    const auto shape=limitedRandom(controlRandom,4);
                    for(int attempt=0;attempt<8 && !capacity;++attempt) {
                        const int baseColumn=bounds.x+int(limitedRandom(controlRandom,unsigned(bounds.width-rule.width-1)));
                        const int baseRow=bounds.y+int(limitedRandom(controlRandom,unsigned(bounds.height-rule.height-1)));
                        const int checkedBaseColumn=int(uint16_t(baseColumn+int(definition.origin.x)))-int(definition.origin.x);
                        const int checkedBaseRow=int(uint16_t(baseRow+int(definition.origin.y)))-int(definition.origin.y);
                        if(bounds.width<7 || bounds.height<7 || checkedBaseColumn<=bounds.x+1 || checkedBaseRow<=bounds.y+1 ||
                            checkedBaseColumn>=bounds.x+bounds.width-7 || checkedBaseRow>=bounds.y+bounds.height-7 ||
                            !clear(baseColumn,baseRow,12,12,0x0c01) || !clear(baseColumn,baseRow,5,5,0x3f11)) continue;
                        for(int index=0;index<lengths[shape] && !capacity;++index) {
                            const auto [offsetX,offsetY]=shapes[shape][index];
                            const int column=baseColumn+offsetX,row=baseRow+offsetY;
                            const int checkedColumn=int(uint16_t(column+int(definition.origin.x)))-int(definition.origin.x);
                            const int checkedRow=int(uint16_t(row+int(definition.origin.y)))-int(definition.origin.y);
                            if(bounds.width<rule.width+2 || bounds.height<rule.height+2 ||
                                checkedColumn<=bounds.x+1 || checkedRow<=bounds.y+1 ||
                                checkedColumn>=bounds.x+bounds.width-rule.width-2 || checkedRow>=bounds.y+bounds.height-rule.height-2 ||
                                !clear(column,row,rule.width+7,rule.height+7,0x0c01) ||
                                !clear(column,row,rule.width,rule.height,0x3f11)) continue;
                            const int selectedType=57+int(limitedRandom(controlRandom,2));
                            if(add(selectedType,column,row) && limitedRandom(controlRandom,100)>70) add(103,column,row);
                        }
                        break;
                    }
                } else if(generation.function==5) {
                    limitedRandom(controlRandom,100);
                    constexpr int variants[]{4,9,52,94,95};
                    constexpr int horizontal[]{-1,0,1,-1,1,-1,0,1};
                    constexpr int vertical[]{-1,-1,-1,0,0,1,1,1};
                    const auto eligible=[&](int column,int row) {
                        const int checkedColumn=int(uint16_t(column+int(definition.origin.x)))-int(definition.origin.x);
                        const int checkedRow=int(uint16_t(row+int(definition.origin.y)))-int(definition.origin.y);
                        return bounds.width>=2 && bounds.height>=2 &&
                            checkedColumn>bounds.x+2 && checkedRow>bounds.y+2 &&
                            checkedColumn<bounds.x+bounds.width-rule.width && checkedRow<bounds.y+bounds.height-rule.height &&
                            clear(column,row,rule.width+2,rule.height+2,0x3f11);
                    };
                    int remaining=count,attempts=4*count,members=0;
                    for(int budget=2*count;remaining>0 && budget>0 && members<8 && !capacity;--budget) {
                        int selectedType=type==46?type:variants[limitedRandom(controlRandom,5)];
                        int column=bounds.x+int(limitedRandom(controlRandom,unsigned(bounds.width)));
                        int row=bounds.y+int(limitedRandom(controlRandom,unsigned(bounds.height)));
                        if(!eligible(column,row)) continue;
                        if(selectedType!=46) selectedType=variants[limitedRandom(controlRandom,5)];
                        if(!add(selectedType,column,row)) break;
                        int clusterSize=1;
                        bool placed=true;
                        while(limitedRandom(controlRandom,unsigned(clusterSize/2))==0 && placed && !capacity) {
                            placed=false;
                            for(int attempt=0;attempt<std::max(4,attempts) && !placed;++attempt) {
                                const auto direction=limitedRandom(controlRandom,8);
                                column+=generation.spacingX*horizontal[direction];
                                row+=generation.spacingY*vertical[direction];
                                placed=eligible(column,row);
                            }
                            if(placed) {
                                if(!add(selectedType,column,row)) break;
                                if(++members>=8) break;
                                ++clusterSize;
                            }
                        }
                        --remaining;attempts-=4;
                    }
                } else if(generation.function==4) {
                    limitedRandom(controlRandom,100);
                    const auto &barrel=definition.objectRules.at(7);
                    const auto &spacing=definition.objectPopulation.objects.at(7);
                    constexpr int horizontal[]{-1,0,1,-1,1,-1,0,1};
                    constexpr int vertical[]{-1,-1,-1,0,0,1,1,1};
                    const auto eligible=[&](int column,int row) {
                        const int checkedColumn=int(uint16_t(column+int(definition.origin.x)))-int(definition.origin.x);
                        const int checkedRow=int(uint16_t(row+int(definition.origin.y)))-int(definition.origin.y);
                        return bounds.width>=2 && bounds.height>=2 &&
                            checkedColumn>bounds.x+2 && checkedRow>bounds.y+2 &&
                            checkedColumn<bounds.x+bounds.width-barrel.width && checkedRow<bounds.y+bounds.height-barrel.height &&
                            clear(column,row,barrel.width+2,barrel.height+2,0x3f11);
                    };
                    int remaining=count,total=0;
                    for(int budget=2*count;remaining>0 && budget>0 && total<8 && !capacity;--budget) {
                        const int selectedType=limitedRandom(controlRandom,3)==0?11:7;
                        int column=bounds.x+int(limitedRandom(controlRandom,unsigned(bounds.width)));
                        int row=bounds.y+int(limitedRandom(controlRandom,unsigned(bounds.height)));
                        if(!eligible(column,row)) continue;
                        if(!add(selectedType,column,row)) break;
                        if(++total>=8) break;
                        int clusterSize=1;
                        bool placed=true;
                        while(limitedRandom(controlRandom,unsigned(clusterSize/2))==0 && placed && !capacity) {
                            placed=false;
                            for(int attempt=0;attempt<15 && !placed;++attempt) {
                                const auto direction=limitedRandom(controlRandom,8);
                                column+=spacing.spacingX*horizontal[direction];
                                row+=spacing.spacingY*vertical[direction];
                                placed=eligible(column,row);
                            }
                            if(placed) {
                                const int memberType=limitedRandom(controlRandom,4)==0?11:7;
                                if(!add(memberType,column,row)) break;
                                ++clusterSize;
                                if(++total>=8) break;
                            }
                        }
                        --remaining;
                    }
                } else if(generation.function==2 || generation.function==8) {
                    const bool well=generation.function==8;
                    const bool healing=!well && healingRequired();
                    auto &coordinates=well?next.wells:next.shrines;
                    const auto full=[&] {return coordinates.size()>=size_t(well?4:10) || int(coordinates.size())>populated/8;};
                    if(well && full()) continue;
                    if(!healing) limitedRandom(roomRandom,100);
                    if(!well && full()) continue;
                    const int width=well?2*rule.width+1:rule.width,height=well?2*rule.height+1:rule.height;
                    if(well?(bounds.width<2 || bounds.height<2 || bounds.width<=width || bounds.height<=height)
                        :(bounds.width<width+2 || bounds.height<height+2)) continue;
                    bool done=false;
                    for(int attempt=0;attempt<(well?5:healing?150:15) && !done && !capacity;++attempt) {
                        const int x=!well && generation.orientation==1?bounds.x+bounds.width/4+int(limitedRandom(roomRandom,unsigned(bounds.width/2)))
                            :!well && generation.orientation==2?bounds.x+1+int(limitedRandom(roomRandom,1))
                            :bounds.x+int(limitedRandom(roomRandom,unsigned(bounds.width-width-1)));
                        const int y=!well && generation.orientation==1?bounds.y+1+int(limitedRandom(roomRandom,1))
                            :!well && generation.orientation==2?bounds.y+bounds.height/4+int(limitedRandom(roomRandom,unsigned(bounds.height/2)))
                            :bounds.y+int(limitedRandom(roomRandom,unsigned(bounds.height-height-1)));
                        if(!inside(x,y) || !clear(x,y,width+6,height+6,0x3f11)) continue;
                        if(std::any_of(coordinates.begin(),coordinates.end(),[&](Vec previous) {
                            return std::abs(int(previous.x)-x)<(well?100:50) || std::abs(int(previous.y)-y)<(well?100:50);
                        })) continue;
                        auto *object=add(type,x,y);if(!object) break;
                        if(!well && object->rule.shrine && object->rule.shrine->code==2) ++next.healingShrines;
                        else if(healing) {
                            const int replacement=rule.parameters[1]>0 && rule.parameters[1]<573?rule.parameters[1]:84;
                            if(!definition.objectRules.contains(replacement)) throw std::logic_error("Missing native healing shrine replacement");
                            object->definition=replacement;
                            std::vector<size_t> candidates;
                            for(size_t index=0;index<definition.shrineGeneration.size();++index)
                                if(definition.shrineGeneration[index].effectClass==2) candidates.push_back(index);
                            if(candidates.empty()) throw std::logic_error("Missing native healing shrine class");
                            for(int retry=0;retry<8;++retry) {
                                auto index=candidates[limitedRandom(controlRandom,uint32_t(candidates.size()))];if(!index) index=1;
                                if(definition.shrineGeneration.at(index).minimumLevel<=1) break;
                            }
                            object->rule.shrine=definition.shrineRules.at(2);++next.healingShrines;
                        }
                        coordinates.push_back({float(x),float(y)});done=true;
                    }
                } else {
                    published.deferred.insert(type);
                    return StepStatus::NotImplemented;
                }
            }
            if(capacity) return StepStatus::Blocked;
            for(const auto &[id,object]:created) {
                (void)id;
                if(object.rule.initialization==79) {published.deferred.insert(object.definition);return StepStatus::NotImplemented;}
            }
            next.rooms.insert(roomIndex);
            if(!ports_.areas.validateRoomRandom(*loan.value)) return StepStatus::Blocked;
            const auto identities=groundItems.empty() || !goldIds.empty()?
                ports_.ids.preparePublication(size_t(ids.cursor()-ports_.ids.cursor())):nullptr;
            std::optional<monsters::ActorPublication> actors;
            if(!births.actors.empty()) actors=ports_.monsters.prepareActorPublication(std::move(births.actors),size_t(ids.cursor()-ports_.ids.cursor()),{},identities);
            if(actors && actors->uniquesNext) {
                for(const auto row:*actors->uniquesNext) if(!actors->uniquesExpected->contains(row) && !uniques.insert(row).second)
                    return StepStatus::NotImplemented;
                actors->uniquesNext=uniques;
            }
            if(!groundItems.empty()) {
                if(goldIds.empty() && !created.empty()) throw std::logic_error("Native item theme unexpectedly created objects");
                const auto installed=ports_.items.installNativeGroundBatch(groundItems,tick.tick,goldIds,identities);
                if(!installed) return isDeferred(installed.status) || installed.status==DomainStatus::Conflict
                    ?StepStatus::Blocked:StepStatus::NotImplemented;
                if(goldIds.empty()) ids=ports_.ids;
            }
            for(const auto &[id,object]:created) {(void)id;publishInitialization(object);}
            state_.objects.merge(created);
            for(auto &registration:initializationOrder) ports_.areas.publishRoomUnit(*registration);
            if(identities) ports_.ids.publish(*identities);
            ports_.random=gameRandom;state_.controlRandom=controlRandom;
            if(actors) {
                ports_.monsters.publishBirthComponents(births);ports_.monsters.publishActors(*actors);
            }
            if(!groundItems.empty() && (!actors || !actors->uniquesNext)) ports_.loot.commitUniques(std::move(uniques));
            ports_.areas.publishRoomRandom(*loan.value);published=std::move(next);
            groundGeneration_.clear();
            groundGenerationOwner_.reset();
            if(!registrations.empty()) collision(region);
        }
    }
    return StepStatus::Complete;
}
}