import SwiftUI

// 设置页：连接 / 陀螺仪 / 网络 / 安全。

struct SettingsView: View {
    @EnvironmentObject var conn: ConnectionManager
    @EnvironmentObject var settings: SettingsStore

    @State private var editingHost = ""
    @State private var testing = false
    @State private var testResult: String?

    private let discovery = DiscoveryManager()
    @State private var discovered: [DiscoveryManager.Host] = []
    @State private var scanning = false

    var body: some View {
        Form {
            connectionSection
            discoverySection
            gyroSection
            networkSection
            safetySection
            diagnosticsSection
        }
        .onAppear {
            editingHost = settings.host
            if conn.phase == .disconnected { scan() }
        }
        .onDisappear {
            discovery.stop()
            scanning = false
        }
        .onChange(of: editingHost) { _, v in
            testResult = nil
            _ = v
        }
    }

    // MARK: - CONNECTION

    private var connectionSection: some View {
        Section("CONNECTION") {
            HStack {
                Text("Host IP")
                TextField("192.168.1.100", text: $editingHost)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .keyboardType(.numbersAndPunctuation)
                    .multilineTextAlignment(.trailing)
            }
            HStack {
                Text("UDP Port")
                Spacer()
                TextField("UDP", value: $settings.udpPort, format: .number)
                    .keyboardType(.numberPad)
                    .frame(width: 80)
                    .multilineTextAlignment(.trailing)
            }
            HStack {
                Text("TCP Port")
                Spacer()
                TextField("TCP", value: $settings.tcpPort, format: .number)
                    .keyboardType(.numberPad)
                    .frame(width: 80)
                    .multilineTextAlignment(.trailing)
            }

            Button {
                test()
            } label: {
                Text(testing ? "测试中..." : "Test Connection")
            }
            .disabled(testing)

            if let r = testResult {
                Text(r)
                    .font(.caption)
                    .foregroundColor(r.hasPrefix("✓") ? .green : .red)
            }

            Button("保存并连接") {
                saveAndConnect()
            }
            .disabled(trimmedHost.isEmpty)

            statusRow
        }
    }

    private var statusRow: some View {
        Group {
            if conn.pcConnected {
                Label("PC 已连接", systemImage: "checkmark.circle.fill").foregroundColor(.green)
            } else {
                Label("PC 未连接", systemImage: "xmark.circle.fill").foregroundColor(.red)
            }
            if conn.pcConnected {
                Label(conn.simConnected ? "MSFS 已连接" : "等待 MSFS...",
                      systemImage: conn.simConnected ? "airplane" : "clock")
                    .foregroundColor(conn.simConnected ? .green : .yellow)
            }
            if let rtt = conn.rttMs {
                Label("延迟 \(rtt) ms", systemImage: "bolt").foregroundColor(.secondary)
            }
        }
        .font(.caption)
    }

    // MARK: - DISCOVER

    private var discoverySection: some View {
        Section("DISCOVER") {
            Button {
                scan()
            } label: {
                Label(scanning ? "扫描中..." : "自动探测局域网主机", systemImage: "wifi")
            }
            .disabled(scanning)

            if !discovered.isEmpty {
                ForEach(discovered) { host in
                    Button {
                        connectTo(host: host)
                    } label: {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(host.name.isEmpty ? "MSFS 主机" : host.name)
                                .font(.headline)
                            Text(host.ips.joined(separator: ", "))
                                .font(.caption)
                                .foregroundColor(.secondary)
                        }
                    }
                }
            } else if scanning {
                HStack { Spacer(); ProgressView(); Spacer() }
            }

            if !scanning && discovered.isEmpty {
                Text("未发现主机：请确认手机与电脑在同一局域网，并在 iOS「设置 > 隐私 > 本地网络」允许本 App")
                    .font(.caption)
                    .foregroundColor(.orange)
            }
        }
    }

    private func scan() {
        guard !scanning else { return }
        scanning = true
        discovered.removeAll()
        discovery.onLog = { [weak self] msg in
            // DiscoveryManager 是类，闭包可用 weak self 捕获当前视图上下文
            self?.conn.logDiag(msg)
        }
        discovery.start { host in
            let existingIps = Set(self.discovered.flatMap { $0.ips })
            let newIps = host.ips.filter { !existingIps.contains($0) }
            guard !newIps.isEmpty else { return }
            self.discovered.append(host)
        }
        conn.logDiag("开始自动探测 (6s)")
        DispatchQueue.main.asyncAfter(deadline: .now() + 6) {
            if self.scanning {
                self.scanning = false
                self.discovery.stop()
                self.conn.logDiag("自动探测结束")
            }
        }
    }

    private func connectTo(host: DiscoveryManager.Host) {
        guard let ip = host.ips.first else { return }
        settings.host = ip
        settings.udpPort = host.udpPort
        settings.tcpPort = host.tcpPort
        editingHost = ip
        discovery.stop()
        scanning = false
        conn.connect()
        Haptics.success()
    }

    // MARK: - GYRO

    private var gyroSection: some View {
        Section("GYRO") {
            percentSlider("Roll Sensitivity",
                          value: $settings.gyro.rollSensitivity, range: 0.5...1.5)
            percentSlider("Pitch Sensitivity",
                          value: $settings.gyro.pitchSensitivity, range: 0.5...1.5)
            degreeSlider("Roll Max Angle",
                         value: $settings.gyro.rollMaxAngle, range: 10...60, suffix: "°")
            degreeSlider("Pitch Max Angle",
                         value: $settings.gyro.pitchMaxAngle, range: 10...60, suffix: "°")
            degreeSlider("Deadzone",
                         value: $settings.gyro.deadzoneDeg, range: 0...10, suffix: "°")
            expoSilder
            Toggle("Smoothing", isOn: $settings.gyro.smoothing)
            Toggle("Invert Roll", isOn: $settings.gyro.invertRoll)
            Toggle("Invert Pitch", isOn: $settings.gyro.invertPitch)
        }
    }

    private func percentSlider(_ title: String, value: Binding<Double>, range: ClosedRange<Double>) -> some View {
        VStack(alignment: .leading) {
            Text("\(title)  \(Int(value.wrappedValue * 100))%")
            Slider(value: value, in: range)
        }
    }

    private func degreeSlider(_ title: String, value: Binding<Double>, range: ClosedRange<Double>, suffix: String) -> some View {
        VStack(alignment: .leading) {
            Text("\(title)  \(Int(value.wrappedValue))\(suffix)")
            Slider(value: value, in: range)
        }
    }

    private var expoSilder: some View {
        VStack(alignment: .leading) {
            Text(String(format: "Expo  %.1f", settings.gyro.expo))
            Slider(value: $settings.gyro.expo, in: 1.0...2.5)
        }
    }

    // MARK: - NETWORK

    private var networkSection: some View {
        Section("NETWORK") {
            Picker("Control Rate", selection: $settings.controlRate) {
                Text("60 Hz").tag(60.0)
                Text("100 Hz").tag(100.0)
            }
            .pickerStyle(.segmented)
        }
    }

    // MARK: - SAFETY

    private var safetySection: some View {
        Section("SAFETY") {
            LabeledContent("Disconnect Timeout", value: "250 ms")
        }
    }

    // MARK: - DIAGNOSTICS

    private var diagnosticsSection: some View {
        Section {
            LabeledContent("网络路径", value: conn.networkStatus)
            if conn.networkStatus.contains("本地网络权限被拒绝") {
                Text("请到 iPhone「设置 > 隐私与安全性 > 本地网络」允许本 App，然后完全退出并重启 App")
                    .font(.caption)
                    .foregroundColor(.red)
            }
            LabeledContent("TCP/UDP", value: conn.phase == .connected ? "已连接" : "未连接")
            LabeledContent("探测发送/应答", value: "\(discovery.sentCount) / \(discovery.replyCount)")

            Button("清除日志") {
                conn.clearDiagnostics()
            }

            ScrollView {
                VStack(alignment: .leading, spacing: 2) {
                    ForEach(conn.diagnostics, id: \.self) { line in
                        Text(line)
                            .font(.system(size: 10, design: .monospaced))
                            .foregroundColor(.secondary)
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }
                }
            }
            .frame(maxHeight: 180)
        } header: {
            Text("DIAGNOSTICS")
        } footer: {
            Text("若日志为空，说明 App 未发起任何网络请求：请确认 iPhone 已连接 Wi-Fi（非蜂窝），并检查本地网络权限。")
        }
    }

    // MARK: - 动作

    private var trimmedHost: String {
        editingHost.trimmingCharacters(in: .whitespaces)
    }

    private func test() {
        guard !trimmedHost.isEmpty else {
            testResult = "请输入主机 IP"
            return
        }
        testing = true
        testResult = nil
        conn.testConnection(host: trimmedHost, udpPort: settings.udpPort, tcpPort: settings.tcpPort) { ok, msg in
            DispatchQueue.main.async {
                testing = false
                testResult = ok ? "✓ \(msg)" : "✗ \(msg)"
            }
        }
    }

    private func saveAndConnect() {
        settings.host = trimmedHost
        conn.connect()
    }
}
