#include "SimConnectManager.h"
#include "Protocol.h"
#include <SimConnect.h>
#include <chrono>
#include <cstring>
#include <algorithm>

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
    EV_FLIGHTPLAN,
    EV_FLIGHTPLAN_DEACTIVATED,
};

// 与 AddToDataDefinition 顺序严格一致的遥测结构（全 double）
struct SimData {
    double lat, lon, altitude, altAgl;
    double heading, pitch, roll;
    double groundSpeed, indicatedAirspeed, verticalSpeed;
    double flapsPercent, elevatorTrim, throttle;
    double gearDown, parkingBrake, onGround;
};

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
    data(DEF_PLANE, "PLANE PITCH DEGREES", "degrees");
    data(DEF_PLANE, "PLANE BANK DEGREES", "degrees");
    data(DEF_PLANE, "GPS GROUND SPEED", "meters per second");
    data(DEF_PLANE, "AIRSPEED INDICATED", "meters per second");
    data(DEF_PLANE, "VERTICAL SPEED", "meters per second");
    data(DEF_PLANE, "FLAPS HANDLE PERCENT", "percent");
    // PCT 的原生规范是 Percent Over 100，直接得到协议要求的 -1..1。
    data(DEF_PLANE, "ELEVATOR TRIM PCT", "percent over 100");
    data(DEF_PLANE, "GENERAL ENG THROTTLE LEVER POSITION:1", "percent");
    data(DEF_PLANE, "GEAR HANDLE POSITION", "bool");
    data(DEF_PLANE, "BRAKE PARKING INDICATOR", "bool");
    data(DEF_PLANE, "SIM ON GROUND", "bool");
    data(DEF_TITLE, "TITLE", nullptr, SIMCONNECT_DATATYPE_STRING256);

    auto map = [&](DWORD id, const char* eventName) {
        HRESULT hr = SimConnect_MapClientEventToSimEvent(h, id, eventName);
        if (FAILED(hr)) ok = false;
    };
    map(EV_AILERON, "AXIS_AILERONS_SET");
    map(EV_ELEVATOR, "AXIS_ELEVATOR_SET");
    map(EV_RUDDER, "AXIS_RUDDER_SET");
    map(EV_THROTTLE, "AXIS_THROTTLE_SET");
    map(EV_FLAPS_INCR, "FLAPS_INCR");
    map(EV_FLAPS_DECR, "FLAPS_DECR");
    map(EV_GEAR, "GEAR_TOGGLE");
    map(EV_TRIM_UP, "ELEV_TRIM_UP");
    map(EV_TRIM_DN, "ELEV_TRIM_DN");
    map(EV_PARKING, "PARKING_BRAKES");
    map(EV_BRAKE_LEFT, "AXIS_LEFT_BRAKE_SET");
    map(EV_BRAKE_RIGHT, "AXIS_RIGHT_BRAKE_SET");

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
    const bool wasConnected = simConnected_.exchange(false);
    {
        std::lock_guard<std::mutex> lock(cmdMtx_);
        cmdQueue_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(titleMtx_);
        aircraftName_.clear();
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
                SendEvent(hSimConnect_, EV_BRAKE_LEFT, 0);
                SendEvent(hSimConnect_, EV_BRAKE_RIGHT, 0);
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
        case kEvFlapsIncr: SendEvent(h, EV_FLAPS_INCR, 1); break;
        case kEvFlapsDecr: SendEvent(h, EV_FLAPS_DECR, 1); break;
        case kEvGear:      SendEvent(h, EV_GEAR, 1); break;
        case kEvTrimUp:    SendEvent(h, EV_TRIM_UP, 1); break;
        case kEvTrimDn:    SendEvent(h, EV_TRIM_DN, 1); break;
        case kEvParking:   SendEvent(h, EV_PARKING, 1); break;
        case kEvBrakeHold:
            SendEvent(h, EV_BRAKE_LEFT, proto::kThrottleMax);
            SendEvent(h, EV_BRAKE_RIGHT, proto::kThrottleMax);
            break;
        case kEvBrakeRelease:
            SendEvent(h, EV_BRAKE_LEFT, 0);
            SendEvent(h, EV_BRAKE_RIGHT, 0);
            break;
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
            t.heading = d->heading; t.pitch = d->pitch; t.roll = d->roll;
            t.groundSpeed = d->groundSpeed; t.indicatedAirspeed = d->indicatedAirspeed;
            t.verticalSpeed = d->verticalSpeed;
            t.flapsPercent = d->flapsPercent;
            t.elevatorTrim = d->elevatorTrim;
            // SimConnect 的 percent 单位为 0..100，线协议约定为 0..1。
            t.throttle = std::clamp(d->throttle / 100.0, 0.0, 1.0);
            t.gearDown = d->gearDown > 0.5;
            t.parkingBrake = d->parkingBrake > 0.5;
            t.onGround = d->onGround > 0.5;
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
            std::wstring path = PathFromSimConnect(ev->szFileName);
            if (!path.empty()) onFlightPlan_(path);
        }
        break;
    }
    case SIMCONNECT_RECV_ID_EVENT: {
        auto* ev = static_cast<SIMCONNECT_RECV_EVENT*>(pRecv);
        if (ev->uEventID == EV_FLIGHTPLAN_DEACTIVATED && onFlightPlan_)
            onFlightPlan_(L"");
        // 某些 SDK 会额外投递普通 EVENT，但这里不含文件名，等待
        // EVENT_FILENAME，避免用空路径覆盖有效路线。
        break;
    }
    case SIMCONNECT_RECV_ID_OPEN:
        simConnected_ = true;
        break;
    case SIMCONNECT_RECV_ID_QUIT:
    case SIMCONNECT_RECV_ID_EXCEPTION:
        reconnectRequested_ = true;
        break;
    default:
        break;
    }
}
