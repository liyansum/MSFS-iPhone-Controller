import Foundation

// 与 Windows/Protocol.h 严格一致的协议定义。

enum Proto {
    static let magic: UInt32 = 0x4D534631          // "MSF1"
    static let protocolVersion: UInt8 = 1
    static let defaultUdpPort: UInt16 = 36666
    static let defaultTcpPort: UInt16 = 36667
    static let discoveryPort: UInt16 = 36668
    static let controlTimeout: TimeInterval = 0.25
    static let serverVersion = "1.1.0"
    static let discoveryRequest = "MSFS_DISCOVER\n"

    static let kAxisAileron: UInt16 = 1 << 0
    static let kAxisElevator: UInt16 = 1 << 1
    static let kAxisRudder: UInt16 = 1 << 2
    static let kAxisThrottle: UInt16 = 1 << 3

    static let axisMin: Int16 = -16383
    static let axisMax: Int16 = 16384
    static let rudderMin: Int16 = -16383
    static let rudderMax: Int16 = 16383
    static let throttleMax: UInt16 = 16383

    enum UdpType: UInt8 {
        case control = 1
        case ping = 2
        case pong = 3
    }
}

// 32 字节固定二进制 UDP 包（小端）
struct UdpPacket {
    var type: UInt8
    var sequence: UInt32
    var timestampMs: UInt64
    var sessionId: UInt32
    var axisMask: UInt16
    var aileron: Int16
    var elevator: Int16
    var rudder: Int16
    var throttle: UInt16

    func encode() -> Data {
        var d = Data()
        d.appendLE(Proto.magic)
        d.append(type)
        d.append(Proto.protocolVersion)
        d.appendLE(sequence)
        d.appendLE(timestampMs)
        d.appendLE(sessionId)
        d.appendLE(axisMask)
        d.appendLE(aileron)
        d.appendLE(elevator)
        d.appendLE(rudder)
        d.appendLE(throttle)
        return d
    }

    static func decode(_ data: Data) -> UdpPacket? {
        guard data.count == 32 else { return nil }
        var idx = 0
        guard data.readUInt32LE(at: &idx) == Proto.magic else { return nil }
        let type = data[idx]; idx += 1
        let version = data[idx]; idx += 1
        guard version == Proto.protocolVersion else { return nil }
        let seq = data.readUInt32LE(at: &idx)
        let ts = data.readUInt64LE(at: &idx)
        let sid = data.readUInt32LE(at: &idx)
        let mask = data.readUInt16LE(at: &idx)
        let ail = data.readInt16LE(at: &idx)
        let elev = data.readInt16LE(at: &idx)
        let rud = data.readInt16LE(at: &idx)
        let thr = data.readUInt16LE(at: &idx)
        return UdpPacket(type: type, sequence: seq, timestampMs: ts,
                         sessionId: sid, axisMask: mask,
                         aileron: ail, elevator: elev, rudder: rud, throttle: thr)
    }
}

// TCP 消息类型（JSON，一行一条）
enum TcpMsg {
    static let hello = "hello"
    static let cmd = "cmd"
    static let welcome = "welcome"
    static let status = "status"
    static let telemetry = "telemetry"
    static let route = "route"
    static let error = "error"
    static let hostDiscovery = "msfs_host"
}

enum TcpCmd {
    static let flapsIncr = "flaps_incr"
    static let flapsDecr = "flaps_decr"
    static let gear = "gear"
    static let trimUp = "trim_up"
    static let trimDn = "trim_dn"
    static let parkingBrake = "parking_brake"
    static let brake = "brake"
    static let autopilot = "autopilot"
    static let autopilotMode = "autopilot_mode"
    static let autopilotHeading = "autopilot_heading"
    static let navigationSource = "navigation_source"
    static let autopilotAltitude = "autopilot_altitude"
    static let autopilotVerticalMode = "autopilot_vertical_mode"
    static let autopilotVerticalSpeed = "autopilot_vertical_speed"
    static let autopilotSpeed = "autopilot_speed"
    static let autopilotApproach = "autopilot_approach"
    static let syncFlightPlan = "sync_flight_plan"
}

// MARK: - 小端编解码辅助

extension Data {
    mutating func appendLE(_ v: UInt16) { append(UInt8(v & 0xFF)); append(UInt8((v >> 8) & 0xFF)) }
    mutating func appendLE(_ v: UInt32) {
        for i in 0..<4 { append(UInt8((v >> (8 * i)) & 0xFF)) }
    }
    mutating func appendLE(_ v: UInt64) {
        for i in 0..<8 { append(UInt8((v >> (8 * i)) & 0xFF)) }
    }
    mutating func appendLE(_ v: Int16) { appendLE(UInt16(bitPattern: v)) }

    func readUInt16LE(at idx: inout Int) -> UInt16 {
        let v = UInt16(self[idx]) | (UInt16(self[idx + 1]) << 8)
        idx += 2
        return v
    }
    func readInt16LE(at idx: inout Int) -> Int16 { Int16(bitPattern: readUInt16LE(at: &idx)) }
    func readUInt32LE(at idx: inout Int) -> UInt32 {
        var v: UInt32 = 0
        for i in 0..<4 { v |= UInt32(self[idx + i]) << (8 * i) }
        idx += 4
        return v
    }
    func readUInt64LE(at idx: inout Int) -> UInt64 {
        var v: UInt64 = 0
        for i in 0..<8 { v |= UInt64(self[idx + i]) << (8 * i) }
        idx += 8
        return v
    }
}
