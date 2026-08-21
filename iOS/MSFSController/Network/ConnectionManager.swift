import Foundation
import SwiftUI
import Combine
import Network

// 连接与控制的协调者：TCP 会话、UDP 控制、陀螺仪生命周期、命令发送。

final class ConnectionManager: ObservableObject {
    // MARK: - 已发布状态（仅在主线程修改）

    @Published var phase: ConnectionPhase = .disconnected
    @Published var pcConnected = false
    @Published var simConnected = false
    @Published var aircraftName = ""
    @Published var flightPlan = FlightPlan()
    @Published var aircraft = AircraftState()
    @Published private(set) var hasTelemetry = false
    @Published var rttMs: Int?
    @Published var gyroState: GyroArmState = .disarmed
    @Published var lastError: String?
    @Published private(set) var diagnostics: [String] = []
    @Published private(set) var networkStatus = "未知"
    @Published private(set) var controlResetToken: UInt64 = 0
    @Published private(set) var routeSyncMessage = ""

    // MARK: - 内部

    private let settings: SettingsStore
    private let tcp = TCPClient()
    private let udp = UDPController()
    private let engine: ControlEngine
    private var motion: MotionManager?
    private var cancellables = Set<AnyCancellable>()
    private var pathMonitor: NWPathMonitor?

    private var pendingTestCompletion: ((Bool, String) -> Void)?
    private var connectTimer: DispatchWorkItem?
    private var connectionAttempt: UInt64 = 0
    private var udpHost = ""
    private var udpPort: UInt16 = 0
    private var controlRate = 60.0

    private static let maxLogCount = 60

    init(settings: SettingsStore) {
        self.settings = settings
        self.engine = ControlEngine(settings: settings.gyro)
        // 设置变化实时应用到控制引擎
        settings.$gyro
            .sink { [weak self] g in self?.engine.applySettings(g) }
            .store(in: &cancellables)
        startPathMonitor()
    }

    private func startPathMonitor() {
        let monitor = NWPathMonitor()
        pathMonitor = monitor
        monitor.pathUpdateHandler = { [weak self] path in
            let desc = Self.describe(path)
            self?.onMain { [weak self] in
                self?.networkStatus = desc
            }
        }
        monitor.start(queue: DispatchQueue(label: "msfs.path"))
    }

    private static func describe(_ path: NWPath) -> String {
        var parts: [String] = []
        switch path.status {
        case .satisfied: parts.append("网络可用")
        case .unsatisfied:
            parts.append("网络不可用")
            if #available(iOS 15, *) {
                switch path.unsatisfiedReason {
                case .localNetworkDenied:
                    parts.append("本地网络权限被拒绝！请到 设置>隐私>本地网络 允许本 App，并完全重启 App")
                case .notAvailable:
                    parts.append("未连接到网络")
                case .cellularDenied:
                    parts.append("蜂窝数据被拒绝")
                case .wifiDenied:
                    parts.append("Wi-Fi 被拒绝")
                @unknown default:
                    break
                }
            }
        case .requiresConnection: parts.append("需要连接")
        @unknown default: break
        }
        if path.usesInterfaceType(.wifi) { parts.append("Wi-Fi") }
        if path.usesInterfaceType(.cellular) { parts.append("蜂窝") }
        if path.usesInterfaceType(.wiredEthernet) { parts.append("有线") }
        return parts.joined(separator: " | ")
    }

    /// 追加诊断日志（主线程安全）
    func logDiag(_ msg: String) {
        let line = Self.nowString() + " " + msg
        onMain { [weak self] in
            guard let self = self else { return }
            self.diagnostics.append(line)
            if self.diagnostics.count > Self.maxLogCount {
                self.diagnostics.removeFirst(self.diagnostics.count - Self.maxLogCount)
            }
        }
    }

    func clearDiagnostics() {
        onMain { [weak self] in
            self?.diagnostics.removeAll()
        }
    }

    private static func nowString() -> String {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss.SSS"
        return f.string(from: Date())
    }

    var hasConnection: Bool { phase == .connected }

    // MARK: - 连接

    func connect() {
        beginConnection(host: settings.host, udpPort: settings.udpPort,
                        tcpPort: settings.tcpPort, testCompletion: nil)
    }

    func connect(host: String, udpPort: UInt16, tcpPort: UInt16) {
        beginConnection(host: host, udpPort: udpPort, tcpPort: tcpPort, testCompletion: nil)
    }

    private func beginConnection(host: String, udpPort: UInt16, tcpPort: UInt16,
                                 testCompletion: ((Bool, String) -> Void)?) {
        let normalizedHost = host.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !normalizedHost.isEmpty else {
            onMain { [self] in
                phase = .disconnected
                lastError = "请先填写 Windows 主机 IP"
                testCompletion?(false, "主机 IP 为空")
            }
            return
        }

        if let previous = pendingTestCompletion {
            pendingTestCompletion = nil
            previous(false, "连接已被新的请求替换")
        }
        resetConnectionState(clearError: true)
        connectionAttempt &+= 1
        let attempt = connectionAttempt
        pendingTestCompletion = testCompletion
        udpHost = normalizedHost
        self.udpPort = udpPort
        controlRate = settings.controlRate
        onMain { [self] in
            phase = .connecting
            lastError = nil
        }

        tcp.onConnected = { [weak self] in
            self?.onMain { [weak self] in
                guard let self, self.connectionAttempt == attempt else { return }
                self.sendHello()
            }
        }
        tcp.onDisconnected = { [weak self] _ in
            self?.onMain { [weak self] in
                guard let self, self.connectionAttempt == attempt else { return }
                self.handleDisconnected()
            }
        }
        tcp.onMessage = { [weak self] data in
            self?.onMain { [weak self] in
                guard let self, self.connectionAttempt == attempt else { return }
                self.handleTcpMessage(data)
            }
        }
        tcp.onStateLog = { [weak self] msg in self?.logDiag(msg) }
        logDiag("connect #\(attempt) host=\(normalizedHost) udp=\(udpPort) tcp=\(tcpPort)")
        tcp.connect(host: normalizedHost, port: tcpPort)

        connectTimer?.cancel()
        let timer = DispatchWorkItem { [weak self] in
            self?.handleConnectTimeout(attempt: attempt)
        }
        connectTimer = timer
        DispatchQueue.main.asyncAfter(deadline: .now() + 7, execute: timer)
    }

    /// 设置页测试连接：成功/失败通过 completion 回调
    func testConnection(host: String, udpPort: UInt16, tcpPort: UInt16,
                        completion: @escaping (Bool, String) -> Void) {
        beginConnection(host: host, udpPort: udpPort, tcpPort: tcpPort,
                        testCompletion: completion)
    }

    func disconnect() {
        connectionAttempt &+= 1
        if let completion = pendingTestCompletion {
            pendingTestCompletion = nil
            completion(false, "连接已取消")
        }
        resetConnectionState(clearError: false)
    }

    private func resetConnectionState(clearError: Bool) {
        releaseAllControls(notifyServer: true)
        motion?.stop()
        motion = nil
        udp.stop()
        tcp.disconnect()
        connectTimer?.cancel()
        connectTimer = nil
        engine.setSession(0)
        onMain { [self] in
            phase = .disconnected
            pcConnected = false
            simConnected = false
            rttMs = nil
            hasTelemetry = false
            gyroState = .disarmed
            routeSyncMessage = ""
            if clearError { lastError = nil }
        }
    }

    // MARK: - 后台安全

    /// App 进入后台时调用：尽量发送回中，解除武装
    func handleBackground() {
        releaseAllControls(notifyServer: true)
    }

    private func releaseAllControls(notifyServer: Bool) {
        engine.setArmed(false)
        engine.cancelTransientInputs()
        motion?.stop()
        motion = nil
        onMain { [weak self] in
            self?.gyroState = .disarmed
            self?.controlResetToken &+= 1
        }
        guard notifyServer else { return }
        if let data = engine.zeroAxesPacket() { udp.send(data) }
        if phase == .connected && simConnected,
           let json = Self.encode(["type": TcpMsg.cmd,
                                   "name": TcpCmd.brake,
                                   "value": false]) {
            tcp.send(json: json)
        }
    }

    // MARK: - 陀螺仪

    func armGyro() {
        guard phase == .connected, simConnected else {
            onMain { [weak self] in self?.lastError = "请先启动 MSFS 并等待模拟器连接" }
            return
        }
        engine.setArmed(false)
        motion?.stop()

        let m = MotionManager()
        guard m.isAvailable else {
            onMain { [self] in lastError = "此设备无法使用姿态传感器" }
            return
        }
        motion = m
        m.onAngles = { [weak self] pitch, roll in
            self?.engine.updateRawAngles(pitchDeg: pitch, rollDeg: roll)
        }
        m.rawAngles.value = (0, 0)
        let orientation = UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }
            .first?.interfaceOrientation ?? .landscapeLeft
        m.start(orientation: orientation, recenterOnFirstSample: true,
                onReady: { [weak self, weak m] in
                    self?.onMain { [weak self] in
                        guard let self, let m, self.motion === m,
                              self.phase == .connected, self.simConnected else { return }
                        self.engine.setArmed(true)
                        self.gyroState = .armed
                        self.lastError = nil
                        Haptics.light()
                    }
                },
                onError: { [weak self, weak m] message in
                    self?.onMain { [weak self] in
                        guard let self, let m, self.motion === m else { return }
                        m.stop()
                        self.motion = nil
                        self.engine.setArmed(false)
                        self.gyroState = .disarmed
                        self.lastError = "姿态传感器错误：\(message)"
                    }
                })
    }

    func disarmGyro() {
        engine.setArmed(false)
        motion?.stop()
        motion = nil
        gyroState = .disarmed
        if let data = engine.zeroGyroAxesPacket() { udp.send(data) }
        Haptics.light()
    }

    func recenter() {
        guard let m = motion else { return }
        guard m.recenter() else { return }
        engine.setArmed(false)
        engine.setArmed(true)
        Haptics.light()
    }

    // MARK: - 轴输入（由视图调用）

    func increaseThrottle() { sendCommand(TcpCmd.throttleIncr) }
    func decreaseThrottle() { sendCommand(TcpCmd.throttleDecr) }

    func beginRudder(_ v: Float) {
        guard phase == .connected, simConnected else { return }
        engine.beginRudder(v)
    }
    func setRudder(_ v: Float) {
        guard phase == .connected, simConnected else { return }
        engine.setRudder(v)
    }
    func endRudder() {
        engine.endRudder()
        if let data = engine.zeroRudderPacket() { udp.send(data) }
    }

    var gyroDisplay: (roll: Float, pitch: Float) { engine.displaySnapshot() }

    // MARK: - 命令

    func sendCommand(_ name: String, value: Bool? = nil) {
        guard phase == .connected, simConnected else {
            onMain { [weak self] in self?.lastError = "MSFS 尚未连接，命令未发送" }
            return
        }
        var obj: [String: Any] = ["type": TcpMsg.cmd, "name": name]
        if let v = value { obj["value"] = v }
        if let json = Self.encode(obj) { tcp.send(json: json) }
    }

    func setAutopilot(_ enabled: Bool) {
        guard phase == .connected, simConnected else {
            onMain { [weak self] in self?.lastError = "MSFS 尚未连接，命令未发送" }
            return
        }
        // 先暂停/恢复手机姿态轴，避免 AP 接通瞬间被 60 Hz 轴输入再次断开。
        engine.setAutopilotActive(enabled)
        sendCommand(TcpCmd.autopilot, value: enabled)
    }

    func setAutopilotMode(_ mode: String) {
        guard phase == .connected, simConnected else {
            onMain { [weak self] in self?.lastError = "MSFS 尚未连接，命令未发送" }
            return
        }
        let obj: [String: Any] = ["type": TcpMsg.cmd,
                                  "name": TcpCmd.autopilotMode,
                                  "value": mode]
        if let json = Self.encode(obj) { tcp.send(json: json) }
    }

    func setAutopilotHeading(_ degrees: Int) {
        guard phase == .connected, simConnected else {
            onMain { [weak self] in self?.lastError = "MSFS 尚未连接，命令未发送" }
            return
        }
        let normalized = ((degrees % 360) + 360) % 360
        let obj: [String: Any] = ["type": TcpMsg.cmd,
                                  "name": TcpCmd.autopilotHeading,
                                  "value": normalized]
        if let json = Self.encode(obj) { tcp.send(json: json) }
    }

    func setNavigationSource(_ source: String) {
        guard phase == .connected, simConnected else {
            onMain { [weak self] in self?.lastError = "MSFS 尚未连接，命令未发送" }
            return
        }
        let obj: [String: Any] = ["type": TcpMsg.cmd,
                                  "name": TcpCmd.navigationSource,
                                  "value": source]
        if let json = Self.encode(obj) { tcp.send(json: json) }
    }

    func setAutopilotAltitude(_ feet: Int) {
        sendNumericCommand(TcpCmd.autopilotAltitude, value: min(max(feet, 0), 60_000))
    }

    func setAutopilotVerticalSpeed(_ feetPerMinute: Int) {
        sendNumericCommand(TcpCmd.autopilotVerticalSpeed,
                           value: min(max(feetPerMinute, -6_000), 6_000))
    }

    func setAutopilotSpeed(_ knots: Int) {
        sendNumericCommand(TcpCmd.autopilotSpeed, value: min(max(knots, 40), 400))
    }

    func setAutopilotVerticalMode(_ mode: String) {
        sendStringCommand(TcpCmd.autopilotVerticalMode, value: mode)
    }

    func setAutopilotApproach(_ enabled: Bool) {
        sendCommand(TcpCmd.autopilotApproach, value: enabled)
    }

    func syncFlightPlan() {
        guard !flightPlan.waypoints.isEmpty else {
            lastError = "没有可同步的活动航路"
            return
        }
        if usesExternalFmsRoute {
            routeSyncMessage = "当前机型使用专有 FMGS/FMC，请先在机内导入或设置航路"
            return
        }
        sendCommand(TcpCmd.syncFlightPlan)
        routeSyncMessage = "已请求将当前 .PLN 重新载入标准 GPS"
    }

    func flyHeading(_ degrees: Int) {
        setAutopilotHeading(degrees)
        setAutopilotMode("heading")
        setAutopilot(true)
    }

    func followRoute(syncFirst: Bool) {
        let shouldSyncStandardGps = syncFirst && !usesExternalFmsRoute
        if shouldSyncStandardGps { syncFlightPlan() }
        // 老款 Asobo A320neo 可走标准活动 .PLN/GPS 流程；V2 与第三方客机
        // 使用专有 FMGS/FMC，GPS DRIVES NAV1 不能用于写入其 MCDU。
        if !usesExternalFmsRoute { setNavigationSource("gps") }
        let delay = shouldSyncStandardGps ? 0.8 : 0
        DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
            guard let self, self.phase == .connected, self.simConnected else { return }
            self.setAutopilotMode("nav")
            self.setAutopilot(true)
        }
    }

    func holdCurrentAltitude() {
        let target = Int((aircraft.altitude / 100).rounded() * 100)
        setAutopilotAltitude(target)
        setAutopilotVerticalMode("hold")
        setAutopilot(true)
    }

    func flyVerticalSpeed(targetAltitude: Int, feetPerMinute: Int) {
        setAutopilotAltitude(targetAltitude)
        setAutopilotVerticalSpeed(feetPerMinute)
        setAutopilotVerticalMode("vs")
        setAutopilot(true)
    }

    func flyFlightLevelChange(targetAltitude: Int, speedKnots: Int) {
        setAutopilotAltitude(targetAltitude)
        setAutopilotSpeed(speedKnots)
        setAutopilotVerticalMode("flc")
        setAutopilot(true)
    }

    func prepareApproach() {
        // 只预位已在机载导航中配置的进近，不再重新加载标准 .PLN。
        DispatchQueue.main.async { [weak self] in
            guard let self, self.phase == .connected, self.simConnected else { return }
            self.setAutopilotApproach(true)
            self.setAutopilot(true)
        }
    }

    var isA320neoV2: Bool {
        let title = aircraftName.lowercased()
        return title.contains("a320") &&
            (title.contains("v2") || title.contains("ini builds") || title.contains("inibuilds"))
    }

    /// 主要支持目标：MSFS 2020 原始 Asobo/Legacy A320neo，而不是 A320neo V2。
    var isLegacyA320neo: Bool {
        let title = aircraftName.lowercased()
        guard title.contains("a320"), title.contains("neo") else { return false }
        return !usesExternalFmsRoute
    }

    /// 这些机型的专有 FMGS/FMC 航路不能按标准 GPS `.PLN` 路径可靠写入。
    var usesExternalFmsRoute: Bool {
        let title = aircraftName.lowercased()
        return isA320neoV2 || title.contains("flybywire") || title.contains("a32nx") ||
            title.contains("fenix")
    }

    var aircraftProfileLabel: String {
        if isLegacyA320neo { return "老款 A320neo · 标准航路适配" }
        if usesExternalFmsRoute { return "专有 FMGS/FMC · 仅后续 AP" }
        return "标准 GPS / SimConnect"
    }

    private func sendNumericCommand(_ name: String, value: Int) {
        guard phase == .connected, simConnected else {
            onMain { [weak self] in self?.lastError = "MSFS 尚未连接，命令未发送" }
            return
        }
        let obj: [String: Any] = ["type": TcpMsg.cmd, "name": name, "value": value]
        if let json = Self.encode(obj) { tcp.send(json: json) }
    }

    private func sendStringCommand(_ name: String, value: String) {
        guard phase == .connected, simConnected else {
            onMain { [weak self] in self?.lastError = "MSFS 尚未连接，命令未发送" }
            return
        }
        let obj: [String: Any] = ["type": TcpMsg.cmd, "name": name, "value": value]
        if let json = Self.encode(obj) { tcp.send(json: json) }
    }

    // MARK: - TCP

    private func sendHello() {
        let obj: [String: Any] = [
            "type": TcpMsg.hello,
            "protocolVersion": Proto.protocolVersion,
            "appVersion": Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "1.1.3",
            "deviceName": UIDevice.current.name,
        ]
        if let json = Self.encode(obj) { tcp.send(json: json) }
    }

    private func handleTcpMessage(_ data: Data) {
        guard let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let type = obj["type"] as? String else { return }

        switch type {
        case TcpMsg.welcome:
            let version = (obj["protocolVersion"] as? NSNumber)?.uint8Value ?? 0
            guard version == Proto.protocolVersion else {
                logDiag("协议版本不匹配: PC=\(version) iOS=\(Proto.protocolVersion)")
                failCurrentConnection("PC 与 iPhone 协议版本不兼容")
                return
            }
            let sid = (obj["sessionId"] as? NSNumber)?.uint32Value ?? 0
            guard sid != 0 else {
                logDiag("welcome 缺少有效 sessionId")
                failCurrentConnection("PC 返回了无效会话")
                return
            }
            engine.setSession(sid)
            let sim = obj["simConnected"] as? Bool ?? false
            let name = obj["aircraftName"] as? String ?? ""
            logDiag("welcome: session=\(sid) sim=\(sim) aircraft=\(name)")
            onMain { [self] in
                connectTimer?.cancel()
                connectTimer = nil
                phase = .connected
                pcConnected = true
                simConnected = sim
                aircraftName = name
                lastError = nil
                if let comp = pendingTestCompletion {
                    pendingTestCompletion = nil
                    comp(true, "已连接")
                }
            }
            startUdp()

        case TcpMsg.status:
            let sim = obj["simConnected"] as? Bool ?? false
            let name = obj["aircraftName"] as? String ?? ""
            onMain { [self] in
                simConnected = sim
                if !sim { hasTelemetry = false }
                if !name.isEmpty || !sim { aircraftName = name }
            }
            if !sim { releaseAllControls(notifyServer: false) }

        case TcpMsg.telemetry:
            if let state = try? JSONDecoder().decode(AircraftState.self, from: data) {
                guard (-90.0...90.0).contains(state.lat),
                      (-180.0...180.0).contains(state.lon) else {
                    logDiag("丢弃越界遥测坐标: \(state.lat), \(state.lon)")
                    return
                }
                engine.setAutopilotActive(state.autopilot)
                onMain { [self] in
                    aircraft = state
                    hasTelemetry = true
                }
            }

        case TcpMsg.route:
            if let fp = Self.parseRoute(obj) {
                onMain { [self] in
                    if fp != flightPlan { routeSyncMessage = "" }
                    flightPlan = fp
                }
            }

        case TcpMsg.error:
            let msg = obj["message"] as? String ?? "未知错误"
            logDiag("PC 错误: \(msg)")
            if phase == .connecting { failCurrentConnection(msg) }
            else if lastError != msg { lastError = msg }

        default:
            break
        }
    }

    private func handleDisconnected() {
        releaseAllControls(notifyServer: false)
        engine.setSession(0)
        udp.stop()
        logDiag("TCP 连接断开")
        onMain { [self] in
            let wasConnecting = phase == .connecting
            if wasConnecting { lastError = "无法连接主机，请检查 IP、端口及 Windows 防火墙" }
            phase = .disconnected
            pcConnected = false
            simConnected = false
            rttMs = nil
            hasTelemetry = false
            gyroState = .disarmed
            connectTimer?.cancel()
            connectTimer = nil
            if let completion = pendingTestCompletion {
                pendingTestCompletion = nil
                completion(false, lastError ?? "连接已断开")
            }
        }
    }

    private func failCurrentConnection(_ message: String) {
        connectionAttempt &+= 1
        connectTimer?.cancel()
        connectTimer = nil
        releaseAllControls(notifyServer: false)
        engine.setSession(0)
        udp.stop()
        tcp.disconnect()
        phase = .disconnected
        pcConnected = false
        simConnected = false
        rttMs = nil
        hasTelemetry = false
        lastError = message
        if let completion = pendingTestCompletion {
            pendingTestCompletion = nil
            completion(false, message)
        }
    }

    private func handleConnectTimeout(attempt: UInt64) {
        onMain { [self] in
            guard attempt == connectionAttempt, phase == .connecting else { return }
            connectionAttempt &+= 1
            phase = .disconnected
            lastError = "连接超时，请检查主机 IP、局域网权限及 Windows 防火墙"
            if let completion = pendingTestCompletion {
                pendingTestCompletion = nil
                completion(false, lastError ?? "连接超时")
            }
            connectTimer = nil
            udp.stop()
            tcp.disconnect()
        }
    }

    // MARK: - UDP

    private func startUdp() {
        logDiag("UDP 启动 host=\(udpHost) port=\(udpPort) rate=\(controlRate)Hz")
        udp.start(host: udpHost, port: udpPort)
        udp.receive { [weak self] data in self?.handleUdpPacket(data) }
        udp.startControlLoop(rateHz: controlRate) { [weak self] in
            self?.engine.makeControlPacket()
        }
        udp.startPingLoop(intervalMs: 1000) { [weak self] in
            self?.engine.makePing()
        }
    }

    private func sendControlImmediately() {
        if let data = engine.makeControlPacket() { udp.send(data) }
    }

    private func handleUdpPacket(_ data: Data) {
        guard let pkt = UdpPacket.decode(data),
              pkt.type == Proto.UdpType.pong.rawValue,
              engine.matchesSession(pkt.sessionId) else { return }
        let now = UInt64(DispatchTime.now().uptimeNanoseconds) / 1_000_000
        guard now >= pkt.timestampMs else { return }
        let rtt = Int(now - pkt.timestampMs)
        onMain { [self] in
            if rtt >= 0 && rtt < 10000 { rttMs = rtt }
        }
    }

    // MARK: - 工具

    private func onMain(_ block: @escaping () -> Void) {
        if Thread.isMainThread { block() } else { DispatchQueue.main.async(execute: block) }
    }

    private static func encode(_ obj: [String: Any]) -> String? {
        guard let data = try? JSONSerialization.data(withJSONObject: obj) else { return nil }
        return String(data: data, encoding: .utf8)
    }

    private static func parseRoute(_ obj: [String: Any]) -> FlightPlan? {
        guard let wps = obj["waypoints"] as? [[String: Any]] else { return nil }
        var list: [Waypoint] = []
        for (i, w) in wps.enumerated() {
            list.append(Waypoint(index: w["index"] as? Int ?? i,
                                 ident: w["ident"] as? String ?? "",
                                 lat: w["lat"] as? Double ?? 0,
                                 lon: w["lon"] as? Double ?? 0,
                                 alt: w["alt"] as? Double ?? 0))
        }
        return FlightPlan(departure: obj["departure"] as? String ?? "",
                          destination: obj["destination"] as? String ?? "",
                          departureRunway: obj["departureRunway"] as? String ?? "",
                          departureProcedure: obj["departureProcedure"] as? String ?? "",
                          arrivalProcedure: obj["arrivalProcedure"] as? String ?? "",
                          approachType: obj["approachType"] as? String ?? "",
                          destinationRunway: obj["destinationRunway"] as? String ?? "",
                          cruisingAltitude: (obj["cruisingAltitude"] as? NSNumber)?.doubleValue ?? 0,
                          waypoints: list)
    }
}
