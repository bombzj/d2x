#include "ai_impl.hpp"
#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>

namespace d2x {
namespace {
using Json=nlohmann::json;
int coordinate(const Json &value,int minimum,int maximum) {
    if(!value.is_number_integer()) throw std::invalid_argument("Navigation experience coordinates must be integers");
    const auto number=value.get<int64_t>();
    if(number<minimum || number>maximum) throw std::invalid_argument("Navigation experience coordinate is out of range");
    return int(number);
}
void fields(const Json &value,std::initializer_list<std::string_view> allowed) {
    if(!value.is_object()) throw std::invalid_argument("Navigation experience entries must be objects");
    for(const auto &[key,item]:value.items()) {
        (void)item;
        if(std::find(allowed.begin(),allowed.end(),key)==allowed.end())
            throw std::invalid_argument("Unknown navigation experience field: "+key);
    }
}
}
DebugAi::Impl::NavigationExperience DebugAi::Impl::parseExperience(const Json &value) const {
    fields(value,{"enabled","areas"});
    if(value.contains("enabled") && !value.at("enabled").is_boolean())
        throw std::invalid_argument("navigationExperience.enabled must be boolean");
    NavigationExperience result;
    const auto areas=value.value("areas",Json::array());
    if(!areas.is_array() || areas.size()>136) throw std::invalid_argument("Navigation experience requires at most 136 areas");
    for(const auto &entry:areas) {
        fields(entry,{"area","bounds","travelDirections","landmarks"});
        const auto area=definitions->area(entry.at("area"));
        if(result.contains(area)) throw std::invalid_argument("Duplicate navigation experience area");
        auto &profile=result[area];
        if(entry.contains("bounds")) {
            const auto &b=entry.at("bounds");
            if(!b.is_array() || b.size()!=4) throw std::invalid_argument("Experience bounds require [x,y,width,height] in subtiles");
            std::array<int,4> bounds{coordinate(b[0],0,65535),coordinate(b[1],0,65535),
                coordinate(b[2],1,65535),coordinate(b[3],1,65535)};
            if(bounds[0]+bounds[2]>65536 || bounds[1]+bounds[3]>65536)
                throw std::invalid_argument("Experience bounds exceed world coordinates");
            profile.bounds=bounds;
        }
        const auto directions=entry.value("travelDirections",Json::array());
        if(!directions.is_array() || directions.size()>16) throw std::invalid_argument("Too many experience travel directions");
        for(const auto &direction:directions) {
            fields(direction,{"to","direction"});
            const auto destination=definitions->area(direction.at("to"));const auto &d=direction.at("direction");
            if(!d.is_array() || d.size()!=2) throw std::invalid_argument("Experience direction requires [x,y]");
            const DebugExplorationMemory::Cell axis{coordinate(d[0],-1,1),coordinate(d[1],-1,1)};
            if((!axis.first && !axis.second) || !profile.travelDirections.emplace(destination,axis).second)
                throw std::invalid_argument("Experience travel direction must be nonzero and unique for its destination");
        }
        const auto landmarks=entry.value("landmarks",Json::array());
        if(!landmarks.is_array() || landmarks.size()>128) throw std::invalid_argument("Too many experience landmarks");
        std::set<std::tuple<uint8_t,uint16_t,std::optional<int>>> identities;
        for(const auto &landmark:landmarks) {
            fields(landmark,{"npc","object","connection","townVariant","positions"});
            if(unsigned(landmark.contains("npc"))+unsigned(landmark.contains("object"))+unsigned(landmark.contains("connection"))!=1)
                throw std::invalid_argument("Experience landmark requires one NPC, object or connection");
            ExperienceLandmark hint;
            if(landmark.contains("connection")) {hint.type=5;hint.identity=definitions->area(landmark.at("connection"));}
            else {const auto selector=definitions->selector(landmark);hint.type=selector.type;hint.identity=selector.identity;}
            if(landmark.contains("townVariant")) hint.townVariant=coordinate(landmark.at("townVariant"),0,5);
            if(hint.type==1 && !hint.townVariant) throw std::invalid_argument("NPC experience requires the visible town variant");
            if(!identities.emplace(hint.type,hint.identity,hint.townVariant).second)
                throw std::invalid_argument("Duplicate experience landmark; combine its positions");
            const auto &positions=landmark.at("positions");
            if(!positions.is_array() || positions.empty() || positions.size()>16)
                throw std::invalid_argument("Experience landmark requires 1..16 positions");
            for(const auto &position:positions) {
                if(!position.is_array() || position.size()!=2) throw std::invalid_argument("Experience position requires [x,y]");
                const OnlinePoint at{uint16_t(coordinate(position[0],0,65535)),uint16_t(coordinate(position[1],0,65535))};
                if(!hint.townVariant && profile.bounds) {
                    const auto &b=*profile.bounds;
                    if(at.x<b[0] || at.y<b[1] || at.x>=b[0]+b[2] || at.y>=b[1]+b[3])
                        throw std::invalid_argument("Experience landmark lies outside its configured bounds");
                }
                hint.positions.push_back(at);
            }
            profile.landmarks.push_back(std::move(hint));
        }
    }
    return value.value("enabled",true)?result:NavigationExperience{};
}
const DebugAi::Impl::AreaExperience *DebugAi::Impl::areaExperience(const Context &c) const {
    if(!area) return nullptr;
    const auto found=navigation.navigationExperience.find(*area);if(found==navigation.navigationExperience.end()) return nullptr;
    const auto &profile=found->second;
    if(profile.bounds) if(const auto *memory=c.walk.navigationSearch().explorationMemory.area(*area)) {
        const auto &b=*profile.bounds;
        // Acquired ground overrides experience. Never crop a differing map to
        // make it fit the configured shape, or infer the shape from hidden rooms.
        for(const auto &[tile,owner]:memory->terrainLevels) if(owner==*area &&
            (tile.first*5<b[0] || tile.second*5<b[1] || tile.first*5+5>b[0]+b[2] || tile.second*5+5>b[1]+b[3])) return nullptr;
    }
    return &profile;
}
std::optional<OnlinePoint> DebugAi::Impl::experienceHint(const Context &c,const Selector &selector) const {
    const auto *profile=areaExperience(c);if(!profile || selector.superUnique || selector.eliteOnly) return {};
    std::optional<OnlinePoint> result;float closest=std::numeric_limits<float>::max();
    for(const auto &hint:profile->landmarks) if(hint.type==selector.type && hint.identity==selector.identity) {
        const auto town=hint.townVariant?c.town.knownTownFrame(*area):std::nullopt;
        if(hint.townVariant && (!town || town->variant!=*hint.townVariant)) continue;
        for(const auto offset:hint.positions) {
            const int x=int(offset.x)+(town?town->origin.x:0),y=int(offset.y)+(town?town->origin.y:0);
            if(x>65535 || y>65535 || (town && (offset.x>=town->width || offset.y>=town->height))) continue;
            const OnlinePoint at{uint16_t(x),uint16_t(y)};
            if(navigation.checkedHints.contains({*area,selector.type,selector.identity,at.x,at.y})) continue;
            if(navigation.approachHint==at) return at; // Keep an already submitted relay.
            const float span=(Vec{float(x),float(y)}-c.position).length();
            if(span<closest) {closest=span;result=at;}
        }
    }
    return result;
}
std::optional<DebugExplorationMemory::Cell> DebugAi::Impl::experienceDirection(const Context &c,ExplorationFocus focus) const {
    if(focus!=ExplorationFocus::Connection || !navigation.legDestination) return {};
    const auto *profile=areaExperience(c);if(!profile) return {};
    const auto found=profile->travelDirections.find(*navigation.legDestination);
    if(found==profile->travelDirections.end()) return {};
    return found->second;
}
}
