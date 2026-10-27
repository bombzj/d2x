#include "pvpgn_storage.hpp"
#include "network/tcp_stream.hpp"
#include "network/protocol/pvpgn.hpp"
#include "resources/atomic_file.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace d2x::hosting {
using namespace net::protocol;
namespace {
using Clock = std::chrono::steady_clock;
struct LoadRejected : std::runtime_error { using std::runtime_error::runtime_error; };
std::string folded(std::string value) {
    for(auto &letter:value) if(letter>='A' && letter<='Z') letter+='a'-'A';
    return value;
}
}
struct PvpgnStorage::Impl {
    net::Endpoint endpoint;
    std::filesystem::path root;
    net::TcpStream stream;
    PvpgnStream input;
    uint32_t sequence{};
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<RealmStorageRequest> requests;
    std::vector<RealmStorageResult> results;
    size_t queuedBytes{}, resultBytes{};
    bool connected{};
    std::string failure;
    std::jthread worker;
    Impl(net::Endpoint address,std::filesystem::path directory):endpoint(std::move(address)),root(std::move(directory)) {
        worker=std::jthread([this](std::stop_token stop) {
            try {
                stream.connect(endpoint,std::chrono::seconds(15));
                const auto deadline=Clock::now()+std::chrono::seconds(15);
                bool opened=false;
                while(!opened && !stop.stop_requested()) {
                    for(auto &event:stream.poll()) {
                        if(event.kind==net::StreamEventKind::Connected) {
                            if(!stream.send({0x65})) throw ProtocolError("Cannot initialize D2DBS");
                            opened=true;
                        } else if(event.kind==net::StreamEventKind::Data && opened) input.append(event.data);
                        else throw ProtocolError("D2DBS connection failed");
                    }
                    if(Clock::now()>=deadline) throw ProtocolError("D2DBS startup timed out");
                    if(!opened) wait(stop);
                }
                {std::lock_guard lock(mutex);connected=opened;}
                while(!stop.stop_requested()) {
                    std::optional<RealmStorageRequest> request;
                    {
                        std::lock_guard lock(mutex);
                        if(!requests.empty()) {
                            request=std::move(requests.front());requests.pop_front();
                            queuedBytes-=request->save.size()+request->portrait.size();
                        }
                    }
                    if(request) process(*request,stop);
                    else {
                        poll(); PvpgnPacket packet;
                        while(input.next(packet)) heartbeat(packet);
                        wait(stop);
                    }
                }
            } catch(const std::exception &error) {std::lock_guard lock(mutex);failure=error.what();}
            stream.close();
        });
    }
    ~Impl(){stop();}
    void stop(){if(worker.joinable()){worker.request_stop();wake.notify_all();worker.join();}}
    void wait(std::stop_token stop) {
        std::unique_lock lock(mutex);
        wake.wait_for(lock,std::chrono::milliseconds(5),[&]{return stop.stop_requested();});
    }
    uint32_t next() {
        if(sequence==UINT32_MAX) throw ProtocolError("DBS sequence exhausted");
        return ++sequence;
    }
    void send(uint16_t type,uint32_t serial,Writer body={}) {
        if(!stream.send(pvpgnFrame(type,serial,body.release()))) throw ProtocolError("DBS send queue unavailable");
    }
    void poll() {
        for(auto &event:stream.poll()) {
            if(event.kind!=net::StreamEventKind::Data) throw ProtocolError("D2DBS connection lost; transaction outcome unknown");
            input.append(event.data);
        }
    }
    void heartbeat(const PvpgnPacket &packet) {
        if(packet.type!=0x34 || !packet.body.empty()) throw ProtocolError("Unexpected D2DBS reply");
        send(0x34,packet.sequence);
    }
    PvpgnPacket exchange(uint16_t type,Writer body,std::stop_token stop) {
        const auto serial=next();send(type,serial,std::move(body));
        const auto deadline=Clock::now()+std::chrono::seconds(15);
        while(!stop.stop_requested() && Clock::now()<deadline) {
            poll();PvpgnPacket packet;
            while(input.next(packet)) {
                if(packet.type==0x34){heartbeat(packet);continue;}
                if(packet.type!=type || packet.sequence!=serial) throw ProtocolError("DBS response does not match transaction");
                return packet;
            }
            wait(stop);
        }
        throw ProtocolError("D2DBS transaction interrupted or timed out; outcome unknown");
    }
    void identity(Writer &body,const RealmStorageRequest &request) {
        body.string(request.account,15);body.string(request.name,15);body.string(request.realm,31);
    }
    Bytes load(const RealmStorageRequest &request,uint16_t type,bool &locked,std::stop_token stop) {
        Writer body;body.u16(type);identity(body,request);
        auto packet=exchange(0x31,std::move(body),stop);net::protocol::Reader in(packet.body);
        const auto status=in.u32();in.u32();in.u32();const auto received=in.u16(),size=in.u16();
        const auto name=in.string(15);
        if(!status && type==1) locked=true;
        if(received!=type || folded(name)!=folded(request.name)) throw ProtocolError("D2DBS character load identity mismatch");
        const auto bytes=in.take(size);in.finish();
        if(status) throw LoadRejected("D2DBS character load rejected");
        return {bytes.begin(),bytes.end()};
    }
    void save(const RealmStorageRequest &request,uint16_t type,const Bytes &bytes,std::stop_token stop) {
        if(bytes.size()>65000) throw ProtocolError("DBS character exceeds packet capacity");
        Writer body;body.u16(type);body.u16(uint16_t(bytes.size()));identity(body,request);body.append(bytes);
        auto packet=exchange(0x30,std::move(body),stop);net::protocol::Reader in(packet.body);
        const auto status=in.u32();const auto received=in.u16();const auto name=in.string(15);in.finish();
        if(status || received!=type || folded(name)!=folded(request.name)) throw ProtocolError("D2DBS character save rejected");
    }
    void process(const RealmStorageRequest &request,std::stop_token stop) {
        RealmStorageResult result;result.kind=request.kind;result.connection=request.connection;result.request=request.request;
        try {
            const auto path=root/(folded(request.account)+"-"+folded(request.name));
            if(request.kind==RealmStorageRequest::Kind::Load) {
                result.save=load(request,1,result.locked,stop);result.portrait=load(request,2,result.locked,stop);
                writeFileAtomically(path.string()+".d2s",result.save,true);
                writeFileAtomically(path.string()+".charinfo",result.portrait,true);
            } else if(request.kind==RealmStorageRequest::Kind::Save) {
                writeFileAtomically(path.string()+".d2s",request.save,true);
                writeFileAtomically(path.string()+".charinfo",request.portrait,true);
                save(request,1,request.save,stop);save(request,2,request.portrait,stop);
            } else {
                Writer body;body.u32(0);identity(body,request);send(0x33,next(),std::move(body));
                const auto deadline=Clock::now()+std::chrono::seconds(2);
                while(stream.pendingWrites() && !stop.stop_requested() && Clock::now()<deadline) {
                    poll();PvpgnPacket packet;while(input.next(packet)) heartbeat(packet);wait(stop);
                }
                if(stream.pendingWrites()) throw ProtocolError("DBS unlock output not drained");
            }
        } catch(const LoadRejected &error) {result.error=error.what();}
        catch(const std::exception &error) {result.error=error.what();result.fatal=true;}
        const auto error=result.fatal?result.error:std::string{};
        {
            std::lock_guard lock(mutex);
            const auto bytes=result.save.size()+result.portrait.size();
            if(results.size()>=256 || bytes>16*1024*1024-resultBytes) throw ProtocolError("Storage completion capacity exceeded");
            resultBytes+=bytes;results.push_back(std::move(result));
        }
        if(!error.empty()) throw ProtocolError(error);
    }
};
PvpgnStorage::PvpgnStorage(net::Endpoint endpoint,std::filesystem::path root):impl_(std::make_unique<Impl>(std::move(endpoint),std::move(root))){}
PvpgnStorage::~PvpgnStorage()=default;
void PvpgnStorage::submit(RealmStorageRequest request) {
    std::lock_guard lock(impl_->mutex);
    if(!impl_->failure.empty()) throw ProtocolError(impl_->failure);
    const auto bytes=request.save.size()+request.portrait.size();
    if(impl_->requests.size()>=256 || bytes>16*1024*1024-impl_->queuedBytes) throw ProtocolError("Storage request capacity exceeded");
    impl_->queuedBytes+=bytes;impl_->requests.push_back(std::move(request));impl_->wake.notify_one();
}
std::vector<RealmStorageResult> PvpgnStorage::take() {
    std::lock_guard lock(impl_->mutex);
    if(impl_->results.empty() && !impl_->failure.empty()) throw ProtocolError(impl_->failure);
    impl_->resultBytes=0;return std::exchange(impl_->results,{});
}
bool PvpgnStorage::ready() const {
    std::lock_guard lock(impl_->mutex);
    if(!impl_->failure.empty()) throw ProtocolError(impl_->failure);
    return impl_->connected;
}
void PvpgnStorage::stop(){impl_->stop();}
}