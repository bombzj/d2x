#include "debug_pipe.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// Windows types must be defined before SDDL declarations (also with MinGW).
// clang-format off
#include <windows.h>
#include <sddl.h>
// clang-format on
#endif

namespace d2x {
namespace {
void eraseRequest(std::string &value) {
    volatile char *bytes = value.data();
    for (size_t i = 0; i < value.size(); ++i)
        bytes[i] = 0;
    value.clear();
}
} // namespace
struct DebugPipe::Impl {
#ifdef _WIN32
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE stopEvent = nullptr, ioEvent = nullptr;
    std::atomic<bool> stopping = false;
    std::atomic<bool> replyPending = false;
    std::thread worker;
    std::mutex mutex;
    std::condition_variable ready;
    uint64_t sequence{};
    std::optional<std::string> request, response;
    ~Impl() {
        stopping=true;
        if(stopEvent) SetEvent(stopEvent);
        ready.notify_all();
        if(pipe!=INVALID_HANDLE_VALUE) CancelIoEx(pipe,nullptr);
        if(worker.joinable()) worker.join();
        if(request) eraseRequest(*request);
        if(response) eraseRequest(*response);
        if(pipe!=INVALID_HANDLE_VALUE) CloseHandle(pipe);
        if(ioEvent) CloseHandle(ioEvent);
        if(stopEvent) CloseHandle(stopEvent);
    }
    bool finish(BOOL done,OVERLAPPED &operation,DWORD &bytes,bool connecting=false) {
        if(done) return true;
        const auto error=GetLastError();
        if(connecting && error==ERROR_PIPE_CONNECTED) return true;
        if(error!=ERROR_IO_PENDING) return false;
        HANDLE events[]{stopEvent,ioEvent};
        const auto result=WaitForMultipleObjects(2,events,FALSE,15000);
        if(result==WAIT_OBJECT_0+1) return GetOverlappedResult(pipe,&operation,&bytes,FALSE)!=FALSE;
        // The OVERLAPPED remains alive until cancellation has completed.
        CancelIoEx(pipe,&operation);GetOverlappedResult(pipe,&operation,&bytes,TRUE);
        return false;
    }
    bool transfer(bool reading,char *data,DWORD size,DWORD &bytes) {
        OVERLAPPED operation{};operation.hEvent=ioEvent;ResetEvent(ioEvent);
        return finish(reading?ReadFile(pipe,data,size,&bytes,&operation):WriteFile(pipe,data,size,&bytes,&operation),operation,bytes);
    }
    void disconnect() {
        {std::lock_guard lock(mutex);
            ++sequence;
            if(request) eraseRequest(*request);
            if(response) eraseRequest(*response);
            request.reset();response.reset();
            replyPending=false;
        }
        DisconnectNamedPipe(pipe);
    }
    void run() {
        while(!stopping) {
            OVERLAPPED connection{};connection.hEvent=ioEvent;ResetEvent(ioEvent);DWORD ignored=0;
            if(!finish(ConnectNamedPipe(pipe,&connection),connection,ignored,true)) {disconnect();continue;}
            std::string input,output;
            bool valid=true;
            for(;;) {
                std::array<char,4096> buffer{};DWORD bytes=0;
                if(!transfer(true,buffer.data(),DWORD(buffer.size()),bytes) || !bytes) {valid=false;break;}
                input.append(buffer.data(),bytes);
                volatile char *secret=buffer.data();for(size_t i=0;i<buffer.size();++i) secret[i]=0;
                if(input.size()>16384) {output="{\"ok\":false,\"error\":\"Request exceeds 16 KiB\"}\n";break;}
                if(const auto end=input.find('\n');end!=std::string::npos) {
                    input.resize(end);
                    std::unique_lock lock(mutex);request=std::move(input);
                    const auto ticket=sequence;
                    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
                    while(!stopping && sequence==ticket && !response) {
                        ready.wait_for(lock,std::chrono::milliseconds(10));
                        DWORD available=0;
                        if(!PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr) || std::chrono::steady_clock::now()>=deadline) break;
                    }
                    if(response && sequence==ticket) {output=std::move(*response);response.reset();}
                    else valid=false;
                    break;
                }
            }
            eraseRequest(input);
            if(valid) {
                size_t sent=0;
                while(sent<output.size() && !stopping) {
                    DWORD bytes=0;
                    if(!transfer(false,output.data()+sent,DWORD(std::min<size_t>(65536,output.size()-sent)),bytes) || !bytes) break;
                    sent+=bytes;
                }
                // DisconnectNamedPipe discards unread output. Retain the last
                // chunk until the client consumes the newline and disconnects.
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
                while(sent==output.size() && !stopping && std::chrono::steady_clock::now()<deadline) {
                    DWORD available=0;if(!PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr)) break;
                    if(WaitForSingleObject(stopEvent,10)!=WAIT_TIMEOUT) break;
                }
            }
            eraseRequest(output);disconnect();
        }
    }
    void start() {
        stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);ioEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!stopEvent || !ioEvent) throw std::runtime_error("Cannot create debug pipe IO events");
        worker=std::thread([this]{run();});
    }
#endif
};
DebugPipe::DebugPipe(const std::string &name) {
    if (name.empty())
        return;
    if (name.size() > 80 || !std::all_of(name.begin(), name.end(), [](unsigned char character) {
            return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9') || character == '-' || character == '_';
        }))
        throw std::runtime_error("Debug pipe name must be 1-80 ASCII letters, digits, '-' or '_'");
#ifdef _WIN32
    impl_ = std::make_unique<Impl>();
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        throw std::runtime_error("Cannot read debug pipe owner");
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<unsigned char> buffer(bytes);
    bool obtained = GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes) != FALSE;
    CloseHandle(token);
    LPSTR sid = nullptr;
    if (!obtained || !ConvertSidToStringSidA(reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &sid))
        throw std::runtime_error("Cannot resolve debug pipe owner SID");
    std::string sddl = "D:P(A;;GA;;;" + std::string(sid) + ")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(sddl.c_str(), SDDL_REVISION_1, &descriptor,
                                                              nullptr))
        throw std::runtime_error("Cannot create debug pipe ACL");
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    std::string path = "\\\\.\\pipe\\" + name;
    impl_->pipe =
        CreateNamedPipeA(path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
                         PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
                         65536, 65536, 0, &security);
    LocalFree(descriptor);
    if (impl_->pipe == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot create debug pipe (name may already be in use)");
    impl_->start();
#else
    throw std::runtime_error("--debug-pipe is available only on Windows");
#endif
}
DebugPipe::~DebugPipe() = default;
void DebugPipe::poll(const std::function<std::string(const std::string &)> &handler) {
    if (!impl_)
        return;
#ifdef _WIN32
    auto &state=*impl_;
    std::string request;
    uint64_t sequence;
    {
        std::lock_guard lock(state.mutex);
        if(!state.request || state.stopping) return;
        sequence=state.sequence;request=std::move(*state.request);state.request.reset();
    }
    std::string response;
    try {response=handler(request)+"\n";}
    catch(...) {eraseRequest(request);throw;}
    eraseRequest(request);
    if(response.size()>4*1024*1024)
        response="{\"ok\":false,\"error\":\"Response exceeds 4 MiB\"}\n";
    {
        std::lock_guard lock(state.mutex);
        if(state.sequence!=sequence || state.stopping) {eraseRequest(response);return;}
        state.response=std::move(response);
        state.replyPending=true;
    }
    state.ready.notify_one();
#else
    (void)handler;
#endif
}
bool DebugPipe::responsePending() const {
#ifdef _WIN32
    return impl_ && impl_->replyPending;
#else
    return false;
#endif
}
} // namespace d2x
