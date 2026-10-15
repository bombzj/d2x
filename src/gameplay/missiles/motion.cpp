#include "motion.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace d2x {
namespace {
constexpr int64_t scale=65536;
int64_t precise(float value) {return int64_t(std::llround(double(value)*scale));}
float projection(int64_t value) {
    auto result=float(double(value)/scale);
    // Keep the native cell when a float cannot represent the last FP16 units
    // before its boundary. Consumers query cells from this public projection.
    if(std::floor(result)>double(value>>16)) result=std::nextafter(result,-std::numeric_limits<float>::infinity());
    return result;
}
bool equal(Vec a,Vec b) {return a.x==b.x && a.y==b.y;}
}
void NativeMissileMotion::reset() noexcept {initialized_=directionReady_=false;stepCount_=0;}
Vec NativeMissileMotion::translate(Vec position,Vec offset) {
    if(!initialized_ || !equal(position,published_)) {reset();return position+offset;}
    x_+=precise(offset.x);y_+=precise(offset.y);target_=target_+offset;
    for(size_t index=0;index<stepCount_;++index) steps_[index]=steps_[index]+offset;
    published_={projection(x_),projection(y_)};return published_;
}
NativeMissileMotionResult NativeMissileMotion::advance(Vec position,Vec target,int32_t velocity,Path path,uint64_t revision,const Collision &collision) {
    if(!initialized_ || !equal(position,published_)) {
        x_=precise(position.x);y_=precise(position.y);published_=position;initialized_=true;directionReady_=false;
    }
    stepCount_=0;NativeMissileMotionResult result{position};
    if(velocity<=0) {steps_[stepCount_++]={float(x_>>16)+.5f,float(y_>>16)+.5f};return result;}
    const auto targetX=int64_t(std::floor(target.x))*scale+scale/2;
    const auto targetY=int64_t(std::floor(target.y))*scale+scale/2;
    const auto dx=targetX-x_,dy=targetY-y_;
    if(!directionReady_ || revision!=revision_ || !equal(target,target_) || path==Path::Waypoint) {
        if(!dx && !dy) {result.reached=true;published_=position;return result;}
        direction_=nativeDirectionVector(dx,dy);target_=target;revision_=revision;directionReady_=true;
    }
    // PathMisc::sub_6FD5CEB0 at base velocity 1024. dwVelocity is in
    // 1/4096 cells per frame; multiplication precedes the signed shift.
    const auto magnitude=nativeMultiply(1024,velocity)>>6;
    int64_t vx=nativeMultiply(magnitude,direction_.x)>>12,vy=nativeMultiply(magnitude,direction_.y)>>12;
    if(path==Path::Waypoint) {
        const auto maximum=std::max(std::abs(vx),std::abs(vy));
        if(std::abs(dx)<=maximum && std::abs(dy)<=maximum) {vx=dx;vy=dy;result.reached=true;}
    }
    const auto nextX=x_+vx,nextY=y_+vy;
    auto column=int32_t(x_>>16),row=int32_t(y_>>16);
    const auto endColumn=int32_t(nextX>>16),endRow=int32_t(nextY>>16);
    auto sampleX=x_,sampleY=y_;
    while(std::abs(vx)>scale || std::abs(vy)>scale) {vx>>=1;vy>>=1;}
    while(column!=endColumn || row!=endRow) {
        sampleX+=vx;sampleY+=vy;
        const auto sampleColumn=int32_t(sampleX>>16),sampleRow=int32_t(sampleY>>16);
        if(sampleColumn==column && sampleRow==row) continue;
        if(collision(sampleColumn,sampleRow)&5) {
            x_=int64_t(column)*scale+scale/2;y_=int64_t(row)*scale+scale/2;
            result.blocked=true;result.reached=false;break;
        }
        column=sampleColumn;row=sampleRow;steps_[stepCount_++]={float(column)+.5f,float(row)+.5f};
        if(stepCount_==steps_.size()) break;
    }
    if(!result.blocked) {x_=nextX;y_=nextY;}
    published_={projection(x_),projection(y_)};result.position=published_;result.movedCell=stepCount_!=0;
    return result;
}
}
