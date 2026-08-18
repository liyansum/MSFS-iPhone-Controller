import Foundation
import Network

// TCP 36667：会话 / 命令 / 遥测 / 航线。JSON 一行一条。

final class TCPClient {
    private let queue = DispatchQueue(label: "msfs.tcp")
    private var conn: NWConnection?
    private var buffer = Data()

    var onConnected: (() -> Void)?
    var onDisconnected: ((Error?) -> Void)?
    var onMessage: ((Data) -> Void)?

    func connect(host: String, port: UInt16) {
        guard let port = NWEndpoint.Port(rawValue: port) else {
            onDisconnected?(nil)
            return
        }
        let c = NWConnection(host: NWEndpoint.Host(host), port: port, using: .tcp)
        conn = c
        c.stateUpdateHandler = { [weak self] state in
            switch state {
            case .ready:
                self?.onConnected?()
                self?.startReceiving()
            case .failed(let err):
                self?.conn = nil
                self?.onDisconnected?(err)
            case .cancelled:
                self?.conn = nil
                self?.onDisconnected?(nil)
            default:
                break
            }
        }
        c.start(queue: queue)
    }

    func disconnect() {
        conn?.cancel()
        conn = nil
    }

    func send(json: String) {
        queue.async { [weak self] in
            guard let self = self, let conn = self.conn else { return }
            var payload = Data(json.utf8)
            payload.append(0x0A)
            conn.send(content: payload, completion: .contentProcessed { _ in })
        }
    }

    private func startReceiving() {
        guard let conn = conn else { return }
        conn.receive(minimumIncompleteLength: 1, maximumLength: 64 * 1024) { [weak self] data, _, isComplete, error in
            guard let self = self else { return }
            if let data = data, !data.isEmpty {
                self.buffer.append(data)
                self.drainLines()
            }
            if isComplete || error != nil {
                self.conn = nil
                self.onDisconnected?(error)
                return
            }
            self.startReceiving()
        }
    }

    private func drainLines() {
        while let nl = buffer.firstIndex(of: 0x0A) {
            let line = Data(buffer[..<nl])
            buffer = Data(buffer[buffer.index(after: nl)...])
            if !line.isEmpty {
                onMessage?(line)
            }
        }
    }
}
