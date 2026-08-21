#include "TelemetryManager.h"
#include "Protocol.h"
#include "Json.h"
#include <sstream>
#include <chrono>

namespace {
std::string FormatTelemetry(const AircraftTelemetry& t, const std::string& aircraft) {
    std::ostringstream os;
    os << "{\"type\":\"" << proto::kMsgTelemetry
       << "\",\"lat\":" << t.lat
       << ",\"lon\":" << t.lon
       << ",\"alt\":" << t.altitude
       << ",\"altAgl\":" << t.altAgl
       << ",\"hdg\":" << t.heading
       << ",\"magHdg\":" << t.magneticHeading
       << ",\"pitch\":" << t.pitch
       << ",\"roll\":" << t.roll
       << ",\"gs\":" << t.groundSpeed
       << ",\"ias\":" << t.indicatedAirspeed
       << ",\"vs\":" << t.verticalSpeed
       << ",\"flaps\":" << t.flapsPercent
       << ",\"trim\":" << t.elevatorTrim
       << ",\"throttle\":" << t.throttle
       << ",\"autothrottleActive\":" << (t.autothrottleActive ? "true" : "false")
       << ",\"autothrottleArmed\":" << (t.autothrottleArmed ? "true" : "false")
       << ",\"gear\":" << (t.gearDown ? "true" : "false")
       << ",\"parkingBrake\":" << (t.parkingBrake ? "true" : "false")
       << ",\"onGround\":" << (t.onGround ? "true" : "false")
       << ",\"autopilot\":" << (t.autopilotMaster ? "true" : "false")
       << ",\"apHeadingLock\":" << (t.autopilotHeadingLock ? "true" : "false")
       << ",\"apNavLock\":" << (t.autopilotNavLock ? "true" : "false")
       << ",\"apHeading\":" << t.autopilotHeading
       << ",\"gpsDrivesNav1\":" << (t.gpsDrivesNav1 ? "true" : "false")
       << ",\"apAltitudeLock\":" << (t.autopilotAltitudeLock ? "true" : "false")
       << ",\"apAltitudeArm\":" << (t.autopilotAltitudeArm ? "true" : "false")
       << ",\"apAltitude\":" << t.autopilotAltitude
       << ",\"apVerticalHold\":" << (t.autopilotVerticalHold ? "true" : "false")
       << ",\"apVerticalSpeed\":" << t.autopilotVerticalSpeed
       << ",\"apFlc\":" << (t.autopilotFlightLevelChange ? "true" : "false")
       << ",\"apSpeed\":" << t.autopilotSpeed
       << ",\"apApproachArm\":" << (t.autopilotApproachArm ? "true" : "false")
       << ",\"apApproachActive\":" << (t.autopilotApproachActive ? "true" : "false")
       << ",\"apGlideslopeArm\":" << (t.autopilotGlideslopeArm ? "true" : "false")
       << ",\"apGlideslopeActive\":" << (t.autopilotGlideslopeActive ? "true" : "false")
       << ",\"gpsWpIndex\":" << t.gpsWaypointIndex
       << ",\"gpsWpDistance\":" << t.gpsWaypointDistance
       << ",\"nav1Frequency\":" << t.nav1Frequency
       << ",\"nav1HasLocalizer\":" << (t.nav1HasLocalizer ? "true" : "false")
       << ",\"nav1HasGlideslope\":" << (t.nav1HasGlideslope ? "true" : "false")
       << ",\"brakeLeft\":" << t.brakeLeft
       << ",\"brakeRight\":" << t.brakeRight
       << ",\"seq\":" << t.seq
       << ",\"aircraft\":\"" << Json::escape(aircraft) << "\"}";
    return os.str();
}
} // namespace

void TelemetryManager::Start(SendFn send) {
    send_ = std::move(send);
    if (running_.load()) return;
    running_ = true;
    thread_ = std::thread(&TelemetryManager::Loop, this);
}

void TelemetryManager::Stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void TelemetryManager::Push(const AircraftTelemetry& t) {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        latest_ = t;
        hasData_ = true;
    }
}

void TelemetryManager::SetAircraftName(const std::string& name) {
    std::lock_guard<std::mutex> lock(nameMtx_);
    aircraftName_ = name;
}

void TelemetryManager::SetSimConnected(bool connected) {
    simConnected_ = connected;
    hasData_ = false;
}

void TelemetryManager::Loop() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 10 Hz
        if (!send_ || !simConnected_.load() || !hasData_.load()) continue;
        AircraftTelemetry t;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            t = latest_;
        }
        std::string name;
        {
            std::lock_guard<std::mutex> lock(nameMtx_);
            name = aircraftName_;
        }
        send_(FormatTelemetry(t, name));
    }
}
