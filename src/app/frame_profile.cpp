#include "frame_profile.hpp"
#include <algorithm>
#include <filesystem>
#include <numeric>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#endif

namespace d2x {
nlohmann::json FrameProfile::processSample() {
    nlohmann::json sample=nlohmann::json::object();
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
    if(K32GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),sizeof(memory)))
        sample={{"privateBytes",memory.PrivateUsage},{"workingSetBytes",memory.WorkingSetSize},
            {"peakWorkingSetBytes",memory.PeakWorkingSetSize},{"pageFaults",memory.PageFaultCount}};
    MEMORYSTATUSEX system{};system.dwLength=sizeof(system);
    if(GlobalMemoryStatusEx(&system)) {
        sample["physicalAvailableBytes"]=system.ullAvailPhys;
        sample["commitAvailableBytes"]=system.ullAvailPageFile;
    }
    IO_COUNTERS io{};
    if(GetProcessIoCounters(GetCurrentProcess(),&io)) {
        sample["readBytes"]=io.ReadTransferCount;sample["writeBytes"]=io.WriteTransferCount;
    }
    FILETIME creation{},exit{},kernel{},user{};
    if(GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user)) {
        const auto milliseconds=[](FILETIME value) {return double((uint64_t(value.dwHighDateTime)<<32)|value.dwLowDateTime)/10000.;};
        sample["kernelCpuMs"]=milliseconds(kernel);sample["userCpuMs"]=milliseconds(user);
    }
    if(GetThreadTimes(GetCurrentThread(),&creation,&exit,&kernel,&user)) {
        const auto milliseconds=[](FILETIME value) {return double((uint64_t(value.dwHighDateTime)<<32)|value.dwLowDateTime)/10000.;};
        sample["mainThreadCpuMs"]=milliseconds(kernel)+milliseconds(user);
    }
#elif defined(__linux__)
    uint64_t total{},resident{};std::ifstream memory("/proc/self/statm");
    if(memory>>total>>resident) {
        const auto page=sysconf(_SC_PAGESIZE);
        if(page>0) {sample["virtualBytes"]=total*uint64_t(page);sample["workingSetBytes"]=resident*uint64_t(page);}
    }
    rusage usage{};
    if(getrusage(RUSAGE_SELF,&usage)==0) {
        sample["minorFaults"]=usage.ru_minflt;sample["majorFaults"]=usage.ru_majflt;
        sample["kernelCpuMs"]=double(usage.ru_stime.tv_sec)*1000.+double(usage.ru_stime.tv_usec)/1000.;
        sample["userCpuMs"]=double(usage.ru_utime.tv_sec)*1000.+double(usage.ru_utime.tv_usec)/1000.;
    }
    timespec threadTime{};
    if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&threadTime)==0)
        sample["mainThreadCpuMs"]=double(threadTime.tv_sec)*1000.+double(threadTime.tv_nsec)/1000000.;
#endif
    return sample;
}
FrameProfile::FrameProfile(const std::string &path,std::function<nlohmann::json()> context):path_(path),context_(std::move(context)) {
    if(path.empty()) return;
    const std::filesystem::path destination{std::u8string(path.begin(),path.end())};
    if(std::filesystem::exists(destination)) throw std::runtime_error("Frame profile output already exists: "+path);
    if(destination.has_parent_path()) std::filesystem::create_directories(destination.parent_path());
    output_.open(destination,std::ios::binary);
    if(!output_) throw std::runtime_error("Cannot open frame profile output: "+path);
    previousMemory_=processSample();sampled_=Clock::now();
}
void FrameProfile::record(uint64_t frame,std::span<const double> values,std::span<const char *const> names) noexcept {
    if(!output_.is_open() || !error_.empty()) return;
    try {
        const auto now=Clock::now();
        const double total=std::accumulate(values.begin(),values.end(),0.0);
        maximum_=std::max(maximum_,total);
        intervalSums_.resize(values.size());intervalMaxima_.resize(values.size());
        for(size_t index=0;index<values.size();++index) {
            intervalSums_[index]+=values[index];
            intervalMaxima_[index]=std::max(intervalMaxima_[index],values[index]);
        }
        ++intervalFrames_;intervalTotal_+=total;intervalMaximum_=std::max(intervalMaximum_,total);
        if(total>1000./60.) ++intervalOverBudget_;
        if(total>=100) ++intervalSlow_;
        if(total<100 && now-sampled_<std::chrono::seconds(1)) return;
        const auto measured=Clock::now();auto memory=processSample();
        const double gapMs=std::chrono::duration<double,std::milli>(now-sampled_).count();
        nlohmann::json event={{"frame",frame},{"kind",total>=100?"slow-frame":"sample"},
            {"elapsedMs",std::chrono::duration<double,std::milli>(now-started_).count()},
            {"epochMs",std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()},
            {"totalMs",total},{"milliseconds",nlohmann::json::object()},
            {"previousMemory",previousMemory_},{"memory",memory},
            {"memorySampleGapMs",std::chrono::duration<double,std::milli>(now-sampled_).count()}};
        for(size_t i=0;i<std::min(values.size(),names.size());++i) event["milliseconds"][names[i]]=values[i];
        auto &interval=event["interval"];
        interval={{"frames",intervalFrames_},{"wallMs",gapMs},{"meanFrameMs",intervalTotal_/double(intervalFrames_)},
            {"maxFrameMs",intervalMaximum_},{"framesOver16_67Ms",intervalOverBudget_},{"framesAtLeast100Ms",intervalSlow_},
            {"meanMilliseconds",nlohmann::json::object()},{"maxMilliseconds",nlohmann::json::object()}};
        for(size_t index=0;index<std::min(values.size(),names.size());++index) {
            interval["meanMilliseconds"][names[index]]=intervalSums_[index]/double(intervalFrames_);
            interval["maxMilliseconds"][names[index]]=intervalMaxima_[index];
        }
        if(gapMs>0) {
            if(memory.contains("kernelCpuMs") && memory.contains("userCpuMs") &&
               previousMemory_.contains("kernelCpuMs") && previousMemory_.contains("userCpuMs")) {
                const double cpuMs=memory["kernelCpuMs"].get<double>()+memory["userCpuMs"].get<double>()-
                    previousMemory_["kernelCpuMs"].get<double>()-previousMemory_["userCpuMs"].get<double>();
                interval["processCpuMs"]=cpuMs;interval["processCpuOneCorePercent"]=100.*cpuMs/gapMs;
            }
            if(memory.contains("mainThreadCpuMs") && previousMemory_.contains("mainThreadCpuMs")) {
                const double cpuMs=memory["mainThreadCpuMs"].get<double>()-previousMemory_["mainThreadCpuMs"].get<double>();
                interval["mainThreadCpuMs"]=cpuMs;interval["mainThreadCpuOneCorePercent"]=100.*cpuMs/gapMs;
            }
        }
        if(context_) event["context"]=context_();
        event["diagnosticCollectionMs"]=std::chrono::duration<double,std::milli>(Clock::now()-measured).count();
        auto line=event.dump();line+='\n';
        if(bytes_+line.size()>32u*1024*1024) {error_="32 MiB frame log limit reached";output_.flush();return;}
        output_.write(line.data(),std::streamsize(line.size()));
        if(total>=100) {++slow_;output_.flush();}
        if(!output_) {error_="Frame profile write failed";return;}
        bytes_+=line.size();++events_;sampled_=now;previousMemory_=std::move(memory);
        std::fill(intervalSums_.begin(),intervalSums_.end(),0.);
        std::fill(intervalMaxima_.begin(),intervalMaxima_.end(),0.);
        intervalFrames_=intervalOverBudget_=intervalSlow_=0;intervalTotal_=intervalMaximum_=0;
    } catch(const std::exception &e) {error_=e.what();}
    catch(...) {error_="Frame profile observation failed";}
}
nlohmann::json FrameProfile::summary() const {
    return {{"enabled",!path_.empty()},{"path",path_},{"events",events_},{"slowFrames",slow_},
        {"maximumFrameMs",maximum_},{"bytes",bytes_},{"error",error_}};
}
}
