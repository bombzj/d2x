#include "navigation_snapshot.hpp"
#include "navigation.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace d2x {
int NavigationSnapshot::blockCoordinate(int value) {
    return value>=0?value/NavigationSnapshot::blockSize:(value+1)/NavigationSnapshot::blockSize-1;
}
uint64_t NavigationSnapshot::address(Key key) {
    uint64_t result=0;
    for(unsigned bit=0;bit<32;++bit) {
        result|=uint64_t((uint32_t(key.first)>>bit)&1)<<(bit*2);
        result|=uint64_t((uint32_t(key.second)>>bit)&1)<<(bit*2+1);
    }
    return result;
}
std::shared_ptr<const NavigationSnapshot::Block> NavigationSnapshot::block(Key key) const {
    const auto code=address(key);const Directory *node=root_.get();
    for(unsigned depth=0;node && depth<64;++depth) node=node->children[(code>>(63-depth))&1].get();
    return node?node->value:nullptr;
}
std::shared_ptr<const NavigationSnapshot::Directory> NavigationSnapshot::replace(
        const std::shared_ptr<const Directory> &node,uint64_t code,const std::shared_ptr<const Block> &value,unsigned depth) {
    if(depth==64) {
        if(node && node->value==value) return node;
        if(!value) return {};
        auto result=std::make_shared<Directory>();result->value=value;return result;
    }
    const size_t side=(code>>(63-depth))&1;
    const auto previous=node?node->children[side]:nullptr;
    const auto next=replace(previous,code,value,depth+1);
    if(previous==next) return node;
    auto result=node?std::make_shared<Directory>(*node):std::make_shared<Directory>();
    result->children[side]=next;
    if(!result->children[0] && !result->children[1]) return {};
    return result;
}
uint16_t NavigationSnapshot::mask(int x,int y) const {
    if(x< -1 || y< -1 || x>width || y>height) return 0xffff;
    if(base) {
        const auto found=overlay.find({x,y});
        return uint16_t(base->mask(x,y)|(found==overlay.end()?0:found->second));
    }
    const int globalX=x+originX,globalY=y+originY;
    const int column=blockCoordinate(globalX),row=blockCoordinate(globalY);
    const auto found=block({column,row});
    if(!found) return 0xffff;
    return (*found)[size_t(globalY-row*blockSize)*blockSize+globalX-column*blockSize];
}
bool NavigationSnapshot::sameRegion(const NavigationSnapshot &other,int left,int top,int right,int bottom) const {
    if(world!=other.world || originX!=other.originX || originY!=other.originY || width!=other.width || height!=other.height)
        return false;
    if(base || other.base) {
        if(!base || !other.base || !base->sameRegion(*other.base,left,top,right,bottom)) return false;
        const auto equalOverlay=[&](const Overlay &first,const Overlay &second) {
            for(auto entry=first.lower_bound({left,std::numeric_limits<int>::min()});
                entry!=first.end() && entry->first.first<=right;++entry) {
                if(entry->first.second<top || entry->first.second>bottom) continue;
                const auto found=second.find(entry->first);
                if(found==second.end() || found->second!=entry->second) return false;
            }
            return true;
        };
        return equalOverlay(overlay,other.overlay) && equalOverlay(other.overlay,overlay);
    }
    if(root_==other.root_) return true;
    for(int row=blockCoordinate(top+originY);row<=blockCoordinate(bottom+originY);++row)
        for(int column=blockCoordinate(left+originX);column<=blockCoordinate(right+originX);++column)
            if(block({column,row})!=other.block({column,row})) return false;
    return true;
}
bool NavigationSnapshot::sameGlobalRegion(const NavigationSnapshot &other,int left,int top,int right,int bottom) const {
    if(world!=other.world) return false;
    const auto contains=[&](const NavigationSnapshot &snapshot) {
        return left>=snapshot.originX-1 && top>=snapshot.originY-1 &&
            right<=snapshot.originX+snapshot.width && bottom<=snapshot.originY+snapshot.height;
    };
    if(!contains(*this) || !contains(other)) return false;
    if(base || other.base) {
        if(!base || !other.base || !base->sameGlobalRegion(*other.base,left,top,right,bottom)) return false;
        const auto equalOverlay=[&](const NavigationSnapshot &first,const NavigationSnapshot &second) {
            for(auto entry=first.overlay.lower_bound({left-first.originX,std::numeric_limits<int>::min()});
                entry!=first.overlay.end() && entry->first.first<=right-first.originX;++entry) {
                const int worldY=entry->first.second+first.originY;
                if(worldY<top || worldY>bottom) continue;
                const auto found=second.overlay.find({entry->first.first+first.originX-second.originX,worldY-second.originY});
                if(found==second.overlay.end() || found->second!=entry->second) return false;
            }
            return true;
        };
        return equalOverlay(*this,other) && equalOverlay(other,*this);
    }
    if(root_==other.root_) return true;
    for(int row=blockCoordinate(top);row<=blockCoordinate(bottom);++row)
        for(int column=blockCoordinate(left);column<=blockCoordinate(right);++column)
            if(block({column,row})!=other.block({column,row})) return false;
    return true;
}
std::shared_ptr<const NavigationSnapshot> NavigationSnapshot::publish(const Grid &source,Vec origin,
    const std::shared_ptr<const NavigationSnapshot> &previous,bool native,const std::set<Key> *dirty,NavigationWorldKey world) {
    if(source.width<=0 || source.height<=0 || source.width>65536 || source.height>65536 ||
        !std::isfinite(origin.x) || !std::isfinite(origin.y) || std::abs(origin.x)>65536 || std::abs(origin.y)>65536 ||
        std::floor(origin.x)!=origin.x || std::floor(origin.y)!=origin.y || size_t(source.width)*source.height>16*1024*1024)
        throw std::runtime_error("Navigation snapshot extent exceeds budget");
    auto result=std::make_shared<NavigationSnapshot>();
    result->width=source.width;result->height=source.height;
    result->world=std::move(world);
    result->originX=int(origin.x);result->originY=int(origin.y);
    static std::atomic<uint64_t> next{};result->publication=++next;
    const bool sameWorld=previous && previous->world==result->world;
    const bool sameExtent=sameWorld && !previous->base && previous->width==source.width && previous->height==source.height &&
        previous->originX==result->originX && previous->originY==result->originY;
    if(sameWorld && !previous->base) {
        result->root_=previous->root_;
        if(!sameExtent) {
            const int firstColumn=blockCoordinate(result->originX-1),lastColumn=blockCoordinate(result->originX+source.width);
            const int firstRow=blockCoordinate(result->originY-1),lastRow=blockCoordinate(result->originY+source.height);
            for(int row=blockCoordinate(previous->originY-1);row<=blockCoordinate(previous->originY+previous->height);++row)
                for(int column=blockCoordinate(previous->originX-1);column<=blockCoordinate(previous->originX+previous->width);++column)
                    if(column<firstColumn || column>lastColumn || row<firstRow || row>lastRow)
                        result->root_=replace(result->root_,address({column,row}),{});
        }
    }
    const auto update=[&](int column,int row) {
            const Key key{column,row};
            Block values;values.fill(0xffff);bool known=false;
            for(int localY=0;localY<blockSize;++localY) for(int localX=0;localX<blockSize;++localX) {
                const int x=column*blockSize+localX-result->originX,y=row*blockSize+localY-result->originY;
                if(x< -1 || y< -1 || x>source.width || y>source.height) continue;
                const auto value=native?source.nativeMovementMask(x,y):source.movementMask(x,y);
                values[size_t(localY)*blockSize+localX]=value;known|=value!=0xffff;++result->scannedCells;
            }
            if(!known) {result->root_=replace(result->root_,address(key),{});return;}
            if(sameWorld && !previous->base) {
                const auto old=previous->block(key);
                if(old && *old==values) {
                    result->root_=replace(result->root_,address(key),old);++result->reusedBlocks;return;
                }
            }
            result->root_=replace(result->root_,address(key),std::make_shared<const Block>(std::move(values)));
            result->copiedBytes+=sizeof(Block);
    };
    if(dirty && sameExtent) {
        for(const auto &[column,row]:*dirty) update(column,row);
    } else {
        for(int row=blockCoordinate(result->originY-1);row<=blockCoordinate(result->originY+source.height);++row)
            for(int column=blockCoordinate(result->originX-1);column<=blockCoordinate(result->originX+source.width);++column)
                update(column,row);
    }
    if(sameExtent && previous->root_==result->root_) return previous;
    return result;
}
std::shared_ptr<const Grid> NavigationSnapshot::grid() const {
    auto owned=shared_from_this();
    auto result=std::make_shared<Grid>();result->width=width;result->height=height;
    result->obstacleRevision=publication;
    result->freezeMovement=[owned]{return owned;};
    const NavigationSnapshot *terrain=this;
    std::vector<const Overlay *> overlays;
    while(terrain->base) {overlays.push_back(&terrain->overlay);terrain=terrain->base.get();}
    const int firstX=blockCoordinate(originX-1),firstY=blockCoordinate(originY-1);
    const int columns=blockCoordinate(originX+width)-firstX+1,rows=blockCoordinate(originY+height)-firstY+1;
    std::vector<const Block *> table(size_t(columns)*rows);
    for(int row=0;row<rows;++row) for(int column=0;column<columns;++column)
        table[size_t(row)*columns+column]=terrain->block({firstX+column,firstY+row}).get();
    result->collisionSample=[owned,table=std::move(table),overlays=std::move(overlays),firstX,firstY,columns](int x,int y,EntityId)->uint16_t {
        if(x< -1 || y< -1 || x>owned->width || y>owned->height) return 0xffff;
        const int globalX=x+owned->originX,globalY=y+owned->originY;
        const int column=blockCoordinate(globalX),row=blockCoordinate(globalY);
        const auto *payload=table[size_t(row-firstY)*columns+column-firstX];
        if(!payload) return 0xffff;
        auto value=(*payload)[size_t(globalY-row*blockSize)*blockSize+globalX-column*blockSize];
        for(const auto *overlay:overlays) {
            const auto extra=overlay->find({x,y});
            if(extra!=overlay->end()) value|=extra->second;
        }
        return value;
    };
    return result;
}
}