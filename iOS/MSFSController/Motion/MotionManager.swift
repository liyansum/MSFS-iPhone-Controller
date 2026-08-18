import Foundation
import CoreMotion

// 线程安全的原始角度缓存（运动队列写入 / 控制循环读取）。

final class RawAnglesBox {
    private let lock = NSLock()
    private var storage: (pitchDeg: Double, rollDeg: Double) = (0, 0)

    var value: (pitchDeg: Double, rollDeg: Double) {
        get { lock.lock(); defer { lock.unlock() }; return storage }
        set { lock.lock(); storage = newValue; lock.unlock() }
    }
}

// CoreMotion 管理：100 Hz 采样设备姿态，转换为相对中性的度数。
final class MotionManager {
    private let motion = CMMotionManager()
    private let processor = AttitudeProcessor()
    private let queue = OperationQueue()
    let rawAngles = RawAnglesBox()

    var hasNeutral: Bool { processor.hasNeutral }

    var isAvailable: Bool { motion.isDeviceMotionAvailable }

    func start() {
        guard motion.isDeviceMotionAvailable else { return }
        motion.deviceMotionUpdateInterval = 1.0 / 100.0
        motion.startDeviceMotionUpdates(using: .xArbitraryZVertical, to: queue) { [weak self] dm, _ in
            guard let self = self, let dm = dm else { return }
            let rel = self.processor.relative(attitude: dm.attitude)
            self.rawAngles.value = (rel.pitchDeg, rel.rollDeg)
        }
    }

    func stop() {
        motion.stopDeviceMotionUpdates()
    }

    /// ARM 时自动校准：以当前姿态为零点
    func recenter() {
        guard let dm = motion.deviceMotion else { return }
        processor.recenter(with: dm.attitude)
    }

    func reset() {
        processor.reset()
    }
}
