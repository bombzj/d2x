#include "formula_data.hpp"
#include "description_data.hpp"
#include "skill_eligibility.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "content/classic_data.hpp"
#include "content/pd2/skill_data.hpp"
#include <cctype>
#include <stdexcept>
#include <set>
namespace d2x {
std::shared_ptr<const SkillFormulaCatalog> prepareSkillFormulaCatalog(const ClassicData &data, std::span<const uint8_t> missileRecords) {
    if(!data.skills.descriptions) throw std::runtime_error("Missing prepared native calculation sources");
    const auto &sources=*data.skills.descriptions;
    std::map<std::string,int16_t,std::less<>> eventIds;
    const auto &events=data.tables.at("events");
    for(size_t row=0;row<events.rows().size();++row) {
        const auto name=events.value(row,"event");
        if(name.empty() || row>INT16_MAX || !eventIds.emplace(std::string(name),int16_t(row)).second)
            throw std::runtime_error("Invalid native skill event linker identity");
    }
    auto result=std::make_shared<SkillFormulaCatalog>();
    result->skillShrineState=data.states.at("shrine_skill").definition.id;
    result->corpseNoDrawState=data.states.at("corpse_nodraw").definition.id;
    result->corpseNoSelectState=data.states.at("corpse_noselect").definition.id;
    result->infernoState=data.states.at("inferno").definition.id;
    for(const auto &character:data.characters) if(character.sourceRow==0)
        result->maximumSkillRank=int(data.experienceByClass.at(character.name).size())-1;
    if(result->maximumSkillRank<1 || result->maximumSkillRank>255) throw std::runtime_error("Missing native skill maximum from Experience class zero");
    std::map<std::string,uint8_t,std::less<>> elementIds;
    const auto &elements=data.tables.at("elemtypes");
    for(size_t row=0;row<elements.rows().size();++row) {
        const auto name=elements.value(row,"code");if(name.empty()) continue;
        if(row>UINT8_MAX || !elementIds.emplace(std::string(name),uint8_t(row)).second) throw std::runtime_error("Invalid native element linker identity");
    }
    const auto copy=[&](const SkillDescriptionSource &source, bool bindElement = true) {
        SkillFormulaRecord record;
        record.id=source.id;record.numbers=source.numbers;record.formulas=source.formulas;record.references=source.references;
        record.numbers.emplace("hasdescription",int(source.hasDescription));
        const auto element=source.reference("etype");
        // Missiles.txt uses spaces for some absent element links; these are
        // element zero in the original compiled table, just like empty cells.
        if(bindElement && element.find_first_not_of(" \t\r\n")!=std::string_view::npos) {
            const auto found=elementIds.find(element);
            if(found==elementIds.end()) throw std::runtime_error("Missing native element linker record: "+std::string(element)+
                " for formula source "+std::to_string(source.id));
            record.elementType=found->second;
        }
        return record;
    };
    result->skillIds=sources.skillIds;
    const auto &types=data.tables.at("itemtypes");
    std::map<std::string,uint16_t,std::less<>> typeIds;
    for(size_t row=0;row<types.rows().size();++row) {
        const auto code=types.value(row,"Code");if(code.empty()) continue;
        if(row>UINT16_MAX || !typeIds.emplace(std::string(code),uint16_t(row)).second) throw std::runtime_error("Invalid native item type layer");
        result->itemTypes.emplace(uint16_t(row),std::string(code));
    }
    const auto &missiles=data.tables.at("missiles");
    for(size_t row=0;row<missiles.rows().size();++row) {
        const auto id=missiles.number(row,"Id");if(!id) continue;
        std::string name(missiles.value(row,"Missile"));
        for(auto &letter:name) letter=char(std::tolower(static_cast<unsigned char>(letter)));
        const auto found=sources.missiles.find(name);
        if(found==sources.missiles.end()) throw std::runtime_error("Missing native missile formula record: "+name);
        auto record=copy(found->second);record.id=*id;
        if(const auto name=record.reference("progoverlay");!name.empty()) {
            const auto &overlays=data.tables.at("overlay");
            for(size_t overlay=0;overlay<overlays.rows().size();++overlay)
                if(overlays.value(overlay,"overlay")==name) {record.procOverlay=int(overlay);break;}
            if(record.procOverlay<0) {
                // Current PD2 BIN explicitly compiles four stale TXT names to
                // FFFF. Accept only that verified native absence, not a guessed art.
                constexpr size_t stride=0x1A4, offset=0x36;
                const auto word=[&](size_t at) { return uint16_t(missileRecords[at]|uint16_t(missileRecords[at+1])<<8); };
                if(missileRecords.size()!=4+missiles.rows().size()*stride ||
                    (uint32_t(word(0))|uint32_t(word(2))<<16)!=missiles.rows().size() ||
                    word(4+row*stride+offset)!=UINT16_MAX)
                    throw std::runtime_error("Missing native missile program overlay: "+std::string(name));
            }
        }
        if(!result->missileIds.emplace(name,*id).second || !result->missiles.emplace(*id,std::move(record)).second)
            throw std::runtime_error("Duplicate native missile formula identity");
    }
    for(const auto &[id,source]:sources.skills) {
        const auto action=source.number("srvdofunc").value_or(0);
        const bool equipped=data.pd2Host && source.number("aura").value_or(0) &&
            (action==18 || action==23 || action==29 || action==54 || action==124);
        const auto *display=data.skills.find(id);
        const bool playerProgram=display && (!display->classCode.empty() || display->itemSkill ||
            source.number("general").value_or(0) || action==30);
        const bool prepared = !data.pd2Host || playerProgram || equipped;
        auto record=copy(source,prepared);
        // State links belong to the compiled skill record, including skills
        // used only by monsters. Program admission must not erase them.
        const auto sourceState=[&](std::string_view field) {
            const auto found=data.states.find(source.reference(field));
            return found==data.states.end()?-1:found->second.definition.id;
        };
        record.auraState=sourceState("aurastate");record.auraTargetState=sourceState("auratargetstate");
        if(data.pd2Host) record.useThrowMastery=pd2SkillUsesThrowMastery(data,source.reference("itypea1"));
        // Keep references for unsupported programs without granting class
        // availability. Equipment-only aura records still own real metadata.
        if(!prepared) {
            result->skills.emplace(id,std::move(record));
            continue;
        }
        const auto &skills=data.tables.at("skills");
        for(size_t row=0;row<skills.rows().size();++row) if(skills.number(row,"Id")==id) {
            std::set<std::string,std::less<>> visited;
            const auto thrown=[&](auto &&self,std::string_view code)->bool {
                if(code.empty()) return false;
                if(code=="thro") return true;
                if(!visited.emplace(code).second) return false;
                for(size_t type=0;type<types.rows().size();++type) if(types.value(type,"Code")==code)
                    return self(self,types.value(type,"Equiv1")) || self(self,types.value(type,"Equiv2"));
                throw std::runtime_error("Missing native skill weapon type");
            };
            if(!data.pd2Host) record.useThrowMastery=skills.value(row,"range")=="rng" && thrown(thrown,skills.value(row,"itypea1"));
            for(size_t slot=0;slot<record.unitEvents.size();++slot) {
                const auto suffix=std::to_string(slot+1);
                const auto name=skills.value(row,"auraevent"+suffix);
                auto &binding=record.unitEvents[slot];
                if(name.find_first_not_of(" \t\r\n")!=std::string_view::npos) {
                    const auto linked=eventIds.find(name);
                    if(linked==eventIds.end()) throw std::runtime_error("Missing native skill event: "+std::string(name));
                    binding.event=linked->second;
                }
                const int function=skills.number(row,"auraeventfunc"+suffix).value_or(0);
                if(function<INT16_MIN || function>INT16_MAX) throw std::runtime_error("Invalid native skill event function");
                binding.function=int16_t(function);
            }
            const auto overlay=[&](std::string_view field) {
                const auto name=skills.value(row,field);if(name.empty()) return -1;
                const auto &overlays=data.tables.at("overlay");
                for(size_t visual=0;visual<overlays.rows().size();++visual) if(overlays.value(visual,"overlay")==name) return int(visual);
                throw std::runtime_error("Missing native skill event overlay: "+std::string(name));
            };
            const auto nativeState=[&](std::string_view field) {
                const auto name=skills.value(row,field);if(name.empty()) return -1;
                const auto found=data.states.find(name);if(found==data.states.end()) throw std::runtime_error("Missing native skill state linker identity");
                return found->second.definition.id;
            };
            record.auraState=nativeState("aurastate");record.auraTargetState=nativeState("auratargetstate");
            record.reactionOverlay=overlay("cltoverlaya");record.procOverlay=overlay("prgoverlay");record.serverOverlay=overlay("srvoverlay");
            record.summonOverlay=overlay("sumoverlay");
            const auto metadata=loadSkillEligibilityMetadata(skills,row);
            static_cast<SkillEquipmentRule &>(record.restore)=metadata;
            record.restore.inGame=metadata.inGame;record.restore.aura=metadata.aura;record.restore.passive=metadata.passive;
            record.restore.scroll=skills.number(row,"scroll").value_or(0)!=0;
            const auto code=skills.value(row,"charclass");
            if(!code.empty()) {
                bool linked=false;
                for(const auto &character:data.characters) if(character.code==code) {
                    record.classId=int(character.sourceRow);linked=true;break;
                }
                if(!linked) throw std::runtime_error("Missing native skill class linker identity");
            }
            if(const auto *entry=data.skills.find(id)) record.page=entry->page;
            const auto state=[&](std::string_view field) {
                const auto name=skills.value(row,field);if(name.empty()) return -1;
                const auto found=data.states.find(name);
                if(found==data.states.end()) throw std::runtime_error("Missing native passive state: "+std::string(name));
                return found->second.definition.id;
            };
            record.passiveState=state("passivestate");record.suppressionState=state("aurastate");
            const auto type=skills.value(row,"passiveitype");
            if(!type.empty()) {
                const auto found=typeIds.find(type);
                if(found==typeIds.end()) throw std::runtime_error("Missing native passive item type: "+std::string(type));
                record.passiveLayer=found->second;
            }
            break;
        }
        for(size_t slot=0;slot<source.description.missiles.size();++slot) {
            const auto &name=source.description.missiles[slot];if(name.empty()) continue;
            const auto missile=result->missileIds.find(name);
            if(missile==result->missileIds.end()) throw std::runtime_error("Missing native descriptor missile: "+name);
            record.descriptionMissiles[slot]=missile->second;
        }
        result->skills.emplace(id,std::move(record));
    }
    return result;
}
}
