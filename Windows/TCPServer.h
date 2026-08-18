#pragma once
// TCP 状态与命令服务（默认 36667）。
// 职责：会话建立(HELLO/WELCOME)、命令分发、遥测/航线推送。
// 第一版只支持单台 iPhone。

#include <atomic>
#include <thread>
#include <mutex>
#include <cstdint>
#include <string>
#include <functional>

#include <winsock2.h>

class SimConnectManager;
class FlightController;
class FlightPlanManager;

class TCPServer {
public:
    using StatusGetter = std::function<std::pair<bool, std::string>()>;

    void Start(uint16_t port, SimConnectManager* sim, FlightController* fc,
               FlightPlanManager* fp, StatusGetter status);
    void Stop();

    uint32_t SessionId() const { return sessionId_.load(); }

    void SendToClient(const std::string& json);
    void SendStatus(bool simConnected, const std::string& aircraft);
    void SendRoute(const FlightPlanManager& fp);

private:
    void AcceptLoop(uint16_t port);
    void HandleClient(SOCKET client);
    void ProcessLine(SOCKET client, const std::string& line);

    static bool SendAll(SOCKET s, const std::string& data);

    std::atomic<bool> running_{ false };
    std::atomic<bool> started_{ false };
    std::thread thread_;
    std::atomic<unsigned long long> listenSock_{ 0 };

    SimConnectManager* sim_ = nullptr;
    FlightController* fc_ = nullptr;
    FlightPlanManager* fp_ = nullptr;
    StatusGetter status_;

    std::atomic<uint32_t> sessionId_{ 0 };
    std::atomic<bool> clientActive_{ false };
    std::mutex sendMtx_;
    SOCKET clientSock_ = INVALID_SOCKET;
};
