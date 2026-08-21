import Foundation

// 来自 Windows 端 telemetry JSON 的飞机状态（以 MSFS 回读为准）。

struct AircraftState: Identifiable, Decodable, Equatable {
    var id = UUID()
    var lat: Double = 0
    var lon: Double = 0
    var altitude: Double = 0      // 英尺
    var altAgl: Double = 0        // 英尺
    var heading: Double = 0       // 度
    var magneticHeading: Double = 0 // 度，AP 航向游标使用
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
    var autopilot: Bool = false
    var autopilotHeadingLock: Bool = false
    var autopilotNavLock: Bool = false
    var autopilotHeading: Double = 0
    var gpsDrivesNav1: Bool = false
    var autopilotAltitudeLock: Bool = false
    var autopilotAltitudeArm: Bool = false
    var autopilotAltitude: Double = 0
    var autopilotVerticalHold: Bool = false
    var autopilotVerticalSpeed: Double = 0
    var autopilotFlightLevelChange: Bool = false
    var autopilotSpeed: Double = 0
    var autopilotApproachArm: Bool = false
    var autopilotApproachActive: Bool = false
    var autopilotGlideslopeArm: Bool = false
    var autopilotGlideslopeActive: Bool = false
    var gpsWaypointIndex: Int = 0
    var gpsWaypointDistance: Double = 0
    var nav1Frequency: Double = 0
    var nav1HasLocalizer: Bool = false
    var nav1HasGlideslope: Bool = false
    var brakeLeft: Double = 0    // 0..1
    var brakeRight: Double = 0   // 0..1
    var seq: UInt64 = 0

    enum CodingKeys: String, CodingKey {
        case lat, lon, alt, altAgl, hdg, magHdg, pitch, roll
        case gs, ias, vs, flaps, trim, throttle
        case gear, parkingBrake, onGround, autopilot
        case apHeadingLock, apNavLock, apHeading, gpsDrivesNav1
        case apAltitudeLock, apAltitudeArm, apAltitude
        case apVerticalHold, apVerticalSpeed, apFlc, apSpeed
        case apApproachArm, apApproachActive, apGlideslopeArm, apGlideslopeActive
        case gpsWpIndex, gpsWpDistance
        case nav1Frequency, nav1HasLocalizer, nav1HasGlideslope
        case brakeLeft, brakeRight, seq
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
        magneticHeading = try c.decodeIfPresent(Double.self, forKey: .magHdg) ?? heading
        pitch = try c.decodeIfPresent(Double.self, forKey: .pitch) ?? 0
        roll = try c.decodeIfPresent(Double.self, forKey: .roll) ?? 0
        groundSpeed = try c.decodeIfPresent(Double.self, forKey: .gs) ?? 0
        indicatedAirspeed = try c.decodeIfPresent(Double.self, forKey: .ias) ?? 0
        verticalSpeed = try c.decodeIfPresent(Double.self, forKey: .vs) ?? 0
        flapsPercent = min(max(try c.decodeIfPresent(Double.self, forKey: .flaps) ?? 0, 0), 100)
        elevatorTrim = min(max(try c.decodeIfPresent(Double.self, forKey: .trim) ?? 0, -1), 1)
        throttle = min(max(try c.decodeIfPresent(Double.self, forKey: .throttle) ?? 0, 0), 1)
        gearDown = try c.decodeIfPresent(Bool.self, forKey: .gear) ?? false
        parkingBrake = try c.decodeIfPresent(Bool.self, forKey: .parkingBrake) ?? false
        onGround = try c.decodeIfPresent(Bool.self, forKey: .onGround) ?? false
        autopilot = try c.decodeIfPresent(Bool.self, forKey: .autopilot) ?? false
        autopilotHeadingLock = try c.decodeIfPresent(Bool.self, forKey: .apHeadingLock) ?? false
        autopilotNavLock = try c.decodeIfPresent(Bool.self, forKey: .apNavLock) ?? false
        autopilotHeading = Self.normalizedHeading(
            try c.decodeIfPresent(Double.self, forKey: .apHeading) ?? 0)
        gpsDrivesNav1 = try c.decodeIfPresent(Bool.self, forKey: .gpsDrivesNav1) ?? false
        autopilotAltitudeLock = try c.decodeIfPresent(Bool.self, forKey: .apAltitudeLock) ?? false
        autopilotAltitudeArm = try c.decodeIfPresent(Bool.self, forKey: .apAltitudeArm) ?? false
        autopilotAltitude = max(try c.decodeIfPresent(Double.self, forKey: .apAltitude) ?? 0, 0)
        autopilotVerticalHold = try c.decodeIfPresent(Bool.self, forKey: .apVerticalHold) ?? false
        autopilotVerticalSpeed = try c.decodeIfPresent(Double.self, forKey: .apVerticalSpeed) ?? 0
        autopilotFlightLevelChange = try c.decodeIfPresent(Bool.self, forKey: .apFlc) ?? false
        autopilotSpeed = max(try c.decodeIfPresent(Double.self, forKey: .apSpeed) ?? 0, 0)
        autopilotApproachArm = try c.decodeIfPresent(Bool.self, forKey: .apApproachArm) ?? false
        autopilotApproachActive = try c.decodeIfPresent(Bool.self, forKey: .apApproachActive) ?? false
        autopilotGlideslopeArm = try c.decodeIfPresent(Bool.self, forKey: .apGlideslopeArm) ?? false
        autopilotGlideslopeActive = try c.decodeIfPresent(Bool.self, forKey: .apGlideslopeActive) ?? false
        gpsWaypointIndex = max(try c.decodeIfPresent(Int.self, forKey: .gpsWpIndex) ?? 0, 0)
        gpsWaypointDistance = max(try c.decodeIfPresent(Double.self, forKey: .gpsWpDistance) ?? 0, 0)
        nav1Frequency = max(try c.decodeIfPresent(Double.self, forKey: .nav1Frequency) ?? 0, 0)
        nav1HasLocalizer = try c.decodeIfPresent(Bool.self, forKey: .nav1HasLocalizer) ?? false
        nav1HasGlideslope = try c.decodeIfPresent(Bool.self, forKey: .nav1HasGlideslope) ?? false
        brakeLeft = min(max(try c.decodeIfPresent(Double.self, forKey: .brakeLeft) ?? 0, 0), 1)
        brakeRight = min(max(try c.decodeIfPresent(Double.self, forKey: .brakeRight) ?? 0, 0), 1)
        seq = try c.decodeIfPresent(UInt64.self, forKey: .seq) ?? 0
    }

    private static func normalizedHeading(_ value: Double) -> Double {
        let result = value.truncatingRemainder(dividingBy: 360)
        return result < 0 ? result + 360 : result
    }
}
