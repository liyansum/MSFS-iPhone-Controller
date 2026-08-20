import SwiftUI

// 油门：垂直拖动条 0..100。松手后短暂保持目标值，待 MSFS 遥测确认再接管显示。

struct ThrottleView: View {
    let telemetryThrottle: Double   // 0..1（未拖动时显示 MSFS 实际值）
    let enabled: Bool
    let onBegin: (Float) -> Void
    let onChange: (Float) -> Void
    let onEnd: () -> Void

    @State private var dragging = false
    @State private var dragValue: Float = 0
    @State private var settling = false
    @State private var settleGeneration: UInt64 = 0

    private var display: Float {
        dragging || settling ? dragValue : Float(telemetryThrottle)
    }

    var body: some View {
        GeometryReader { geo in
            let h = geo.size.height
            ZStack(alignment: .bottom) {
                RoundedRectangle(cornerRadius: 8)
                    .fill(Color(.systemGray5))

                VStack {
                    Text("100")
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

                Text("\(Int(display * 100))%")
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
                            settling = false
                            settleGeneration &+= 1
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
                        settling = true
                        settleGeneration &+= 1
                        let generation = settleGeneration
                        onEnd()
                        DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
                            if settleGeneration == generation { settling = false }
                        }
                    }
            )
            .onChange(of: telemetryThrottle) { _, value in
                if settling && abs(Float(value) - dragValue) <= 0.02 {
                    settling = false
                    settleGeneration &+= 1
                }
            }
            .onChange(of: enabled) { _, value in
                guard !value else { return }
                if dragging {
                    dragging = false
                    onEnd()
                }
                settling = false
                settleGeneration &+= 1
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
