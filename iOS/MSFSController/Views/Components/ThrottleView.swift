import SwiftUI
import Combine

// 油门：以模拟器实际回读为基准相对增减。拖动期间实时控制，松手后释放轴并
// 恢复显示实际值，避免手机目标与 A/THR/驾驶舱油门长期互相争夺。

struct ThrottleView: View {
    let telemetryThrottle: Double   // 0..1（未拖动时显示 MSFS 实际值）
    let autothrottleActive: Bool
    let enabled: Bool
    let onBegin: (Float) -> Void
    let onChange: (Float) -> Void
    let onEnd: () -> Void
    let onTakeover: () -> Void
    let onIdle: () -> Void

    @State private var dragging = false
    @State private var dragValue: Float = 0
    @State private var dragStartValue: Float = 0
    @State private var dragStartY: CGFloat = 0
    @State private var idleSent = false

    private let idleConfirmation = Timer.publish(every: 0.75, on: .main, in: .common)
        .autoconnect()

    private var display: Float {
        dragging ? dragValue : Float(telemetryThrottle)
    }

    var body: some View {
        GeometryReader { geo in
            let h = geo.size.height
            ZStack(alignment: .bottom) {
                RoundedRectangle(cornerRadius: 8)
                    .fill(Color(.systemGray5))

                VStack {
                    HStack {
                        Text("100")
                        Spacer()
                        VStack(alignment: .trailing, spacing: 0) {
                            Text("SIM \(Int(telemetryThrottle * 100))%")
                            if autothrottleActive {
                                Text("A/THR · 触摸后手动接管")
                                    .foregroundColor(.orange)
                            }
                        }
                    }
                    .font(.caption2)
                    .foregroundColor(.secondary)
                    Spacer()
                    Text("0")
                        .font(.caption2)
                        .foregroundColor(.secondary)
                }
                .padding(.vertical, 4)

                RoundedRectangle(cornerRadius: 8)
                    .fill(LinearGradient(colors: [.blue, .cyan],
                                         startPoint: .bottom, endPoint: .top))
                    .frame(height: max(CGFloat(display) * (h - 16), 8))
                    .padding(.vertical, 8)

                Text(dragging ? "SET \(Int(dragValue * 100))%" :
                        "SIM \(Int(telemetryThrottle * 100))%")
                    .font(.caption.bold())
                    .foregroundColor(.white)
                    .shadow(radius: 2)
                    .padding(.bottom, 10)
            }
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { v in
                        guard enabled else { return }
                        if !dragging {
                            dragging = true
                            dragStartY = v.location.y
                            dragStartValue = Float(min(max(telemetryThrottle, 0), 1))
                            dragValue = dragStartValue
                            onBegin(dragStartValue)
                        } else {
                            let val = relativeValue(for: v.location.y, height: h)
                            dragValue = val
                            onChange(val)
                            if val <= 0.015 {
                                if !idleSent {
                                    idleSent = true
                                    onIdle()
                                }
                            } else if val >= 0.03 {
                                idleSent = false
                            }
                        }
                    }
                    .onEnded { _ in
                        guard dragging else { return }
                        dragging = false
                        idleSent = false
                        onEnd()
                    }
            )
            .onChange(of: enabled) { _, value in
                guard !value else { return }
                if dragging {
                    dragging = false
                    onEnd()
                }
                idleSent = false
            }
            .onDisappear {
                guard dragging else { return }
                dragging = false
                idleSent = false
                onEnd()
            }
            .onReceive(idleConfirmation) { _ in
                guard enabled, dragging else { return }
                if dragValue <= 0.015 {
                    // 0% 期间以实际回读闭环确认。
                    if autothrottleActive || telemetryThrottle > 0.03 { onIdle() }
                } else if autothrottleActive {
                    // 某些机模可能重新接通 A/THR；拖动期间低频重申手动接管。
                    onTakeover()
                }
            }
            .opacity(enabled ? 1 : 0.5)
        }
    }

    private func relativeValue(for y: CGFloat, height: CGFloat) -> Float {
        guard height > 0 else { return 0 }
        let delta = Float((dragStartY - y) / height)
        return min(max(dragStartValue + delta, 0), 1)
    }
}
