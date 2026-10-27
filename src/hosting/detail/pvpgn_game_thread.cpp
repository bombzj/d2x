#include "pvpgn_game_thread.hpp"
#include "native_realm_service.hpp"
#include "hosting/character_creation.hpp"
#include "persistence/save_codec.hpp"
#include "persistence/d2s_header.hpp"
#include "resources/archive.hpp"
#include "resources/atomic_file.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <limits>
#include <mutex>
#include <map>
#include <optional>
#include <random>
#include <stdexcept>
#include <thread>
#include <utility>

namespace d2x::hosting {
using namespace net::protocol;
namespace {
using Clock = std::chrono::steady_clock;
std::string folded(std::string value) {
    for (auto &letter : value) if (letter >= 'A' && letter <= 'Z') letter += 'a' - 'A';
    return value;
}
std::string field(const Bytes &bytes, size_t offset, size_t size) {
    if (offset > bytes.size() || size > bytes.size() - offset) throw ProtocolError("Truncated character identity");
    const auto first = bytes.begin() + offset, last = first + size;
    const auto end = std::find(first, last, uint8_t{});
    if (end == last) throw ProtocolError("Unterminated character identity");
    return {first, end};
}
void store32(Bytes &bytes, size_t offset, uint32_t value) {
    if (offset > bytes.size() || bytes.size() - offset < 4) throw ProtocolError("Truncated charinfo");
    for (unsigned index = 0; index < 4; ++index) bytes[offset + index] = uint8_t(value >> (8 * index));
}
Bytes saveIdentity(Bytes bytes) {
    store32(bytes,0x0C,0);
    store32(bytes,0x30,0);
    return bytes;
}
}
struct PvpgnGameThread::Impl {
    struct Peer {
        RealmCharacter identity;
        std::unique_ptr<NativeRealmService> service;
        bool entered{}, leaving{}, frozen{}, savePending{};
        uint64_t saveRequest{};
        uint32_t created{};
        Bytes pendingSave, confirmedSave;
        std::optional<PersistentCharacter> finalCharacter;
        std::string saveError;
        Clock::time_point checkpoint = Clock::now() + std::chrono::seconds(60);
    };
    std::unique_ptr<Archives> archives;
    NativeRealmHost host;
    std::map<uint64_t, std::unique_ptr<Peer>> peers;
    std::map<uint32_t, std::string> rooms;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::function<void()>> input;
    size_t inputBytes{};
    std::vector<RealmGameOutput> output;
    size_t outputBytes{};
    std::string fatal;
    bool draining{};
    std::jthread worker;
    Impl(const Archives &source, std::filesystem::path root, ContentPreparation preparation)
        : archives(source.independent()), host(*archives, std::move(root), preparation) {
        host.initialize();
        worker = std::jthread([this](std::stop_token stop) {
            auto previous = Clock::now();
            try {
                while (!stop.stop_requested()) {
                    std::deque<std::function<void()>> pending;
                    {
                        std::lock_guard lock(mutex);
                        pending.swap(input); inputBytes = 0;
                    }
                    for (auto &operation : pending) operation();
                    const auto now = Clock::now();
                    host.advance(std::chrono::duration<double>(now - previous).count());
                    previous = now;
                    finish();
                    std::unique_lock lock(mutex);
                    wake.wait_for(lock, std::chrono::milliseconds(5), [&] { return stop.stop_requested() || !input.empty(); });
                }
            } catch (const std::exception &error) {
                std::lock_guard lock(mutex); fatal = error.what();
            }
        });
    }
    ~Impl() { stop(); }
    void stop() {
        if (worker.joinable()) { worker.request_stop(); wake.notify_all(); worker.join(); }
    }
    void post(std::function<void()> operation, size_t bytes = 0) {
        {
            std::lock_guard lock(mutex);
            if (!fatal.empty()) throw ProtocolError(fatal);
            if (input.size() >= 4096 || bytes > 8 * 1024 * 1024 - inputBytes)
                throw ProtocolError("Game thread input capacity exceeded");
            input.emplace_back(std::move(operation)); inputBytes += bytes;
        }
        wake.notify_one();
    }
    void emit(RealmGameOutput event) {
        std::lock_guard lock(mutex);
        const auto bytes = event.bytes.size() + event.portrait.size();
        if (output.size() >= 8192 || bytes > 16 * 1024 * 1024 - outputBytes)
            throw ProtocolError("Game thread output capacity exceeded");
        outputBytes += bytes; output.push_back(std::move(event));
    }
    void packetOut(uint64_t connection, Bytes bytes) {
        RealmGameOutput event; event.kind = RealmGameOutput::Kind::Packet;
        event.connection = connection; event.bytes = std::move(bytes); emit(std::move(event));
    }
    PersistentCharacter decode(const RealmCharacter &identity) {
        if (identity.portrait.size() != 192) throw ProtocolError("Unsupported D2DBS charinfo length");
        net::protocol::Reader info(identity.portrait);
        if (info.u32() != 0x12345678 || info.u32() != 0x10000 ||
            folded(field(identity.portrait,48,16)) != folded(identity.name) ||
            folded(field(identity.portrait,64,16)) != folded(identity.account) ||
            folded(field(identity.portrait,80,32)) != folded(identity.realm))
            throw ProtocolError("D2DBS charinfo identity mismatch");
        net::protocol::Reader registration(identity.save);
        PersistentCharacter character;
        Bytes bytes = identity.save;
        if (bytes.size() == 130 && registration.u32() == 0xAA55AA55 && registration.u32() == 89) {
            const auto name = field(bytes,8,16);
            registration.take(16); const auto status = registration.u32(); registration.take(4);
            const auto marker = registration.u16(), characterClass = registration.u16();
            net::protocol::Reader summary(std::span<const uint8_t>(identity.portrait).subspan(176));
            const auto experience = summary.u32(), mode = summary.u32(), level = summary.u32(), summaryClass = summary.u32();
            if (folded(name) != folded(identity.name) || (status != 0x21 && status != 0x25) || marker != 0x82 ||
                characterClass != identity.characterClass || characterClass > 6 || experience || level != 1 ||
                (mode & ~1u) != (status & ~1u) || summaryClass != characterClass)
                throw ProtocolError("Invalid PvPGN new-character registration");
            character = createCharacter(*host.content,name,characterClass,uint32_t(std::random_device{}()),(status & 4) != 0);
            bytes = encodeSave(character,*host.content);
        } else {
            try { character = decodeSave(bytes,*host.content); }
            catch (const std::exception &) { throw AdmissionFailure(14,"Invalid D2DBS character save"); }
        }
        const auto header = readD2sHeader(bytes);
        if (folded(character.player.name) != folded(identity.name) || header.characterClass != identity.characterClass)
            throw ProtocolError("D2DBS character identity mismatch");
        if (!(header.flags & 0x20)) throw AdmissionFailure(23,"Expansion character required");
        if ((header.flags & 0x0C) == 0x0C) throw AdmissionFailure(21,"Hardcore character is dead");
        const auto room = rooms.find(identity.game);
        if (room == rooms.end()) throw ProtocolError("D2CS game expired during load");
        const auto &definition = host.games.at(room->second);
        if (character.player.hardcore != definition.hardcore)
            throw AdmissionFailure(definition.hardcore ? 19 : 20,"Character hardcore mode does not match game");
        const auto progression = (header.flags >> 8) & 31;
        if (unsigned(definition.settings.difficulty) > (progression >= 10 ? 2u : progression >= 5 ? 1u : 0u))
            throw AdmissionFailure(definition.settings.difficulty == 1 ? 17 : 18,"Character difficulty is locked");
        return character;
    }
    bool checkpoint(Peer &peer, const PersistentCharacter &character) {
        if (!peer.saveError.empty()) throw ProtocolError(peer.saveError);
        if (peer.savePending) return false;
        auto bytes = encodeSave(character,*host.content);
        auto header = readD2sHeader(bytes);
        if (!peer.created) peer.created = header.created;
        header.created = peer.created;
        writeD2sHeader(bytes,header);
        auto identity = saveIdentity(bytes);
        if (peer.confirmedSave == identity) return true;
        if (peer.saveRequest == std::numeric_limits<uint64_t>::max()) throw ProtocolError("Save sequence exhausted");
        RealmGameOutput event; event.kind = RealmGameOutput::Kind::Save;
        event.connection = peer.identity.connection; event.game = peer.identity.game;
        event.request = ++peer.saveRequest;
        event.bytes = std::move(bytes); decodeSave(event.bytes,*host.content);
        event.portrait = peer.identity.portrait;
        const auto appearance = host.portraits->encode(characterAppearance(*host.content,character),header.characterClass,header.level,uint16_t(header.flags));
        if (appearance.size() != 33) throw ProtocolError("Unsupported realm portrait length");
        std::copy(appearance.begin(),appearance.end(),event.portrait.begin() + 112); event.portrait[145] = 0;
        store32(event.portrait,12,uint32_t(std::time(nullptr)));
        store32(event.portrait,176,uint32_t(character.player.experience)); store32(event.portrait,180,header.flags & 0xffffu);
        store32(event.portrait,184,header.level); store32(event.portrait,188,header.characterClass);
        event.level = header.level; event.characterClass = header.characterClass;
        peer.pendingSave = std::move(identity);
        peer.identity.portrait = event.portrait;
        emit(std::move(event)); peer.savePending = true;
        return false;
    }
    void leave(Peer &peer) {
        peer.leaving = true;
        if (peer.service) {
            peer.service->peer.phase = GamePhase::Closed;
            peer.service->leavePending = false;
        }
    }
    void receive(uint64_t connection, Bytes packet) {
        const auto found = peers.find(connection); if (found == peers.end()) return;
        auto &peer = *found->second;
        if (!peer.service || peer.leaving) return;
        if (packet.empty()) throw ProtocolError("Empty game packet");
        try {
            if (packet.front() == uint8_t(ClientMessage::SaveAndLeave)) {
                requirePhase(clientMessage(packet.front()),peer.service->peer.phase);
                leave(peer); return;
            }
            peer.service->game(packet);
            if (!peer.entered && peer.service->peer.phase == GamePhase::Entered) {
                peer.entered = true;
                RealmGameOutput event; event.kind = RealmGameOutput::Kind::Entered; event.connection = connection;
                event.game = peer.identity.game; event.level = uint32_t(peer.service->selected->player.level);
                event.characterClass = peer.identity.characterClass; emit(std::move(event));
            }
            if (peer.service->peer.phase == GamePhase::Closed) leave(peer);
        } catch (const std::exception &error) { reject(peer,error); }
    }
    void reject(Peer &peer, const std::exception &error) {
        if (!peer.entered) {
            const auto *admission = dynamic_cast<const AdmissionFailure *>(&error);
            packetOut(peer.identity.connection,encodeServerPacket(ServerMessage::AdmissionError,[&](auto &out) {out.u32(admission ? admission->code : 6);}));
        }
        RealmGameOutput event; event.kind = RealmGameOutput::Kind::Failure;
        event.connection = peer.identity.connection; event.error = error.what(); emit(std::move(event));
        leave(peer);
    }
    void admit(RealmCharacter identity) {
        const auto connection = identity.connection;
        auto value = std::make_unique<Peer>(); value->identity = std::move(identity);
        auto &peer = *value;
        if (!peers.emplace(connection,std::move(value)).second) throw ProtocolError("Duplicate game connection");
        try {
            if (draining) throw ProtocolError("Game thread is draining");
            auto character = decode(peer.identity);
            peer.service = std::make_unique<NativeRealmService>(host,
                [](uint8_t,Bytes) {throw ProtocolError("MCP output on GS connection");},
                [this,connection](Bytes bytes) {packetOut(connection,std::move(bytes));});
            peer.service->externalCheckpoint = [this,target=&peer](const PersistentCharacter &saved) {return checkpoint(*target,saved);};
            peer.service->admitExternal(std::move(character),host.games.at(rooms.at(peer.identity.game)),peer.identity.token,uint16_t(peer.identity.game));
            peer.service->connectGame(false);
            receive(connection,std::move(peer.identity.logon));
        } catch (const std::exception &error) { reject(peer,error); }
    }
    void finish() {
        for (auto current = peers.begin(); current != peers.end();) {
            auto &peer = *current->second;
            if (draining) leave(peer);
            if (peer.service && peer.service->peer.phase == GamePhase::Closed) leave(peer);
            if (!peer.saveError.empty()) throw ProtocolError(peer.saveError);
            if (peer.service && peer.service->binding) {
                const auto binding = *peer.service->binding;
                if (peer.leaving) {
                    if (peer.entered) {
                        if (!peer.finalCharacter) {
                            if (!host.host.prepareSave(binding) || !host.host.exportCharacter(binding)) {++current; continue;}
                            if (!peer.frozen) {
                                if (!host.host.enter(binding,false)) {++current; continue;}
                                peer.frozen = true;
                            }
                            peer.finalCharacter = host.host.exportCharacter(binding);
                        }
                        if (!peer.finalCharacter || !checkpoint(peer,*peer.finalCharacter)) {++current; continue;}
                    }
                    peer.service->close(false);
                } else if (peer.entered && Clock::now() >= peer.checkpoint && host.host.exportCharacter(binding)) {
                    if (peer.service->checkpoint()) peer.checkpoint = Clock::now() + std::chrono::seconds(60);
                }
            }
            if (!peer.leaving) {++current; continue;}
            RealmGameOutput event; event.kind = RealmGameOutput::Kind::Retired;
            event.connection = peer.identity.connection; event.game = peer.identity.game; emit(std::move(event));
            current = peers.erase(current);
        }
        for (auto current = rooms.begin(); current != rooms.end();) {
            if (host.games.contains(current->second)) {++current; continue;}
            RealmGameOutput event; event.kind = RealmGameOutput::Kind::RoomClosed; event.game = current->first;
            emit(std::move(event)); current = rooms.erase(current);
        }
    }
};
PvpgnGameThread::PvpgnGameThread(const Archives &source, std::filesystem::path root, ContentPreparation preparation)
    : impl_(std::make_unique<Impl>(source,std::move(root),preparation)) {}
PvpgnGameThread::~PvpgnGameThread() = default;
void PvpgnGameThread::reserve(HostedGame room) {
    impl_->post([state=impl_.get(),room=std::move(room)]() mutable {
        const auto key = folded(room.name);
        state->rooms.emplace(room.index,key); state->host.games.emplace(key,std::move(room));
    });
}
void PvpgnGameThread::admit(RealmCharacter character) {
    const auto bytes = character.save.size() + character.portrait.size() + character.logon.size();
    impl_->post([state=impl_.get(),character=std::move(character)]() mutable {state->admit(std::move(character));},bytes);
}
void PvpgnGameThread::packet(uint64_t connection, Bytes packet) {
    const auto bytes = packet.size();
    impl_->post([state=impl_.get(),connection,packet=std::move(packet)]() mutable {state->receive(connection,std::move(packet));},bytes);
}
void PvpgnGameThread::disconnect(uint64_t connection) {
    impl_->post([state=impl_.get(),connection] {if (auto found=state->peers.find(connection);found!=state->peers.end()) state->leave(*found->second);});
}
void PvpgnGameThread::saved(uint64_t connection, uint64_t request, std::string error) {
    impl_->post([state=impl_.get(),connection,request,error=std::move(error)] {
        const auto found=state->peers.find(connection); if(found==state->peers.end()) return;
        auto &peer=*found->second;
        if (!peer.savePending || peer.saveRequest != request) throw ProtocolError("Save completion does not match game request");
        peer.savePending=false; peer.saveError=error;
        if(error.empty()) {
            peer.confirmedSave=std::move(peer.pendingSave);
            peer.checkpoint=Clock::now()+std::chrono::seconds(60);
        }
    });
}
void PvpgnGameThread::retireRoom(uint32_t game) {
    impl_->post([state=impl_.get(),game] {
        const auto found=state->rooms.find(game); if(found==state->rooms.end()) return;
        const auto room=state->host.games.find(found->second);
        if(room!=state->host.games.end() && !room->second.handle.generation) state->host.games.erase(room);
    });
}
void PvpgnGameThread::drain() {impl_->post([state=impl_.get()] {state->draining=true;});}
std::vector<RealmGameOutput> PvpgnGameThread::take() {
    std::lock_guard lock(impl_->mutex);
    if(!impl_->fatal.empty()) throw ProtocolError("Game thread failed: "+impl_->fatal);
    impl_->outputBytes=0; return std::exchange(impl_->output,{});
}
void PvpgnGameThread::stop() {impl_->stop();}
std::vector<std::string> PvpgnGameThread::recover() {
    impl_->stop();
    std::vector<std::string> issues;
    for (const auto &[connection,peer] : impl_->peers) {
        (void)connection;
        if (!peer->service || !peer->service->binding) continue;
        try {
            const auto character=peer->finalCharacter ? peer->finalCharacter : impl_->host.host.exportCharacter(*peer->service->binding);
            if (!character) throw ProtocolError("Character settlement is incomplete");
            const auto bytes=encodeSave(*character,*impl_->host.content);
            decodeSave(bytes,*impl_->host.content);
            const auto name=folded(peer->identity.account)+"-"+folded(peer->identity.name)+".shutdown.d2s";
            writeFileAtomically(impl_->host.root/name,bytes,true);
        } catch (const std::exception &error) {issues.push_back(peer->identity.name+": "+error.what());}
    }
    return issues;
}
}