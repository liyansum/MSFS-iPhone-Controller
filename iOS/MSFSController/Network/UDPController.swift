import Foundation
import Network

// UDP 36666：实时控制包 + Ping/Pong。
// 控制包在独立队列上按控制频率发送，只发最新状态。

final class UDPController {
    private let queue = DispatchQueue(label: "msfs.udp")
    private var conn: NWConnection?
    private var controlTimer: DispatchSourceTimer?
    private var pingTimer: DispatchSourceTimer?

    var isStarted: Bool { conn != nil }

    func start(host: String, port: UInt16) {
        guard let port = NWEndpoint.Port(rawValue: port) else { return }
        let h = NWEndpoint.Host(host)
        let c = NWConnection(host: h, port: port, using: .udp)
        c.stateUpdateHandler = { [weak self] state in
            switch state {
            case .failed(let err), .cancelled(let err):
                _ = err
                self?.conn = nil
            default:
                break
            }
        }
        c.start(queue: queue)
        conn = c
    }

    func stop() {
        stopControlLoop()
        stopPingLoop()
        conn?.cancel()
        conn = nil
    }

    func send(_ data: Data) {
        guard let conn = conn else { return }
        queue.async {
            conn.send(content: data, completion: .contentProcessed { _ in })
        }
    }

    /// 以 rateHz 频率发送由 sample 提供的控制包；sample 返回 nil 时不发送。
    func startControlLoop(rateHz: Double, sample: @escaping () -> Data?) {
        stopControlLoop()
        let t = DispatchSource.makeTimerSource(queue: queue)
        t.schedule(deadline: .now(), repeating: 1.0 / max(rateHz, 1))
        t.setEventHandler { [weak self] in
            guard let self = self, let data = sample() else { return }
            self.send(data)
        }
        t.resume()
        controlTimer = t
    }

    func stopControlLoop() {
        controlTimer?.cancel()
        controlTimer = nil
    }

    /// 周期发送 Ping（Pong 由 ConnectionManager 通过回调接收）。
    func startPingLoop(intervalMs: Int, makePing: @escaping () -> Data?) {
        stopPingLoop()
        let t = DispatchSource.makeTimerSource(queue: queue)
        t.schedule(deadline: .now() + 0.2, repeating: Double(intervalMs) / 1000.0)
        t.setEventHandler { [weak self] in
            guard let self = self, let data = makePing() else { return }
            self.send(data)
        }
        t.resume()
        pingTimer = t
    }

    func stopPingLoop() {
        pingTimer?.cancel()
        pingTimer = nil
    }

    /// 收取 Pong（连接状态更新后调用一次即可持续接收）。
    func receive(onPacket: @escaping (Data) -> Void) {
        guard let conn = conn else { return }
        conn.receive(minimumIncompleteLength: 1, maximumLength: 1024) { [weak self] data, _, isComplete, _ in
            if let data = data, !data.isEmpty {
                onPacket(data)
            }
            if isComplete {
                self?.conn = nil
                return
            }
            self?.receive(onPacket: onPacket)
        }
    }
}
