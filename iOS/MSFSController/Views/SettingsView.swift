import SwiftUI

// 设置页：连接 / 陀螺仪 / 网络 / 安全。

struct SettingsView: View {
    @EnvironmentObject var conn: ConnectionManager
    @EnvironmentObject var settings: SettingsStore

    @State private var editingHost = ""
    @State private var testing = false
    @State private var testResult: String?

    var body: some View {
        Form {
            connectionSection
            gyroSection
            networkSection
            safetySection
        }
        .onAppear { editingHost = settings.host }
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
