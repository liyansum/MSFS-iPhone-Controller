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
                .onAppear { forceLandscape() }
        }
        .onChange(of: scenePhase) { _, newPhase in
            switch newPhase {
            case .background, .inactive:
                conn.handleBackground()
            case .active:
                break
            @unknown default:
                break
            }
        }
    }

    private func forceLandscape() {
        UIDevice.current.setValue(UIInterfaceOrientation.landscapeRight.rawValue, forKey: "orientation")
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
        ZStack {
            TabView(selection: $tab) {
                ControlView()
                    .tabItem { Label("CONTROL", systemImage: "gamecontroller") }
                    .tag(RootTab.control)
                MapView()
                    .tabItem { Label("MAP", systemImage: "map") }
                    .tag(RootTab.map)
                SettingsView()
                    .tabItem { Label("SETTINGS", systemImage: "gearshape") }
                    .tag(RootTab.settings)
            }
            .onChange(of: tab) { _, newValue in
                if newValue != .control { conn.autoDisarm() }
            }

            // 未连接提示只在 CONTROL 页显示，不遮挡设置/地图页
            if conn.phase == .disconnected && tab == .control {
                DisconnectedOverlay(
                    host: settings.hasHost ? settings.host : "",
                    lastError: conn.lastError,
                    onReconnect: { conn.connect() },
                    onEdit: { tab = .settings }
                )
            }
        }
        .onAppear {
            if settings.hasHost && conn.phase == .disconnected {
                conn.connect()
            }
        }
    }
}

// MARK: - 未连接覆盖层

struct DisconnectedOverlay: View {
    let host: String
    let lastError: String?
    let onReconnect: () -> Void
    let onEdit: () -> Void

    var body: some View {
        VStack(spacing: 16) {
            Text("PC 未连接")
                .font(.title3.bold())
            Text(host.isEmpty ? "请在设置中填写主机 IP" : "目标主机: \(host)")
                .font(.caption)
                .foregroundColor(.secondary)
            if let err = lastError, !err.isEmpty {
                Text(err)
                    .font(.caption)
                    .foregroundColor(.orange)
            }
            HStack(spacing: 20) {
                Button(action: onReconnect) {
                    Label("重新连接", systemImage: "arrow.clockwise")
                        .padding(.horizontal, 20)
                        .padding(.vertical, 10)
                }
                .buttonStyle(.borderedProminent)

                Button(action: onEdit) {
                    Label("修改主机", systemImage: "pencil")
                        .padding(.horizontal, 20)
                        .padding(.vertical, 10)
                }
                .buttonStyle(.bordered)
            }
        }
        .padding(30)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(.systemBackground).opacity(0.96))
    }
}
