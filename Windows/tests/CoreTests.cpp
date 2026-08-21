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

    Json mode = Json::parse(R"({"type":"cmd","name":"autopilot_mode","value":"nav"})", ok);
    Check(ok && mode.str("name") == proto::kCmdAutopilotMode,
          "autopilot mode command was not parsed");
    Check(mode.str("value") == "nav", "autopilot mode value was lost");

    Json heading = Json::parse(R"({"type":"cmd","name":"autopilot_heading","value":273})", ok);
    Check(ok && heading.str("name") == proto::kCmdAutopilotHeading,
          "autopilot heading command was not parsed");
    Check(heading.num("value", -1) == 273, "autopilot heading value was lost");

    Json source = Json::parse(R"({"type":"cmd","name":"navigation_source","value":"gps"})", ok);
    Check(ok && source.str("name") == proto::kCmdNavigationSource,
          "navigation source command was not parsed");
    Check(source.str("value") == "gps", "navigation source value was lost");

    Json altitude = Json::parse(R"({"type":"cmd","name":"autopilot_altitude","value":10000})", ok);
    Check(ok && altitude.str("name") == proto::kCmdAutopilotAltitude,
          "autopilot altitude command was not parsed");
    Check(altitude.num("value", -1) == 10000, "autopilot altitude value was lost");

    Json vertical = Json::parse(
        R"({"type":"cmd","name":"autopilot_vertical_mode","value":"flc"})", ok);
    Check(ok && vertical.str("name") == proto::kCmdAutopilotVerticalMode,
          "autopilot vertical mode command was not parsed");
    Check(vertical.str("value") == "flc", "autopilot vertical mode value was lost");

    Json approach = Json::parse(
        R"({"type":"cmd","name":"autopilot_approach","value":true})", ok);
    Check(ok && approach.str("name") == proto::kCmdAutopilotApproach,
          "autopilot approach command was not parsed");
    Check(approach.boolean("value", false), "autopilot approach value was lost");

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

    Check(controller.UpdateFromControl(2, 2, proto::kAxisThrottle,
                                       0, 0, 0, 0), "zero throttle packet rejected");
    Check(controller.Snapshot().throttle == 0, "zero throttle was not preserved");
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
    <CruisingAlt>12000</CruisingAlt>
    <DepartureID>ZBAA</DepartureID>
    <DeparturePosition>36L</DeparturePosition>
    <DestinationID>ZSPD</DestinationID>
    <ATCWaypoint id="CUSTOM &amp; ONE">
      <ATCWaypointType>User</ATCWaypointType>
      <WorldPosition>N40° 00' 00.00",E116° 00' 00.00",+001000.00</WorldPosition>
      <DepartureFP>RENOB1</DepartureFP>
      <RunwayNumberFP>36</RunwayNumberFP>
      <RunwayDesignatorFP>LEFT</RunwayDesignatorFP>
    </ATCWaypoint>
    <ATCWaypoint id="SECOND">
      <ATCWaypointType>Airport</ATCWaypointType>
      <WorldPosition>N41.5,E117.25,+002000.00</WorldPosition>
      <ICAO><ICAOIdent>ZBBB</ICAOIdent></ICAO>
      <ArrivalFP>SASAN2</ArrivalFP>
      <ApproachTypeFP>ILS</ApproachTypeFP>
      <RunwayNumberFP>16</RunwayNumberFP>
      <RunwayDesignatorFP>RIGHT</RunwayDesignatorFP>
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
    Check(plan.Departure() == "ZBAA" && plan.Destination() == "ZSPD",
          "plan endpoints were not parsed");
    Check(plan.DepartureRunway() == "36L", "departure runway was not parsed");
    Check(plan.DepartureProcedure() == "RENOB1", "departure procedure was not parsed");
    Check(plan.ArrivalProcedure() == "SASAN2", "arrival procedure was not parsed");
    Check(plan.ApproachType() == "ILS" && plan.DestinationRunway() == "16R",
          "approach and destination runway were not parsed");
    Check(plan.CruisingAltitude() == 12000, "cruising altitude was not parsed");

    plan.Clear();
    Check(plan.Waypoints().empty() && plan.Departure().empty() &&
          plan.DestinationRunway().empty(), "flight plan metadata was not cleared");
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
