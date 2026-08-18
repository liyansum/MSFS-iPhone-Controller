import Foundation

// 控制轴状态。所有值均为归一化：
// aileron/elevator/rudder: -1.0 ... +1.0
// throttle: 0.0 ... 1.0

struct AxisState: Equatable {
    var aileron: Float = 0
    var elevator: Float = 0
    var rudder: Float = 0
    var throttle: Float = 0
}

enum GyroArmState: Equatable {
    case disarmed
    case arming       // 点击 ARM 后校准瞬间
    case armed
}
