#pragma once
// UDP 实时控制服务（默认 36666）。
// 处理：控制包 -> FlightController / 看门狗 Touch；Ping -> Pong。

#include <atomic>
#include <thread>
#include <cstdint>
#include <functional>

class FlightController;
class SafetyWatchdog;

class UDPServer {
public:
    using SessionCheck = std::function<bool(uint32_t sessionId)>;

    void Start(uint16_t port, SessionCheck check, FlightController* fc, SafetyWatchdog* wd);
    void Stop();

private:
    void ThreadMain(uint16_t port);

    std::atomic<bool> running_{ false };
    std::thread thread_;
    std::atomic<unsigned long long> sock_{ 0 };
    SessionCheck check_;
    FlightController* fc_ = nullptr;
    SafetyWatchdog* wd_ = nullptr;
};
