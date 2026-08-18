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
    var onStateLog: ((String) -> Void)?

    func connect(host: String, port: UInt16) {
        guard let port = NWEndpoint.Port(rawValue: port) else {
            onStateLog?("TCP 端口无效")
            onDisconnected?(nil)
            return
        }
        onStateLog?("TCP 连接 \(host):\(port) 发起")
        let c = NWConnection(host: NWEndpoint.Host(host), port: port, using: .tcp)
        conn = c
        c.stateUpdateHandler = { [weak self] state in
            switch state {
            case .setup:
                self?.onStateLog?("TCP 状态: 初始化")
            case .waiting(let err):
                self?.onStateLog?("TCP 状态: 等待 (\(self?.describe(err) ?? ""))")
            case .preparing:
                self?.onStateLog?("TCP 状态: 连接中(preparing)")
            case .ready:
                self?.onStateLog?("TCP 状态: 已连接")
                self?.onConnected?()
                self?.startReceiving()
            case .failed(let err):
                self?.onStateLog?("TCP 状态: 失败 (\(self?.describe(err) ?? ""))")
                self?.conn = nil
                self?.onDisconnected?(err)
            case .cancelled:
                self?.onStateLog?("TCP 状态: 已取消")
                self?.conn = nil
                self?.onDisconnected?(nil)
            @unknown default:
                break
            }
        }
        c.start(queue: queue)
    }

    private func describe(_ error: NWError) -> String {
        switch error {
        case .posix(let code):
            return "posix \(code.rawValue)"
        case .dns(let dns):
            return "dns \(dns)"
        case .tls(let tls):
            return "tls \(tls)"
        @unknown default:
            return "\(error)"
        }
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
