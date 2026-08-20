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
       << ",\"pitch\":" << t.pitch
       << ",\"roll\":" << t.roll
       << ",\"gs\":" << t.groundSpeed
       << ",\"ias\":" << t.indicatedAirspeed
       << ",\"vs\":" << t.verticalSpeed
       << ",\"flaps\":" << t.flapsPercent
       << ",\"trim\":" << t.elevatorTrim
       << ",\"throttle\":" << t.throttle
       << ",\"gear\":" << (t.gearDown ? "true" : "false")
       << ",\"parkingBrake\":" << (t.parkingBrake ? "true" : "false")
       << ",\"onGround\":" << (t.onGround ? "true" : "false")
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
