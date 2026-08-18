import Foundation

// 来自 Windows 端 telemetry JSON 的飞机状态（以 MSFS 回读为准）。

struct AircraftState: Identifiable, Codable, Equatable {
    var id = UUID()
    var lat: Double = 0
    var lon: Double = 0
    var altitude: Double = 0      // 英尺
    var altAgl: Double = 0        // 英尺
    var heading: Double = 0       // 度
    var pitch: Double = 0         // 度
    var roll: Double = 0          // 度
    var groundSpeed: Double = 0   // m/s
    var indicatedAirspeed: Double = 0 // m/s
    var verticalSpeed: Double = 0 // m/s
    var flapsPercent: Double = 0  // 0..100
    var elevatorTrim: Double = 0  // -1..1
    var throttle: Double = 0      // 0..1
    var gearDown: Bool = false
    var parkingBrake: Bool = false
    var onGround: Bool = false
    var seq: UInt64 = 0

    enum CodingKeys: String, CodingKey {
        case lat, lon, alt, altAgl, hdg, pitch, roll
        case gs, ias, vs, flaps, trim, throttle
        case gear, parkingBrake, onGround, seq
    }

    init() {}

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = UUID()
        lat = try c.decodeIfPresent(Double.self, forKey: .lat) ?? 0
        lon = try c.decodeIfPresent(Double.self, forKey: .lon) ?? 0
        altitude = try c.decodeIfPresent(Double.self, forKey: .alt) ?? 0
        altAgl = try c.decodeIfPresent(Double.self, forKey: .altAgl) ?? 0
        heading = try c.decodeIfPresent(Double.self, forKey: .hdg) ?? 0
        pitch = try c.decodeIfPresent(Double.self, forKey: .pitch) ?? 0
        roll = try c.decodeIfPresent(Double.self, forKey: .roll) ?? 0
        groundSpeed = try c.decodeIfPresent(Double.self, forKey: .gs) ?? 0
        indicatedAirspeed = try c.decodeIfPresent(Double.self, forKey: .ias) ?? 0
        verticalSpeed = try c.decodeIfPresent(Double.self, forKey: .vs) ?? 0
        flapsPercent = try c.decodeIfPresent(Double.self, forKey: .flaps) ?? 0
        elevatorTrim = try c.decodeIfPresent(Double.self, forKey: .trim) ?? 0
        throttle = try c.decodeIfPresent(Double.self, forKey: .throttle) ?? 0
        gearDown = try c.decodeIfPresent(Bool.self, forKey: .gear) ?? false
        parkingBrake = try c.decodeIfPresent(Bool.self, forKey: .parkingBrake) ?? false
        onGround = try c.decodeIfPresent(Bool.self, forKey: .onGround) ?? false
        seq = try c.decodeIfPresent(UInt64.self, forKey: .seq) ?? 0
    }
}
