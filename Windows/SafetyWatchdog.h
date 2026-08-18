#pragma once
// 安全看门狗：实时控制包超时（默认 250ms）后回中 Aileron/Elevator/Rudder。
// Throttle / Trim / Flaps / Gear 等持久状态保持不变。

#include <atomic>
#include <thread>
#include <functional>

class SafetyWatchdog {
public:
    void Start(std::function<void()> onTimeout);
    void Stop();
    void Touch();
    long long LastControlMs() const { return lastControlMs_.load(); }
    bool IsExpired() const { return expired_.load(); }

private:
    void Loop();

    std::atomic<bool> running_{ false };
    std::atomic<long long> lastControlMs_{ 0 };
    std::atomic<bool> expired_{ true };
    std::thread thread_;
    std::function<void()> onTimeout_;
};
