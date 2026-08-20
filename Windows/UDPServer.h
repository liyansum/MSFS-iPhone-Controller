#pragma once
// UDP 实时控制服务（默认 36666）。
// 处理：控制包 -> FlightController / 看门狗 Touch；Ping -> Pong。

#include <atomic>
#include <thread>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

class FlightController;
class SafetyWatchdog;

class UDPServer {
public:
    using SessionCheck = std::function<bool(uint32_t sessionId)>;
    using ControlEnabled = std::function<bool()>;

    void Start(uint16_t port, SessionCheck check, ControlEnabled enabled,
               FlightController* fc, SafetyWatchdog* wd);
    void Stop();

    // 自动探测状态（供 UI 显示）
    bool IsControlReady() const { return controlReady_.load(); }
    bool IsDiscoveryReady() const { return discoveryReady_.load(); }
    int DiscoveryReplies() const { return discoveryReplies_.load(); }
    std::string ControlError() const;
    std::string DiscoveryError() const;

private:
    void ThreadMain(uint16_t port);
    void DiscoveryThread(uint16_t port);
    void SetControlError(const std::string& msg);
    void SetDiscoveryError(const std::string& msg);

    std::atomic<bool> running_{ false };
    std::thread thread_;
    std::thread discoveryThread_;
    std::atomic<unsigned long long> sock_{ 0 };
    std::atomic<unsigned long long> discoverySock_{ 0 };
    SessionCheck check_;
    ControlEnabled controlEnabled_;
    FlightController* fc_ = nullptr;
    SafetyWatchdog* wd_ = nullptr;

    std::atomic<bool> controlReady_{ false };
    std::atomic<bool> discoveryReady_{ false };
    std::atomic<int> discoveryReplies_{ 0 };
    mutable std::mutex controlErrMtx_;
    std::string controlError_;
    mutable std::mutex discoveryErrMtx_;
    std::string discoveryError_;
};
