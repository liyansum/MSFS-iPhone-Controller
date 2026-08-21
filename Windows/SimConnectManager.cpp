#include "SimConnectManager.h"
#include "Protocol.h"
#include <SimConnect.h>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <cmath>

#ifdef _MSC_VER
#pragma comment(lib, "SimConnect.lib")
#endif

namespace {

// 数据定义
constexpr DWORD DEF_PLANE = 0;
constexpr DWORD DEF_TITLE = 1;

// 事件 ID
enum : DWORD {
    EV_AILERON = 1,
    EV_ELEVATOR,
    EV_RUDDER,
    EV_THROTTLE,
    EV_FLAPS_INCR,
    EV_FLAPS_DECR,
    EV_GEAR,
    EV_TRIM_UP,
    EV_TRIM_DN,
    EV_PARKING,
    EV_BRAKE_LEFT,
    EV_BRAKE_RIGHT,
    EV_THROTTLE_INCR,
    EV_THROTTLE_DECR,
    EV_THROTTLE_CUT,
    EV_AUTOTHROTTLE_DISCONNECT,
    EV_AUTOPILOT_MASTER,
    EV_AUTOPILOT_HEADING_ON,
    EV_AUTOPILOT_HEADING_OFF,
    EV_AUTOPILOT_NAV_ON,
    EV_AUTOPILOT_NAV_OFF,
    EV_AUTOPILOT_HEADING_SET,
    EV_TOGGLE_GPS_DRIVES_NAV1,
    EV_AP_ALTITUDE_SET,
    EV_AP_ALTITUDE_ON,
    EV_AP_ALTITUDE_OFF,
    EV_AP_VS_SET,
    EV_AP_VS_ON,
    EV_AP_VS_OFF,
    EV_AP_FLC_ON,
    EV_AP_FLC_OFF,
    EV_AP_SPEED_SET,
    EV_AP_APPROACH_ON,
    EV_AP_APPROACH_OFF,
    EV_FLIGHTPLAN,
    EV_FLIGHTPLAN_DEACTIVATED,
};

// 与 AddToDataDefinition 顺序严格一致的遥测结构（全 double）
struct SimData {
    double lat, lon, altitude, altAgl;
    double heading, magneticHeading, pitch, roll;
    double groundSpeed, indicatedAirspeed, verticalSpeed;
    double flapsPercent, elevatorTrim, throttle;
    double autothrottleActive, autothrottleArmed;
    double gearDown, parkingBrake, onGround, autopilotMaster;
    double autopilotHeadingLock, autopilotNavLock, autopilotHeading;
    double gpsDrivesNav1;
    double autopilotAltitudeLock, autopilotAltitudeArm, autopilotAltitude;
    double autopilotVerticalHold, autopilotVerticalSpeed, autopilotFlightLevelChange;
    double autopilotSpeed;
    double autopilotApproachArm, autopilotApproachActive;
    double autopilotGlideslopeArm, autopilotGlideslopeActive;
    double gpsWaypointIndex, gpsWaypointDistance;
    double nav1Frequency, nav1HasLocalizer, nav1HasGlideslope;
    double brakeLeft, brakeRight;
};

constexpr DWORD kBrakeFull = 16383;
constexpr int32_t kBrakeReleased = -16383;

struct SimTitle {
    char title[256];
};

long long NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::wstring PathFromSimConnect(const char* path) {
    if (!path || !*path) return {};
    // FlightPlanActivated 的 SDK 结构使用窄字符串。优先按 UTF-8，旧版 SDK
    // 返回本地代码页时再回退到 ACP。
    auto convert = [&](UINT codePage, DWORD flags) -> std::wstring {
        int count = MultiByteToWideChar(codePage, flags, path, -1, nullptr, 0);
        if (count <= 1) return {};
        std::wstring value((size_t)count, L'\0');
        MultiByteToWideChar(codePage, flags, path, -1, value.data(), count);
        value.resize((size_t)count - 1);
        return value;
    };
    std::wstring result = convert(CP_UTF8, MB_ERR_INVALID_CHARS);
    return result.empty() ? convert(CP_ACP, 0) : result;
}

SimConnectManager* g_instance = nullptr;

void CALLBACK SimDispatch(SIMCONNECT_RECV* pData, DWORD /*cbData*/, void* /*pContext*/) {
    if (g_instance && pData) g_instance->OnDispatch(pData);
}

} // namespace

void SimConnectManager::Start(FlightController* fc, TelemetryCallback tc,
                              StatusCallback sc, FlightPlanCallback fpc) {
    fc_ = fc;
    onTelemetry_ = std::move(tc);
    onStatus_ = std::move(sc);
    onFlightPlan_ = std::move(fpc);
    if (running_.load()) return;
    running_ = true;
    g_instance = this;
    thread_ = std::thread(&SimConnectManager::ThreadMain, this);
}

void SimConnectManager::Stop() {
    running_ = false;
    // hSimConnect_ 只由 SimConnect 线程访问和关闭，避免主线程关闭句柄时
    // CallDispatch/TransmitClientEvent 仍在使用它。
    if (thread_.joinable()) thread_.join();
    simConnected_ = false;
    if (g_instance == this) g_instance = nullptr;
}

std::string SimConnectManager::AircraftName() const {
    std::lock_guard<std::mutex> lock(titleMtx_);
    return aircraftName_;
}

bool SimConnectManager::HasActiveFlightPlan() const {
    std::lock_guard<std::mutex> lock(activePlanMtx_);
    return !activeFlightPlanPath_.empty();
}

bool SimConnectManager::EnqueueCommand(int cmd, int arg0) {
    if (!simConnected_.load()) return false;
    std::lock_guard<std::mutex> lock(cmdMtx_);
    if (!simConnected_.load()) return false;
    cmdQueue_.push_back(Cmd{ cmd, arg0 });
    return true;
}

bool SimConnectManager::ConfigureConnection(HANDLE h) {
    bool ok = true;
    auto data = [&](DWORD definition, const char* name, const char* unit,
                    SIMCONNECT_DATATYPE type = SIMCONNECT_DATATYPE_FLOAT64) {
        HRESULT hr = SimConnect_AddToDataDefinition(h, definition, name, unit, type);
        if (FAILED(hr)) ok = false;
    };
    data(DEF_PLANE, "PLANE LATITUDE", "degrees");
    data(DEF_PLANE, "PLANE LONGITUDE", "degrees");
    data(DEF_PLANE, "PLANE ALTITUDE", "feet");
    data(DEF_PLANE, "PLANE ALT ABOVE GROUND", "feet");
    data(DEF_PLANE, "PLANE HEADING DEGREES TRUE", "degrees");
    data(DEF_PLANE, "PLANE HEADING DEGREES MAGNETIC", "degrees");
    data(DEF_PLANE, "PLANE PITCH DEGREES", "degrees");
    data(DEF_PLANE, "PLANE BANK DEGREES", "degrees");
    data(DEF_PLANE, "GPS GROUND SPEED", "meters per second");
    data(DEF_PLANE, "AIRSPEED INDICATED", "meters per second");
    data(DEF_PLANE, "VERTICAL SPEED", "meters per second");
    data(DEF_PLANE, "FLAPS HANDLE PERCENT", "percent");
    // PCT 的原生规范是 Percent Over 100，直接得到协议要求的 -1..1。
    data(DEF_PLANE, "ELEVATOR TRIM PCT", "percent over 100");
    data(DEF_PLANE, "GENERAL ENG THROTTLE LEVER POSITION:1", "percent");
    data(DEF_PLANE, "AUTOTHROTTLE ACTIVE", "bool");
    data(DEF_PLANE, "AUTOPILOT THROTTLE ARM", "bool");
    data(DEF_PLANE, "GEAR HANDLE POSITION", "bool");
    data(DEF_PLANE, "BRAKE PARKING INDICATOR", "bool");
    data(DEF_PLANE, "SIM ON GROUND", "bool");
    data(DEF_PLANE, "AUTOPILOT MASTER", "bool");
    data(DEF_PLANE, "AUTOPILOT HEADING LOCK", "bool");
    data(DEF_PLANE, "AUTOPILOT NAV1 LOCK", "bool");
    data(DEF_PLANE, "AUTOPILOT HEADING LOCK DIR:1", "degrees");
    data(DEF_PLANE, "GPS DRIVES NAV1", "bool");
    data(DEF_PLANE, "AUTOPILOT ALTITUDE LOCK", "bool");
    data(DEF_PLANE, "AUTOPILOT ALTITUDE ARM", "bool");
    data(DEF_PLANE, "AUTOPILOT ALTITUDE LOCK VAR:1", "feet");
    data(DEF_PLANE, "AUTOPILOT VERTICAL HOLD", "bool");
    data(DEF_PLANE, "AUTOPILOT VERTICAL HOLD VAR:1", "feet per minute");
    data(DEF_PLANE, "AUTOPILOT FLIGHT LEVEL CHANGE", "bool");
    data(DEF_PLANE, "AUTOPILOT AIRSPEED HOLD VAR:1", "knots");
    data(DEF_PLANE, "AUTOPILOT APPROACH ARM", "bool");
    data(DEF_PLANE, "AUTOPILOT APPROACH ACTIVE", "bool");
    data(DEF_PLANE, "AUTOPILOT GLIDESLOPE ARM", "bool");
    data(DEF_PLANE, "AUTOPILOT GLIDESLOPE ACTIVE", "bool");
    data(DEF_PLANE, "GPS FLIGHT PLAN WP INDEX", "number");
    data(DEF_PLANE, "GPS WP DISTANCE", "nautical miles");
    data(DEF_PLANE, "NAV ACTIVE FREQUENCY:1", "MHz");
    data(DEF_PLANE, "NAV HAS LOCALIZER:1", "bool");
    data(DEF_PLANE, "NAV HAS GLIDE SLOPE:1", "bool");
    data(DEF_PLANE, "BRAKE LEFT POSITION", "position 32k");
    data(DEF_PLANE, "BRAKE RIGHT POSITION", "position 32k");
    data(DEF_TITLE, "TITLE", nullptr, SIMCONNECT_DATATYPE_STRING256);

    auto map = [&](DWORD id, const char* eventName) {
        HRESULT hr = SimConnect_MapClientEventToSimEvent(h, id, eventName);
        if (FAILED(hr)) ok = false;
    };
    map(EV_AILERON, "AXIS_AILERONS_SET");
    map(EV_ELEVATOR, "AXIS_ELEVATOR_SET");
    map(EV_RUDDER, "AXIS_RUDDER_SET");
    // THROTTLE_SET 与手机协议同为 0..16383，0 明确代表 idle。
    map(EV_THROTTLE, "THROTTLE_SET");
    map(EV_FLAPS_INCR, "FLAPS_INCR");
    map(EV_FLAPS_DECR, "FLAPS_DECR");
    map(EV_GEAR, "GEAR_TOGGLE");
    map(EV_TRIM_UP, "ELEV_TRIM_UP");
    map(EV_TRIM_DN, "ELEV_TRIM_DN");
    map(EV_PARKING, "PARKING_BRAKES");
    map(EV_BRAKE_LEFT, "AXIS_LEFT_BRAKE_SET");
    map(EV_BRAKE_RIGHT, "AXIS_RIGHT_BRAKE_SET");
    map(EV_THROTTLE_INCR, "THROTTLE_INCR");
    map(EV_THROTTLE_DECR, "THROTTLE_DECR");
    map(EV_THROTTLE_CUT, "THROTTLE_CUT");
    map(EV_AUTOTHROTTLE_DISCONNECT, "AUTO_THROTTLE_DISCONNECT");
    map(EV_AUTOPILOT_MASTER, "AP_MASTER");
    map(EV_AUTOPILOT_HEADING_ON, "AP_HDG_HOLD_ON");
    map(EV_AUTOPILOT_HEADING_OFF, "AP_HDG_HOLD_OFF");
    map(EV_AUTOPILOT_NAV_ON, "AP_NAV1_HOLD_ON");
    map(EV_AUTOPILOT_NAV_OFF, "AP_NAV1_HOLD_OFF");
    map(EV_AUTOPILOT_HEADING_SET, "HEADING_BUG_SET");
    map(EV_TOGGLE_GPS_DRIVES_NAV1, "TOGGLE_GPS_DRIVES_NAV1");
    map(EV_AP_ALTITUDE_SET, "AP_ALT_VAR_SET_ENGLISH");
    map(EV_AP_ALTITUDE_ON, "AP_ALT_HOLD_ON");
    map(EV_AP_ALTITUDE_OFF, "AP_ALT_HOLD_OFF");
    map(EV_AP_VS_SET, "AP_VS_VAR_SET_ENGLISH");
    map(EV_AP_VS_ON, "AP_VS_ON");
    map(EV_AP_VS_OFF, "AP_VS_OFF");
    map(EV_AP_FLC_ON, "FLIGHT_LEVEL_CHANGE_ON");
    map(EV_AP_FLC_OFF, "FLIGHT_LEVEL_CHANGE_OFF");
    map(EV_AP_SPEED_SET, "AP_SPD_VAR_SET");
    map(EV_AP_APPROACH_ON, "AP_APR_HOLD_ON");
    map(EV_AP_APPROACH_OFF, "AP_APR_HOLD_OFF");

    if (FAILED(SimConnect_SubscribeToSystemEvent(
            h, EV_FLIGHTPLAN, "FlightPlanActivated"))) ok = false;
    if (FAILED(SimConnect_SubscribeToSystemEvent(
            h, EV_FLIGHTPLAN_DEACTIVATED, "FlightPlanDeactivated"))) ok = false;
    if (FAILED(SimConnect_RequestDataOnSimObject(
            h, DEF_PLANE, DEF_PLANE, SIMCONNECT_OBJECT_ID_USER,
            SIMCONNECT_PERIOD_SIM_FRAME))) ok = false;
    if (FAILED(SimConnect_RequestDataOnSimObject(
            h, DEF_TITLE, DEF_TITLE, SIMCONNECT_OBJECT_ID_USER,
            SIMCONNECT_PERIOD_ONCE))) ok = false;
    return ok;
}

void SimConnectManager::HandleConnectionLost() {
    if (hSimConnect_) {
        SimConnect_Close(hSimConnect_);
        hSimConnect_ = nullptr;
    }
    reconnectRequested_ = false;
    brakeHeld_ = false;
    lastBrakeRefreshMs_ = 0;
    brakeReleaseUntilMs_ = 0;
    autopilotMaster_ = false;
    autopilotHeadingLock_ = false;
    autopilotNavLock_ = false;
    gpsDrivesNav1_ = false;
    autopilotAltitudeLock_ = false;
    autopilotVerticalHold_ = false;
    autopilotFlightLevelChange_ = false;
    autopilotApproach_ = false;
    const bool wasConnected = simConnected_.exchange(false);
    {
        std::lock_guard<std::mutex> lock(cmdMtx_);
        cmdQueue_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(titleMtx_);
        aircraftName_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(activePlanMtx_);
        activeFlightPlanPath_.clear();
    }
    if (fc_) fc_->NeutralizeAxes();
    if (wasConnected && onStatus_) onStatus_(false, "");
}

void SimConnectManager::ThreadMain() {
    while (running_.load()) {
        if (!hSimConnect_) {
            HRESULT hr = SimConnect_Open(&hSimConnect_, "MSFS iPhone Controller",
                                         nullptr, 0, 0, 0);
            if (SUCCEEDED(hr)) {
                if (!ConfigureConnection(hSimConnect_)) {
                    SimConnect_Close(hSimConnect_);
                    hSimConnect_ = nullptr;
                    simConnected_ = false;
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    continue;
                }
                lastTitleRequestMs_ = NowMs();
                reconnectRequested_ = false;
                simConnected_ = true;
                // BRAKE 是按住型瞬时控制；任何 SimConnect 新会话都先释放，
                // 防止旧连接中断时刹车轴停在最大值。
                brakeHeld_ = false;
                lastBrakeRefreshMs_ = 0;
                brakeReleaseUntilMs_ = NowMs() + 1000;
                SendEvent(hSimConnect_, EV_BRAKE_LEFT,
                          static_cast<DWORD>(kBrakeReleased));
                SendEvent(hSimConnect_, EV_BRAKE_RIGHT,
                          static_cast<DWORD>(kBrakeReleased));
                if (onStatus_) {
                    std::string name = AircraftName();
                    onStatus_(true, name);
                }
            } else {
                simConnected_ = false;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
        }

        HRESULT hr = SimConnect_CallDispatch(hSimConnect_, SimDispatch, nullptr);
        if (FAILED(hr) || reconnectRequested_.load()) {
            HandleConnectionLost();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        if (hSimConnect_) {
            ApplyControlState(hSimConnect_);
            DrainCommands(hSimConnect_);

            // 部分硬件轴或机模会很快覆盖一次性的刹车事件。按住期间以 20 Hz
            // 刷新左右轮全刹车，松开时再明确发送 SDK 规定的 -16383（0%）。
            const long long now = NowMs();
            if ((brakeHeld_ || now < brakeReleaseUntilMs_) &&
                now - lastBrakeRefreshMs_ >= 50) {
                const DWORD brakeValue = brakeHeld_
                    ? kBrakeFull : static_cast<DWORD>(kBrakeReleased);
                SendEvent(hSimConnect_, EV_BRAKE_LEFT, brakeValue);
                SendEvent(hSimConnect_, EV_BRAKE_RIGHT, brakeValue);
                lastBrakeRefreshMs_ = now;
            }

            // 周期性刷新飞机名称
            if (NowMs() - lastTitleRequestMs_ >= 10000) {
                lastTitleRequestMs_ = NowMs();
                if (FAILED(SimConnect_RequestDataOnSimObject(
                        hSimConnect_, DEF_TITLE, DEF_TITLE, SIMCONNECT_OBJECT_ID_USER,
                        SIMCONNECT_PERIOD_ONCE))) {
                    reconnectRequested_ = true;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (hSimConnect_) {
        SimConnect_Close(hSimConnect_);
        hSimConnect_ = nullptr;
    }
    simConnected_ = false;
}

void SimConnectManager::ApplyControlState(HANDLE h) {
    if (!fc_) return;
    ControllerState st = fc_->Snapshot();
    if (st.generation == lastTx_.generation) return;
    lastTx_ = st;

    if (st.axisMask & proto::kAxisAileron)
        SendEvent(h, EV_AILERON, (DWORD)(int32_t)st.aileron);
    if (st.axisMask & proto::kAxisElevator)
        SendEvent(h, EV_ELEVATOR, (DWORD)(int32_t)st.elevator);
    if (st.axisMask & proto::kAxisRudder)
        SendEvent(h, EV_RUDDER, (DWORD)(int32_t)st.rudder);
    if (st.axisMask & proto::kAxisThrottle)
        SendEvent(h, EV_THROTTLE, (DWORD)st.throttle);
}

void SimConnectManager::DrainCommands(HANDLE h) {
    if (!h) return;
    std::deque<Cmd> pending;
    {
        std::lock_guard<std::mutex> lock(cmdMtx_);
        pending.swap(cmdQueue_);
    }
    for (const Cmd& c : pending) {
        switch (c.cmd) {
        case kEvFlapsIncr: SendEvent(h, EV_FLAPS_INCR, 0); break;
        case kEvFlapsDecr: SendEvent(h, EV_FLAPS_DECR, 0); break;
        case kEvGear:      SendEvent(h, EV_GEAR, 1); break;
        case kEvTrimUp:    SendEvent(h, EV_TRIM_UP, 1); break;
        case kEvTrimDn:    SendEvent(h, EV_TRIM_DN, 1); break;
        case kEvParking:   SendEvent(h, EV_PARKING, 1); break;
        case kEvBrakeHold:
            brakeHeld_ = true;
            brakeReleaseUntilMs_ = 0;
            SendEvent(h, EV_BRAKE_LEFT, kBrakeFull);
            SendEvent(h, EV_BRAKE_RIGHT, kBrakeFull);
            lastBrakeRefreshMs_ = NowMs();
            break;
        case kEvBrakeRelease:
            brakeHeld_ = false;
            brakeReleaseUntilMs_ = NowMs() + 1000;
            SendEvent(h, EV_BRAKE_LEFT, static_cast<DWORD>(kBrakeReleased));
            SendEvent(h, EV_BRAKE_RIGHT, static_cast<DWORD>(kBrakeReleased));
            lastBrakeRefreshMs_ = NowMs();
            break;
        case kEvThrottleIncr:
            SendEvent(h, EV_AUTOTHROTTLE_DISCONNECT, 0);
            SendEvent(h, EV_THROTTLE_INCR, 0);
            break;
        case kEvThrottleDecr:
            SendEvent(h, EV_AUTOTHROTTLE_DISCONNECT, 0);
            SendEvent(h, EV_THROTTLE_DECR, 0);
            break;
        case kEvThrottleTakeover:
            SendEvent(h, EV_AUTOTHROTTLE_DISCONNECT, 0);
            break;
        case kEvThrottleSet:
            // TCP 可靠提交松手时的最终值，并释放手机 UDP 轴所有权。
            SendEvent(h, EV_AUTOTHROTTLE_DISCONNECT, 0);
            SendEvent(h, EV_THROTTLE,
                      static_cast<DWORD>(std::clamp(c.arg0, 0,
                                                   static_cast<int>(proto::kThrottleMax))));
            break;
        case kEvThrottleIdle:
            // 0% 是明确的 idle 意图。A/THR 若仍在接管，单纯轴值可能被
            // 机模重新覆盖；先断开全部发动机自动油门，再执行官方 CUT。
            SendEvent(h, EV_AUTOTHROTTLE_DISCONNECT, 0);
            SendEvent(h, EV_THROTTLE_CUT, 0);
            SendEvent(h, EV_THROTTLE, 0);
            break;
        case kEvAutopilotOn:
            if (!autopilotMaster_) {
                // AP 接管前先交还中立操纵面，避免最后一个手机姿态包与 AP 争夺控制。
                SendEvent(h, EV_AILERON, 0);
                SendEvent(h, EV_ELEVATOR, 0);
                SendEvent(h, EV_RUDDER, 0);
                SendEvent(h, EV_AUTOPILOT_MASTER, 0);
                autopilotMaster_ = true; // 合并同一批队列中的重复 ON，随后由遥测校正
            }
            break;
        case kEvAutopilotOff:
            if (autopilotMaster_) {
                SendEvent(h, EV_AUTOPILOT_MASTER, 0);
                autopilotMaster_ = false;
            }
            break;
        case kEvAutopilotHeadingMode:
            if (autopilotNavLock_) {
                SendEvent(h, EV_AUTOPILOT_NAV_OFF, 0);
                autopilotNavLock_ = false;
            }
            if (!autopilotHeadingLock_) {
                SendEvent(h, EV_AUTOPILOT_HEADING_ON, 0);
                autopilotHeadingLock_ = true;
            }
            break;
        case kEvAutopilotNavMode:
            if (autopilotHeadingLock_) {
                SendEvent(h, EV_AUTOPILOT_HEADING_OFF, 0);
                autopilotHeadingLock_ = false;
            }
            if (!autopilotNavLock_) {
                SendEvent(h, EV_AUTOPILOT_NAV_ON, 0);
                autopilotNavLock_ = true;
            }
            break;
        case kEvAutopilotLateralOff:
            if (autopilotHeadingLock_) SendEvent(h, EV_AUTOPILOT_HEADING_OFF, 0);
            if (autopilotNavLock_) SendEvent(h, EV_AUTOPILOT_NAV_OFF, 0);
            autopilotHeadingLock_ = false;
            autopilotNavLock_ = false;
            break;
        case kEvAutopilotHeadingSet: {
            const int heading = ((c.arg0 % 360) + 360) % 360;
            SendEvent(h, EV_AUTOPILOT_HEADING_SET, static_cast<DWORD>(heading));
            break;
        }
        case kEvNavigationSourceGps:
            if (!gpsDrivesNav1_) {
                SendEvent(h, EV_TOGGLE_GPS_DRIVES_NAV1, 0);
                gpsDrivesNav1_ = true;
            }
            break;
        case kEvNavigationSourceNav1:
            if (gpsDrivesNav1_) {
                SendEvent(h, EV_TOGGLE_GPS_DRIVES_NAV1, 0);
                gpsDrivesNav1_ = false;
            }
            break;
        case kEvAutopilotAltitudeSet:
            SendEvent(h, EV_AP_ALTITUDE_SET,
                      static_cast<DWORD>(std::clamp(c.arg0, 0, 60000)));
            break;
        case kEvAutopilotAltitudeHold:
            if (autopilotVerticalHold_) SendEvent(h, EV_AP_VS_OFF, 0);
            if (autopilotFlightLevelChange_) SendEvent(h, EV_AP_FLC_OFF, 0);
            if (!autopilotAltitudeLock_) SendEvent(h, EV_AP_ALTITUDE_ON, 0);
            autopilotVerticalHold_ = false;
            autopilotFlightLevelChange_ = false;
            autopilotAltitudeLock_ = true;
            break;
        case kEvAutopilotVerticalSpeedSet:
            SendEvent(h, EV_AP_VS_SET, static_cast<DWORD>(static_cast<int32_t>(
                std::clamp(c.arg0, -6000, 6000))));
            break;
        case kEvAutopilotVerticalSpeedMode:
            if (autopilotAltitudeLock_) SendEvent(h, EV_AP_ALTITUDE_OFF, 0);
            if (autopilotFlightLevelChange_) SendEvent(h, EV_AP_FLC_OFF, 0);
            if (!autopilotVerticalHold_) SendEvent(h, EV_AP_VS_ON, 0);
            autopilotAltitudeLock_ = false;
            autopilotFlightLevelChange_ = false;
            autopilotVerticalHold_ = true;
            break;
        case kEvAutopilotFlightLevelChangeMode:
            if (autopilotAltitudeLock_) SendEvent(h, EV_AP_ALTITUDE_OFF, 0);
            if (autopilotVerticalHold_) SendEvent(h, EV_AP_VS_OFF, 0);
            if (!autopilotFlightLevelChange_) SendEvent(h, EV_AP_FLC_ON, 0);
            autopilotAltitudeLock_ = false;
            autopilotVerticalHold_ = false;
            autopilotFlightLevelChange_ = true;
            break;
        case kEvAutopilotVerticalOff:
            if (autopilotAltitudeLock_) SendEvent(h, EV_AP_ALTITUDE_OFF, 0);
            if (autopilotVerticalHold_) SendEvent(h, EV_AP_VS_OFF, 0);
            if (autopilotFlightLevelChange_) SendEvent(h, EV_AP_FLC_OFF, 0);
            autopilotAltitudeLock_ = false;
            autopilotVerticalHold_ = false;
            autopilotFlightLevelChange_ = false;
            break;
        case kEvAutopilotSpeedSet:
            SendEvent(h, EV_AP_SPEED_SET,
                      static_cast<DWORD>(std::clamp(c.arg0, 40, 400)));
            break;
        case kEvAutopilotApproachOn:
            if (!autopilotApproach_) SendEvent(h, EV_AP_APPROACH_ON, 0);
            autopilotApproach_ = true;
            break;
        case kEvAutopilotApproachOff:
            if (autopilotApproach_) SendEvent(h, EV_AP_APPROACH_OFF, 0);
            autopilotApproach_ = false;
            break;
        case kEvSyncFlightPlan: {
            std::string path;
            {
                std::lock_guard<std::mutex> lock(activePlanMtx_);
                path = activeFlightPlanPath_;
            }
            if (!path.empty() && FAILED(SimConnect_FlightPlanLoad(h, path.c_str())))
                reconnectRequested_ = true;
            break;
        }
        default: break;
        }
    }
}

void SimConnectManager::SendEvent(HANDLE h, DWORD eventId, DWORD value) {
    if (FAILED(SimConnect_TransmitClientEvent(
            h, SIMCONNECT_OBJECT_ID_USER, eventId, value,
            SIMCONNECT_GROUP_PRIORITY_HIGHEST,
            SIMCONNECT_EVENT_FLAG_GROUPID_IS_PRIORITY))) {
        reconnectRequested_ = true;
    }
}

void SimConnectManager::OnDispatch(void* pData) {
    auto* pRecv = static_cast<SIMCONNECT_RECV*>(pData);
    switch (pRecv->dwID) {
    case SIMCONNECT_RECV_ID_SIMOBJECT_DATA: {
        auto* obj = static_cast<SIMCONNECT_RECV_SIMOBJECT_DATA*>(pRecv);
        if (obj->dwDefineID == DEF_PLANE) {
            auto* d = reinterpret_cast<const SimData*>(&obj->dwData);
            AircraftTelemetry t;
            t.lat = d->lat; t.lon = d->lon;
            t.altitude = d->altitude; t.altAgl = d->altAgl;
            t.heading = d->heading; t.magneticHeading = d->magneticHeading;
            t.pitch = d->pitch; t.roll = d->roll;
            t.groundSpeed = d->groundSpeed; t.indicatedAirspeed = d->indicatedAirspeed;
            t.verticalSpeed = d->verticalSpeed;
            t.flapsPercent = d->flapsPercent;
            t.elevatorTrim = d->elevatorTrim;
            // SimConnect 的 percent 单位为 0..100，线协议约定为 0..1。
            t.throttle = std::clamp(d->throttle / 100.0, 0.0, 1.0);
            t.autothrottleActive = d->autothrottleActive > 0.5;
            t.autothrottleArmed = d->autothrottleArmed > 0.5;
            t.gearDown = d->gearDown > 0.5;
            t.parkingBrake = d->parkingBrake > 0.5;
            t.onGround = d->onGround > 0.5;
            t.autopilotMaster = d->autopilotMaster > 0.5;
            autopilotMaster_ = t.autopilotMaster;
            t.autopilotHeadingLock = d->autopilotHeadingLock > 0.5;
            t.autopilotNavLock = d->autopilotNavLock > 0.5;
            t.autopilotHeading = std::fmod(d->autopilotHeading, 360.0);
            if (t.autopilotHeading < 0) t.autopilotHeading += 360.0;
            autopilotHeadingLock_ = t.autopilotHeadingLock;
            autopilotNavLock_ = t.autopilotNavLock;
            t.gpsDrivesNav1 = d->gpsDrivesNav1 > 0.5;
            gpsDrivesNav1_ = t.gpsDrivesNav1;
            t.autopilotAltitudeLock = d->autopilotAltitudeLock > 0.5;
            t.autopilotAltitudeArm = d->autopilotAltitudeArm > 0.5;
            t.autopilotAltitude = d->autopilotAltitude;
            t.autopilotVerticalHold = d->autopilotVerticalHold > 0.5;
            t.autopilotVerticalSpeed = d->autopilotVerticalSpeed;
            t.autopilotFlightLevelChange = d->autopilotFlightLevelChange > 0.5;
            t.autopilotSpeed = d->autopilotSpeed;
            t.autopilotApproachArm = d->autopilotApproachArm > 0.5;
            t.autopilotApproachActive = d->autopilotApproachActive > 0.5;
            t.autopilotGlideslopeArm = d->autopilotGlideslopeArm > 0.5;
            t.autopilotGlideslopeActive = d->autopilotGlideslopeActive > 0.5;
            t.gpsWaypointIndex = std::max(0, static_cast<int>(d->gpsWaypointIndex));
            t.gpsWaypointDistance = std::max(0.0, d->gpsWaypointDistance);
            t.nav1Frequency = std::max(0.0, d->nav1Frequency);
            t.nav1HasLocalizer = d->nav1HasLocalizer > 0.5;
            t.nav1HasGlideslope = d->nav1HasGlideslope > 0.5;
            autopilotAltitudeLock_ = t.autopilotAltitudeLock;
            autopilotVerticalHold_ = t.autopilotVerticalHold;
            autopilotFlightLevelChange_ = t.autopilotFlightLevelChange;
            autopilotApproach_ = t.autopilotApproachArm || t.autopilotApproachActive ||
                                 t.autopilotGlideslopeArm || t.autopilotGlideslopeActive;
            t.brakeLeft = std::clamp(d->brakeLeft / 32768.0, 0.0, 1.0);
            t.brakeRight = std::clamp(d->brakeRight / 32768.0, 0.0, 1.0);
            t.seq = telemetrySeq_.fetch_add(1);
            if (onTelemetry_) onTelemetry_(t);
        } else if (obj->dwDefineID == DEF_TITLE) {
            auto* title = reinterpret_cast<SimTitle*>(&obj->dwData);
            title->title[sizeof(SimTitle::title) - 1] = '\0';
            {
                std::lock_guard<std::mutex> lock(titleMtx_);
                aircraftName_ = title->title;
            }
            if (onStatus_) onStatus_(true, AircraftName());
        }
        break;
    }
    case SIMCONNECT_RECV_ID_EVENT_FILENAME: {
        auto* ev = static_cast<SIMCONNECT_RECV_EVENT_FILENAME*>(pRecv);
        if (ev->uEventID == EV_FLIGHTPLAN && onFlightPlan_) {
            ev->szFileName[MAX_PATH - 1] = '\0';
            {
                std::lock_guard<std::mutex> lock(activePlanMtx_);
                activeFlightPlanPath_ = ev->szFileName;
            }
            std::wstring path = PathFromSimConnect(ev->szFileName);
            if (!path.empty()) onFlightPlan_(path);
        }
        break;
    }
    case SIMCONNECT_RECV_ID_EVENT: {
        auto* ev = static_cast<SIMCONNECT_RECV_EVENT*>(pRecv);
        if (ev->uEventID == EV_FLIGHTPLAN_DEACTIVATED) {
            {
                std::lock_guard<std::mutex> lock(activePlanMtx_);
                activeFlightPlanPath_.clear();
            }
            if (onFlightPlan_) onFlightPlan_(L"");
        }
        // 某些 SDK 会额外投递普通 EVENT，但这里不含文件名，等待
        // EVENT_FILENAME，避免用空路径覆盖有效路线。
        break;
    }
    case SIMCONNECT_RECV_ID_OPEN:
        simConnected_ = true;
        break;
    case SIMCONNECT_RECV_ID_QUIT:
        reconnectRequested_ = true;
        break;
    case SIMCONNECT_RECV_ID_EXCEPTION:
        // 机模拒绝某个标准事件或 FlightPlanLoad 失败，不代表 SimConnect
        // 连接已经断开。若在这里重连，复杂机型的一次不支持命令会让全部
        // 遥测和控制进入永久重连循环；保持会话并让真实遥测反映未生效状态。
        break;
    default:
        break;
    }
}
