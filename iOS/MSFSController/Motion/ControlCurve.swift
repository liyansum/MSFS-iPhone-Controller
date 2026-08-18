import Foundation

// 陀螺仪参数（与设置页对应）。
struct GyroSettings: Equatable {
    var rollMaxAngle: Double = 30      // aileron 轴满舵角度
    var pitchMaxAngle: Double = 20     // elevator 轴满舵角度
    var deadzoneDeg: Double = 2
    var expo: Double = 1.4
    var smoothing: Bool = true
    var rollSensitivity: Double = 1.0  // 0.5...1.5
    var pitchSensitivity: Double = 1.0
    var invertRoll: Bool = false       // aileron
    var invertPitch: Bool = false      // elevator
}

// Deadzone + Expo + Sensitivity 曲线。
// 角度 -> 归一化输出 -1..1。
enum ControlCurve {
    static func normalized(angle: Double,
                           maxAngle: Double,
                           deadzoneDeg: Double,
                           expo: Double,
                           sensitivity: Double,
                           invert: Bool) -> Float {
        guard maxAngle > deadzoneDeg + 0.5 else { return 0 }
        let sign = angle < 0 ? -1.0 : 1.0
        let absAngle = abs(angle)
        guard absAngle > deadzoneDeg else { return 0 }

        var t = (absAngle - deadzoneDeg) / (maxAngle - deadzoneDeg)
        t = min(max(t, 0), 1)
        if expo > 0 { t = pow(t, expo) }

        var out = t * sensitivity
        out = min(out, 1.0)
        if invert { out = -out }
        return Float(sign * out)
    }

    // 简单指数平滑（smoothing 关闭时 factor = 1）
    static func smooth(current: Float, target: Float, factor: Float) -> Float {
        current + (target - current) * factor
    }
}
