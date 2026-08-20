#pragma once
// SimConnect 管理：连接/自动重连、遥测读取（SIM_FRAME）、
// 飞控事件发送、FlightPlanActivated 事件订阅。
// 所有 SimConnect 调用集中在 SimConnect 线程。

#include <windows.h>
#include <atomic>
#include <thread>
#include <deque>
#include <mutex>
#include <string>
#include <functional>
#include "FlightController.h"

// 单次遥测快照（全 double，避免结构体对齐问题）
struct AircraftTelemetry {
    double lat = 0, lon = 0;
    double altitude = 0, altAgl = 0;
    double heading = 0, pitch = 0, roll = 0;
    double groundSpeed = 0, indicatedAirspeed = 0, verticalSpeed = 0;
    double flapsPercent = 0;   // 0..100
    double elevatorTrim = 0;   // -1..1
    double throttle = 0;       // 0..1
    bool gearDown = false;
    bool parkingBrake = false;
    bool onGround = false;
    bool autopilotMaster = false;
    uint64_t seq = 0;
};

// 由 TCP 线程入队、SimConnect 线程执行的飞控命令
enum SimEventCmd {
    kEvFlapsIncr,
    kEvFlapsDecr,
    kEvGear,
    kEvTrimUp,
    kEvTrimDn,
    kEvParking,
    kEvBrakeHold,
    kEvBrakeRelease,
    kEvAutopilotOn,
    kEvAutopilotOff,
};

class SimConnectManager {
public:
    using TelemetryCallback = std::function<void(const AircraftTelemetry&)>;
    using StatusCallback    = std::function<void(bool connected, const std::string& aircraft)>;
    using FlightPlanCallback= std::function<void(const std::wstring& plnFile)>;

    void Start(FlightController* fc,
               TelemetryCallback onTelemetry,
               StatusCallback onStatus,
               FlightPlanCallback onFlightPlan);
    void Stop();

    bool IsSimConnected() const { return simConnected_.load(); }
    std::string AircraftName() const;

    // 模拟器在线时才入队；离线命令不会在稍后重连时意外执行。
    bool EnqueueCommand(int cmd, int arg0 = 0);

    // SimConnect 分发回调（由 SimConnect 线程调用）
    void OnDispatch(void* pData);

private:
    void ThreadMain();
    void ApplyControlState(HANDLE h);
    void DrainCommands(HANDLE h);
    void SendEvent(HANDLE h, DWORD eventId, DWORD value);
    bool ConfigureConnection(HANDLE h);
    void HandleConnectionLost();

    std::atomic<bool> running_{ false };
    std::thread thread_;
    HANDLE hSimConnect_ = nullptr;

    std::atomic<bool> simConnected_{ false };
    std::atomic<bool> reconnectRequested_{ false };
    mutable std::mutex titleMtx_;
    std::string aircraftName_;
    std::atomic<uint64_t> telemetrySeq_{ 0 };
    long long lastTitleRequestMs_ = 0;
    bool brakeHeld_ = false;
    long long lastBrakeRefreshMs_ = 0;

    struct Cmd { int cmd; int arg0; };
    std::deque<Cmd> cmdQueue_;
    std::mutex cmdMtx_;

    FlightController* fc_ = nullptr;
    TelemetryCallback onTelemetry_;
    StatusCallback onStatus_;
    FlightPlanCallback onFlightPlan_;

    ControllerState lastTx_;  // 上次已发送的控制状态
};
