#include "native_realm_service.hpp"
#include "hosting/native_item_wire.hpp"
#include "hosting/native_character_wire.hpp"
#include "hosting/native_quest_wire.hpp"
#include "hosting/native_combat_wire.hpp"
#include "gameplay/areas/waypoint.hpp"
#include <limits>
#include <cmath>

namespace d2x::hosting {
void NativeRealmService::sendGameBatch(std::vector<Bytes> packets) {
    size_t size = 0;
    for (const auto &packet : packets) {
        validateServerPacket(packet);
        if (packet.size() > std::numeric_limits<size_t>::max() - size) throw std::runtime_error("Native output batch overflow");
        size += packet.size();
    }
    if (!size) throw std::logic_error("Empty native event batch");
    Bytes bytes; bytes.reserve(size);
    for (const auto &packet : packets) bytes.insert(bytes.end(), packet.begin(), packet.end());
    // One stream write accepts the entire fact or fails the connection. The
    // existing native stream decoder splits the concatenated original packets.
    gameOutput(std::move(bytes));
    for (const auto &packet : packets) ++counters.gameResponses[packet[0]];
}
void NativeRealmService::publishEvents() {
    if (binding) shared.publishEvents(binding->game);
}
void NativeRealmService::receiveEvent(const server::EventBatch &batch) {
    std::vector<Bytes> packets;
    const auto visibleActor=[&](EntityId actor,uint8_t type) {
        if(type==1) return peer.monsters.contains(actor) || peer.npcs.contains(actor);
        if(type==2) return peer.objects.contains(actor) || peer.portals.contains(actor);
        return type==0 && (actor==host.read(*binding)->actor.id || std::any_of(peer.visible.begin(),peer.visible.end(),[&](const auto &entry){return entry.second.actor==actor;}));
    };
    for (const auto &fact : batch.facts) {
        if (const auto *travel = std::get_if<server::TravelFact>(&fact)) {
            if (!packets.empty()) { sendGameBatch(std::move(packets)); packets.clear(); }
            changeArea(*travel); continue;
        }
        std::vector<Bytes> delta;
        if (const auto *list = std::get_if<server::HirelingListFact>(&fact)) {
            delta.push_back(encodeServerPacket(ServerMessage::HirelingListReset,[](auto &){}));
            for(const auto &[name,seed]:list->offers)
                delta.push_back(encodeServerPacket(ServerMessage::HirelingOffer,[&](auto &out){out.u16(name);out.u32(seed);}));
        }
        else if (const auto *life = std::get_if<server::LifeFact>(&fact)) delta = nativeLife(*content, *life);
        else if (const auto *mana = std::get_if<server::ManaFact>(&fact)) delta = nativeMana(*content, *mana);
        else if (const auto *position = std::get_if<server::RepositionFact>(&fact)) {
            if (position->actor != host.read(*binding)->actor.id &&
                std::none_of(peer.visible.begin(), peer.visible.end(), [&](const auto &entry) { return entry.second.actor == position->actor; })) continue;
            delta.push_back(nativeReposition(*position, shared.terrain.at(binding->game).at(position->area).origin));
            peer.lastMotion.clear();
            for (auto &[id, player] : peer.visible) { (void)id; if (player.actor == position->actor) player.motion.clear(); }
        }
        else if (const auto *attack = std::get_if<server::AttackFact>(&fact)) {
            if (attack->actorType == 1 && !peer.monsters.contains(attack->actor)) continue;
            if (attack->targetType == 0 && attack->target != host.read(*binding)->actor.id &&
                std::none_of(peer.visible.begin(), peer.visible.end(), [&](const auto &entry) { return entry.second.actor == attack->target; })) continue;
            if (attack->targetType == 1 && attack->target && !visibleActor(attack->target,1)) continue;
            if (attack->actorType == 0 && attack->actor != host.read(*binding)->actor.id &&
                std::none_of(peer.visible.begin(), peer.visible.end(), [&](const auto &entry) { return entry.second.actor == attack->actor; })) continue;
            // PlrMsg::sub_6FC81D20 omits ordinary owner skill actions. Its client
            // predicts the submitted cast; other visible clients receive 4C/4D.
            // Charge's arrival and forced avoidance synchronize the owner too.
            if (attack->forced || attack->actorType != 0 || attack->actor != host.read(*binding)->actor.id)
                delta = nativeAttack(*attack, shared.terrain.at(binding->game).at(attack->area).origin);
            peer.lastMotion.clear();
            for (auto &[id, player] : peer.visible) { (void)id; if (player.actor == attack->actor) player.motion.clear(); }
            if (auto found = peer.monsters.find(attack->actor); found != peer.monsters.end()) found->second.motion.clear();
        } else if (const auto *hit = std::get_if<server::HitFact>(&fact)) {
            if (hit->type == 1 && !peer.monsters.contains(hit->target)) continue;
            if (hit->type == 0 && hit->target != host.read(*binding)->actor.id &&
                std::none_of(peer.visible.begin(), peer.visible.end(), [&](const auto &entry) { return entry.second.actor == hit->target; })) continue;
            delta = nativeHit(*hit, shared.terrain.at(binding->game).at(hit->area).origin);
            if (auto found = peer.monsters.find(hit->target); found != peer.monsters.end() && hit->killed) found->second.motion.clear();
        } else if (const auto *inventory = std::get_if<server::InventoryFact>(&fact)) {
            delta = nativeInventoryDelta(*content, *inventory);
            for (const auto &change : inventory->changes) if (change.kind == ItemChangeKind::Created) peer.groundItems.erase(change.item);
        }
        else if (const auto *drop = std::get_if<server::GroundDropFact>(&fact)) {
            const auto &at = std::get<GroundLocation>(drop->item.location);
            delta.push_back(nativeGroundItem(*content, drop->item,
                shared.terrain.at(binding->game).at(at.region).origin, GroundItemAction::Drop));
            peer.groundItems.insert_or_assign(drop->item.id, drop->item.revision);
        }
        else if(const auto *sound=std::get_if<server::SoundFact>(&fact)) {
            if(!visibleActor(sound->actor,sound->type)) continue;
            delta.push_back(encodeServerPacket(ServerMessage::UnitSound,[&](auto &out){out.u8(sound->type);out.u32(uint32_t(sound->actor.value));out.u16(sound->sound);}));
        }
        else if(const auto *overlay=std::get_if<server::OverlayFact>(&fact)) {
            if(!visibleActor(overlay->actor,overlay->type)) continue;
            delta.push_back(encodeServerPacket(ServerMessage::Overlay,[&](auto &out){out.u8(overlay->type);out.u32(uint32_t(overlay->actor.value));out.u16(uint16_t(overlay->overlay));}));
        }
        else if(const auto *restored=std::get_if<server::GroundRestoredFact>(&fact)) {
            // ItemMode only sends 3E to an owner. An unowned ground item
            // silently restores; its next admission/pickup contains the value.
            if(auto old=peer.groundItems.find(restored->item);old!=peer.groundItems.end()) old->second=restored->revision;
        }
        else if(const auto *trigger=std::get_if<server::ItemSkillFact>(&fact)) {
            if(!visibleActor(trigger->owner,0)) continue;
            const auto origin=shared.terrain.at(binding->game).at(trigger->area).origin;
            if(trigger->target) delta.push_back(encodeServerPacket(ServerMessage::CastUnitAlternate,[&](auto &out){
                out.u8(0);out.u32(uint32_t(trigger->owner.value));out.u16(uint16_t(trigger->skill));out.u8(uint8_t(trigger->rank));
                out.u8(trigger->targetType);out.u32(uint32_t(trigger->target.value));out.u16(trigger->flags);
            }));
            else delta.push_back(encodeServerPacket(ServerMessage::CastPointAlternate,[&](auto &out){
                out.u8(0);out.u32(uint32_t(trigger->owner.value));out.u32(uint32_t(trigger->skill));out.u8(uint8_t(trigger->rank));
                out.u16(uint16_t(std::floor(trigger->position.x+origin.x)));out.u16(uint16_t(std::floor(trigger->position.y+origin.y)));out.u16(trigger->flags);
            }));
        }
        else if (const auto *pulse = std::get_if<server::SkillPulseFact>(&fact)) {
            if(!visibleActor(pulse->owner,pulse->ownerType) || !visibleActor(pulse->target,pulse->targetType)) continue;
            delta.push_back(encodeServerPacket(ServerMessage::SkillEvent, [&](auto &out) {
                out.u8(0);out.u16(uint16_t(pulse->skill));out.u16(uint16_t(pulse->rank));
                out.u8(pulse->ownerType);out.u32(uint32_t(pulse->owner.value));out.u8(pulse->targetType);out.u32(uint32_t(pulse->target.value));out.u32(0);out.u32(0);
            }));
        }
        else if (const auto *missile = std::get_if<server::MissileFact>(&fact)) {
            if(!visibleActor(missile->owner,missile->ownerType)) continue;
            const auto origin = shared.terrain.at(binding->game).at(missile->area).origin;
            delta.push_back(encodeServerPacket(ServerMessage::Missile, [&](auto &out) {
                out.u32(0); out.u16(uint16_t(missile->definition));
                out.u32(uint32_t(std::floor(missile->position.x + origin.x))); out.u32(uint32_t(std::floor(missile->position.y + origin.y)));
                const bool stationary=missile->destination.x==0 && missile->destination.y==0;
                out.u32(stationary?0:uint32_t(std::floor(missile->destination.x + origin.x))); out.u32(stationary?0:uint32_t(std::floor(missile->destination.y + origin.y)));
                out.u16(uint16_t(missile->frame)); out.u8(missile->ownerType); out.u32(uint32_t(missile->owner.value)); out.u8(uint8_t(missile->rank)); out.u8(missile->pierce);
            }));
        }
        else if (const auto *state = std::get_if<server::StateFact>(&fact)) {
            if (state->state < 0 || state->state > 255) throw std::runtime_error("State exceeds native capacity");
            if (state->type == 1) {
                const auto entry = peer.monsters.find(state->actor);
                if (entry == peer.monsters.end() && !peer.npcs.contains(state->actor)) continue;
                if(peer.npcs.contains(state->actor)) {if(state->enabled) peer.npcStates[state->actor].insert(state->state);else peer.npcStates[state->actor].erase(state->state);}
                if(entry!=peer.monsters.end()) {if (state->enabled) entry->second.states.insert(state->state);else entry->second.states.erase(state->state);}
            }
            if(state->type==0 && state->actor!=host.read(*binding)->actor.id && std::none_of(peer.visible.begin(),peer.visible.end(),[&](const auto &entry){return entry.second.actor==state->actor;})) continue;
            delta.push_back(nativeState(*content,*state));
        }
        else if (const auto *quest = std::get_if<server::QuestFact>(&fact)) delta=nativeQuestUpdate(*content,*quest);
        else if (const auto *removed=std::get_if<server::GroundRemoveFact>(&fact)) {
            if(peer.groundItems.erase(removed->item)) delta.push_back(encodeServerPacket(ServerMessage::RemoveUnit,[&](auto &out){out.u8(4);out.u32(uint32_t(removed->item.value));}));
        }
        else if (const auto *character = std::get_if<server::CharacterFact>(&fact)) delta = nativeCharacterDelta(*content, *character);
        else if (const auto *chat = std::get_if<server::ChatFact>(&fact)) {
            if (std::find(chat->recipients.begin(), chat->recipients.end(), binding->player) == chat->recipients.end()) continue;
            delta.push_back(encodeServerPacket(ServerMessage::Chat, [&](auto &out) {
                out.u8(chat->type); out.u8(chat->language); out.u8(chat->unitType); out.u32(uint32_t(chat->actor.value)); out.u8(0); out.u8(chat->nameColor);
                out.string(chat->name); out.string(chat->text);
            }));
        } else if (const auto *relation=std::get_if<server::ChatRelationFact>(&fact)) {
            const auto local=host.read(*binding)->actor.id;
            delta.push_back(encodeServerPacket(ServerMessage::PlayerRelationFlags,[&](auto &out){out.u32(uint32_t(relation->from.value));out.u32(uint32_t(relation->to.value));out.u16(relation->flags);}));
            if(local==relation->from || local==relation->to)
                delta.push_back(encodeServerPacket(ServerMessage::PartyMember,[&](auto &out){
                    out.u32(uint32_t((local==relation->from?relation->to:relation->from).value));out.u16(UINT16_MAX);
                    out.u16(local==relation->from?relation->toLevel:relation->fromLevel);out.u16(local==relation->from?relation->flags:relation->reverse);out.u16(0);
                }));
        } else if (const auto *trade = std::get_if<server::TradeFact>(&fact)) {
            delta.push_back(encodeServerPacket(ServerMessage::UiAction,[&](auto &out){out.u8(trade->action);}));
            if(trade->partner) delta.push_back(encodeServerPacket(ServerMessage::TradePeer,[&](auto &out) {
                std::array<uint8_t,16> name{};
                if(trade->name.empty() || trade->name.size()>15) throw std::logic_error("Invalid trade peer name");
                std::copy(trade->name.begin(),trade->name.end(),name.begin());out.append(name);out.u32(uint32_t(trade->partner.value));
            }));
            if(trade->gold) for(const auto side : {uint8_t(1),uint8_t(0)})
                delta.push_back(encodeServerPacket(ServerMessage::TradeGold,[&](auto &out){out.u8(side);out.u32(side?trade->gold->first:trade->gold->second);}));
        } else if(const auto *items=std::get_if<server::TradeItemsFact>(&fact)) {
            delta=nativeTradeItems(*content,*items,host.read(*binding)->actor.id);
        } else if(const auto *message=std::get_if<server::PlayerMessageFact>(&fact)) {
            delta.push_back(encodeServerPacket(ServerMessage::Reserved5A,[&](auto &out){
                out.u8(message->type);out.u8(0);out.u32(0);out.u8(0); std::array<uint8_t,32> names{};
                if(message->name.size()>15) throw std::logic_error("Invalid player message name");
                std::copy(message->name.begin(),message->name.end(),names.begin());out.append(names);
            }));
        } else if (const auto *npc = std::get_if<server::NpcMessagesFact>(&fact)) {
            delta.push_back(encodeServerPacket(ServerMessage::NpcMessages,[&](auto &out) {
                out.u8(npc->type); out.u32(uint32_t(npc->npc.value)); out.u8(uint8_t(npc->messages.size())); out.u8(0);
                for (size_t i=0;i<8;++i) { const auto message=i<npc->messages.size()?npc->messages[i]:server::NpcMessage{}; out.u8(message.menu); out.u8(0); out.u16(message.text); }
            }));
        } else if (const auto *merchant = std::get_if<server::MerchantFact>(&fact)) {
            if (merchant->refreshShop) peer.shopItems.clear();
            delta.push_back(encodeServerPacket(ServerMessage::MerchantResult,[&](auto &out) {out.u8(merchant->operation);out.u8(merchant->result);out.u32(0);out.u32(uint32_t(merchant->item.value));out.u32(merchant->gold);}));
        } else if (const auto *waypoint=std::get_if<server::WaypointFact>(&fact)) {
            std::array<uint16_t,8> history{}; history[0]=0x102; history[1]=1; const auto &levels=content->tables.at("levels");
            for(const auto region:waypoint->unlocked) for(size_t row=0;row<levels.rows().size();++row) if(levels.number(row,"Id")==int(region)) {
                const auto index=levels.number(row,"Waypoint").value_or(255); if(nativeWaypointIndex(index)) history[size_t(1+index/16)]|=uint16_t(1u<<(index%16));
            }
            delta.push_back(encodeServerPacket(ServerMessage::Waypoints,[&](auto &out){out.u32(uint32_t(waypoint->source.value));for(const auto word:history)out.u16(word);}));
        } else if (const auto *targeting=std::get_if<server::ItemTargetingFact>(&fact)) {
            delta.push_back(encodeServerPacket(ServerMessage::ItemTargeting,[&](auto &out){out.u8(uint8_t(targeting->cursor));out.u32(uint32_t(targeting->source.value));out.u16(uint16_t(targeting->skill));}));
        } else if (const auto *service = std::get_if<server::NpcServiceFact>(&fact)) {
            delta.push_back(encodeServerPacket(ServerMessage::NpcServiceResult,[&](auto &out){out.u32(uint32_t(service->npc.value));out.u8(service->result);out.u8(service->state);}));
        } else if (const auto *ui = std::get_if<server::UiFact>(&fact)) {
            delta.push_back(encodeServerPacket(ServerMessage::UiAction,[&](auto &out){out.u8(ui->action);}));
        } else throw std::logic_error("Native event encoder is not implemented for this fact");
        for (auto &packet : delta) packets.push_back(std::move(packet));
    }
    if (!packets.empty()) sendGameBatch(std::move(packets));
}
}
