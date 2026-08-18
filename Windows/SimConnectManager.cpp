#include "SimConnectManager.h"
#include "Protocol.h"
#include <SimConnect.h>
#include <chrono>
#include <cstring>

#pragma comment(lib, "SimConnect.lib")

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
    if (hSimConnect_) {
        SimConnect_Close(hSimConnect_);
        hSimConnect_ = nullptr;
    }
    if (thread_.joinable()) thread_.join();
    simConnected_ = false;
    if (g_instance == this) g_instance = nullptr;
}

std::string SimConnectManager::AircraftName() const {
    std::lock_guard<std::mutex> lock(titleMtx_);
    return aircraftName_;
}

void SimConnectManager::EnqueueCommand(int cmd, int arg0) {
    std::lock_guard<std::mutex> lock(cmdMtx_);
    cmdQueue_.push_back(Cmd{ cmd, arg0 });
}

void SimConnectManager::BuildDataDefinitions(HANDLE h) {
    SimConnect_AddToDataDefinition(h, DEF_PLANE, "PLANE LATITUDE", "degrees");
    SimConnect_AddToDataDefinition(h, DEF_PLANE, "PLANE LONGITUDE", "degrees");
    SimConnect_AddToDataDefinition(h, DEF_PLANE, "PLANE ALTITUDE", "feet");
    SimConnect_AddToDataDefinition(h, DEF_PLANE, "PLANE ALT ABOVE GROUND", "feet");
    SimConnect_AddToDataDefinition(h, DEF_PLANE, "PLANE HEADING DEGREES TRUE", "degrees");
    SimConnect_AddToDataDefinition(h, DEF_PLANE, "PLANE PITCH DEGREES", "degrees");
    SimConnect_AddToDataDefinition(h, DEF_PLANE, "PLANE BANK DEGREES", "degrees");
    SimConnect_AddToDataDefinition(h, DEF_PLANE, "GPS GROUND SPEED", "meters per second");
    SimConnect_AddToDataDefinition(h, DEF_PLANE, "AIRSPEED INDICATED", "meters per second");
    SimConnect_AddToDataDefinition(h, DEF_PLANE, "VERTICAL SPEED", "meters per second");
    SimConnect_AddToDataDefinition(h, DEF_PLANE, "FLAPS HANDLE PERCENT", "percent over 100");
    SimConnect_AddToDataDefinition(h, DEF_PLANE, "ELEVATOR TRIM POSITION", "position");
    SimConnect_AddToDataDefinition(h, DEF_PLANE, "GENERAL ENG THROTTLE LEVER POSITION:1", "percent");
    SimConnect_AddToDataDefinition(h, DEF_PLANE, "GEAR HANDLE POSITION", "bool");
    SimConnect_AddToDataDefinition(h, DEF_PLANE, "BRAKE PARKING INDICATOR", "bool");
    SimConnect_AddToDataDefinition(h, DEF_PLANE, "SIM ON GROUND", "bool");
    SimConnect_AddToDataDefinition(h, DEF_TITLE, "TITLE", nullptr, SIMCONNECT_DATATYPE_STRING256);
}

void SimConnectManager::ThreadMain() {
    while (running_.load()) {
        if (!hSimConnect_) {
            HRESULT hr = SimConnect_Open(&hSimConnect_, "MSFS iPhone Controller",
                                         nullptr, 0, 0, 0);
            if (SUCCEEDED(hr)) {
                BuildDataDefinitions(hSimConnect_);

                SimConnect_MapClientEventToSimEvent(hSimConnect_, EV_AILERON, "AXIS_AILERONS_SET");
                SimConnect_MapClientEventToSimEvent(hSimConnect_, EV_ELEVATOR, "AXIS_ELEVATOR_SET");
                SimConnect_MapClientEventToSimEvent(hSimConnect_, EV_RUDDER, "AXIS_RUDDER_SET");
                SimConnect_MapClientEventToSimEvent(hSimConnect_, EV_THROTTLE, "AXIS_THROTTLE_SET");
                SimConnect_MapClientEventToSimEvent(hSimConnect_, EV_FLAPS_INCR, "FLAPS_INCR");
                SimConnect_MapClientEventToSimEvent(hSimConnect_, EV_FLAPS_DECR, "FLAPS_DECR");
                SimConnect_MapClientEventToSimEvent(hSimConnect_, EV_GEAR, "GEAR_TOGGLE");
                SimConnect_MapClientEventToSimEvent(hSimConnect_, EV_TRIM_UP, "ELEV_TRIM_UP");
                SimConnect_MapClientEventToSimEvent(hSimConnect_, EV_TRIM_DN, "ELEV_TRIM_DN");
                SimConnect_MapClientEventToSimEvent(hSimConnect_, EV_PARKING, "PARKING_BRAKES");
                SimConnect_MapClientEventToSimEvent(hSimConnect_, EV_BRAKE_LEFT, "AXIS_LEFT_BRAKE_SET");
                SimConnect_MapClientEventToSimEvent(hSimConnect_, EV_BRAKE_RIGHT, "AXIS_RIGHT_BRAKE_SET");

                SimConnect_SubscribeToSystemEvent(hSimConnect_, EV_FLIGHTPLAN, "FlightPlanActivated");

                SimConnect_RequestDataOnSimObject(hSimConnect_, DEF_PLANE, DEF_PLANE,
                                                  SIMCONNECT_OBJECT_ID_USER,
                                                  SIMCONNECT_PERIOD_SIM_FRAME);
                SimConnect_RequestDataOnSimObject(hSimConnect_, DEF_TITLE, DEF_TITLE,
                                                  SIMCONNECT_OBJECT_ID_USER,
                                                  SIMCONNECT_PERIOD_ONCE);
                lastTitleRequestMs_ = NowMs();

                simConnected_ = true;
                if (onStatus_) {
                    std::string name = AircraftName();
                    onStatus_(true, name);
                }
            } else {
                simConnected_ = false;
                if (onStatus_) onStatus_(false, "");
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
        }

        HRESULT hr = SimConnect_CallDispatch(hSimConnect_, SimDispatch, nullptr);
        if (FAILED(hr)) {
            SimConnect_Close(hSimConnect_);
            hSimConnect_ = nullptr;
            simConnected_ = false;
            if (onStatus_) onStatus_(false, "");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        if (hSimConnect_) {
            ApplyControlState(hSimConnect_);
            DrainCommands(hSimConnect_);

            // 周期性刷新飞机名称
            if (NowMs() - lastTitleRequestMs_ >= 10000) {
                lastTitleRequestMs_ = NowMs();
                SimConnect_RequestDataOnSimObject(hSimConnect_, DEF_TITLE, DEF_TITLE,
                                                  SIMCONNECT_OBJECT_ID_USER,
                                                  SIMCONNECT_PERIOD_ONCE);
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
    SimConnect_TransmitClientEvent(h, SIMCONNECT_OBJECT_ID_USER, eventId, value,
                                   SIMCONNECT_GROUP_PRIORITY_HIGHEST,
                                   SIMCONNECT_EVENT_FLAG_GROUPID_IS_PRIORITY);
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
            t.throttle = d->throttle;
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
#ifdef SIMCONNECT_RECV_ID_EVENT_FILENAME
    case SIMCONNECT_RECV_ID_EVENT_FILENAME: {
        auto* ev = static_cast<SIMCONNECT_RECV_EVENT_FILENAME*>(pRecv);
        if (ev->uEventID == EV_FLIGHTPLAN && onFlightPlan_) {
            const char* bytes = reinterpret_cast<const char*>(ev->fFileName);
            size_t size = ev->fFileNameSize;
            if (size >= 2 && bytes[0] != '\0' && bytes[1] == '\0') {
                std::wstring ws(reinterpret_cast<const wchar_t*>(ev->fFileName));
                onFlightPlan_(ws);
            } else {
                std::wstring ws(bytes, bytes + strlen(bytes));
                onFlightPlan_(ws);
            }
        }
        break;
    }
#endif
    case SIMCONNECT_RECV_ID_EVENT: {
        auto* ev = static_cast<SIMCONNECT_RECV_EVENT*>(pRecv);
        if (ev->uEventID == EV_FLIGHTPLAN && onFlightPlan_) {
            onFlightPlan_(L"");
        }
        break;
    }
    case SIMCONNECT_RECV_ID_OPEN:
        simConnected_ = true;
        break;
    case SIMCONNECT_RECV_ID_QUIT:
    case SIMCONNECT_RECV_ID_EXCEPTION:
        simConnected_ = false;
        break;
    default:
        break;
    }
}
