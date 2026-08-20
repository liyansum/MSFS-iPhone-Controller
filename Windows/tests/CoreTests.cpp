#include "FlightController.h"
#include "Json.h"
#include "Protocol.h"
#include "FlightPlanManager.h"

#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void TestJsonParserInitializesResult() {
    bool ok = false;
    Json hello = Json::parse(R"({"type":"hello","protocolVersion":1})", ok);
    Check(ok, "valid JSON rejected");
    Check(hello.isObject(), "HELLO is not an object");
    Check(hello.str("type") == "hello", "HELLO type lost");
    Check(hello.num("protocolVersion", -1) == 1, "protocol version lost");

    Json autopilot = Json::parse(R"({"type":"cmd","name":"autopilot","value":true})", ok);
    Check(ok && autopilot.str("name") == proto::kCmdAutopilot,
          "autopilot command was not parsed");
    Check(autopilot.boolean("value", false), "autopilot boolean value was lost");

    Json invalid = Json::parse(R"({"type":)", ok);
    Check(!ok, "invalid JSON accepted");
    Check(invalid.isNull(), "invalid JSON did not return null");
}

void TestNewSessionAcceptsRestartedSequence() {
    FlightController controller;
    Check(controller.UpdateFromControl(900, 1, proto::kAxisAileron, 1200, 0, 0, 0),
          "initial packet reported rejected");
    Check(controller.Snapshot().aileron == 1200, "initial packet rejected");

    controller.ResetSession();
    ControllerState reset = controller.Snapshot();
    Check(reset.aileron == 0, "new session did not center aileron");
    Check((reset.axisMask & proto::kAxisAileron) != 0, "new session did not publish center");

    Check(controller.UpdateFromControl(1, 2, proto::kAxisAileron, -800, 0, 0, 0),
          "new-session packet reported rejected");
    Check(controller.Snapshot().sequence == 1, "restarted sequence rejected");
    Check(controller.Snapshot().aileron == -800, "new-session packet not applied");

    // 相同序号必须被视为重复包。
    Check(!controller.UpdateFromControl(1, 3, proto::kAxisAileron, 999, 0, 0, 0),
          "duplicate packet reported accepted");
    Check(controller.Snapshot().aileron == -800, "duplicate packet applied");
}

void TestAxisValuesAreClampedToSimConnectRanges() {
    FlightController controller;
    Check(controller.UpdateFromControl(1, 1,
          proto::kAxisAileron | proto::kAxisElevator |
          proto::kAxisRudder | proto::kAxisThrottle,
          30000, -30000, 30000, 65000), "range test packet rejected");
    ControllerState state = controller.Snapshot();
    Check(state.aileron == proto::kAxisMax, "aileron was not clamped");
    Check(state.elevator == proto::kAxisMin, "elevator was not clamped");
    Check(state.rudder == proto::kRudderMax, "rudder was not clamped");
    Check(state.throttle == proto::kThrottleMax, "throttle was not clamped");
}

void TestSequenceWrapAndOutOfOrderProtection() {
    FlightController controller;
    Check(controller.UpdateFromControl(0xFFFFFFFEu, 1, proto::kAxisAileron,
                                       100, 0, 0, 0), "high sequence rejected");
    Check(controller.UpdateFromControl(0xFFFFFFFFu, 2, proto::kAxisAileron,
                                       200, 0, 0, 0), "max sequence rejected");
    Check(controller.UpdateFromControl(0u, 3, proto::kAxisAileron,
                                       300, 0, 0, 0), "wrapped sequence rejected");
    Check(!controller.UpdateFromControl(0xFFFFFFFFu, 4, proto::kAxisAileron,
                                        400, 0, 0, 0), "old wrapped packet accepted");
    Check(controller.Snapshot().aileron == 300, "old wrapped packet changed state");
}

void TestFlightPlanBlocksAreIsolated() {
    const std::string xml = R"xml(<?xml version="1.0"?>
<SimBase.Document>
  <FlightPlan.FlightPlan>
    <ATCWaypoint id="CUSTOM &amp; ONE">
      <ATCWaypointType>User</ATCWaypointType>
      <WorldPosition>N40° 00' 00.00",E116° 00' 00.00",+001000.00</WorldPosition>
    </ATCWaypoint>
    <ATCWaypoint id="SECOND">
      <ATCWaypointType>Airport</ATCWaypointType>
      <WorldPosition>N41.5,E117.25,+002000.00</WorldPosition>
      <ICAO><ICAOIdent>ZBBB</ICAOIdent></ICAO>
    </ATCWaypoint>
  </FlightPlan.FlightPlan>
</SimBase.Document>)xml";

    const std::wstring path = L"MSFSControllerCoreTests-plan.pln";
    {
        std::ofstream out(std::filesystem::path(path), std::ios::binary);
        out << xml;
    }
    FlightPlanManager plan;
    const bool loaded = plan.LoadFile(path);
    std::filesystem::remove(std::filesystem::path(path));

    Check(loaded, "flight plan failed to load");
    Check(plan.Waypoints().size() == 2, "flight plan waypoint count incorrect");
    Check(plan.Waypoints()[0].ident == "CUSTOM & ONE", "custom waypoint id was not preserved");
    Check(plan.Waypoints()[1].ident == "ZBBB", "ICAO identifier was not parsed");
    Check(plan.Waypoints()[0].alt == 1000, "comma-separated altitude was not parsed");
    Check(plan.Waypoints()[1].lat == 41.5 && plan.Waypoints()[1].lon == 117.25,
          "decimal waypoint coordinates were not parsed");
}

} // namespace

int main() {
    TestJsonParserInitializesResult();
    TestNewSessionAcceptsRestartedSequence();
    TestAxisValuesAreClampedToSimConnectRanges();
    TestSequenceWrapAndOutOfOrderProtection();
    TestFlightPlanBlocksAreIsolated();
    return 0;
}
