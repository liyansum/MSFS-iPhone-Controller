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
    @Published var rttMs: Int?
    @Published var gyroState: GyroArmState = .disarmed
    @Published var lastError: String?
    @Published private(set) var diagnostics: [String] = []
    @Published private(set) var networkStatus = "未知"

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
                case .vpnBlocked:
                    parts.append("VPN 拦截")
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
        connect(host: settings.host, udpPort: settings.udpPort, tcpPort: settings.tcpPort)
    }

    func connect(host: String, udpPort: UInt16, tcpPort: UInt16) {
        disconnect()
        udpHost = host
        self.udpPort = udpPort
        controlRate = settings.controlRate
        onMain { [self] in
            phase = .connecting
            lastError = nil
        }

        tcp.onConnected = { [weak self] in self?.sendHello() }
        tcp.onDisconnected = { [weak self] _ in self?.handleDisconnected() }
        tcp.onMessage = { [weak self] data in self?.handleTcpMessage(data) }
        tcp.onStateLog = { [weak self] msg in self?.logDiag(msg) }
        logDiag("connect host=\(host) udp=\(udpPort) tcp=\(tcpPort)")
        tcp.connect(host: host, port: tcpPort)

        connectTimer?.cancel()
        let timer = DispatchWorkItem { [weak self] in
            self?.handleConnectTimeout()
        }
        connectTimer = timer
        DispatchQueue.main.asyncAfter(deadline: .now() + 8, execute: timer)
    }

    /// 设置页测试连接：成功/失败通过 completion 回调
    func testConnection(host: String, udpPort: UInt16, tcpPort: UInt16,
                        completion: @escaping (Bool, String) -> Void) {
        connect(host: host, udpPort: udpPort, tcpPort: tcpPort)
        pendingTestCompletion = completion
        DispatchQueue.main.asyncAfter(deadline: .now() + 5) { [weak self] in
            guard let self = self else { return }
            if let comp = self.pendingTestCompletion {
                self.pendingTestCompletion = nil
                comp(false, "连接超时")
            }
        }
    }

    func disconnect() {
        engine.setArmed(false)
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
            gyroState = .disarmed
        }
    }

    // MARK: - 后台 / 切页安全

    /// App 进入后台时调用：尽量发送回中，解除武装
    func handleBackground() {
        if engine.isArmed { disarmGyro() }
        if let data = engine.zeroAxesPacket() { udp.send(data) }
    }

    /// 切到地图/设置页自动 DISARM
    func autoDisarm() {
        guard engine.isArmed else { return }
        disarmGyro()
    }

    // MARK: - 陀螺仪

    func armGyro() {
        guard phase == .connected else { return }
        engine.setArmed(false)
        motion?.stop()

        let m = MotionManager()
        motion = m
        m.start()
        m.rawAngles.value = (0, 0)
        // 收集一个姿态样本后以此为中性
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) { [weak self] in
            guard let self = self, self.motion === m else { return }
            m.recenter()
            self.engine.setArmed(true)
            self.gyroState = .armed
            Haptics.light()
        }
    }

    func disarmGyro() {
        engine.setArmed(false)
        motion?.stop()
        motion = nil
        gyroState = .disarmed
        if let data = engine.zeroAxesPacket() { udp.send(data) }
        Haptics.light()
    }

    func recenter() {
        guard let m = motion else { return }
        m.recenter()
        engine.setArmed(false)
        engine.setArmed(true)
        Haptics.light()
    }

    // MARK: - 轴输入（由视图调用）

    func beginThrottle(_ v: Float) { engine.beginThrottle(v) }
    func setThrottle(_ v: Float) { engine.setThrottle(v) }
    func endThrottle() { engine.endThrottle() }

    func beginRudder(_ v: Float) { engine.beginRudder(v) }
    func setRudder(_ v: Float) { engine.setRudder(v) }
    func endRudder() {
        engine.endRudder()
        if let data = engine.zeroRudderPacket() { udp.send(data) }
    }

    var gyroDisplay: (roll: Float, pitch: Float) { engine.displaySnapshot() }

    // MARK: - 命令

    func sendCommand(_ name: String, value: Bool? = nil) {
        guard phase == .connected else { return }
        var obj: [String: Any] = ["type": TcpMsg.cmd, "name": name]
        if let v = value { obj["value"] = v }
        if let json = Self.encode(obj) { tcp.send(json: json) }
    }

    // MARK: - TCP

    private func sendHello() {
        let obj: [String: Any] = [
            "type": TcpMsg.hello,
            "protocolVersion": Proto.protocolVersion,
            "appVersion": Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "1.0.0",
            "deviceName": UIDevice.current.name,
        ]
        if let json = Self.encode(obj) { tcp.send(json: json) }
    }

    private func handleTcpMessage(_ data: Data) {
        guard let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let type = obj["type"] as? String else { return }

        switch type {
        case TcpMsg.welcome:
            let sid = obj["sessionId"] as? UInt32 ?? 0
            engine.setSession(sid)
            let sim = obj["simConnected"] as? Bool ?? false
            let name = obj["aircraftName"] as? String ?? ""
            logDiag("welcome: session=\(sid) sim=\(sim) aircraft=\(name)")
            onMain { [self] in
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
                if !name.isEmpty { aircraftName = name }
            }

        case TcpMsg.telemetry:
            if let state = try? JSONDecoder().decode(AircraftState.self, from: data) {
                onMain { [self] in aircraft = state }
            }

        case TcpMsg.route:
            if let fp = Self.parseRoute(obj) {
                onMain { [self] in flightPlan = fp }
            }

        case TcpMsg.error:
            let msg = obj["message"] as? String ?? "未知错误"
            onMain { [self] in lastError = msg }

        default:
            break
        }
    }

    private func handleDisconnected() {
        engine.setArmed(false)
        engine.setSession(0)
        udp.stop()
        logDiag("TCP 连接断开")
        onMain { [self] in
            if phase == .connecting { lastError = "无法连接主机，请检查 IP 与端口" }
            phase = .disconnected
            pcConnected = false
            simConnected = false
            rttMs = nil
            gyroState = .disarmed
        }
    }

    private func handleConnectTimeout() {
        onMain { [self] in
            if phase == .connecting {
                phase = .disconnected
                lastError = "连接超时，请检查主机 IP 与端口"
            }
        }
        udp.stop()
        tcp.disconnect()
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

    private func handleUdpPacket(_ data: Data) {
        guard let pkt = UdpPacket.decode(data), pkt.type == Proto.UdpType.pong.rawValue else { return }
        let now = UInt64(DispatchTime.now().uptimeNanoseconds) / 1_000_000
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
                          waypoints: list)
    }
}
