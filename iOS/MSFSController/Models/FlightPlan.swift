import Foundation

// 计划航线（来自 Windows 端 route JSON）。

struct Waypoint: Identifiable, Equatable {
    var id = UUID()
    var index: Int
    var ident: String
    var lat: Double
    var lon: Double
    var alt: Double
}

struct FlightPlan: Identifiable, Equatable {
    var id = UUID()
    var departure: String = ""
    var destination: String = ""
    var waypoints: [Waypoint] = []
}
