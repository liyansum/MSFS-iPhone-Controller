import SwiftUI

// Rudder：自动回中横向滑杆。拖动期间持续发送，松手回到 0 并发送回中包。

struct RudderView: View {
    let enabled: Bool
    let onBegin: (Float) -> Void
    let onChange: (Float) -> Void
    let onEnd: () -> Void

    @State private var value: Float = 0
    @State private var dragging = false

    private let knobSize: CGFloat = 40

    var body: some View {
        GeometryReader { geo in
            let w = geo.size.width
            let halfTravel = max((w - knobSize) / 2, 1)
            ZStack {
                Capsule()
                    .fill(Color(.systemGray5))
                    .frame(height: 10)

                Capsule()
                    .fill(LinearGradient(colors: [.gray, .secondary], startPoint: .leading, endPoint: .trailing))
                    .frame(height: 2)

                Text("LEFT")
                    .font(.caption2)
                    .foregroundColor(.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .offset(x: 2)

                Text("RIGHT")
                    .font(.caption2)
                    .foregroundColor(.secondary)
                    .frame(maxWidth: .infinity, alignment: .trailing)
                    .offset(x: -2)

                Circle()
                    .fill(Color.white)
                    .frame(width: knobSize, height: knobSize)
                    .shadow(color: .black.opacity(0.3), radius: 3, y: 1)
                    .overlay(Circle().stroke(Color.blue, lineWidth: 2))
                    .offset(x: CGFloat(value) * halfTravel)
            }
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { v in
                        guard enabled else { return }
                        let normalized = (v.location.x - w / 2) / halfTravel
                        let val = Float(min(max(Double(normalized), -1), 1))
                        if !dragging {
                            dragging = true
                            value = val
                            onBegin(val)
                        } else {
                            value = val
                            onChange(val)
                        }
                    }
                    .onEnded { _ in
                        dragging = false
                        value = 0
                        onEnd()
                    }
            )
            .onChange(of: enabled) { _, isEnabled in
                if !isEnabled && dragging {
                    dragging = false
                    value = 0
                    onEnd()
                }
            }
            .opacity(enabled ? 1 : 0.5)
        }
    }
}
