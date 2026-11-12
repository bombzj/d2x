#include "inventory_workflow.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace d2x {
namespace {
// Revalidate the whole remaining plan against the current observation before
// an irreversible Drop (or an earlier Take) can use an obsolete layout.
bool arrangementFits(const OnlineView &view,const OnlineInventoryView &inventory,
    const std::deque<OnlineItemCommand> &commands,const InventoryCleanupPolicy &policy,uint32_t destination) {
    struct Item {int mode,page,x,y,width,height;bool fixed,discardable;};
    std::map<uint32_t,Item> items;
    for(const auto &[id,wire]:view.world.items) {
        if(!onlineItemOwnedBy(wire,view.load.playerUnitId)) continue;
        const bool referenced=id==destination || std::any_of(commands.begin(),commands.end(),[&](const auto &c){return c.item==id || c.target==id;});
        if(!referenced && wire.mode!=4 && !(wire.mode==0 && wire.page==1)) continue;
        const auto decoded=inventory.items.find(id);
        if(!wire.positionKnown || decoded==inventory.items.end() || !decoded->second.decoded ||
            decoded->second.width<1 || decoded->second.height<1) return false;
        items.emplace(id,Item{wire.mode,wire.page,wire.x,wire.y,decoded->second.width,decoded->second.height,
            view.world.storage.kind==OnlineStorageKind::Cube && view.world.storage.source==id,
            (!decoded->second.quantity || *decoded->second.quantity==1) &&
            std::find(policy.discardableCodes.begin(),policy.discardableCodes.end(),wire.code)!=policy.discardableCodes.end()});
    }
    auto cursor=inventory.cursor;
    for(const auto &command:commands) {
        const auto it=items.find(command.item);if(it==items.end()) return false;
        auto &held=it->second;
        if(command.action==OnlineItemAction::Take) {
            if(cursor || held.fixed || held.mode!=0 || (held.page!=1 && held.page!=4) ||
                (held.page==4 && view.world.storage.kind!=OnlineStorageKind::Cube)) return false;
            held.mode=4;cursor=command.item;continue;
        }
        if(cursor!=command.item || held.mode!=4 || held.fixed) return false;
        if(command.action==OnlineItemAction::Drop) {
            if(!held.discardable || command.item==destination) return false;
            held.mode=3;cursor.reset();continue;
        }
        if(command.action!=OnlineItemAction::Place && command.action!=OnlineItemAction::Swap) return false;
        if(command.x+held.width>inventory.columns || command.y+held.height>inventory.rows) return false;
        std::optional<uint32_t> displaced;
        for(const auto &[id,other]:items) if(other.mode==0 && other.page==1) {
            if(command.x>=other.x+other.width || command.x+held.width<=other.x ||
                command.y>=other.y+other.height || command.y+held.height<=other.y) continue;
            if(displaced || other.fixed) return false;
            displaced=id;
        }
        if(command.action==OnlineItemAction::Swap) {
            if(displaced!=command.target) return false;
            items.at(command.target).mode=4;
        } else if(displaced) return false;
        held.mode=0;held.page=1;held.x=command.x;held.y=command.y;cursor=displaced;
    }
    const auto stored=items.find(destination);
    return !cursor && stored!=items.end() && stored->second.mode==0 && stored->second.page==1;
}
} // namespace
BackpackArrangement planBackpackArrangement(const OnlineView &view,const OnlineInventoryView &inventory,uint32_t movingItem,
    const InventoryCleanupPolicy &policy) {
    BackpackArrangement result;
    const auto moving=inventory.items.find(movingItem);
    const auto wire=view.world.items.find(movingItem);
    if(moving==inventory.items.end() || wire==view.world.items.end() || !moving->second.decoded ||
        !onlineItemOwnedBy(wire->second,view.load.playerUnitId) || !wire->second.positionKnown ||
        (wire->second.mode!=4 && !(wire->second.mode==0 && (wire->second.page==1 || wire->second.page==4)))) return result;
    if(inventory.cursor && inventory.cursor!=movingItem) return result;
    if(wire->second.mode==4 && inventory.cursor!=movingItem) return result;
    if(wire->second.mode==0 && wire->second.page==4 && view.world.storage.kind!=OnlineStorageKind::Cube) return result;
    const int columns=inventory.columns,rows=inventory.rows;
    if(columns<1 || rows<1 || columns>256 || rows>256) return result;
    struct Item {uint32_t id;int width,height;bool fixed,discardable;};
    std::vector<Item> items;std::vector<int> positions;
    std::vector<bool> occupied(size_t(columns)*rows);
    size_t area=0;
    for(const auto &[id,item]:view.world.items) {
        if(id==movingItem || !onlineItemOwnedBy(item,view.load.playerUnitId) || item.mode!=0 || item.page!=1) continue;
        const auto decoded=inventory.items.find(id);
        if(!item.positionKnown || decoded==inventory.items.end() || !decoded->second.decoded) return result;
        const int width=decoded->second.width,height=decoded->second.height;
        if(width<1 || height<1 || item.x+width>columns || item.y+height>rows) return result;
        for(int y=item.y;y<item.y+height;++y) for(int x=item.x;x<item.x+width;++x) {
            const size_t cell=size_t(y)*columns+x;if(occupied[cell]) return result;occupied[cell]=true;
        }
        items.push_back({id,width,height,view.world.storage.kind==OnlineStorageKind::Cube && view.world.storage.source==id,
            (!decoded->second.quantity || *decoded->second.quantity==1) &&
            std::find(policy.discardableCodes.begin(),policy.discardableCodes.end(),item.code)!=policy.discardableCodes.end()});
        positions.push_back(item.y*columns+item.x);area+=size_t(width)*height;
    }
    result.freeCells=occupied.size()-area;
    const int width=moving->second.width,height=moving->second.height;
    if(width<1 || height<1) return result;
    if(width>columns || height>rows) {
        result.result=BackpackArrangement::Result::InsufficientArea;return result;
    }
    if(view.world.storage.kind==OnlineStorageKind::Cube && view.world.storage.source==movingItem) return result;
    if(wire->second.mode==0 && wire->second.page==1) {result.result=BackpackArrangement::Result::Ready;return result;}
    // -1: Cursor, -2: discarded, -3: still in the cube. These are planning
    // values, never written back to the observed inventory.
    items.push_back({movingItem,width,height,false,false});positions.push_back(wire->second.mode==4?-1:-3);
    const size_t movingIndex=items.size()-1,emptyCursor=items.size();
    struct Node {std::vector<int> positions;size_t cursor,parent;OnlineItemCommand command;};
    const auto search=[&](bool discard) {
        BackpackArrangement found;found.freeCells=result.freeCells;
        size_t available=found.freeCells;
        if(discard) for(const auto &item:items) if(item.discardable && !item.fixed) available+=size_t(item.width)*item.height;
        if(size_t(width)*height>available) {found.result=BackpackArrangement::Result::InsufficientArea;return found;}
        std::vector<Node> nodes;nodes.push_back({positions,positions.back()==-1?movingIndex:emptyCursor,0,{}});
        std::set<std::vector<int>> visited;visited.insert(positions);
        size_t attempts=0;
        for(size_t head=0;head<nodes.size();++head) {
            const auto current=nodes[head];
            if(current.cursor==emptyCursor && current.positions[movingIndex]>=0) {
                for(size_t n=head;n;n=nodes[n].parent) found.commands.push_back(nodes[n].command);
                std::reverse(found.commands.begin(),found.commands.end());
                found.result=BackpackArrangement::Result::Ready;found.searched=nodes.size();return found;
            }
            const auto append=[&](std::vector<int> next,size_t cursor,OnlineItemCommand command) {
                if(cursor==emptyCursor && next[movingIndex]>=0) {
                    found.commands.push_back(command);
                    for(size_t n=head;n;n=nodes[n].parent) found.commands.push_back(nodes[n].command);
                    std::reverse(found.commands.begin(),found.commands.end());
                    found.result=BackpackArrangement::Result::Ready;found.searched=nodes.size();return true;
                }
                if(visited.insert(next).second) nodes.push_back({std::move(next),cursor,head,command});
                return false;
            };
            if(current.cursor==emptyCursor) {
                for(size_t i=0;i<items.size();++i) {
                    if(items[i].fixed || current.positions[i]==-2) continue;
                    // Without discard, the existing cursor-swap search starts
                    // by extracting the requested item. Cleanup may first
                    // take an explicitly disposable backpack item.
                    if(i!=movingIndex && (!discard || !items[i].discardable)) continue;
                    OnlineItemCommand command;command.action=OnlineItemAction::Take;command.item=items[i].id;
                    auto next=current.positions;next[i]=-1;if(append(std::move(next),i,command)) return found;
                }
            } else {
                const auto &held=items[current.cursor];
                // The fallback clears an allowed item immediately, rather
                // than enumerating permutations of interchangeable scrolls.
                // The lossless pass has already considered retaining it.
                if(discard && held.discardable && !held.fixed) {
                    OnlineItemCommand command;command.action=OnlineItemAction::Drop;command.item=held.id;
                    auto next=current.positions;next[current.cursor]=-2;
                    if(append(std::move(next),emptyCursor,command)) return found;
                    continue;
                }
                for(int y=0;y+held.height<=rows;++y) for(int x=0;x+held.width<=columns;++x) {
                    if(++attempts>200000 || nodes.size()>=4096) {
                        found.result=BackpackArrangement::Result::SearchLimit;found.searched=nodes.size();return found;
                    }
                    std::optional<size_t> displaced;bool blocked=false;
                    for(size_t i=0;i<items.size();++i) if(current.positions[i]>=0) {
                        const int at=current.positions[i],ix=at%columns,iy=at/columns;
                        if(x>=ix+items[i].width || x+held.width<=ix || y>=iy+items[i].height || y+held.height<=iy) continue;
                        if(displaced || items[i].fixed) {blocked=true;break;}
                        displaced=i;
                    }
                    if(blocked) continue;
                    OnlineItemCommand command;command.action=displaced?OnlineItemAction::Swap:OnlineItemAction::Place;
                    command.item=held.id;command.x=uint8_t(x);command.y=uint8_t(y);command.page=0;
                    auto next=current.positions;next[current.cursor]=y*columns+x;
                    if(displaced) {command.target=items[*displaced].id;next[*displaced]=-1;}
                    if(append(std::move(next),displaced?*displaced:emptyCursor,command)) return found;
                }
            }
            if(nodes.size()>=4096) {found.result=BackpackArrangement::Result::SearchLimit;found.searched=nodes.size();return found;}
        }
        found.result=BackpackArrangement::Result::NoPlan;found.searched=nodes.size();return found;
    };
    auto lossless=search(false);
    if(lossless.result==BackpackArrangement::Result::Ready || policy.discardableCodes.empty()) return lossless;
    auto cleanup=search(true);cleanup.searched+=lossless.searched;return cleanup;
}
void InventoryWorkflow::begin(const OnlineView &view) {
    if(active()) throw std::runtime_error("Inventory workflow already owns an unfinished job");
    *this={};state_=State::Running;game_=view.gameGeneration;connection_=view.connectionGeneration;player_=view.load.playerUnitId;
    if(!player_) fail("Inventory workflow needs an assigned player");
}
void InventoryWorkflow::store(uint32_t item,const OnlineView &view,InventoryCleanupPolicy policy) {
    begin(view);item_=item;kind_=Kind::Store;cleanup_=std::move(policy);
}
void InventoryWorkflow::combine(std::string container,std::string output,std::vector<std::string> ingredients,const OnlineView &view,InventoryCleanupPolicy policy) {
    begin(view);kind_=Kind::Combine;container_=std::move(container);output_=std::move(output);ingredients_=std::move(ingredients);
    if(container_.empty() || output_.empty() || ingredients_.empty()) fail("Incomplete inventory recipe request");
    cleanup_=std::move(policy);
    std::erase_if(cleanup_.discardableCodes,[&](const auto &code){return code==container_ || code==output_ ||
        std::find(ingredients_.begin(),ingredients_.end(),code)!=ingredients_.end();});
}
[[noreturn]] void InventoryWorkflow::fail(std::string reason) {state_=State::Failed;reason_=std::move(reason);throw std::runtime_error(reason_);}
void InventoryWorkflow::observe(const OnlineView &view) {
    if(!active()) return;
    if(view.gameGeneration!=game_ || view.connectionGeneration!=connection_ || view.load.playerUnitId!=player_)
        fail("Inventory workflow context changed; no operation replayed");
    if(!pending_) return;
    const auto &command=pending_->command;const auto &world=view.world;
    const auto item=world.items.find(command.item),target=world.items.find(command.target);
    const auto ours=[&](auto it){return it!=world.items.end() && onlineItemOwnedBy(it->second,player_);};
    const auto backpack=[&](auto it){return ours(it) && it->second.positionKnown && it->second.mode==0 && it->second.page==1 &&
        it->second.x==command.x && it->second.y==command.y;};
    bool ready=false;
    switch(command.action) {
    case OnlineItemAction::Take:ready=ours(item) && item->second.mode==4;break;
    case OnlineItemAction::Place:ready=backpack(item) && !onlineHasCursorItem(world.items,player_);break;
    case OnlineItemAction::Swap:ready=backpack(item) && ours(target) && target->second.mode==4;break;
    case OnlineItemAction::Drop:ready=item!=world.items.end() && (item->second.mode==3 || item->second.mode==5) && !ours(item) &&
        !onlineHasCursorItem(world.items,player_);break;
    case OnlineItemAction::CubeStore:ready=ours(item) && item->second.mode==0 && item->second.page==4;break;
    case OnlineItemAction::CubeOpen:ready=world.storage.kind==OnlineStorageKind::Cube && world.storage.source==command.item;break;
    case OnlineItemAction::StorageClose:ready=world.storage.kind==OnlineStorageKind::None && world.storage.requested==OnlineStorageKind::None;break;
    case OnlineItemAction::Transmute:
        ready=std::any_of(world.items.begin(),world.items.end(),[&](const auto &entry){return onlineItemOwnedBy(entry.second,player_) && entry.second.code==output_;}) &&
            std::none_of(consumed_.begin(),consumed_.end(),[&](uint32_t id){const auto it=world.items.find(id);return ours(it);});break;
    default:fail("Unsupported inventory workflow operation");
    }
    if(ready) {
        if(pending_->placement) arrangement_.pop_front();
        pending_.reset();++completed_;reason_="operation-observed";return;
    }
    if(world.itemRequest && world.itemRequest->sequence==pending_->sequence &&
        (world.itemRequest->state==OnlineItemRequest::State::Rejected || world.itemRequest->state==OnlineItemRequest::State::Interrupted ||
         world.itemRequest->state==OnlineItemRequest::State::TimedOut)) fail("Inventory operation failed; scene retained, no replay");
    if(Clock::now()>=pending_->deadline) fail("Inventory operation has no verified result; scene retained, no replay");
}
void InventoryWorkflow::arrange(const OnlineView &view,const OnlineInventoryView &inventory,uint32_t item) {
    const auto plan=planBackpackArrangement(view,inventory,item,cleanup_);
    if(plan.result!=BackpackArrangement::Result::Ready) {
        const char *why=plan.result==BackpackArrangement::Result::InsufficientArea?"insufficient backpack area":
            plan.result==BackpackArrangement::Result::UnknownLayout?"unverified inventory layout":
            plan.result==BackpackArrangement::Result::SearchLimit?"backpack arrangement search budget exhausted":"no cursor-swap arrangement found";
        fail(std::string(why)+"; item retained in its observed container/Cursor");
    }
    arrangement_.assign(plan.commands.begin(),plan.commands.end());
    arrangingItem_=item;
}
std::optional<OnlineItemCommand> InventoryWorkflow::next(const OnlineView &view,const OnlineInventoryView &inventory) {
    if(state_==State::Failed) throw std::runtime_error(reason_);
    observe(view);if(!active() || pending_) return {};
    proposed_.reset();proposedPlacement_=false;
    const auto &world=view.world;
    const auto owned=[&](const OnlineItem &item){return onlineItemOwnedBy(item,player_);};
    std::optional<uint32_t> cursor;
    for(const auto &[id,item]:world.items) if(owned(item) && item.mode==4) {
        if(cursor) fail("Multiple owned Cursor assignments");
        cursor=id;
    }
    if(inventory.gameGeneration!=game_ || inventory.cursor!=cursor) {reason_="waiting-for-inventory-projection";return {};}
    const auto byCode=[&](const std::string &code)->const OnlineItem * {
        const OnlineItem *result=nullptr;
        for(const auto &[id,item]:world.items) if(owned(item) && item.code==code) {
            (void)id;
            if(!item.positionKnown || !(item.mode==4 || (item.mode==0 && (item.page==1 || item.page==4))))
                fail("Recipe item exists outside accessible backpack/cube/Cursor; no duplicate acquisition");
            if(result) fail("Recipe has ambiguous duplicate owned ingredients/output");
            result=&item;
        }
        return result;
    };
    const auto propose=[&](OnlineItemCommand command)->std::optional<OnlineItemCommand> {
        if(command.item) {
            const auto it=world.items.find(command.item);if(it==world.items.end() || !owned(it->second)) fail("Workflow item ownership changed");
            command.itemRevision=it->second.revision;
        }
        if(command.target) {
            const auto it=world.items.find(command.target);if(it==world.items.end() || !owned(it->second)) fail("Workflow target ownership changed");
            command.targetRevision=it->second.revision;
        }
        proposed_=command;return command;
    };
    const auto placement=[&]()->std::optional<OnlineItemCommand> {
        if(!arrangementFits(view,inventory,arrangement_,cleanup_,arrangingItem_))
            fail("Remaining inventory arrangement no longer fits; no stale operation submitted");
        const auto command=arrangement_.front();
        if(command.action==OnlineItemAction::Drop && kind_==Kind::Store && command.item==item_)
            fail("The requested storage item cannot be discarded");
        proposedPlacement_=true;
        reason_=command.action==OnlineItemAction::Drop?"discarding-approved-item":"arranging-backpack";
        return propose(command);
    };
    if(!arrangement_.empty()) return placement();
    if(kind_==Kind::Store) {
        const auto it=world.items.find(item_);
        if(it==world.items.end() || !owned(it->second)) fail("Stored item ownership is unavailable");
        if(it->second.mode==0 && it->second.page==1 && !inventory.cursor) {state_=State::Done;reason_="stored";return {};}
        if(inventory.cursor!=item_) fail("Store job requires its actual Cursor item");
        arrange(view,inventory,item_);return placement();
    }
    const auto output=byCode(output_);
    if(output && output->mode==0 && output->page==1 && !inventory.cursor) {
        if(world.storage.kind!=OnlineStorageKind::None || world.storage.requested!=OnlineStorageKind::None) {
            OnlineItemCommand command;command.action=OnlineItemAction::StorageClose;return propose(command);
        }
        state_=State::Done;reason_="combined-and-stored";return {};
    }
    if(output && inventory.cursor) {arrange(view,inventory,*inventory.cursor);return placement();}
    const auto cube=byCode(container_);
    if(!cube || cube->mode!=0 || cube->page!=1) fail("Combine job needs its actual backpack cube");
    if(inventory.cursor) {
        const auto it=world.items.find(*inventory.cursor);
        if(it==world.items.end() || !owned(it->second)) fail("Unverified Cursor ownership");
        const bool ingredient=std::find(ingredients_.begin(),ingredients_.end(),it->second.code)!=ingredients_.end();
        if(!output && ingredient && world.storage.kind==OnlineStorageKind::Cube && world.storage.source==cube->id) {
            OnlineItemCommand command;command.action=OnlineItemAction::CubeStore;command.item=it->first;command.target=cube->id;return propose(command);
        }
        arrange(view,inventory,it->first);return placement();
    }
    if(world.storage.kind!=OnlineStorageKind::Cube || world.storage.source!=cube->id) {
        OnlineItemCommand command;
        if(world.storage.kind!=OnlineStorageKind::None || world.storage.requested!=OnlineStorageKind::None) command.action=OnlineItemAction::StorageClose;
        else {command.action=OnlineItemAction::CubeOpen;command.item=cube->id;}
        return propose(command);
    }
    if(output) {
        if(output->mode!=0 || output->page!=4) fail("Unexpected recipe output position");
        // Reserve the complete cleanup/extraction/placement sequence before
        // touching inventory. A failed query never discards anything.
        arrange(view,inventory,output->id);
        return placement();
    }
    for(const auto &[id,item]:world.items) if(owned(item) && item.mode==0 && item.page==4 &&
        std::find(ingredients_.begin(),ingredients_.end(),item.code)==ingredients_.end()) {
        // Clear unrelated contents through the same verified packing job;
        // they must never become accidental transmutation ingredients.
        arrange(view,inventory,id);return placement();
    }
    consumed_.clear();
    for(const auto &code:ingredients_) {
        const auto ingredient=byCode(code);if(!ingredient) fail("Missing actual recipe ingredient: "+code);
        if(ingredient->mode==0 && ingredient->page==4) {consumed_.push_back(ingredient->id);continue;}
        if(ingredient->mode!=0 || ingredient->page!=1) fail("Recipe ingredient is not in an accessible container");
        OnlineItemCommand command;command.action=OnlineItemAction::Take;command.item=ingredient->id;return propose(command);
    }
    OnlineItemCommand command;command.action=OnlineItemAction::Transmute;command.target=cube->id;return propose(command);
}
void InventoryWorkflow::submitted(const std::optional<OnlineItemRequest> &receipt,uint64_t previousSequence) {
    if(!active() || !proposed_ || pending_ || !receipt || receipt->sequence<=previousSequence)
        fail("Inventory workflow has no new normal client submission receipt; no operation replayed");
    const auto &sent=receipt->command;const auto &wanted=*proposed_;
    if(sent.action!=wanted.action || sent.item!=wanted.item || sent.target!=wanted.target ||
        (wanted.itemRevision && sent.itemRevision!=wanted.itemRevision) ||
        (wanted.targetRevision && sent.targetRevision!=wanted.targetRevision) ||
        ((wanted.action==OnlineItemAction::Place || wanted.action==OnlineItemAction::Swap) &&
         (sent.x!=wanted.x || sent.y!=wanted.y || sent.page!=wanted.page)))
        fail("Inventory workflow submission identity changed; scene retained, no operation replayed");
    pending_=Pending{sent,receipt->sequence,Clock::now()+std::chrono::seconds(20),proposedPlacement_};
    proposed_.reset();reason_="waiting-for-item-result";
}
} // namespace d2x
