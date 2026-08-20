import SwiftUI

// 油门：垂直拖动条 0..100。用户操作后持续保持目标值，直到断线或 App 后台。

struct ThrottleView: View {
    let telemetryThrottle: Double   // 0..1（未拖动时显示 MSFS 实际值）
    let enabled: Bool
    let onBegin: (Float) -> Void
    let onChange: (Float) -> Void
    let onEnd: () -> Void

    @State private var dragging = false
    @State private var dragValue: Float = 0
    @State private var hasTarget = false

    private var display: Float {
        hasTarget ? dragValue : Float(telemetryThrottle)
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
                        Text("SIM \(Int(telemetryThrottle * 100))%")
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

                Text("SET \(Int(display * 100))%")
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
                        let val = value(for: v.location.y, height: h)
                        if !dragging {
                            dragging = true
                            hasTarget = true
                            dragValue = val
                            onBegin(val)
                        } else {
                            dragValue = val
                            onChange(val)
                        }
                    }
                    .onEnded { _ in
                        guard dragging else { return }
                        dragging = false
                        onEnd()
                    }
            )
            .onChange(of: enabled) { _, value in
                guard !value else { return }
                if dragging {
                    dragging = false
                    onEnd()
                }
                hasTarget = false
            }
            .opacity(enabled ? 1 : 0.5)
        }
    }

    private func value(for y: CGFloat, height: CGFloat) -> Float {
        guard height > 0 else { return 0 }
        let ratio = Double(1 - y / height)
        return Float(min(max(ratio, 0), 1))
    }
}
