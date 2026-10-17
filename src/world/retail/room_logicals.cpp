#include "tile_materialization.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace d2x {
void RetailTileMaterializer::initializeLogicals(size_t current) {
    auto &room=rooms_.at(current);
    room.logicalRegions.clear();room.logicalCells.clear();
    auto &counter=logicalCounters_[room.level];
    room.singleLogicalRegion=!room.room.preset || !catalog_.presets().at(room.room.preset).logicals;
    if(room.singleLogicalRegion) {
        counter=1;
        room.logicalRegions.push_back({1,room.room.x,room.room.y,
            room.room.x+room.room.width,room.room.y+room.room.height,false});
        return;
    }
    const int width=room.room.width+1,height=room.room.height+1;
    const size_t cells=size_t(width)*height;
    if(room.grids.width!=width || room.grids.height!=height || room.grids.floors.empty() ||
        room.grids.floors.front().size()!=cells || (!room.grids.walls.empty() && room.grids.walls.front().size()!=cells))
        throw std::runtime_error("Missing native logical room grids");
    std::vector<uint8_t> blocked(cells);
    const auto wall=[](const RetailMaterializedTile &tile) {
        return ((tile.flags&0x1c000)>>14)==2 && tile.type!=15 && !(tile.flags&0x800);
    };
    const auto mark=[&](const RetailMaterializedTile &tile) {
        const int column=tile.x-room.room.x,row=tile.y-room.room.y;
        if(column>=0 && row>=0 && column<width && row<height) blocked[size_t(row)*width+column]=1;
    };
    for(const auto index:room.walls) if(wall(tiles_.at(index))) mark(tiles_.at(index));
    for(const auto adjacent:room.near) {
        if(adjacent==current) continue;
        auto link=rooms_.at(adjacent).mapLinks[0];
        std::set<size_t> visited;
        while(link && visited.insert(*link).second) {
            const auto &tile=tiles_.at(*link);
            if(wall(tile)) mark(tile);
            link=tile.next;
        }
    }
    constexpr int directionsX[]{1,0,-1,0},directionsY[]{0,1,0,-1};
    constexpr int transitions[]{23,0,5,21,17,15,3,0,9,7,39,0,0,5,3,31,31,31,31,31,31,31,31,31,31,1};
    constexpr int types[]{-1,0,1,2,2,0,1,3,0,1,0,1,4,-1,4,0,0,0,0,0};
    struct Visit {int column,row,from;};
    std::vector<Visit> pending;
    std::vector<uint32_t> indices(cells);
    if(!counter) counter=1;
    const int first=counter;
    int next=counter;
    for(int row=0;row<height;++row) for(int column=0;column<width;++column) {
        const auto cell=size_t(row)*width+column;
        if(indices[cell]&0x10000000) continue;
        if(next>=0x0ffffffe) throw std::overflow_error("Native logical region index exhausted");
        uint32_t flags=uint32_t(++next)|0x10000000;
        const auto floor=room.grids.floors.front()[cell].value;
        // 1.13c RVA 81C1C checks this packed mask, including sequence zero.
        if((floor&0x01e0ff00)==0x01e00000 || (floor&0x80000000)) flags|=0x20000000;
        pending.push_back({column,row,-1});
        while(!pending.empty()) {
            const auto visit=pending.back();pending.pop_back();
            if(visit.column<0 || visit.row<0 || visit.column>=width || visit.row>=height) continue;
            const auto at=size_t(visit.row)*width+visit.column;
            if(indices[at]&0x10000000) continue;
            if(!blocked[at]) {
                indices[at]|=flags;
                for(int direction=3;direction>=0;--direction)
                    pending.push_back({visit.column+directionsX[direction],visit.row+directionsY[direction],direction});
                continue;
            }
            const int type=room.grids.walls.empty()?0:room.grids.walls.front()[at].orientation;
            if(type<0 || type>=int(std::size(types))) throw std::runtime_error("Unsupported native logical tile orientation");
            const int entry=visit.from+5*types[type]+1;
            // 1.13c indexes across the preceding type table for orientations
            // 0/13. Its final five words are zero (RVA 8A794..8A7A4).
            if(entry<-5 || entry>=int(std::size(transitions))) throw std::runtime_error("Invalid native logical tile transition");
            const int transition=entry<0?0:transitions[entry];
            if(transition&1) indices[at]|=flags;
            if(transition&32) pending.push_back({visit.column+1,visit.row+1,-1});
            for(int direction=3;direction>=0;--direction)
                if((transition&(2<<direction)) && visit.from!=(direction+2)%4)
                    pending.push_back({visit.column+directionsX[direction],visit.row+directionsY[direction],direction});
        }
    }
    counter+=next-first+1;
    const auto absent=std::numeric_limits<size_t>::max();
    room.logicalCells.assign(cells,absent);
    for(int row=0;row<height;++row) for(int column=0;column<width;++column) {
        const auto cell=size_t(row)*width+column;
        if(room.logicalCells[cell]!=absent) continue;
        const uint32_t index=indices[cell]&0x0fffffff;
        int right=column;
        while(right<width && (indices[size_t(row)*width+right]&0x0fffffff)==index &&
            room.logicalCells[size_t(row)*width+right]==absent) ++right;
        int bottom=row;
        for(;bottom<height;++bottom) {
            bool complete=true;
            for(int cursor=column;cursor<right;++cursor)
                if((indices[size_t(bottom)*width+cursor]&0x0fffffff)!=index ||
                    room.logicalCells[size_t(bottom)*width+cursor]!=absent) {complete=false;break;}
            if(!complete) break;
        }
        const auto position=room.logicalRegions.size();
        room.logicalRegions.push_back({int(index),room.room.x+column,room.room.y+row,
            room.room.x+std::min(right,room.room.width),room.room.y+std::min(bottom,room.room.height),
            (indices[cell]&0x20000000)!=0});
        if(column>=room.room.width || row>=room.room.height) {
            auto &region=room.logicalRegions.back();region.left=region.top=region.right=region.bottom=0;
        }
        for(int cursorRow=row;cursorRow<bottom;++cursorRow) for(int cursor=column;cursor<right;++cursor)
            room.logicalCells[size_t(cursorRow)*width+cursor]=position;
    }
    std::reverse(room.logicalRegions.begin(),room.logicalRegions.end());
    for(auto &index:room.logicalCells) index=room.logicalRegions.size()-index-1;
    const auto at=[&](RetailTileRoom &owner,int column,int row)->RetailTileRoom::LogicalRegion * {
        if(owner.logicalRegions.empty()) return nullptr;
        if(owner.singleLogicalRegion) return &owner.logicalRegions.front();
        column-=owner.room.x;row-=owner.room.y;
        if(column<0 || row<0 || column>owner.room.width || row>owner.room.height) return nullptr;
        return &owner.logicalRegions.at(owner.logicalCells.at(size_t(row)*(owner.room.width+1)+column));
    };
    const auto merge=[&](size_t adjacent,int column,int row) {
        auto &other=rooms_.at(adjacent);
        if(column<other.room.x || row<other.room.y || column>other.room.x+other.room.width || row>other.room.y+other.room.height) return;
        auto *source=at(room,column,row),*target=at(other,column,row);
        if(!source || !target || !source->index || !target->index || source->index==target->index ||
            source->node!=target->node || room.level!=other.level) return;
        const int previous=source->index,replacement=target->index;
        std::vector<size_t> queue{current};std::set<size_t> visited;
        while(!queue.empty()) {
            const auto index=queue.back();queue.pop_back();if(!visited.insert(index).second) continue;
            auto &owner=rooms_.at(index);if(owner.singleLogicalRegion || owner.level!=room.level) continue;
            bool changed=false;
            for(auto &region:owner.logicalRegions) if(region.index==previous) {region.index=replacement;changed=true;}
            if(changed) for(const auto neighbor:owner.near) if(neighbor!=index) queue.push_back(neighbor);
        }
    };
    for(const auto adjacent:room.near) {
        if(adjacent==current || rooms_.at(adjacent).logicalRegions.empty()) continue;
        for(int column=room.room.x;column<=room.room.x+room.room.width;++column) {
            merge(adjacent,column,room.room.y);merge(adjacent,column,room.room.y+room.room.height);
        }
        for(int row=room.room.y;row<=room.room.y+room.room.height;++row) {
            merge(adjacent,room.room.x,row);merge(adjacent,room.room.x+room.room.width,row);
        }
    }
}
}
