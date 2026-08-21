import Foundation

// 计划航线（来自 Windows 端 route JSON）。

struct Waypoint: Identifiable, Equatable {
    var index: Int
    var ident: String
    var lat: Double
    var lon: Double
    var alt: Double

    // route 刷新时保持稳定身份，避免同一航路仅因 UUID 重建而被误判为已变化。
    var id: String { "\(index):\(ident):\(lat):\(lon)" }
}

struct FlightPlan: Equatable {
    var departure: String = ""
    var destination: String = ""
    var departureRunway: String = ""
    var departureProcedure: String = ""
    var arrivalProcedure: String = ""
    var approachType: String = ""
    var destinationRunway: String = ""
    var cruisingAltitude: Double = 0
    var waypoints: [Waypoint] = []
}
