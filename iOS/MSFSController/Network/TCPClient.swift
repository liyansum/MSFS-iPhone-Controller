import Foundation
import Network

// TCP 36667：会话 / 命令 / 遥测 / 航线。JSON 一行一条。
//
// 所有 NWConnection 状态都在同一个串行队列中管理，并用 connectionID 隔离。
// 这样用户修改主机或快速重连时，旧连接迟到的 cancelled/failed 回调不会
// 错误地关闭新连接。

final class TCPClient {
    private struct Handlers {
        let connected: (() -> Void)?
        let disconnected: ((Error?) -> Void)?
        let message: ((Data) -> Void)?
        let stateLog: ((String) -> Void)?
    }

    private let queue = DispatchQueue(label: "msfs.tcp")
    private var connection: NWConnection?
    private var connectionID: UUID?
    private var currentHandlers: Handlers?
    private var buffer = Data()

    var onConnected: (() -> Void)?
    var onDisconnected: ((Error?) -> Void)?
    var onMessage: ((Data) -> Void)?
    var onStateLog: ((String) -> Void)?

    func connect(host: String, port: UInt16) {
        guard let endpointPort = NWEndpoint.Port(rawValue: port) else {
            onStateLog?("TCP 端口无效")
            onDisconnected?(nil)
            return
        }

        let endpointHost = NWEndpoint.Host(host)
        let newConnection = NWConnection(host: endpointHost, port: endpointPort, using: .tcp)
        let id = UUID()
        // 每条连接冻结自己的回调，避免快速重连时旧连接调用到新会话的闭包。
        let handlers = Handlers(connected: onConnected,
                                disconnected: onDisconnected,
                                message: onMessage,
                                stateLog: onStateLog)

        queue.async { [weak self] in
            guard let self else { return }

            // 先清除身份再取消，确保旧连接的 cancelled 回调被识别为过期事件。
            let oldConnection = self.connection
            self.connection = nil
            self.connectionID = nil
            self.currentHandlers = nil
            oldConnection?.stateUpdateHandler = nil
            oldConnection?.cancel()

            self.buffer.removeAll(keepingCapacity: true)
            self.connection = newConnection
            self.connectionID = id
            self.currentHandlers = handlers
            handlers.stateLog?("TCP 连接 \(host):\(endpointPort) 发起")

            newConnection.stateUpdateHandler = { [weak self, weak newConnection] state in
                guard let self, let newConnection else { return }
                self.handle(state, for: newConnection, id: id, handlers: handlers)
            }
            newConnection.start(queue: self.queue)
        }
    }

    func disconnect() {
        queue.async { [weak self] in
            guard let self else { return }
            let oldConnection = self.connection
            self.connection = nil
            self.connectionID = nil
            self.currentHandlers = nil
            self.buffer.removeAll(keepingCapacity: true)
            oldConnection?.stateUpdateHandler = nil
            oldConnection?.cancel()
        }
    }

    func send(json: String) {
        var payload = Data(json.utf8)
        payload.append(0x0A)
        queue.async { [weak self] in
            guard let self, let connection = self.connection,
                  let id = self.connectionID,
                  let handlers = self.currentHandlers else { return }
            connection.send(content: payload, completion: .contentProcessed { [weak self, weak connection] error in
                guard let self, let connection, let error else { return }
                self.finish(connection, id: id, error: error, handlers: handlers)
            })
        }
    }

    private func handle(_ state: NWConnection.State, for connection: NWConnection,
                        id: UUID, handlers: Handlers) {
        guard isCurrent(connection, id: id) else { return }

        switch state {
        case .setup:
            handlers.stateLog?("TCP 状态: 初始化")
        case .waiting(let error):
            handlers.stateLog?("TCP 状态: 等待 (\(describe(error)))")
        case .preparing:
            handlers.stateLog?("TCP 状态: 连接中")
        case .ready:
            handlers.stateLog?("TCP 状态: 已连接")
            handlers.connected?()
            receive(on: connection, id: id, handlers: handlers)
        case .failed(let error):
            handlers.stateLog?("TCP 状态: 失败 (\(describe(error)))")
            finish(connection, id: id, error: error, handlers: handlers)
        case .cancelled:
            // 只有服务端/系统取消“当前连接”才通知上层；主动 disconnect 会先清除身份。
            handlers.stateLog?("TCP 状态: 已取消")
            finish(connection, id: id, error: nil, handlers: handlers)
        @unknown default:
            break
        }
    }

    private func receive(on connection: NWConnection, id: UUID, handlers: Handlers) {
        guard isCurrent(connection, id: id) else { return }
        connection.receive(minimumIncompleteLength: 1, maximumLength: 64 * 1024) {
            [weak self, weak connection] data, _, isComplete, error in
            guard let self, let connection, self.isCurrent(connection, id: id) else { return }

            if let data, !data.isEmpty {
                self.buffer.append(data)
                self.drainLines(handlers: handlers)
            }
            if isComplete || error != nil {
                self.finish(connection, id: id, error: error, handlers: handlers)
            } else {
                self.receive(on: connection, id: id, handlers: handlers)
            }
        }
    }

    private func drainLines(handlers: Handlers) {
        // 限制未换行缓冲区，避免异常服务端无限占用内存。
        if buffer.count > 1_048_576 {
            handlers.stateLog?("TCP 消息超过 1 MB，连接已关闭")
            if let connection, let id = connectionID {
                finish(connection, id: id, error: nil, handlers: handlers)
            }
            return
        }

        while let newline = buffer.firstIndex(of: 0x0A) {
            var line = Data(buffer[..<newline])
            buffer.removeSubrange(...newline)
            if line.last == 0x0D { line.removeLast() }
            if !line.isEmpty { handlers.message?(line) }
        }
    }

    private func finish(_ candidate: NWConnection, id: UUID, error: Error?,
                        handlers: Handlers) {
        guard isCurrent(candidate, id: id) else { return }
        connection = nil
        connectionID = nil
        currentHandlers = nil
        buffer.removeAll(keepingCapacity: true)
        candidate.stateUpdateHandler = nil
        candidate.cancel()
        handlers.disconnected?(error)
    }

    private func isCurrent(_ candidate: NWConnection, id: UUID) -> Bool {
        connection === candidate && connectionID == id
    }

    private func describe(_ error: NWError) -> String {
        switch error {
        case .posix(let code): return "posix \(code.rawValue)"
        case .dns(let code): return "dns \(code)"
        case .tls(let code): return "tls \(code)"
        @unknown default: return "\(error)"
        }
    }
}
