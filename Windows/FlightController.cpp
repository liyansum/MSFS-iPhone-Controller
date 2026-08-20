#include "FlightController.h"
#include "Protocol.h"
#include <algorithm>

bool FlightController::UpdateFromControl(uint32_t seq, uint64_t ts, uint16_t mask,
                                         int16_t aileron, int16_t elevator,
                                         int16_t rudder, uint16_t throttle) {
    std::lock_guard<std::mutex> lock(mtx_);
    // 乱序保护：仅接受更新的序号，丢弃旧包 / 重复包（防止旧控制状态被重新应用）
    uint32_t diff = seq - state_.sequence;
    if (hasSequence_ && (diff == 0 || diff >= 0x80000000u)) return false;
    hasSequence_ = true;
    ++state_.generation;
    state_.sequence = seq;
    state_.timestampMs = ts;
    state_.axisMask = mask;
    state_.aileron = (int16_t)std::clamp<int32_t>(aileron, proto::kAxisMin, proto::kAxisMax);
    state_.elevator = (int16_t)std::clamp<int32_t>(elevator, proto::kAxisMin, proto::kAxisMax);
    state_.rudder = (int16_t)std::clamp<int32_t>(rudder, proto::kRudderMin,
                                                 proto::kRudderMax);
    state_.throttle = (uint16_t)std::min<uint32_t>(throttle, proto::kThrottleMax);
    return true;
}

void FlightController::ResetSession() {
    std::lock_guard<std::mutex> lock(mtx_);
    hasSequence_ = false;
    state_.sequence = 0;
    state_.timestampMs = 0;
    ++state_.generation;
    state_.axisMask = proto::kAxisAileron | proto::kAxisElevator | proto::kAxisRudder;
    state_.aileron = 0;
    state_.elevator = 0;
    state_.rudder = 0;
}

void FlightController::NeutralizeAxes() {
    std::lock_guard<std::mutex> lock(mtx_);
    ++state_.generation;
    state_.axisMask |= (proto::kAxisAileron | proto::kAxisElevator | proto::kAxisRudder);
    state_.aileron = 0;
    state_.elevator = 0;
    state_.rudder = 0;
}

ControllerState FlightController::Snapshot() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return state_;
}
