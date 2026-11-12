#include "object_mode.hpp"
#include "resources/data_table.hpp"

namespace d2x {
std::optional<float> objectEndAnimation(const DataTable &objects, size_t row, int mode) {
    if (mode != 1 || !objects.number(row, "Mode2").value_or(0)) return {};
    if(objects.number(row,"InitFn").value_or(0)==38) {
        // A2Q6_Callback08_MonsterKilled: Tyrael's door uses the original
        // FrameCnt1 ENDANIM (without +1), even though its OperateFn is zero.
        const auto frames=objects.number(row,"FrameCnt1");
        return frames && *frames>0?std::optional<float>{float(*frames)/25.f}:std::nullopt;
    }
    if(objects.number(row,"InitFn").value_or(0)==39) {
        // ACT3Q3_SetAltarMode: Ormus's placed Gidbinn altar has no OperateFn.
        const auto frames=objects.number(row,"FrameCnt1");
        return frames && *frames>0?std::optional<float>{(float(*frames)+1.f)/25.f}:std::nullopt;
    }
    if(objects.number(row,"OperateFn").value_or(0)==46 || objects.number(row,"OperateFn").value_or(0)==49 ||
        objects.number(row,"OperateFn").value_or(0)==66) {
        // A3Q6's HellGate, A4Q3's stone and A5Q5's Ancients door use
        // FrameCnt1 without +1. ENDANIM need not publish another mode packet.
        const auto frames=objects.number(row,"FrameCnt1");
        return frames && *frames>0?std::optional<float>{float(*frames)/25.f}:std::nullopt;
    }
    const auto operation=objects.number(row,"OperateFn").value_or(0);
    if(operation==52 || operation==54 || operation==55 || operation==56) {
        // A4Q2 Operate52 is shared by all five seals and uses FrameCnt1>>7,
        // twice the TXT frame count, rather than the ordinary ENDANIM delay.
        const auto frames=objects.number(row,"FrameCnt1");
        return frames && *frames>0?std::optional<float>{2.f*float(*frames)/25.f}:std::nullopt;
    }
    switch (objects.number(row, "OperateFn").value_or(0)) {
    case 2:
        if (objects.number(row, "CycleAnim1").value_or(0)) return {};
        break;
    case 1: case 3: case 4: case 5: case 7: case 14: case 15: case 18: case 23:
    case 30: case 34: case 48: case 51: case 68:
        break;
    default:
        // In particular, spike traps (11) reset through their own server event.
        return {};
    }
    const auto frames = objects.number(row, "FrameCnt1");
    return frames && *frames > 0 ? std::optional<float>{(float(*frames) + 1.f) / 25.f}
                                : std::nullopt;
}

int objectDisplayMode(const DataTable &objects, size_t row, int mode, float elapsed) {
    const auto end = objectEndAnimation(objects, row, mode);
    return end && elapsed >= *end ? 2 : mode;
}

ObjectPresentation objectPresentation(const DataTable &objects, size_t row, int mode, float elapsed) {
    const bool draw=objects.number(row,"Draw").value_or(0)!=0;
    if(mode==3 && objects.number(row,"Id").value_or(0)==189 && objects.number(row,"Mode4").value_or(0)) {
        // Retail D2Client RVA 25822 decrements the S1 counter and enters S2
        // when it goes below zero, including when CycleAnim3 is set.
        const int frames=objects.number(row,"FrameCnt3").value_or(0);
        const int speed=objects.number(row,"FrameDelta3").value_or(0);
        const int start=objects.number(row,"Start3").value_or(0);
        if(frames>1 && speed>0 && start>=0 && start<frames) {
            const float end=float(int64_t(start)*256/speed+1)/25.f;
            if(elapsed>=end) return {4,elapsed-end,draw};
        }
        return {mode,elapsed,draw};
    }
    auto end = objectEndAnimation(objects, row, mode);
    if(!end && mode==1 && !objects.number(row,"IsDoor").value_or(0) &&
        !objects.number(row,"CycleAnim1").value_or(0) && objects.number(row,"Mode2").value_or(0)) {
        // D2Client 1.13c RVA 25660: local OP -> ON at the last frame,
        // independent of another server mode packet (including Init77).
        const int frames=objects.number(row,"FrameCnt1").value_or(0);
        const int speed=objects.number(row,"FrameDelta1").value_or(0);
        const int start=objects.number(row,"Start1").value_or(0);
        if(frames>1 && speed>0) {
            const auto remaining=(int64_t(frames)-1-start)*256;
            end=remaining>0?float((remaining+speed-1)/speed)/25.f:0.f;
        }
    }
    const bool opened = end && elapsed >= *end;
    return {opened ? 2 : mode, opened ? elapsed - *end : elapsed, draw};
}
} // namespace d2x
