#pragma once
// 遥测管理器：以 10 Hz 将最新飞机状态打包为 TCP JSON 推送给手机。

#include <atomic>
#include <thread>
#include <mutex>
#include <string>
#include <functional>
#include "SimConnectManager.h"

class TelemetryManager {
public:
    using SendFn = std::function<void(const std::string& json)>;

    void Start(SendFn send);
    void Stop();

    void Push(const AircraftTelemetry& t);
    void SetAircraftName(const std::string& name);

private:
    void Loop();

    std::atomic<bool> running_{ false };
    std::thread thread_;
    SendFn send_;

    std::mutex mtx_;
    AircraftTelemetry latest_;
    std::atomic<bool> hasData_{ false };
    std::mutex nameMtx_;
    std::string aircraftName_;
    uint64_t packetSeq_ = 0;
};
