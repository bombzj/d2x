#pragma once
#include "core/math.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
namespace d2x {
// Retail 1.13c Gfx update 6FB77FB0 and D2Common #10132. These
// coordinates have 11 fractional bits; X/Y require the native projection.
// This state is exclusively cosmetic and never consumes a combat seed.
struct ClientMissileGraphics {
    std::array<int32_t,3> position{},velocity{},acceleration{};
    uint32_t flags{};
    int remaining{},bounces{},restitution{};
    static std::optional<ClientMissileGraphics> leap(int distance,int fixedVelocity) {
        const int speed=fixedVelocity>>8;if(speed<=0) return {};
        distance=std::max(1,distance);
        int frames=nativeDivide(nativeShiftLeft(distance,16),nativeShiftLeft(speed,12));
        if(frames>1) --frames;
        frames=std::max(1,frames);
        // Retail C65B0 / table D5F54, then C7C80's signed setup.
        const int gravity=std::max(500,distance<27?nativeSubtract(8462,nativeMultiply(270,distance)):
            nativeSubtract(1224,nativeMultiply(72,distance)));
        ClientMissileGraphics value;value.flags=2;value.remaining=frames;value.acceleration[2]=-gravity;
        value.velocity[2]=nativeDivide(nativeSubtract(0,nativeMultiply(nativeMultiply(frames,frames),value.acceleration[2])/2),frames);
        return value;
    }
    static ClientMissileGraphics arc(int frames,int height,int acceleration=-4096) {
        ClientMissileGraphics value;frames=std::max(frames,1);
        value.flags=2;value.remaining=frames;value.position[2]=nativeShiftLeft(height,11);
        value.acceleration[2]=acceleration?acceleration:-4096;
        value.velocity[2]=nativeDivide(nativeSubtract(nativeSubtract(0,value.position[2]),
            nativeMultiply(nativeMultiply(frames,frames),value.acceleration[2])/2),frames);
        return value;
    }
    bool step() {
        if(flags&1) return false;
        for(size_t axis=0;axis<position.size();++axis) {
            position[axis]=nativeAdd(position[axis],velocity[axis]);
            velocity[axis]=nativeAdd(velocity[axis],acceleration[axis]);
        }
        if(flags&2) {
            if(remaining) --remaining;
            else {flags|=1;position[0]=position[1]=0;}
        }
        bool bounced=false;
        if(flags&4) {
            if((position[2]>>11)<=0) {
                velocity[2]=nativeDivide(nativeMultiply(restitution,velocity[2]),-100);position[2]=0;
                if(bounces) {--bounces;bounced=true;}
                else flags|=1;
            }
        } else if((position[0]>>11)<=0 && (position[1]>>11)<=0 && (position[2]>>11)<=0) {
            position={0,0,0};flags|=1;
        }
        return bounced;
    }
    Vec offset() const {
        const auto x=position[0]>>11,y=position[1]>>11,z=position[2]>>11;
        return {float(nativeSubtract(x,y)>>1),float(nativeSubtract(nativeAdd(x,y)>>2,z))};
    }
};
}
