#include "cube.hpp"
#include "content/classic_data.hpp"
#include "gameplay/items/cube_records.hpp"
#include "server/systems/crafting/system.hpp"
#include <set>
#include <stdexcept>

namespace d2x {
void preparePd2Cube(const ClassicData &,server::crafting::Prepared &result,const Pd2CubeStep &step) {
    // Core 0x365790: mode 2 runs CubeMain again on the actual outputs.
    // Prepare the entire chain against a private candidate so admission or
    // an unsupported later program cannot consume the corruption material.
    auto candidate=result.source;
    auto random=candidate.pending.seed;
    std::set<EntityId> touched;
    bool complete=false;
    for(unsigned iteration=0;iteration<64;++iteration) {
        server::crafting::Prepared stage;stage.source=candidate;
        const auto *recipe=step(stage,random,iteration!=0);
        if(!recipe) {complete=true;break;}
        if(stage.portal) {
            if(iteration) throw std::runtime_error("PD2 recursive CubeMain portal output is unavailable");
            result=std::move(stage);return;
        }
        for(const auto handle:stage.consumed) {candidate.character.inventory.items.erase(handle.id);touched.insert(handle.id);}
        for(auto &output:stage.outputs) {
            touched.insert(output.item.id);
            candidate.character.inventory.items.insert_or_assign(output.item.id,std::move(output.item));
        }
        if(stage.character) {candidate.character.player=*stage.character;result.character=*stage.character;}
        if(stage.pd2Random) {candidate.pd2Random=*stage.pd2Random;result.pd2Random=stage.pd2Random;}
        if(recipe->pd2Continuation!=2) {complete=true;break;}
    }
    if(!complete) throw std::runtime_error("PD2 recursive CubeMain recipe does not converge");
    for(const auto id:touched) {
        const auto before=result.source.character.inventory.items.find(id);
        const auto after=candidate.character.inventory.items.find(id);
        if(after==candidate.character.inventory.items.end()) {
            if(before!=result.source.character.inventory.items.end()) result.consumed.push_back(before->second.handle());
        } else result.outputs.push_back({after->second,before==result.source.character.inventory.items.end()?
            std::nullopt:std::optional{before->second.handle()},candidate.character.containers.cube});
    }
}
}
