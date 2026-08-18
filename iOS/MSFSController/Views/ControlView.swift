import SwiftUI

// 控制首页（横屏）：油门 / 陀螺仪 / Trim / Rudder / 常用按钮。

struct ControlView: View {
    @EnvironmentObject var conn: ConnectionManager

    @State private var displayRoll: Float = 0
    @State private var displayPitch: Float = 0

    private let timer = Timer.publish(every: 0.1, on: .main, in: .common).autoconnect()

    var body: some View {
        VStack(spacing: 8) {
            header
            HStack(alignment: .top, spacing: 12) {
                throttleSection
                    .frame(width: 88)
                gyroSection
                    .frame(maxWidth: .infinity)
                trimSection
                    .frame(width: 120)
            }
            rudderSection
                .frame(height: 56)
            bottomRow
        }
        .padding(10)
        .onReceive(timer) { _ in
            let d = conn.gyroDisplay
            displayRoll = d.roll
            displayPitch = d.pitch
        }
    }

    // MARK: - 顶部状态

    private var header: some View {
        HStack(spacing: 12) {
            Text("● PC")
                .foregroundColor(conn.pcConnected ? .green : .red)
            Text("● MSFS")
                .foregroundColor(conn.simConnected ? .green : (conn.pcConnected ? .yellow : .red))
            Text(conn.rttMs.map { "\($0) ms" } ?? "--")
                .font(.caption.monospacedDigit())
                .foregroundColor(.secondary)
            Spacer()
            Text(conn.aircraftName)
                .font(.caption)
                .foregroundColor(.secondary)
                .lineLimit(1)
        }
        .font(.caption.bold())
    }

    // MARK: - 油门

    private var throttleSection: some View {
        VStack(spacing: 4) {
            Text("THROTTLE")
                .font(.caption)
                .foregroundColor(.secondary)
            ThrottleView(telemetryThrottle: conn.aircraft.throttle,
                         onBegin: { conn.beginThrottle($0) },
                         onChange: { conn.setThrottle($0) },
                         onEnd: { conn.endThrottle() })
                .frame(maxHeight: .infinity)
        }
    }

    // MARK: - 陀螺仪

    private var gyroSection: some View {
        GyroView(roll: displayRoll,
                 pitch: displayPitch,
                 armed: conn.gyroState == .armed,
                 onToggle: { conn.gyroState == .armed ? conn.disarmGyro() : conn.armGyro() },
                 onRecenter: { conn.recenter() })
    }

    // MARK: - Trim

    private var trimSection: some View {
        TrimView(trimValue: conn.aircraft.elevatorTrim,
                 onTrimUp: { conn.sendCommand(TcpCmd.trimUp) },
                 onTrimDn: { conn.sendCommand(TcpCmd.trimDn) })
    }

    // MARK: - Rudder

    private var rudderSection: some View {
        VStack(spacing: 2) {
            Text("RUDDER")
                .font(.caption2)
                .foregroundColor(.secondary)
            RudderView(onBegin: { conn.beginRudder($0) },
                       onChange: { conn.setRudder($0) },
                       onEnd: { conn.endRudder() })
        }
    }

    // MARK: - 底部按钮

    private var bottomRow: some View {
        HStack(spacing: 8) {
            BrakeButton(
                onPress: { conn.sendCommand(TcpCmd.brake, value: true) },
                onRelease: { conn.sendCommand(TcpCmd.brake, value: false) }
            )
            commandButton("FLAPS -", systemImage: "chevron.down") { conn.sendCommand(TcpCmd.flapsDecr) }
            Text("FLAPS \(Int(conn.aircraft.flapsPercent))%")
                .font(.caption.bold())
                .frame(minWidth: 84)
            commandButton("FLAPS +", systemImage: "chevron.up") { conn.sendCommand(TcpCmd.flapsIncr) }
            commandButton("GEAR", systemImage: "gearshape.fill") { conn.sendCommand(TcpCmd.gear) }
                .foregroundColor(conn.aircraft.gearDown ? .green : .orange)
            commandButton(conn.aircraft.parkingBrake ? "PARKED" : "PARKING",
                          systemImage: "hand.raised.fill") { conn.sendCommand(TcpCmd.parkingBrake) }
                .foregroundColor(conn.aircraft.parkingBrake ? .green : .secondary)
        }
        .frame(maxWidth: .infinity)
    }

    private func commandButton(_ title: String, systemImage: String,
                               action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Label(title, systemImage: systemImage)
                .font(.caption.bold())
                .frame(minWidth: 74)
                .padding(.vertical, 10)
        }
        .buttonStyle(.bordered)
    }
}

// 长按保持刹车
struct BrakeButton: View {
    let onPress: () -> Void
    let onRelease: () -> Void
    @State private var pressed = false

    var body: some View {
        Text("BRAKE")
            .font(.caption.bold())
            .foregroundColor(.white)
            .frame(minWidth: 74)
            .padding(.vertical, 10)
            .background(Capsule().fill(pressed ? Color.red : Color(.systemGray)))
            .contentShape(Capsule())
            .onTapGesture {}
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { _ in
                        if !pressed {
                            pressed = true
                            onPress()
                        }
                    }
                    .onEnded { _ in
                        if pressed {
                            pressed = false
                            onRelease()
                        }
                    }
            )
    }
}
