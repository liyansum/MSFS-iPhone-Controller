#pragma once
// 飞行计划管理：解析 FlightPlanActivated 事件给出的 .PLN 文件为航点列表。

#include <string>
#include <vector>

struct Waypoint {
    int index = 0;
    std::string ident;
    std::string type;   // Airport / Vor / Ndb / Waypoint / User
    double lat = 0;
    double lon = 0;
    double alt = 0;
};

class FlightPlanManager {
public:
    bool LoadFile(const std::wstring& path);
    bool LoadFile(const std::string& path);
    void Clear();

    const std::vector<Waypoint>& Waypoints() const { return wps_; }
    std::string Departure() const;
    std::string Destination() const;
    const std::string& DepartureRunway() const { return departureRunway_; }
    const std::string& DepartureProcedure() const { return departureProcedure_; }
    const std::string& ArrivalProcedure() const { return arrivalProcedure_; }
    const std::string& ApproachType() const { return approachType_; }
    const std::string& DestinationRunway() const { return destinationRunway_; }
    double CruisingAltitude() const { return cruisingAltitude_; }
    std::string Summary() const;   // "ZBAA -> ZSPD"

private:
    void ParseXml(const std::string& xml);
    static bool ParseWorldPosition(const std::string& token, double& lat, double& lon);

    std::vector<Waypoint> wps_;
    std::string departureId_;
    std::string destinationId_;
    std::string departureRunway_;
    std::string departureProcedure_;
    std::string arrivalProcedure_;
    std::string approachType_;
    std::string destinationRunway_;
    double cruisingAltitude_ = 0;
};
