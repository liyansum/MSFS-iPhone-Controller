import Foundation
import SwiftUI

// 设置持久化（UserDefaults）。

final class SettingsStore: ObservableObject {
    @Published var host: String {
        didSet { defaults.set(host, forKey: Keys.host) }
    }
    @Published var udpPort: UInt16 {
        didSet { defaults.set(Int(udpPort), forKey: Keys.udpPort) }
    }
    @Published var tcpPort: UInt16 {
        didSet { defaults.set(Int(tcpPort), forKey: Keys.tcpPort) }
    }
    @Published var controlRate: Double {
        didSet { defaults.set(controlRate, forKey: Keys.controlRate) }
    }
    @Published var gyro: GyroSettings {
        didSet { saveGyro() }
    }

    private let defaults: UserDefaults
    private enum Keys {
        static let host = "host"
        static let udpPort = "udpPort"
        static let tcpPort = "tcpPort"
        static let controlRate = "controlRate"
        static let rollMax = "rollMax"
        static let pitchMax = "pitchMax"
        static let deadzone = "deadzone"
        static let expo = "expo"
        static let smoothing = "smoothing"
        static let rollSens = "rollSens"
        static let pitchSens = "pitchSens"
        static let invertRoll = "invertRoll"
        static let invertPitch = "invertPitch"
    }

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        host = defaults.string(forKey: Keys.host) ?? ""
        udpPort = UInt16(defaults.integer(forKey: Keys.udpPort)) == 0
            ? Proto.defaultUdpPort
            : UInt16(defaults.integer(forKey: Keys.udpPort))
        tcpPort = UInt16(defaults.integer(forKey: Keys.tcpPort)) == 0
            ? Proto.defaultTcpPort
            : UInt16(defaults.integer(forKey: Keys.tcpPort))
        controlRate = defaults.double(forKey: Keys.controlRate) == 0
            ? 60 : defaults.double(forKey: Keys.controlRate)

        var g = GyroSettings()
        g.rollMaxAngle = defaults.object(forKey: Keys.rollMax) != nil ? defaults.double(forKey: Keys.rollMax) : g.rollMaxAngle
        g.pitchMaxAngle = defaults.object(forKey: Keys.pitchMax) != nil ? defaults.double(forKey: Keys.pitchMax) : g.pitchMaxAngle
        g.deadzoneDeg = defaults.object(forKey: Keys.deadzone) != nil ? defaults.double(forKey: Keys.deadzone) : g.deadzoneDeg
        g.expo = defaults.object(forKey: Keys.expo) != nil ? defaults.double(forKey: Keys.expo) : g.expo
        g.smoothing = defaults.object(forKey: Keys.smoothing) != nil ? defaults.bool(forKey: Keys.smoothing) : g.smoothing
        g.rollSensitivity = defaults.object(forKey: Keys.rollSens) != nil ? defaults.double(forKey: Keys.rollSens) : g.rollSensitivity
        g.pitchSensitivity = defaults.object(forKey: Keys.pitchSens) != nil ? defaults.double(forKey: Keys.pitchSens) : g.pitchSensitivity
        g.invertRoll = defaults.object(forKey: Keys.invertRoll) != nil ? defaults.bool(forKey: Keys.invertRoll) : g.invertRoll
        g.invertPitch = defaults.object(forKey: Keys.invertPitch) != nil ? defaults.bool(forKey: Keys.invertPitch) : g.invertPitch
        gyro = g
    }

    private func saveGyro() {
        defaults.set(gyro.rollMaxAngle, forKey: Keys.rollMax)
        defaults.set(gyro.pitchMaxAngle, forKey: Keys.pitchMax)
        defaults.set(gyro.deadzoneDeg, forKey: Keys.deadzone)
        defaults.set(gyro.expo, forKey: Keys.expo)
        defaults.set(gyro.smoothing, forKey: Keys.smoothing)
        defaults.set(gyro.rollSensitivity, forKey: Keys.rollSens)
        defaults.set(gyro.pitchSensitivity, forKey: Keys.pitchSens)
        defaults.set(gyro.invertRoll, forKey: Keys.invertRoll)
        defaults.set(gyro.invertPitch, forKey: Keys.invertPitch)
    }

    var hasHost: Bool { !host.trimmingCharacters(in: .whitespaces).isEmpty }
}
