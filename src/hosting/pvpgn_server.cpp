#include "pvpgn_server.hpp"
#include "detail/pvpgn_game_thread.hpp"
#include "detail/pvpgn_storage.hpp"
#include "detail/native_realm_service.hpp"
#include "protocol/client_stream.hpp"
#include "network/tcp_stream.hpp"
#include "network/tcp_listener.hpp"
#include "network/protocol/pvpgn.hpp"
#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <random>
#include <thread>
#include <utility>

namespace d2x {
using namespace net::protocol;
using Clock = std::chrono::steady_clock;
namespace {
std::string folded(std::string value) {
    for (auto &letter : value) if (letter >= 'A' && letter <= 'Z') letter += 'a' - 'A';
    return value;
}
void identity(std::string_view name) {
    if (name.empty() || name.size() > 15 || std::any_of(name.begin(),name.end(),[](unsigned char letter) {
        return !((letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') ||
            (letter >= '0' && letter <= '9') || letter == '-' || letter == '_');
    })) throw ProtocolError("Unsupported backend identity");
}
}
struct PvpgnServer::Impl {
    struct Ticket {uint32_t game{},token{};std::string name,account;Clock::time_point expires;};
    struct Room {hosting::HostedGame definition;size_t thread{};bool expiring{},closed{};};
    struct Peer {
        uint64_t socket{};
        uint32_t game{},token{},level{},characterClass{};
        size_t thread{};
        std::string account,name;
        hosting::ClientPacketStream input;
        Bytes logon;
        std::deque<Bytes> pending;
        size_t pendingBytes{};
        bool loading{},assigned{},entered{},locked{},disconnected{},leaving{},confirmLeave{},unlocking{},draining{};
        Clock::time_point deadline=Clock::now()+std::chrono::seconds(30);
    };
    Archives &archives;
    PvpgnServerOptions options;
    std::function<void(std::string_view)> log;
    net::TcpStream cs;
    net::TcpListener listener;
    PvpgnStream csInput;
    std::vector<std::unique_ptr<hosting::PvpgnGameThread>> games;
    std::unique_ptr<hosting::PvpgnStorage> storage;
    std::map<uint32_t,Room> rooms;
    std::map<uint64_t,Peer> peers;
    std::map<std::string,Ticket> tickets;
    uint32_t sequence{},nextGame{1};
    bool authenticated{},started{},stopping{};
    std::string fatal;
    std::string controlFailure;
    Clock::time_point csDeadline;
    Impl(Archives &source,PvpgnServerOptions config,std::function<void(std::string_view)> logger)
        :archives(source),options(std::move(config)),log(std::move(logger)) {
        if(options.preparation!=ContentPreparation::OriginalRules && options.preparation!=ContentPreparation::Pd2TownHost)
            throw ProtocolError("Unsupported dedicated server content preparation");
    }
    uint32_t nextSequence() {
        if(sequence==UINT32_MAX) throw ProtocolError("Backend sequence exhausted; restart required");
        return ++sequence;
    }
    void send(uint16_t type,uint32_t serial,Writer body={}) {
        if (!controlFailure.empty()) return;
        if(!cs.send(pvpgnFrame(type,serial,body.release()))) throw ProtocolError("D2CS output queue unavailable");
    }
    void control(const PvpgnPacket &packet) {
        net::protocol::Reader in(packet.body);
        switch(packet.type) {
        case 0x10: {
            if(authenticated) throw ProtocolError("Unexpected D2CS authentication challenge");
            in.u32();const auto signature=in.u32();const auto realm=in.string(31);
            if(signature || in.remaining()) throw ProtocolError("Signed D2CS authentication is unsupported");
            if(options.realm.empty()) options.realm=realm;
            if(realm.empty() || realm!=options.realm) throw ProtocolError("D2CS realm mismatch");
            Writer reply;reply.u32(options.version);reply.u32(0);reply.u32(0);reply.u32(0);reply.append(Bytes(128,0));
            send(0x11,packet.sequence,std::move(reply));break;
        }
        case 0x11: {
            const auto result=in.u32();in.finish();
            if(result) throw ProtocolError("D2CS rejected registration");
            authenticated=true;Writer capacity;capacity.u32(options.maximumGames);capacity.u32(0);
            send(0x12,nextSequence(),std::move(capacity));log("D2CS registration accepted");break;
        }
        case 0x12: in.u32();in.u32();in.finish();break;
        case 0x13: in.finish();send(0x13,packet.sequence);break;
        case 0x14: {
            const auto command=in.u32();in.u32();in.finish();
            if(command!=1 && command!=2) throw ProtocolError("Unsupported D2CS control command");
            stopping=true;break;
        }
        case 0x15:
            in.u32();in.u32();if(in.u32()) throw ProtocolError("D2CS anti-cheat module is unsupported");
            in.string(255);in.string(255);in.finish();break;
        case 0x16: {
            const auto size=in.u32();in.u32();if(size>in.remaining()) throw ProtocolError("Truncated D2CS configuration");
            in.take(in.remaining());break;
        }
        case 0x20: {
            const auto ladder=in.u8(),expansion=in.u8(),difficulty=in.u8(),hardcore=in.u8();
            auto name=in.string(15),password=in.string(15),description=in.string(31);
            in.string(15);in.string(15);in.string(63);in.finish();
            uint32_t result=1,id=0;
            const bool duplicate=std::any_of(rooms.begin(),rooms.end(),[&](const auto &entry){return folded(entry.second.definition.name)==folded(name);});
            if(authenticated && !stopping && !ladder && expansion==1 && hardcore<=1 && difficulty<=2 &&
                !name.empty() && !duplicate && rooms.size()<options.maximumGames && nextGame<=UINT16_MAX) {
                std::vector<size_t> loads(games.size());
                for(const auto &[key,room]:rooms){(void)key;++loads[room.thread];}
                const auto thread=size_t(std::min_element(loads.begin(),loads.end())-loads.begin());
                hosting::HostedGame room;room.index=nextGame++;id=room.index;
                room.name=std::move(name);room.password=std::move(password);room.description=std::move(description);
                room.capacity=8;room.levelDifference=99;room.flags=4u|uint32_t(difficulty)<<12;
                room.settings={uint32_t(std::random_device{}()),int(difficulty),false,1,0,bool(ladder)};
                room.hardcore=hardcore!=0;room.settings.hardcore=room.hardcore;if(room.hardcore) room.flags|=0x800;
                games[thread]->reserve(room);rooms.emplace(id,Room{std::move(room),thread});result=0;
                log("D2CS game reserved: "+std::to_string(id)+" on game thread "+std::to_string(thread));
            }
            Writer reply;reply.u32(result);reply.u32(id);send(0x20,packet.sequence,std::move(reply));break;
        }
        case 0x21: {
            const auto game=in.u32(),token=in.u32();auto name=in.string(15),account=in.string(15);
            in.string(63);in.finish();identity(name);identity(account);
            const auto room=rooms.find(game);
            const auto reserved=std::count_if(tickets.begin(),tickets.end(),[&](const auto &entry){return entry.second.game==game;});
            const auto online=std::count_if(peers.begin(),peers.end(),[&](const auto &entry){return entry.second.game==game;});
            const bool duplicate=tickets.contains(folded(name)) || std::any_of(peers.begin(),peers.end(),[&](const auto &entry){return folded(entry.second.name)==folded(name);});
            const uint32_t result=!authenticated || stopping || room==rooms.end() || room->second.expiring || duplicate ? 1 : reserved+online>=8 ? 2 : 0;
            if(!result) tickets.emplace(folded(name),Ticket{game,token,name,account,Clock::now()+std::chrono::seconds(30)});
            Writer reply;reply.u32(result);reply.u32(game);send(0x21,packet.sequence,std::move(reply));break;
        }
        default: throw ProtocolError("Unsupported D2CS message");
        }
    }
    void serviceControl() {
        for(auto &event:cs.poll()) {
            if(event.kind==net::StreamEventKind::Connected) {
                if(!cs.send({0x64})) throw ProtocolError("Cannot initialize D2CS");
            } else if(event.kind==net::StreamEventKind::Data) {
                csDeadline=Clock::now()+std::chrono::seconds(120);csInput.append(event.data);
                PvpgnPacket packet;while(csInput.next(packet)) control(packet);
            } else throw ProtocolError("D2CS connection lost");
        }
        if(Clock::now()>=csDeadline) throw ProtocolError("D2CS timeout");
    }
    void pollControl() {
        if (!controlFailure.empty()) return;
        try { serviceControl(); }
        catch (const std::exception &error) {
            controlFailure=error.what();cs.close();
            if (!stopping) throw;
        }
    }
    hosting::RealmStorageRequest request(const Peer &peer,hosting::RealmStorageRequest::Kind kind) const {
        hosting::RealmStorageRequest value;value.kind=kind;value.connection=peer.socket;
        value.account=peer.account;value.name=peer.name;value.realm=options.realm;return value;
    }
    void update(const Peer &peer,uint32_t flag) {
        Writer out;out.u32(flag);out.u32(peer.game);out.u32(peer.level);out.u32(peer.characterClass);out.string(peer.name,15);
        send(0x22,nextSequence(),std::move(out));
    }
    void finishPeer(uint64_t connection) {
        const auto found=peers.find(connection);if(found==peers.end()) return;
        auto &peer=found->second;
        if(peer.entered) update(peer,2);
        if(!peer.disconnected) {
            if(peer.confirmLeave) {
                if(!listener.send(connection,{0xB0})) throw ProtocolError("Leave confirmation could not be queued");
                // The original client frees its receive buffer while processing
                // B0. An immediate FIN can wake its receive thread after that
                // release; let the client close once its thread has stopped.
                peer.draining=true;peer.deadline=Clock::now()+std::chrono::seconds(15);
                log("Character leave confirmed; awaiting client disconnect: "+peer.name);
                return;
            }
            listener.closeAfterWrites(connection);
        }
        peers.erase(found);
    }
    void unlock(Peer &peer) {
        if(peer.unlocking) return;
        if(!peer.locked){finishPeer(peer.socket);return;}
        storage->submit(request(peer,hosting::RealmStorageRequest::Kind::Unlock));peer.unlocking=true;
    }
    void disconnect(Peer &peer) {
        if(peer.leaving) return;
        peer.leaving=true;peer.deadline=Clock::now()+std::chrono::seconds(120);
        if(peer.assigned) games[peer.thread]->disconnect(peer.socket);
    }
    void reject(Peer &peer,uint32_t code,std::string_view reason) {
        log(reason);
        if(!peer.entered && !peer.disconnected)
            listener.send(peer.socket,hosting::encodeServerPacket(hosting::ServerMessage::AdmissionError,[&](auto &out){out.u32(code);}));
        disconnect(peer);
    }
    void beginLoad(Peer &peer,Bytes packet) {
        net::protocol::Reader in(packet);if(in.u8()!=0x68) throw ProtocolError("First game packet must be logon");
        const auto token=in.u32();const auto game=in.u16();const auto characterClass=in.u8();
        if(in.u32()!=13) throw hosting::AdmissionFailure(16,"Unsupported game client version");
        if(in.u32()!=0xED5DCC50 || in.u32()!=0x91A519B6) throw ProtocolError("Invalid game client signature");
        in.u8();const auto nameBytes=in.take(16);in.finish();
        const auto end=std::find(nameBytes.begin(),nameBytes.end(),uint8_t{});
        if(end==nameBytes.end()) throw ProtocolError("Unterminated character name");
        const std::string name(nameBytes.begin(),end);
        const auto ticket=tickets.find(folded(name));const auto room=rooms.find(game);
        if(ticket==tickets.end() || ticket->second.expires<=Clock::now() || ticket->second.game!=game ||
            ticket->second.token!=token || room==rooms.end() || room->second.expiring || stopping)
            throw ProtocolError("Unknown or expired admission ticket");
        peer.name=ticket->second.name;peer.account=ticket->second.account;peer.game=game;peer.token=token;
        peer.thread=room->second.thread;peer.characterClass=characterClass;peer.logon=std::move(packet);
        tickets.erase(ticket);storage->submit(request(peer,hosting::RealmStorageRequest::Kind::Load));
        peer.loading=true;peer.deadline=Clock::now()+std::chrono::seconds(120);
    }
    void pollGames() {
        for(auto &event:listener.poll()) {
            if(event.stream.kind==net::StreamEventKind::Connected) {
                if(!authenticated || stopping || peers.size()>=64){listener.close(event.connection);continue;}
                Peer peer;peer.socket=event.connection;peers.emplace(peer.socket,std::move(peer));
                if(!listener.send(event.connection,{0xAF,0})) peers.erase(event.connection);
                continue;
            }
            const auto found=peers.find(event.connection);if(found==peers.end()) continue;
            auto &peer=found->second;
            if(event.stream.kind!=net::StreamEventKind::Data) {
                if(peer.draining) {
                    log("Character connection closed: "+peer.name);
                    peers.erase(found);continue;
                }
                peer.disconnected=true;disconnect(peer);continue;
            }
            if(peer.leaving || stopping) continue;
            try {
                if(peer.entered) peer.deadline=Clock::now()+std::chrono::seconds(90);
                peer.input.append(event.stream.data);Bytes packet;
                while(peer.input.next(packet)) {
                    if(!peer.assigned && !peer.loading) beginLoad(peer,std::move(packet));
                    else if(peer.loading) {
                        if(peer.pending.size()>=128 || packet.size()>65536-peer.pendingBytes) throw ProtocolError("Admission input capacity exceeded");
                        peer.pendingBytes+=packet.size();peer.pending.push_back(std::move(packet));
                    } else {
                        const bool leave=packet.front()==uint8_t(hosting::ClientMessage::SaveAndLeave);
                        games[peer.thread]->packet(peer.socket,std::move(packet));
                        if(leave){peer.confirmLeave=true;peer.leaving=true;peer.deadline=Clock::now()+std::chrono::seconds(120);break;}
                    }
                }
            } catch(const std::exception &error) {
                const auto *admission=dynamic_cast<const hosting::AdmissionFailure *>(&error);
                reject(peer,admission?admission->code:6,error.what());
            }
        }
        for(auto current=peers.begin();current!=peers.end();) {
            auto &peer=current->second;++current;
            if(peer.draining) {
                if(Clock::now()>=peer.deadline) {
                    log("Character disconnect timed out: "+peer.name);
                    listener.closeAfterWrites(peer.socket);peers.erase(peer.socket);
                }
                continue;
            }
            if(Clock::now()>=peer.deadline) {
                if(peer.leaving || peer.loading) throw ProtocolError("Character operation exceeded deadline; preserve backend locks");
                reject(peer,6,"Game peer timed out");
            }
            if(peer.leaving && !peer.loading && !peer.assigned && !peer.unlocking) unlock(peer);
        }
    }
    void pollStorage() {
        for(auto &result:storage->take()) {
            const auto found=peers.find(result.connection);
            if(found==peers.end()) throw ProtocolError("Storage completion has no live connection owner");
            auto &peer=found->second;
            if(result.kind==hosting::RealmStorageRequest::Kind::Load) {
                peer.loading=false;peer.locked=result.locked;
                if(!result.error.empty()) {
                    if(result.fatal){fatal=result.error;throw ProtocolError(fatal);}
                    reject(peer,6,result.error);unlock(peer);continue;
                }
                if(peer.leaving || stopping){unlock(peer);continue;}
                hosting::RealmCharacter character;character.connection=peer.socket;character.game=peer.game;character.token=peer.token;
                character.characterClass=uint8_t(peer.characterClass);character.account=peer.account;character.name=peer.name;character.realm=options.realm;
                character.save=std::move(result.save);character.portrait=std::move(result.portrait);character.logon=std::move(peer.logon);
                games[peer.thread]->admit(std::move(character));peer.assigned=true;
                for(auto &packet:peer.pending) {
                    const bool leave=packet.front()==uint8_t(hosting::ClientMessage::SaveAndLeave);
                    games[peer.thread]->packet(peer.socket,std::move(packet));
                    if(leave){peer.confirmLeave=peer.leaving=true;break;}
                }
                peer.pending.clear();peer.pendingBytes=0;peer.deadline=Clock::now()+std::chrono::seconds(peer.leaving?120:30);
            } else if(result.kind==hosting::RealmStorageRequest::Kind::Save) {
                games[peer.thread]->saved(peer.socket,result.request,result.error);
                if(!result.error.empty()){fatal=result.error;throw ProtocolError(fatal);}
                if(peer.entered && !peer.leaving) update(peer,0);
                log("Character save acknowledged: "+peer.name);
            } else {
                if(!result.error.empty()){fatal=result.error;throw ProtocolError(fatal);}
                peer.locked=false;finishPeer(peer.socket);
            }
        }
    }
    void pollThreads() {
        for(size_t index=0;index<games.size();++index) for(auto &event:games[index]->take()) {
            if(event.kind==hosting::RealmGameOutput::Kind::RoomClosed) {
                const auto room=rooms.find(event.game);
                if(room==rooms.end() || room->second.thread!=index) throw ProtocolError("Room completion route mismatch");
                room->second.closed=room->second.expiring=true;
                std::erase_if(tickets,[&](const auto &entry){return entry.second.game==event.game;});continue;
            }
            const auto found=peers.find(event.connection);if(found==peers.end()) continue;
            auto &peer=found->second;if(peer.thread!=index) throw ProtocolError("Game connection route mismatch");
            switch(event.kind) {
            case hosting::RealmGameOutput::Kind::Packet:
                if(!peer.disconnected && !listener.send(peer.socket,std::move(event.bytes))) {
                    listener.close(peer.socket);peer.disconnected=true;disconnect(peer);
                }
                break;
            case hosting::RealmGameOutput::Kind::Save: {
                if(!peer.locked || peer.unlocking) throw ProtocolError("Save has no matching DBS lock");
                auto value=request(peer,hosting::RealmStorageRequest::Kind::Save);
                value.request=event.request;value.save=std::move(event.bytes);value.portrait=std::move(event.portrait);
                peer.level=event.level;peer.characterClass=event.characterClass;storage->submit(std::move(value));break;
            }
            case hosting::RealmGameOutput::Kind::Entered:
                peer.entered=true;peer.level=event.level;peer.characterClass=event.characterClass;
                if(!peer.leaving) peer.deadline=Clock::now()+std::chrono::seconds(90);
                update(peer,1);log("Character entered: "+peer.name);break;
            case hosting::RealmGameOutput::Kind::Retired: peer.assigned=false;peer.leaving=true;unlock(peer);break;
            case hosting::RealmGameOutput::Kind::Failure: log("Game peer failed: "+event.error);peer.leaving=true;peer.confirmLeave=false;break;
            case hosting::RealmGameOutput::Kind::RoomClosed: break;
            }
        }
    }
    void pump() {
        pollControl();pollStorage();pollGames();pollThreads();
        std::erase_if(tickets,[](const auto &entry){return entry.second.expires<=Clock::now();});
        for (auto current=rooms.begin();current!=rooms.end();) {
            const auto id=current->first;
            if (!current->second.closed || std::any_of(peers.begin(),peers.end(),[&](const auto &entry){return entry.second.game==id;})) {
                ++current;continue;
            }
            Writer out;out.u32(id);send(0x23,nextSequence(),std::move(out));current=rooms.erase(current);
        }
        for(auto &[id,room]:rooms) {
            if(room.expiring || Clock::now()-room.definition.created<std::chrono::seconds(60)) continue;
            if(std::any_of(peers.begin(),peers.end(),[&](const auto &entry){return entry.second.game==id;}) ||
                std::any_of(tickets.begin(),tickets.end(),[&](const auto &entry){return entry.second.game==id;})) continue;
            games[room.thread]->retireRoom(id);room.expiring=true;
        }
    }
    void recover() {
        for (auto &thread:games) thread->stop();
        if(storage) storage->stop();
        for (auto &thread:games) for (const auto &issue:thread->recover()) log("Recovery snapshot unavailable: "+issue);
        listener.shutdown();cs.close();started=false;
    }
};
PvpgnServer::PvpgnServer(Archives &archives,PvpgnServerOptions options,std::function<void(std::string_view)> log)
    :impl_(std::make_unique<Impl>(archives,std::move(options),std::move(log))){}
PvpgnServer::~PvpgnServer()=default;
void PvpgnServer::start() {
    auto &state=*impl_;
    if(state.started || state.options.maximumGames<1 || state.options.maximumGames>256 ||
        state.options.gameThreads<1 || state.options.gameThreads>32 || state.options.recovery.empty())
        throw std::invalid_argument("Invalid server thread/capacity configuration");
    std::filesystem::create_directories(state.options.recovery);
    for(uint32_t index=0;index<state.options.gameThreads;++index)
        state.games.push_back(std::make_unique<hosting::PvpgnGameThread>(state.archives,state.options.recovery,state.options.preparation));
    state.storage=std::make_unique<hosting::PvpgnStorage>(state.options.d2dbs,state.options.recovery);
    const auto databaseDeadline=Clock::now()+std::chrono::seconds(15);
    while(!state.storage->ready()) {
        if(Clock::now()>=databaseDeadline) throw ProtocolError("D2DBS startup timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    state.listener.listen(state.options.listen);
    state.csDeadline=Clock::now()+std::chrono::seconds(30);
    state.cs.connect(state.options.d2cs,std::chrono::seconds(15));
    state.started=true;
    while(!state.authenticated) {state.pollControl();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    state.log("Game listener ready; game threads="+std::to_string(state.options.gameThreads));
}
void PvpgnServer::pump(double) {
    try {impl_->pump();}
    catch (const std::exception &error) {impl_->fatal=error.what();throw;}
}
bool PvpgnServer::stopRequested() const {return impl_->stopping;}
void PvpgnServer::shutdown() {
    auto &state=*impl_;if(!state.started) return;
    if(!state.fatal.empty() && state.controlFailure.empty()) {
        state.recover();throw ProtocolError("Shutdown requires recovery: "+state.fatal);
    }
    try {
    state.stopping=true;Writer capacity;capacity.u32(0);capacity.u32(0);state.send(0x12,state.nextSequence(),std::move(capacity));
    for(auto &[id,peer]:state.peers){(void)id;peer.confirmLeave=peer.entered;state.disconnect(peer);}
    for(auto &thread:state.games) thread->drain();
    const auto deadline=Clock::now()+std::chrono::seconds(120);
    while(!state.peers.empty()) {
        state.pump();
        if(Clock::now()>=deadline) throw ProtocolError("Shutdown save deadline exceeded; retain recovery files and locks");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for(auto &thread:state.games) thread->stop();
    for(const auto &[id,room]:state.rooms){(void)room;Writer out;out.u32(id);state.send(0x23,state.nextSequence(),std::move(out));}
    state.rooms.clear();
    const auto drainDeadline=Clock::now()+std::chrono::seconds(2);
    while(Clock::now()<drainDeadline) {
        state.pollControl();state.listener.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if(state.cs.pendingWrites() || state.listener.pendingWrites()) throw ProtocolError("Shutdown output not drained");
    state.storage->stop();state.listener.shutdown();state.cs.close();state.started=false;
    state.log("Shutdown complete; saves acknowledged, unlock requests sent (no DBS unlock ACK)");
    } catch (...) {state.recover();throw;}
    if(!state.controlFailure.empty()) throw ProtocolError("Characters saved but D2CS notifications failed: "+state.controlFailure);
}
}
