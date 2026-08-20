import Foundation
import Network

// UDP 36666：实时控制包 + Ping/Pong。
// 控制包在独立队列上按控制频率发送，只发最新状态。

final class UDPController {
    private let queue = DispatchQueue(label: "msfs.udp")
    private var conn: NWConnection?
    private var connectionID: UUID?
    private var controlTimer: DispatchSourceTimer?
    private var pingTimer: DispatchSourceTimer?
    private var packetHandler: ((Data) -> Void)?

    func start(host: String, port: UInt16) {
        guard let port = NWEndpoint.Port(rawValue: port) else { return }
        let h = NWEndpoint.Host(host)
        let c = NWConnection(host: h, port: port, using: .udp)
        let id = UUID()
        queue.async { [weak self] in
            guard let self else { return }
            self.cancelConnection()
            self.conn = c
            self.connectionID = id
            c.stateUpdateHandler = { [weak self, weak c] state in
                guard let self, let c, self.isCurrent(c, id: id) else { return }
                switch state {
                case .ready:
                    self.receiveCurrent(c, id: id)
                case .failed, .cancelled:
                    self.cancelConnection(ifCurrent: c, id: id)
                default:
                    break
                }
            }
            c.start(queue: self.queue)
        }
    }

    func stop() {
        queue.async { [weak self] in self?.cancelConnection() }
    }

    func send(_ data: Data) {
        queue.async { [weak self] in
            guard let self, let conn = self.conn, let id = self.connectionID else { return }
            conn.send(content: data, completion: .contentProcessed { [weak self, weak conn] error in
                guard let self, let conn, error != nil else { return }
                self.cancelConnection(ifCurrent: conn, id: id)
            })
        }
    }

    /// 以 rateHz 频率发送由 sample 提供的控制包；sample 返回 nil 时不发送。
    func startControlLoop(rateHz: Double, sample: @escaping () -> Data?) {
        queue.async { [weak self] in
            guard let self else { return }
            self.controlTimer?.cancel()
            let t = DispatchSource.makeTimerSource(queue: self.queue)
            t.schedule(deadline: .now(), repeating: 1.0 / max(rateHz, 1))
            t.setEventHandler { [weak self] in
                guard let self, let data = sample(), let conn = self.conn else { return }
                conn.send(content: data, completion: .contentProcessed { _ in })
            }
            t.resume()
            self.controlTimer = t
        }
    }

    func stopControlLoop() {
        queue.async { [weak self] in
            self?.controlTimer?.cancel()
            self?.controlTimer = nil
        }
    }

    /// 周期发送 Ping（Pong 由 ConnectionManager 通过回调接收）。
    func startPingLoop(intervalMs: Int, makePing: @escaping () -> Data?) {
        queue.async { [weak self] in
            guard let self else { return }
            self.pingTimer?.cancel()
            let t = DispatchSource.makeTimerSource(queue: self.queue)
            t.schedule(deadline: .now() + 0.2,
                       repeating: Double(max(intervalMs, 100)) / 1000.0)
            t.setEventHandler { [weak self] in
                guard let self, let data = makePing(), let conn = self.conn else { return }
                conn.send(content: data, completion: .contentProcessed { _ in })
            }
            t.resume()
            self.pingTimer = t
        }
    }

    func stopPingLoop() {
        queue.async { [weak self] in
            self?.pingTimer?.cancel()
            self?.pingTimer = nil
        }
    }

    /// 收取 Pong（连接状态更新后调用一次即可持续接收）。
    func receive(onPacket: @escaping (Data) -> Void) {
        queue.async { [weak self] in
            self?.packetHandler = onPacket
        }
    }

    private func receiveCurrent(_ candidate: NWConnection, id: UUID) {
        guard isCurrent(candidate, id: id) else { return }
        candidate.receiveMessage { [weak self, weak candidate] data, _, _, error in
            guard let self, let candidate, self.isCurrent(candidate, id: id) else { return }
            if let data, !data.isEmpty { self.packetHandler?(data) }
            if error != nil {
                self.cancelConnection(ifCurrent: candidate, id: id)
            } else {
                self.receiveCurrent(candidate, id: id)
            }
        }
    }

    private func isCurrent(_ candidate: NWConnection, id: UUID) -> Bool {
        conn === candidate && connectionID == id
    }

    private func cancelConnection(ifCurrent candidate: NWConnection? = nil, id: UUID? = nil) {
        if let candidate, let id, !isCurrent(candidate, id: id) { return }
        controlTimer?.cancel()
        controlTimer = nil
        pingTimer?.cancel()
        pingTimer = nil
        let old = conn
        conn = nil
        connectionID = nil
        old?.stateUpdateHandler = nil
        old?.cancel()
    }
}
