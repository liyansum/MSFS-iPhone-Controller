import Foundation
import CoreMotion
import UIKit

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
    private let stateLock = NSLock()
    private var waitingForFirstSample = false
    private var readyDelivered = false
    private var errorDelivered = false
    private var orientation: UIInterfaceOrientation = .landscapeLeft
    let rawAngles = RawAnglesBox()
    var onAngles: ((Double, Double) -> Void)?

    var hasNeutral: Bool { processor.hasNeutral }

    var isAvailable: Bool { motion.isDeviceMotionAvailable }

    init() {
        queue.maxConcurrentOperationCount = 1
        queue.qualityOfService = .userInteractive
    }

    func start(orientation: UIInterfaceOrientation,
               recenterOnFirstSample: Bool = false,
               onReady: (() -> Void)? = nil,
               onError: ((String) -> Void)? = nil) {
        guard motion.isDeviceMotionAvailable else { return }
        self.orientation = orientation
        stateLock.lock()
        waitingForFirstSample = recenterOnFirstSample
        readyDelivered = false
        errorDelivered = false
        stateLock.unlock()
        motion.deviceMotionUpdateInterval = 1.0 / 100.0
        motion.startDeviceMotionUpdates(using: .xArbitraryZVertical, to: queue) {
            [weak self] dm, error in
            guard let self else { return }
            if let error {
                self.stateLock.lock()
                let shouldDeliver = !self.errorDelivered
                self.errorDelivered = true
                self.stateLock.unlock()
                if shouldDeliver { onError?(error.localizedDescription) }
                return
            }
            guard let dm else { return }

            self.stateLock.lock()
            let shouldRecenter = self.waitingForFirstSample
            if shouldRecenter { self.waitingForFirstSample = false }
            let shouldNotifyReady = !self.readyDelivered
            if shouldNotifyReady { self.readyDelivered = true }
            self.stateLock.unlock()

            if shouldRecenter { self.processor.recenter(with: dm.attitude) }
            let rel = self.processor.relative(attitude: dm.attitude)
            // CoreMotion 坐标固定在设备上；横屏左右旋转 180° 后两个控制轴都需反号，
            // 才能维持“向左倾斜=左滚转、抬起上沿=相同俯仰方向”的手感。
            let sign = self.orientation == .landscapeRight ? -1.0 : 1.0
            let aileronAngle = rel.pitchDeg * sign
            let elevatorAngle = rel.rollDeg * sign
            self.rawAngles.value = (aileronAngle, elevatorAngle)
            self.onAngles?(aileronAngle, elevatorAngle)
            if shouldNotifyReady { onReady?() }
        }
    }

    func stop() {
        motion.stopDeviceMotionUpdates()
        stateLock.lock()
        waitingForFirstSample = false
        readyDelivered = false
        errorDelivered = false
        stateLock.unlock()
    }

    /// ARM 时自动校准：以当前姿态为零点
    @discardableResult
    func recenter() -> Bool {
        guard let dm = motion.deviceMotion else { return false }
        processor.recenter(with: dm.attitude)
        rawAngles.value = (0, 0)
        onAngles?(0, 0)
        return true
    }

    func reset() {
        processor.reset()
    }
}
