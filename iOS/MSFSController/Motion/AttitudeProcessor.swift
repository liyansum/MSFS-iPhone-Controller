import Foundation
import CoreMotion

// 姿态零点：RECENTER 记录当前姿态为中性，之后输出相对姿态。
// 相对姿态 -> 度数。映射关系（横屏握持）：
//   device pitch  -> aileron（左右倾斜/横滚）
//   device roll   -> elevator（前后倾斜/俯仰）

final class AttitudeProcessor {
    private var neutral: CMAttitude?

    var hasNeutral: Bool { neutral != nil }

    func recenter(with attitude: CMAttitude) {
        neutral = attitude
    }

    func reset() {
        neutral = nil
    }

    /// 返回相对中性姿态的 (pitchDeg, rollDeg)
    func relative(attitude: CMAttitude) -> (pitchDeg: Double, rollDeg: Double) {
        guard let n = neutral,
              let copy = attitude.copy() as? CMAttitude else { return (0, 0) }
        copy.multiply(byInverseOf: n)
        let radToDeg = 180.0 / Double.pi
        return (copy.pitch * radToDeg, copy.roll * radToDeg)
    }
}
