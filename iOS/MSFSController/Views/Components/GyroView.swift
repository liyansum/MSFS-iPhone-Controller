import SwiftUI

// GYRO 区域：Roll/Pitch 输出显示 + ARM/DISARM + RECENTER。

struct GyroView: View {
    let roll: Float          // -100..100
    let pitch: Float         // -100..100
    let armed: Bool
    let onToggle: () -> Void
    let onRecenter: () -> Void

    var body: some View {
        VStack(spacing: 8) {
            Text(armed ? "● GYRO ARMED" : "GYRO DISARMED")
                .font(.caption.bold())
                .foregroundColor(armed ? .green : .secondary)

            VStack(spacing: 2) {
                Text("Roll \(Int(roll))%")
                Text("Pitch \(Int(pitch))%")
            }
            .font(.body.bold().monospacedDigit())
            .foregroundColor(armed ? .primary : .secondary)

            Button(action: onToggle) {
                Text(armed ? "DISARM" : "GYRO ARM")
                    .font(.caption.bold())
                    .padding(.horizontal, 14)
                    .padding(.vertical, 8)
                    .background(Capsule().fill(armed ? Color.red : Color.green))
                    .foregroundColor(.white)
            }
            .buttonStyle(.plain)

            Button(action: onRecenter) {
                Text("RECENTER")
                    .font(.caption)
                    .padding(.horizontal, 14)
                    .padding(.vertical, 6)
                    .overlay(Capsule().stroke(Color.secondary, lineWidth: 1))
            }
            .buttonStyle(.plain)
            .disabled(!armed)
            .opacity(armed ? 1 : 0.4)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 8)
    }
}
