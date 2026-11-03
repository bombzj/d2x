#include "ai_behavior_log.hpp"
#include <ostream>

namespace d2x {
namespace {
void field(std::ostream &out,std::string_view text) {
    for(char ch:text) {
        switch(ch) {
        case '\\':out<<"\\\\";break;
        case '\t':out<<"\\t";break;
        case '\r':out<<"\\r";break;
        case '\n':out<<"\\n";break;
        default:out.put(ch);
        }
    }
}
void point(std::ostream &out,std::optional<OnlinePoint> at) {
    if(at) out<<at->x<<','<<at->y;else out<<'-';
}
}
void AiBehaviorLog::open(const std::filesystem::path &directory,uint64_t game) {
    close();output_.clear();path.clear();error.clear();
    start_=Clock::now();
    const auto epoch=std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    try {
        const auto root=std::filesystem::absolute(directory).lexically_normal();
        std::filesystem::create_directories(root);
        // Atomic directory creation also separates simultaneous client processes.
        for(unsigned suffix=0;;++suffix) {
            auto dir=root/("ai-"+std::to_string(epoch)+(suffix?"-"+std::to_string(suffix):""));
            if(!std::filesystem::create_directory(dir)) continue;
            const auto file=dir/"events.log";
            const auto utf8=file.generic_u8string();path.assign(utf8.begin(),utf8.end());
            output_.open(file,std::ios::binary);
            if(!output_) {error="Could not open AI behavior log";return;}
            output_<<"# d2x-ai-v1 start_unix_ms="<<epoch<<" game="<<game<<"\n"
                <<"ms\tseq\tgoal\tarea\tlocal\thost\tevent\tdetail\n";
            break;
        }
    } catch(const std::filesystem::filesystem_error &e) {
        error="Could not create AI behavior log ("+std::to_string(e.code().value())+")";
    }
}
void AiBehaviorLog::observe(std::optional<uint16_t> area,std::optional<OnlinePoint> local,std::optional<OnlinePoint> host) {
    area_=area;local_=local;host_=host;
}
void AiBehaviorLog::event(size_t goal,std::string_view name,std::string_view detail,uint64_t sequence) {
    if(!output_.is_open()) return;
    output_<<std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start_).count()<<'\t';
    if(sequence) output_<<sequence;else output_<<'-';
    output_<<'\t'<<goal<<'\t';
    if(area_) output_<<*area_;else output_<<'-';
    output_<<'\t';point(output_,local_);output_<<'\t';point(output_,host_);output_<<'\t';
    field(output_,name);output_<<'\t';field(output_,detail);output_<<'\n';
    output_.flush();
    if(!output_) {error="Could not write AI behavior log";output_.close();}
}
void AiBehaviorLog::phase(size_t goal,std::string_view name,std::string_view why) {
    if(!output_.is_open() || (goal==phaseGoal_ && name==phase_ && why==why_)) return;
    endPhase();
    if(name.empty()) return;
    phaseGoal_=goal;phase_=name;why_=why;phaseStart_=Clock::now();
    event(goal,"phase+",phase_+" "+why_);
}
void AiBehaviorLog::endPhase() {
    if(phase_.empty()) return;
    const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-phaseStart_).count();
    event(phaseGoal_,"phase-",phase_+" "+std::to_string(ms)+"ms");phase_.clear();why_.clear();
}
void AiBehaviorLog::close() {
    endPhase();
    if(output_.is_open()) {
        output_.close();if(!output_) error="Could not close AI behavior log";
    }
}
}
