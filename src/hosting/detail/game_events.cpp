#include "server/runtime/diagnostics.hpp"
#include "server/runtime/environment_fact.hpp"
#include "server/runtime/facts/character.hpp"
#include "server/runtime/facts/combat.hpp"
#include "server/runtime/facts/effects.hpp"
#include "server/runtime/facts/inventory.hpp"
#include "server/runtime/facts/map.hpp"
#include "server/runtime/facts/npc.hpp"
#include "server/runtime/facts/object.hpp"
#include "server/runtime/facts/quest.hpp"
#include "server/runtime/facts/quest_signals.hpp"
#include "server/runtime/facts/resources.hpp"
#include "server/runtime/facts/skills.hpp"
#include "server/runtime/facts/social.hpp"
#include "server/runtime/facts/player_message.hpp"
#include "server/runtime/facts/travel.hpp"
#include "server/runtime/facts/ui.hpp"
#include "server/runtime/facts/unit.hpp"
#include "server/game_messages.hpp"
#include "server/runtime/event_batch.hpp"
#include "native_realm_service.hpp"
#include "hosting/pd2/map_wire.hpp"
#include "hosting/native_item_wire.hpp"
#include "hosting/native_character_wire.hpp"
#include "hosting/native_quest_wire.hpp"
#include "hosting/native_combat_wire.hpp"
#include "gameplay/areas/waypoint.hpp"
#include "gameplay/quest/catalog.hpp"
#include <limits>
#include <cmath>

namespace d2x::hosting {
void NativeRealmService::sendGameBatch(std::vector<Bytes> packets) {
    size_t size = 0;
    for (const auto &packet : packets) {
        validateServerPacket(packet,content && content->pd2Host?net::protocol::ClientProfile::Pd2Local:net::protocol::ClientProfile::Lod113c);
        if (packet.size() > std::numeric_limits<size_t>::max() - size) throw std::runtime_error("Native output batch overflow");
        size += packet.size();
    }
    if (!size) throw std::logic_error("Empty native event batch");
    Bytes bytes; bytes.reserve(size);
    for (const auto &packet : packets) bytes.insert(bytes.end(), packet.begin(), packet.end());
    // One stream write accepts the entire fact or fails the connection. The
    // existing native stream decoder splits the concatenated original packets.
    gameOutput(std::move(bytes));
    peer.pendingPlayerUpdate = true;
    for (const auto &packet : packets) {
        ++counters.gameResponses[packet[0]];
        if (shared.gamePacketObserver) shared.gamePacketObserver(diagnosticConnection, GamePacketDirection::ServerToClient, packet);
    }
}
void NativeRealmService::publishEvents() {
    if (binding) shared.publishEvents(binding->game);
}
void NativeRealmService::publishInventoryRefresh() {
    if(!binding || peer.phase!=GamePhase::Entered) return;
    const auto own=host.read(*binding);if(!own) return;
    std::vector<Bytes> packets;
    for(const auto actor:peer.inventoryRefresh) {
        if(actor!=own->actor.id && std::none_of(peer.visible.begin(),peer.visible.end(),
            [&](const auto &entry){return entry.second.actor==actor;})) continue;
        for(const auto message:{ServerMessage::Reserved47,ServerMessage::Reserved48})
            packets.push_back(encodeServerPacket(message,[&](auto &out){
                out.u8(0);out.u8(0);out.u32(wireId(actor));out.u32(0);
            }));
    }
    if(!packets.empty()) sendGameBatch(std::move(packets));
    peer.inventoryRefresh.clear();
}
void NativeRealmService::receiveEvent(const server::EventBatch &batch) {
    std::vector<Bytes> packets;
    const auto visibleActor=[&](EntityId actor,uint8_t type) {
        if(type==1) return peer.monsters.contains(actor);
        if(type==2) return peer.objects.contains(actor) || peer.portals.contains(actor);
        if(type==4) return peer.groundItems.contains(actor);
        if(type==5) return peer.warps.contains(actor);
        return type==0 && (actor==host.read(*binding)->actor.id || peer.corpses.contains(actor) ||
            std::any_of(peer.visible.begin(),peer.visible.end(),[&](const auto &entry){return entry.second.actor==actor;}));
    };
    for (const auto &fact : batch.facts) {
        if (const auto *travel = server::factIf<server::TravelFact>(&fact)) {
            if (!packets.empty()) { sendGameBatch(std::move(packets)); packets.clear(); }
            changeArea(*travel); continue;
        }
        std::vector<Bytes> delta;
        if(const auto *identity=server::factIf<server::MonsterIdentityFact>(&fact)) {
            if(!peer.monsters.contains(identity->actor)) continue;
            delta.push_back(encodeServerPacket(ServerMessage::Reserved57,[&](auto &out) {
                out.u32(wireId(identity->actor));out.u8(1);out.u16(identity->nameSeed);
                out.u16(uint16_t(identity->modifiers[0]) | uint16_t(uint16_t(identity->modifiers[1])<<8));
                out.u16(identity->modifiers[2]);out.u16(identity->champion?1:0);
            }));
        }
        else if(const auto *map=server::factIf<server::Pd2MapAttributesFact>(&fact)) {
            if(!content->pd2Host) throw std::logic_error("PD2 map report in an original game");
            auto projected=*map;projected.portal={identities().guid(map->portal,2)};
            delta.push_back(pd2MapAttributes(projected));
        }
        else if (const auto *list = server::factIf<server::HirelingListFact>(&fact)) {
            delta.push_back(encodeServerPacket(ServerMessage::HirelingListReset,[](auto &){}));
            for(const auto &[name,seed]:list->offers)
                delta.push_back(encodeServerPacket(ServerMessage::HirelingOffer,[&](auto &out){out.u16(name);out.u32(seed);}));
        }
        else if (server::factHolds<server::LifeFact>(fact) || server::factHolds<server::ManaFact>(fact) || server::factHolds<server::ResourcesFact>(fact)) {
            // Ordinary costs and per-frame regeneration are observed by
            // publishMotion's native 18/95/96 checks. Keep domain diagnostics
            // without a second 1D/E/F stream for the same resource writes.
        }
        else if(const auto *object=server::factIf<server::ObjectFact>(&fact)) {
            const auto known=peer.objects.find(object->object);
            // No history for a newly visible unit or a recipient that left its
            // room. Its first assignment describes the current authoritative mode.
            if(known==peer.objects.end() || known->second.revision>=object->revision ||
                !host.visiblePoint(*binding,object->area,object->position)) continue;
            delta.push_back(encodeServerPacket(ServerMessage::UnitMode,[&](auto &out) {
                out.u8(2);out.u32(wireId(object->object));out.u8(3);
                out.u8(object->selectionFlags);
                out.u32(uint32_t(object->mode));
            }));
            if(object->mode==1 && object->shrine && object->actor)
                delta.push_back(encodeServerPacket(ServerMessage::CastPoint,[&](auto &out) {
                    out.u8(2);out.u32(wireId(object->object));out.u32(wireId(object->actor));
                    out.u8(uint8_t(*object->shrine));out.u16(0);out.u16(0);out.u16(0);
                }));
            known->second.revision=object->revision;known->second.mode=object->mode;
        }
        else if (const auto *position = server::factIf<server::RepositionFact>(&fact)) {
            if((!position->notifyOwner || position->revealRoom) && position->actor==host.read(*binding)->actor.id) continue;
            if (position->actor != host.read(*binding)->actor.id &&
                std::none_of(peer.visible.begin(), peer.visible.end(), [&](const auto &entry) { return entry.second.actor == position->actor; })) continue;
            delta.push_back(nativeReposition(*position, shared.terrain.at(binding->game).at(position->area).origin));
            peer.lastMotion.clear();
            for (auto &[id, player] : peer.visible) { (void)id; if (player.actor == position->actor) player.motion.clear(); }
        }
        else if (const auto *attack = server::factIf<server::AttackFact>(&fact)) {
            if (attack->actorType == 1 && !peer.monsters.contains(attack->actor)) continue;
            auto projected=*attack;
            const bool objectSelector=projected.actorType==1 && projected.skill && projected.targetType==2;
            // Do104's point selector survives target visibility filtering,
            // just as the original path's (object GUID, 2) coordinates do.
            if(projected.target && !objectSelector && !visibleActor(projected.target,projected.targetType)) projected.target={};
            if (attack->actorType == 0 && attack->actor != host.read(*binding)->actor.id &&
                std::none_of(peer.visible.begin(), peer.visible.end(), [&](const auto &entry) { return entry.second.actor == attack->actor; })) continue;
            // PlrMsg::sub_6FC81D20 omits ordinary owner skill actions. Its client
            // predicts the submitted cast; other visible clients receive 4C/4D.
            // Charge's arrival and forced avoidance synchronize the owner too.
            if (attack->forced || attack->actorType != 0 || attack->actor != host.read(*binding)->actor.id)
                delta = nativeAttack(projected, shared.terrain.at(binding->game).at(attack->area).origin);
            peer.lastMotion.clear();
            for (auto &[id, player] : peer.visible) { (void)id; if (player.actor == attack->actor) player.motion.clear(); }
            if (auto found = peer.monsters.find(attack->actor); found != peer.monsters.end()) {
                found->second.motion.clear();
                if(attack->skill) found->second.skillAction=attack->action;
            }
        } else if (const auto *hit = server::factIf<server::HitFact>(&fact)) {
            if (hit->type == 1 && !peer.monsters.contains(hit->target)) continue;
            if (hit->type == 0 && hit->target != host.read(*binding)->actor.id &&
                std::none_of(peer.visible.begin(), peer.visible.end(), [&](const auto &entry) { return entry.second.actor == hit->target; })) continue;
            auto projected=*hit;
            if(projected.actionTarget && !visibleActor(projected.actionTarget->id,projected.actionTarget->type)) projected.actionTarget.reset();
            delta = nativeHit(projected, shared.terrain.at(binding->game).at(hit->area).origin);
            if(auto found=peer.monsters.find(hit->target);hit->type==1 && found!=peer.monsters.end() &&
                (!hit->monsterMode || hit->killed)) found->second.life=hit->life;
            if (auto found = peer.monsters.find(hit->target);hit->type==1 && found != peer.monsters.end() && hit->killed) {
                // The reliable fact already started DT. Retain its canonical
                // snapshot baseline so publication cannot restart DT again.
                MonsterSnapshot dying;dying.id=hit->target;dying.position=hit->position;dying.mode=0;
                found->second.motion=nativeMonsterMotion(dying,shared.terrain.at(binding->game).at(hit->area).origin);
            }
        } else if (const auto *inventory = server::factIf<server::InventoryFact>(&fact)) {
            delta = nativeInventoryDelta(*content, *inventory);
            if(!delta.empty()) peer.inventoryRefresh.insert(inventory->projection.player.id);
            for (const auto &change : inventory->changes) if (change.kind == ItemChangeKind::Created) peer.groundItems.erase(change.item);
        }
        else if (const auto *drop = server::factIf<server::GroundDropFact>(&fact)) {
            const auto &at = std::get<GroundLocation>(drop->item.location);
            // Area audiences include distant rooms. Match the ground snapshot's
            // room interest before 9C asks the retail client to attach an item.
            if(!host.visiblePoint(*binding,at.region,at.position)) continue;
            delta.push_back(nativeGroundItem(*content, drop->item,
                shared.terrain.at(binding->game).at(at.region).origin, drop->flip?GroundItemAction::Drop:GroundItemAction::Add));
            peer.groundItems.insert_or_assign(drop->item.id, drop->item.revision);
        }
        else if(const auto *hint=server::factIf<server::MonsterClassHintFact>(&fact)) {
            if(!visibleActor(hint->actor,1)) continue;
            delta.push_back(encodeServerPacket(ServerMessage::MonsterClassHint,[&](auto &out){out.u16(hint->nativeClass);}));
        }
        else if(const auto *refresh=server::factIf<server::UnitRefreshFact>(&fact)) {
            if(!visibleActor(refresh->actor,refresh->type)) continue;
            const auto origin=shared.terrain.at(binding->game).at(refresh->area).origin;
            const auto point=refresh->position+origin;
            delta.push_back(encodeServerPacket(ServerMessage::Reposition,[&](auto &out){out.u8(refresh->type);out.u32(wireId(refresh->actor));out.u16(uint16_t(point.x));out.u16(uint16_t(point.y));out.u8(1);}));
            if(refresh->type==0) {
                if(refresh->actor==host.read(*binding)->actor.id) {peer.lastMotion.clear();peer.sentRevision=0;}
                for(auto &[player,entry]:peer.visible) {(void)player;if(entry.actor==refresh->actor) entry.motion.clear();}
            } else if(auto entry=peer.monsters.find(refresh->actor);entry!=peer.monsters.end() &&
                !entry->second.motion.empty() && (entry->second.motion.front()==0x67 || entry->second.motion.front()==0x68))
                entry->second.motion.clear();
        }
        else if(const auto *alert=server::factIf<server::QuestAlertFact>(&fact)) {
            if(!visibleActor(alert->actor,alert->type)) continue;
            delta.push_back(encodeServerPacket(ServerMessage::QuestAlert,[&](auto &out){out.u8(alert->type);out.u32(wireId(alert->actor));}));
        }
        else if(const auto *clue=server::factIf<server::QuestClueFact>(&fact)) {
            delta.push_back(encodeServerPacket(ServerMessage::QuestPayload,[&](auto &out){out.u16(clue->quest);for(auto value:clue->payload) out.u16(value);}));
        }
        else if(const auto *sound=server::factIf<server::SoundFact>(&fact)) {
            if(!sound->directSound && !visibleActor(sound->actor,sound->type)) continue;
            delta.push_back(encodeServerPacket(ServerMessage::UnitSound,[&](auto &out){out.u8(uint8_t(sound->type+(sound->directSound?0xf0:0)));out.u32(wireId(sound->actor));out.u16(sound->sound);}));
        }
        else if(const auto *overlay=server::factIf<server::OverlayFact>(&fact)) {
            if(!visibleActor(overlay->actor,overlay->type)) continue;
            delta.push_back(encodeServerPacket(ServerMessage::Overlay,[&](auto &out){out.u8(overlay->type);out.u32(wireId(overlay->actor));out.u16(uint16_t(overlay->overlay));}));
        }
        else if(const auto *restored=server::factIf<server::GroundRestoredFact>(&fact)) {
            // ItemMode only sends 3E to an owner. An unowned ground item
            // silently restores; its next admission/pickup contains the value.
            if(auto old=peer.groundItems.find(restored->item);old!=peer.groundItems.end()) old->second=restored->revision;
        }
        else if(const auto *trigger=server::factIf<server::ItemSkillFact>(&fact)) {
            if(!visibleActor(trigger->owner,trigger->ownerType)) continue;
            const auto origin=shared.terrain.at(binding->game).at(trigger->area).origin;
            if(trigger->target) delta.push_back(encodeServerPacket(ServerMessage::CastUnitAlternate,[&](auto &out){
                out.u8(trigger->ownerType);out.u32(wireId(trigger->owner));out.u16(uint16_t(trigger->skill));out.u8(uint8_t(trigger->rank));
                out.u8(trigger->targetType);out.u32(wireId(trigger->target));out.u16(trigger->flags);
            }));
            else delta.push_back(encodeServerPacket(ServerMessage::CastPointAlternate,[&](auto &out){
                out.u8(trigger->ownerType);out.u32(wireId(trigger->owner));out.u32(uint32_t(trigger->skill));out.u8(uint8_t(trigger->rank));
                out.u16(uint16_t(std::floor(trigger->position.x+origin.x)));out.u16(uint16_t(std::floor(trigger->position.y+origin.y)));out.u16(trigger->flags);
            }));
        }
        else if (const auto *pulse = server::factIf<server::SkillPulseFact>(&fact)) {
            if(!visibleActor(pulse->owner,pulse->ownerType) || (pulse->target && !visibleActor(pulse->target,pulse->targetType))) continue;
            delta.push_back(encodeServerPacket(ServerMessage::SkillEvent, [&](auto &out) {
                out.u8(pulse->parameter);out.u16(uint16_t(pulse->skill));out.u16(uint16_t(pulse->rank));
                out.u8(pulse->ownerType);out.u32(wireId(pulse->owner));out.u8(pulse->target?pulse->targetType:0);out.u32(pulse->target?wireId(pulse->target):UINT32_MAX);out.u32(pulse->argumentX);out.u32(pulse->argumentY);
            }));
        }
        else if(const auto *ended=server::factIf<server::SkillEndFact>(&fact)) {
            if(!visibleActor(ended->actor,ended->type)) continue;
            delta.push_back(encodeServerPacket(ServerMessage::SkillEnd,[&](auto &out) {
                out.u8(ended->type);out.u32(wireId(ended->actor));out.u16(ended->skill);
            }));
        }
        else if (const auto *missile = server::factIf<server::MissileFact>(&fact)) {
            if((!missile->forcePublication && !visibleActor(missile->owner,missile->ownerType)) || (missile->missile && peer.missiles.contains(missile->missile))) continue;
            delta.push_back(nativeMissile(*missile,shared.terrain.at(binding->game).at(missile->area).origin));
            if(missile->missile) peer.missiles.insert(missile->missile);
        }
        else if (const auto *state = server::factIf<server::StateFact>(&fact)) {
            if (state->state < 0 || state->state > 255) throw std::runtime_error("State exceeds native capacity");
            if (state->type == 1) {
                const auto entry = peer.monsters.find(state->actor);
                if(entry==peer.monsters.end()) continue;
                if(state->enabled) {entry->second.states.insert(state->state);entry->second.stateStats[state->state]=state->stats;}
                else {if(!entry->second.states.erase(state->state)) continue;entry->second.stateStats.erase(state->state);}
            }
            if(state->type==0 && state->actor!=host.read(*binding)->actor.id && std::none_of(peer.visible.begin(),peer.visible.end(),[&](const auto &entry){return entry.second.actor==state->actor;})) continue;
            if(state->type==0) {
                if(state->enabled) {peer.states[state->actor].insert(state->state);peer.stateStats[state->actor][state->state]=state->stats;}
                else {if(!peer.states[state->actor].erase(state->state)) continue;peer.stateStats[state->actor].erase(state->state);}
            }
            auto packet=nativeState(*content,*state);
            if(!packet.empty()) delta.push_back(std::move(packet));
        }
        else if (const auto *quest = server::factIf<server::QuestFact>(&fact)) {
            if(quest->fullStatus) {
                auto baseline=*quest;baseline.fullStatus=false;
                for(auto &packet:nativeQuestUpdate(*content,baseline))
                    if(packet.front()==uint8_t(ServerMessage::QuestUpdate))
                        peer.questProjection[{packet.front(),packet.at(1)}]=std::move(packet);
            }
            for(auto &packet:nativeQuestUpdate(*content,*quest)) {
                const auto message=ServerMessage(packet.front());
                const uint16_t payload=message==ServerMessage::QuestPayload?uint16_t(packet.at(1)|uint16_t(packet.at(2))<<8):0;
                if(message==ServerMessage::GameQuests || message==ServerMessage::PlayerQuests ||
                    message==ServerMessage::QuestUpdate || (message==ServerMessage::QuestPayload && (payload==1 || payload==4))) {
                    const uint16_t slot=message==ServerMessage::QuestUpdate?packet.at(1):
                        message==ServerMessage::QuestPayload?payload:0;
                    auto &previous=peer.questProjection[{packet.front(),slot}];
                    const bool unchanged=previous==packet;
                    previous=packet;
                    if(unchanged && !quest->fullStatus && !quest->zooOnly) continue;
                }
                delta.push_back(std::move(packet));
            }
        }
        else if(const auto *quest=server::factIf<server::QuestStatusFact>(&fact)) {
            const auto &definition=questDefinition(quest->quest);
            delta.push_back(encodeServerPacket(ServerMessage::QuestUpdate,[&](auto &out){
                out.u8(uint8_t(definition.nativeSlot-unsigned(definition.act)));out.u8(quest->flags);out.u8(quest->status);out.u16(quest->counter);
            }));
            if(!quest->flags)
                peer.questProjection[{uint8_t(ServerMessage::QuestUpdate),uint16_t(definition.nativeSlot-unsigned(definition.act))}]=delta.back();
        }
        else if(const auto *effect=server::factIf<server::QuestEffectFact>(&fact))
            delta.push_back(encodeServerPacket(ServerMessage::QuestEffect,[&](auto &out){out.u8(effect->effect);}));
        else if (const auto *removed=server::factIf<server::GroundRemoveFact>(&fact)) {
            if(peer.groundItems.erase(removed->item)) delta.push_back(encodeServerPacket(ServerMessage::RemoveUnit,[&](auto &out){out.u8(4);out.u32(wireId(removed->item));}));
        }
        else if (const auto *character = server::factIf<server::CharacterFact>(&fact)) delta = nativeCharacterDelta(*content, *character);
        else if (const auto *death = server::factIf<server::DeathFact>(&fact)) {
            if (death->victim != host.read(*binding)->actor.id) continue;
            const auto stat = content->nativeStats->names.at("goldlost");
            if (stat > UINT8_MAX) throw std::runtime_error("Death gold stat lacks native byte identity");
            const auto value = death->goldLost;
            const auto message = value < UINT8_MAX ? ServerMessage::AttributeByte :
                value < UINT16_MAX ? ServerMessage::AttributeWord : ServerMessage::AttributeDword;
            delta.push_back(encodeServerPacket(message, [&](auto &out) {
                out.u8(uint8_t(stat));
                if (value < UINT8_MAX) out.u8(uint8_t(value));
                else if (value < UINT16_MAX) out.u16(uint16_t(value));
                else out.u32(value);
            }));
        }
        else if(const auto *environment=server::factIf<server::EnvironmentFact>(&fact))
            delta.push_back(encodeServerPacket(ServerMessage::Environment,[&](auto &out) {
                out.u32(uint32_t(environment->cycle));out.u32(uint32_t(environment->ticks));out.u8(environment->eclipse?1:0);
            }));
        else if (const auto *chat = server::factIf<server::ChatFact>(&fact)) {
            if (std::find(chat->recipients.begin(), chat->recipients.end(), binding->player) == chat->recipients.end()) continue;
            delta.push_back(encodeServerPacket(ServerMessage::Chat, [&](auto &out) {
                out.u8(chat->type); out.u8(chat->language); out.u8(chat->unitType); out.u32(wireId(chat->actor)); out.u8(chat->messageColor); out.u8(chat->nameColor);
                out.string(chat->name); out.string(chat->text);
            }));
        } else if (const auto *relation=server::factIf<server::ChatRelationFact>(&fact)) {
            delta.push_back(encodeServerPacket(ServerMessage::PlayerRelationFlags,[&](auto &out){out.u32(wireId(relation->from));out.u32(wireId(relation->to));out.u16(relation->flags);}));
            peer.relations[{relation->from,relation->to}]=relation->flags;
            // The roster projection carries current party/status/flags together.
        } else if(const auto *notice=server::factIf<server::PartyNoticeFact>(&fact)) {
            if(notice->recipient!=binding->player) continue;
            delta.push_back(encodeServerPacket(ServerMessage::Reserved5A,[&](auto &out){
                out.u8(7);out.u8(notice->action==3?8:notice->action==4?0:notice->action==6 || notice->action==9?9:2);
                out.u32(wireId(notice->player));out.u8(notice->action);out.append(std::array<uint8_t,32>{});
            }));
        } else if (const auto *trade = server::factIf<server::TradeFact>(&fact)) {
            if(trade->action) delta.push_back(encodeServerPacket(ServerMessage::UiAction,[&](auto &out){out.u8(*trade->action);}));
            if(trade->partner) delta.push_back(encodeServerPacket(ServerMessage::TradePeer,[&](auto &out) {
                std::array<uint8_t,16> name{};
                if(trade->name.empty() || trade->name.size()>15) throw std::logic_error("Invalid trade peer name");
                std::copy(trade->name.begin(),trade->name.end(),name.begin());out.append(name);out.u32(wireId(trade->partner));
            }));
            for(const auto side : {uint8_t(1),uint8_t(0)}) {
                const auto amount=side?trade->ownGold:trade->peerGold;
                if(amount) delta.push_back(encodeServerPacket(ServerMessage::TradeGold,[&](auto &out){out.u8(side);out.u32(*amount);}));
            }
        } else if(const auto *reset=server::factIf<server::InventoryResetFact>(&fact)) {
            const auto owner=reset->projection.player.id;
            delta.push_back(encodeServerPacket(ServerMessage::Reserved92,[&](auto &out){out.u8(0);out.u32(wireId(owner));}));
            for(auto &[id,sent]:peer.visible) if(sent.actor==owner) {
                (void)id;
                // 92 already removed the old equipment. Rebuild the observer's
                // public baseline after the private restore/close facts drain.
                sent.equipment.clear(); sent.inventoryRevision=0;
                break;
            }
        } else if(const auto *items=server::factIf<server::TradeItemsFact>(&fact)) {
            delta=nativeTradeItems(*content,*items,host.read(*binding)->actor.id);
        } else if(const auto *message=server::factIf<server::PlayerMessageFact>(&fact)) {
            auto value=message->value;
            if(message->type==7) value=identities().guid(EntityId{value},0);
            if(message->type==6 && message->parameter==0) {
                const auto character=std::find_if(content->characters.begin(),content->characters.end(),[&](const auto &c){return c.name==message->characterClass;});
                if(character==content->characters.end()) throw std::logic_error("Unknown slain-by player class");
                value=uint32_t(character-content->characters.begin());
            }
            delta.push_back(encodeServerPacket(ServerMessage::Reserved5A,[&](auto &out){
                out.u8(message->type);out.u8(message->type==7?(message->parameter<=2?4:9):message->type==2 || message->type==3 || message->type==6?4:0);out.u32(value);out.u8(message->parameter); std::array<uint8_t,32> names{};
                if(message->name.size()>15 || message->other.size()>15) throw std::logic_error("Invalid player message name");
                std::copy(message->name.begin(),message->name.end(),names.begin());
                std::copy(message->other.begin(),message->other.end(),names.begin()+16);out.append(names);
            }));
        } else if (const auto *npc = server::factIf<server::NpcMessagesFact>(&fact)) {
            if(npc->messages.size()>=8) throw std::runtime_error("NPC message count exceeds native limit");
            delta.push_back(encodeServerPacket(ServerMessage::NpcMessages,[&](auto &out) {
                out.u8(npc->type); out.u32(wireId(npc->npc)); out.u8(uint8_t(npc->messages.size())); out.u8(0);
                for (size_t i=0;i<8;++i) { const auto message=i<npc->messages.size()?npc->messages[i]:server::NpcMessage{}; out.u8(message.menu); out.u8(0); out.u16(message.text); }
            }));
            if(npc->type==1) {
                const auto quests=host.questProjection(*binding);
                if(!quests) throw std::logic_error("NPC interaction quest projection unavailable");
                if(npc->activate) {
                    peer.npcQuestAlerts.clear(); // NPC-context28 clears every old alert in the client.
                    for(auto &packet:nativeQuestUpdate(*content,*quests,EntityId{identities().guid(npc->npc,1)})) {
                        peer.questProjection[{packet.front(),0}]=packet;
                        delta.push_back(std::move(packet));
                    }
                } else delta.push_back(encodeServerPacket(ServerMessage::GameQuests,[&](auto &out) {
                    for(const auto word:quests->gameFlags) out.u16(word);
                }));
                if(!npc->activate) peer.questProjection[{uint8_t(ServerMessage::GameQuests),0}]=delta.back();
            }
        } else if (const auto *merchant = server::factIf<server::MerchantFact>(&fact)) {
            if (merchant->refreshShop) { peer.shopItems.clear(); peer.gambleShopItems.clear(); }
            else delta.push_back(encodeServerPacket(ServerMessage::MerchantResult,[&](auto &out) {
                out.u8(merchant->operation);out.u8(merchant->result);out.u32(0);
                const bool withoutItem=merchant->result==2 || merchant->result>=7;
                out.u32(withoutItem?UINT32_MAX:wireId(merchant->item));out.u32(merchant->gold);
            }));
        } else if (const auto *waypoint=server::factIf<server::WaypointFact>(&fact)) {
            std::array<uint16_t,8> history{}; history[0]=0x102; history[1]=1; const auto &levels=content->tables.at("levels");
            for(const auto region:waypoint->unlocked) for(size_t row=0;row<levels.rows().size();++row) if(levels.number(row,"Id")==int(region)) {
                const auto index=levels.number(row,"Waypoint").value_or(255); if(nativeWaypointIndex(index)) history[size_t(1+index/16)]|=uint16_t(1u<<(index%16));
            }
            delta.push_back(encodeServerPacket(ServerMessage::Waypoints,[&](auto &out){out.u32(wireId(waypoint->source));for(const auto word:history)out.u16(word);}));
        } else if (const auto *targeting=server::factIf<server::ItemTargetingFact>(&fact)) {
            delta.push_back(encodeServerPacket(ServerMessage::ItemTargeting,[&](auto &out){out.u8(uint8_t(targeting->cursor));out.u32(wireId(targeting->source));out.u16(uint16_t(targeting->skill));}));
        } else if(const auto *ended=server::factIf<server::ItemSpellEndFact>(&fact)) {
            if(ended->afterInventory && ended->skill>=0) {
                const auto own=host.read(*binding);
                delta.push_back(encodeServerPacket(ServerMessage::ItemSkillQuantity,[&](auto &out){
                    out.u8(0);out.u8(0);out.u32(wireId(own->actor.id));out.u16(uint16_t(ended->skill));out.u8(uint8_t(ended->quantity));out.u8(0);out.u8(0);
                }));
                peer.itemSkills[ended->skill]=ended->quantity;
            }
            delta.push_back(encodeServerPacket(ServerMessage::Reserved7C,[&](auto &out){out.u8(4);out.u32(wireId(ended->source));}));
            if(ended->afterInventory)
                delta.push_back(encodeServerPacket(ServerMessage::ItemTargeting,[&](auto &out){out.u8(255);out.u32(wireId(ended->source));out.u16(UINT16_MAX);}));
        } else if (const auto *service = server::factIf<server::NpcServiceFact>(&fact)) {
            delta.push_back(encodeServerPacket(ServerMessage::NpcServiceResult,[&](auto &out){out.u32(wireId(service->npc));out.u8(service->result);out.u8(service->state);}));
        } else if (const auto *ui = server::factIf<server::UiFact>(&fact)) {
            delta.push_back(encodeServerPacket(ui->kind==server::UiFact::Kind::Cinematic?ServerMessage::Cinematic:ServerMessage::UiAction,
                [&](auto &out){out.u8(ui->action);}));
        } else throw std::logic_error("Native event encoder is not implemented for this fact");
        for (auto &packet : delta) packets.push_back(std::move(packet));
    }
    if (!packets.empty()) sendGameBatch(std::move(packets));
}
}
