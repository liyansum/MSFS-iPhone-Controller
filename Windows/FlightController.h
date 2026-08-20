#pragma once
// 最新控制状态共享区。UDP 线程写入，SimConnect 线程每模拟帧读取。
// 通过 generation 递增保证回中动作也会被重新应用一次。

#include <cstdint>
#include <mutex>

struct ControllerState {
    uint32_t generation = 0;  // 每次更新（新控制包或回中）递增
    uint32_t sequence = 0;
    uint64_t timestampMs = 0;
    uint16_t axisMask = 0;
    int16_t  aileron = 0;
    int16_t  elevator = 0;
    int16_t  rudder = 0;
    uint16_t throttle = 0;
};

class FlightController {
public:
    // UDP 实时控制包更新
    // 返回 true 表示包已被接收；重复/乱序包返回 false。
    bool UpdateFromControl(uint32_t seq, uint64_t ts, uint16_t mask,
                           int16_t aileron, int16_t elevator,
                           int16_t rudder, uint16_t throttle);

    // TCP HELLO 建立新会话时重置 UDP 序号窗口，并立即回中瞬时轴。
    void ResetSession();

    // 看门狗回中：Aileron/Elevator/Rudder = 0；Throttle 保持不变
    void NeutralizeAxes();

    ControllerState Snapshot() const;

private:
    mutable std::mutex mtx_;
    ControllerState state_;
    bool hasSequence_ = false;
};
