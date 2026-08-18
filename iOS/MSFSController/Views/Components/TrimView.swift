import SwiftUI

// Trim：类实心配平轮。点击单步，长按连续发送增量命令。
// 显示值为 MSFS 回读的配平状态。

struct TrimView: View {
    let trimValue: Double           // -1..1
    let onTrimUp: () -> Void
    let onTrimDn: () -> Void

    @State private var timer: Timer?

    var body: some View {
        VStack(spacing: 6) {
            PressHoldButton(action: onTrimUp) {
                Image(systemName: "chevron.up.circle.fill")
                    .font(.system(size: 34))
                    .foregroundColor(.blue)
            }

            VStack(spacing: 0) {
                Text("TRIM")
                    .font(.caption2)
                    .foregroundColor(.secondary)
                Text(percentText)
                    .font(.body.bold())
                    .monospacedDigit()
            }

            PressHoldButton(action: onTrimDn) {
                Image(systemName: "chevron.down.circle.fill")
                    .font(.system(size: 34))
                    .foregroundColor(.orange)
            }
        }
        .onDisappear { timer?.invalidate(); timer = nil }
    }

    private var percentText: String {
        let p = Int((trimValue * 100).rounded())
        return p >= 0 ? "+\(p)%" : "\(p)%"
    }
}

// 点击立即执行一次；长按 0.4s 后每 120ms 重复执行，松手停止。
struct PressHoldButton<Content: View>: View {
    let action: () -> Void
    let content: Content

    @State private var repeatTimer: Timer?
    @State private var active = false

    init(action: @escaping () -> Void, @ViewBuilder content: () -> Content) {
        self.action = action
        self.content = content()
    }

    var body: some View {
        content
            .scaleEffect(active ? 0.9 : 1)
            .animation(.easeOut(duration: 0.1), value: active)
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { _ in
                        if repeatTimer == nil {
                            action()          // 立即单步
                            active = true
                            let initial = Timer.scheduledTimer(withTimeInterval: 0.4, repeats: false) { _ in
                                let repeatT = Timer.scheduledTimer(withTimeInterval: 0.12, repeats: true) { _ in
                                    action()
                                }
                                repeatTimer = repeatT
                            }
                            repeatTimer = initial
                        }
                    }
                    .onEnded { _ in
                        stopTimer()
                    }
            )
    }

    private func stopTimer() {
        repeatTimer?.invalidate()
        repeatTimer = nil
        active = false
    }
}
