import SwiftUI

// 油门只使用两个离散命令。每次点击由 TCP 可靠发送一次 ±10%，不保存手机目标，
// 不占用 UDP 油门轴；中间数值始终来自 MSFS 遥测。

struct ThrottleView: View {
    let telemetryThrottle: Double
    let autothrottleActive: Bool
    let enabled: Bool
    let onDecrease: () -> Void
    let onIncrease: () -> Void

    var body: some View {
        VStack(spacing: 12) {
            ThrottleRepeatButton(title: "油门 +", systemImage: "plus",
                                 color: .blue, enabled: enabled,
                                 action: onIncrease)

            Spacer(minLength: 4)

            VStack(spacing: 5) {
                Text("SIM 实际油门")
                    .font(.caption)
                    .foregroundColor(.secondary)
                Text("\(Int(min(max(telemetryThrottle, 0), 1) * 100))%")
                    .font(.system(size: 30, weight: .bold, design: .rounded))
                    .monospacedDigit()
                Text("点按 10% · 按住连续")
                    .font(.caption2)
                    .foregroundColor(.secondary)
                if autothrottleActive {
                    Text("A/THR ON · 点击后转手动")
                        .font(.caption2.bold())
                        .foregroundColor(.orange)
                        .multilineTextAlignment(.center)
                }
            }

            Spacer(minLength: 4)

            ThrottleRepeatButton(title: "油门 −", systemImage: "minus",
                                 color: .indigo, enabled: enabled,
                                 action: onDecrease)
        }
        .padding(8)
        .background(RoundedRectangle(cornerRadius: 10)
            .fill(Color(.secondarySystemBackground)))
        .disabled(!enabled)
        .opacity(enabled ? 1 : 0.5)
    }
}

private struct ThrottleRepeatButton: View {
    @Environment(\.scenePhase) private var scenePhase

    let title: String
    let systemImage: String
    let color: Color
    let enabled: Bool
    let action: () -> Void

    @State private var pressed = false
    @State private var repeatTask: Task<Void, Never>?

    var body: some View {
        Label(title, systemImage: systemImage)
            .font(.headline)
            .foregroundColor(.white)
            .frame(maxWidth: .infinity, minHeight: 52)
            .background(RoundedRectangle(cornerRadius: 9)
                .fill(color.opacity(pressed ? 0.65 : 1)))
            .scaleEffect(pressed ? 0.97 : 1)
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { _ in startRepeating() }
                    .onEnded { _ in stopRepeating() }
            )
            .onChange(of: enabled) { _, value in
                if !value { stopRepeating() }
            }
            .onChange(of: scenePhase) { _, phase in
                if phase != .active { stopRepeating() }
            }
            .onDisappear { stopRepeating() }
    }

    private func startRepeating() {
        guard enabled, !pressed else { return }
        pressed = true
        action()
        let repeatedAction = action
        repeatTask = Task { @MainActor in
            do {
                try await Task.sleep(nanoseconds: 400_000_000)
                while !Task.isCancelled {
                    repeatedAction()
                    try await Task.sleep(nanoseconds: 180_000_000)
                }
            } catch {
                // 松手取消属于正常结束，不需要提示错误。
            }
        }
    }

    private func stopRepeating() {
        repeatTask?.cancel()
        repeatTask = nil
        pressed = false
    }
}
