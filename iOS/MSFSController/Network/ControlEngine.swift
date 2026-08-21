import Foundation

// 控制引擎：线程安全地计算每个控制包的内容。
// UDP 控制循环在其队列调用 makeControlPacket；主线程调用 ARM/拖动等方法。

final class ControlEngine {
    private let lock = NSLock()

    private var sessionId: UInt32 = 0
    private var sequence: UInt32 = 0

    private var armed = false
    private var rawPitchDeg: Double = 0
    private var rawRollDeg: Double = 0
    private var smoothAileron: Float = 0
    private var smoothElevator: Float = 0
    private var autopilotActive = false

    private var throttleActive = false
    private var throttle: Float = 0
    private var rudderDragging = false
    private var rudder: Float = 0

    var settings: GyroSettings

    init(settings: GyroSettings) {
        self.settings = settings
    }

    /// 设置变化时调用（主线程），与打包路径加锁保护
    func applySettings(_ s: GyroSettings) {
        lock.lock(); settings = s; lock.unlock()
    }

    var isArmed: Bool {
        lock.lock(); defer { lock.unlock() }
        return armed
    }

    func matchesSession(_ id: UInt32) -> Bool {
        lock.lock(); defer { lock.unlock() }
        return id != 0 && sessionId == id
    }

    func setSession(_ id: UInt32) {
        lock.lock()
        sessionId = id
        sequence = 0
        armed = false
        rawPitchDeg = 0
        rawRollDeg = 0
        smoothAileron = 0
        smoothElevator = 0
        autopilotActive = false
        throttleActive = false
        rudderDragging = false
        rudder = 0
        lock.unlock()
    }

    func updateRawAngles(pitchDeg: Double, rollDeg: Double) {
        lock.lock(); rawPitchDeg = pitchDeg; rawRollDeg = rollDeg; lock.unlock()
    }

    /// AP 接管时暂停姿态轴，但保留 ARM 状态；AP 关闭后自动恢复手机姿态控制。
    func setAutopilotActive(_ active: Bool) {
        lock.lock(); autopilotActive = active; lock.unlock()
    }

    func setArmed(_ v: Bool) {
        lock.lock()
        armed = v
        if !v {
            smoothAileron = 0
            smoothElevator = 0
            rawPitchDeg = 0
            rawRollDeg = 0
        }
        lock.unlock()
    }

    func beginThrottle(_ v: Float) {
        lock.lock()
        throttleActive = true
        throttle = min(max(v, 0), 1)
        lock.unlock()
    }
    func setThrottle(_ v: Float) {
        lock.lock()
        throttleActive = true
        throttle = min(max(v, 0), 1)
        lock.unlock()
    }
    /// 松手后返回最终目标并释放实时 UDP 轴；最终值由 TCP 可靠提交。
    func endThrottle() -> Float {
        lock.lock()
        throttleActive = false
        let finalValue = throttle
        lock.unlock()
        return finalValue
    }

    func beginRudder(_ v: Float) {
        lock.lock(); rudderDragging = true; rudder = min(max(v, -1), 1); lock.unlock()
    }
    func setRudder(_ v: Float) {
        lock.lock(); rudder = min(max(v, -1), 1); lock.unlock()
    }
    func endRudder() {
        lock.lock(); rudderDragging = false; rudder = 0; lock.unlock()
    }

    /// 后台、断线或 MSFS 离线时终止所有瞬时输入。
    /// 油门数值保留用于 UI，但不再覆盖模拟器；方向舵立即归零。
    func cancelTransientInputs() {
        lock.lock()
        throttleActive = false
        rudderDragging = false
        rudder = 0
        lock.unlock()
    }

    /// 当前陀螺仪显示值（-100..100），供 UI 显示
    func displaySnapshot() -> (roll: Float, pitch: Float) {
        lock.lock(); defer { lock.unlock() }
        return (smoothAileron * 100, smoothElevator * 100)
    }

    // MARK: - 打包

    private func nowMs() -> UInt64 {
        UInt64(DispatchTime.now().uptimeNanoseconds) / 1_000_000
    }

    /// 实时控制包；无有效轴数据时返回 nil（不发送）
    func makeControlPacket() -> Data? {
        lock.lock()
        defer { lock.unlock() }

        guard sessionId != 0 else { return nil }
        var mask: UInt16 = 0
        var ail: Int16 = 0, elev: Int16 = 0, rud: Int16 = 0
        var thr: UInt16 = 0
        let timestamp = nowMs()

        if armed && !autopilotActive {
            let s = settings
            let rawAil = ControlCurve.normalized(angle: rawPitchDeg,
                                                 maxAngle: s.rollMaxAngle,
                                                 deadzoneDeg: s.deadzoneDeg,
                                                 expo: s.expo,
                                                 sensitivity: s.rollSensitivity,
                                                 invert: s.invertRoll)
            let rawElev = ControlCurve.normalized(angle: rawRollDeg,
                                                  maxAngle: s.pitchMaxAngle,
                                                  deadzoneDeg: s.deadzoneDeg,
                                                  expo: s.expo,
                                                  sensitivity: s.pitchSensitivity,
                                                  invert: s.invertPitch)
            let factor: Float = s.smoothing ? 0.4 : 1.0
            smoothAileron = ControlCurve.smooth(current: smoothAileron, target: rawAil, factor: factor)
            smoothElevator = ControlCurve.smooth(current: smoothElevator, target: rawElev, factor: factor)
            ail = Self.axis(smoothAileron)
            elev = Self.axis(smoothElevator)
            mask |= Proto.kAxisAileron | Proto.kAxisElevator
        }

        if rudderDragging {
            rud = Self.rudderAxis(rudder)
            mask |= Proto.kAxisRudder
        }
        // 只在拖动期间实时占用；松手由可靠 TCP 提交最终值后释放，避免与
        // A/THR、驾驶舱操作或实体油门长期争夺同一控制轴。
        if throttleActive {
            thr = UInt16(min(max(throttle, 0), 1) * Float(Proto.throttleMax))
            mask |= Proto.kAxisThrottle
        }

        guard mask != 0 else { return nil }
        sequence &+= 1
        return UdpPacket(type: Proto.UdpType.control.rawValue,
                         sequence: sequence,
                         timestampMs: timestamp,
                         sessionId: sessionId,
                         axisMask: mask,
                         aileron: ail,
                         elevator: elev,
                         rudder: rud,
                         throttle: thr).encode()
    }

    /// Ping 包
    func makePing() -> Data? {
        lock.lock(); defer { lock.unlock() }
        guard sessionId != 0 else { return nil }
        sequence &+= 1
        return UdpPacket(type: Proto.UdpType.ping.rawValue,
                         sequence: sequence,
                         timestampMs: nowMs(),
                         sessionId: sessionId,
                         axisMask: 0, aileron: 0, elevator: 0, rudder: 0, throttle: 0).encode()
    }

    /// 回中包：Aileron/Elevator/Rudder 置 0（用于后台 / 断线）
    func zeroAxesPacket() -> Data? {
        lock.lock(); defer { lock.unlock() }
        guard sessionId != 0 else { return nil }
        sequence &+= 1
        let mask = Proto.kAxisAileron | Proto.kAxisElevator | Proto.kAxisRudder
        return UdpPacket(type: Proto.UdpType.control.rawValue,
                         sequence: sequence,
                         timestampMs: nowMs(),
                         sessionId: sessionId,
                         axisMask: mask,
                         aileron: 0, elevator: 0, rudder: 0, throttle: 0).encode()
    }

    /// 仅交还陀螺仪控制的姿态轴。关闭 GYRO 不得清除油门或方向舵所有权。
    func zeroGyroAxesPacket() -> Data? {
        lock.lock(); defer { lock.unlock() }
        guard sessionId != 0 else { return nil }
        sequence &+= 1
        return UdpPacket(type: Proto.UdpType.control.rawValue,
                         sequence: sequence,
                         timestampMs: nowMs(),
                         sessionId: sessionId,
                         axisMask: Proto.kAxisAileron | Proto.kAxisElevator,
                         aileron: 0, elevator: 0, rudder: 0, throttle: 0).encode()
    }

    /// 仅 Rudder 回中包（不影响陀螺仪轴）
    func zeroRudderPacket() -> Data? {
        lock.lock(); defer { lock.unlock() }
        guard sessionId != 0 else { return nil }
        sequence &+= 1
        return UdpPacket(type: Proto.UdpType.control.rawValue,
                         sequence: sequence,
                         timestampMs: nowMs(),
                         sessionId: sessionId,
                         axisMask: Proto.kAxisRudder,
                         aileron: 0, elevator: 0, rudder: 0, throttle: 0).encode()
    }

    static func axis(_ v: Float) -> Int16 {
        let clamped = min(max(v, -1), 1)
        let scaled = clamped * Float(Proto.axisMax)
        let i = Int32(scaled.rounded())
        return Int16(min(max(i, Int32(Proto.axisMin)), Int32(Proto.axisMax)))
    }

    static func rudderAxis(_ v: Float) -> Int16 {
        let clamped = min(max(v, -1), 1)
        let scaled = clamped * Float(Proto.rudderMax)
        return Int16(min(max(Int32(scaled.rounded()), Int32(Proto.rudderMin)),
                         Int32(Proto.rudderMax)))
    }
}
