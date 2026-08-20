import SwiftUI

@main
struct MSFSControllerApp: App {
    @StateObject private var settings: SettingsStore
    @StateObject private var conn: ConnectionManager
    @Environment(\.scenePhase) private var scenePhase

    init() {
        let s = SettingsStore()
        _settings = StateObject(wrappedValue: s)
        _conn = StateObject(wrappedValue: ConnectionManager(settings: s))
    }

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(settings)
                .environmentObject(conn)
                .preferredColorScheme(.dark)
                .onAppear {
                    forceLandscape()
                    setScreenAwake(true)
                }
        }
        .onChange(of: scenePhase) { _, newPhase in
            switch newPhase {
            case .background:
                setScreenAwake(false)
                conn.handleBackground()
            case .inactive:
                conn.handleBackground()
            case .active:
                setScreenAwake(true)
            @unknown default:
                break
            }
        }
    }

    private func forceLandscape() {
        DispatchQueue.main.async {
            guard let scene = UIApplication.shared.connectedScenes
                .compactMap({ $0 as? UIWindowScene }).first else { return }
            scene.requestGeometryUpdate(.iOS(interfaceOrientations: .landscape))
        }
    }

    /// 飞行控制期间保持屏幕常亮，避免自动锁屏令 CoreMotion/TCP/UDP 被系统挂起。
    private func setScreenAwake(_ awake: Bool) {
        UIApplication.shared.isIdleTimerDisabled = awake
    }
}

// MARK: - 根视图

enum RootTab: Hashable {
    case control, map, settings
}

struct RootView: View {
    @EnvironmentObject var settings: SettingsStore
    @EnvironmentObject var conn: ConnectionManager
    @State private var tab: RootTab = .control

    var body: some View {
        TabView(selection: $tab) {
            ControlView()
                .id(conn.controlResetToken)
                .overlay(alignment: .top) {
                    if conn.phase != .connected {
                        ConnectionNotice(
                            connecting: conn.phase == .connecting,
                            host: settings.hasHost ? settings.host : "",
                            lastError: conn.lastError,
                            onReconnect: { conn.connect() },
                            onEdit: { tab = .settings }
                        )
                        .padding(.horizontal, 18)
                        .padding(.top, 28)
                        .transition(.move(edge: .top).combined(with: .opacity))
                    }
                }
                .tabItem { Label("CONTROL", systemImage: "gamecontroller") }
                .tag(RootTab.control)
            MapView()
                .tabItem { Label("MAP", systemImage: "map") }
                .tag(RootTab.map)
            SettingsView(onDone: { tab = .control })
                .tabItem { Label("SETTINGS", systemImage: "gearshape") }
                .tag(RootTab.settings)
        }
        .animation(.easeInOut(duration: 0.2), value: conn.phase)
        .onAppear {
            if settings.hasHost && conn.phase == .disconnected {
                conn.connect()
            }
        }
    }
}

// MARK: - 未连接覆盖层

struct ConnectionNotice: View {
    let connecting: Bool
    let host: String
    let lastError: String?
    let onReconnect: () -> Void
    let onEdit: () -> Void

    var body: some View {
        HStack(spacing: 14) {
            Image(systemName: connecting ? "wifi" : "wifi.slash")
                .font(.title3)
                .foregroundStyle(connecting ? .yellow : .orange)

            VStack(alignment: .leading, spacing: 3) {
                Text(connecting ? "正在连接 Windows 主机" : "PC 未连接")
                    .font(.headline)
                Text(detailText)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(2)
            }
            Spacer(minLength: 8)

            if !connecting {
                Button(action: onReconnect) {
                    Label("重连", systemImage: "arrow.clockwise")
                }
                .buttonStyle(.borderedProminent)
            }
            Button(action: onEdit) {
                Label("修改主机", systemImage: "pencil")
            }
            .buttonStyle(.bordered)
        }
        .padding(.horizontal, 18)
        .padding(.vertical, 12)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 16))
        .overlay {
            RoundedRectangle(cornerRadius: 16)
                .stroke(Color.orange.opacity(0.35), lineWidth: 1)
        }
        .shadow(color: .black.opacity(0.25), radius: 14, y: 6)
    }

    private var detailText: String {
        if let lastError, !lastError.isEmpty { return lastError }
        return host.isEmpty ? "请打开设置填写或自动发现主机" : "目标主机：\(host)"
    }
}
