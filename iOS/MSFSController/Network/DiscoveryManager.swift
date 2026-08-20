import Foundation
import Darwin

// 局域网自动探测：向 255.255.255.255:36668 广播 MSFS_DISCOVER，
// 接收 Windows 端应答（msfs_host），自动发现主机。
// 使用 BSD socket 以便接收来自多个主机的单播应答。

final class DiscoveryManager {
    struct Host: Identifiable, Equatable {
        let id = UUID()
        var name: String
        var ips: [String]
        var udpPort: UInt16
        var tcpPort: UInt16
    }

    private let stateLock = NSLock()
    private var sock: Int32 = -1
    private var scanID: UUID?
    private var thread: Thread?
    private var onHost: ((Host) -> Void)?
    var onLog: ((String) -> Void)?
    private(set) var sentCount = 0
    private(set) var replyCount = 0

    var isRunning: Bool {
        stateLock.lock(); defer { stateLock.unlock() }
        return scanID != nil
    }

    /// 启动探测：每 2 秒广播一次，期间收到的应答通过 onHost 回调（主线程）。
    func start(onHost: @escaping (Host) -> Void) {
        guard !isRunning else { return }
        self.onHost = onHost
        sentCount = 0
        replyCount = 0

        let newSocket = socket(AF_INET, SOCK_DGRAM, 0)
        if newSocket < 0 {
            onLog?("UDP 探测: socket 创建失败 (errno \(errno))")
            return
        }
        onLog?("UDP 探测: socket 已创建")

        var broadcast: Int32 = 1
        setsockopt(newSocket, SOL_SOCKET, SO_BROADCAST, &broadcast, socklen_t(MemoryLayout<Int32>.size))
        var reuse: Int32 = 1
        setsockopt(newSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, socklen_t(MemoryLayout<Int32>.size))

        var addr = sockaddr_in()
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_port = 0                        // 随机本地端口
        addr.sin_addr.s_addr = INADDR_ANY
        let bound = withUnsafePointer(to: &addr) { p -> Int32 in
            p.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                bind(newSocket, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        if bound != 0 {
            onLog?("UDP 探测: bind 失败 (errno \(errno))")
            close(newSocket)
            return
        }
        let id = UUID()
        stateLock.lock()
        sock = newSocket
        scanID = id
        stateLock.unlock()
        onLog?("UDP 探测: 开始广播 \(Proto.discoveryRequest.trimmingCharacters(in: .newlines)) -> 255.255.255.255:\(Proto.discoveryPort)")

        let t = Thread { [weak self] in self?.loop(socket: newSocket, id: id) }
        t.name = "msfs.discovery"
        thread = t
        t.start()
    }

    func stop() {
        stateLock.lock()
        let oldSocket = sock
        sock = -1
        scanID = nil
        stateLock.unlock()
        if oldSocket >= 0 { close(oldSocket) }
    }

    deinit {
        stop()
    }

    // MARK: - 内部

    private func isCurrent(_ id: UUID) -> Bool {
        stateLock.lock(); defer { stateLock.unlock() }
        return scanID == id
    }

    private func loop(socket: Int32, id: UUID) {
        var lastSend = Date(timeIntervalSince1970: 0)
        var buf = [UInt8](repeating: 0, count: 2048)

        while isCurrent(id) {
            if Date().timeIntervalSince(lastSend) >= 2.0 {
                sendBroadcast(socket: socket)
                lastSend = Date()
            }

            var pfd = pollfd(fd: socket, events: Int16(POLLIN), revents: 0)
            let n = poll(&pfd, 1, 400)
            guard isCurrent(id) else { break }
            if n > 0 && (pfd.revents & Int16(POLLIN)) != 0 {
                var from = sockaddr_in()
                var len = socklen_t(MemoryLayout<sockaddr_in>.size)
                let bufCount = buf.count
                let r = buf.withUnsafeMutableBytes { ptr -> Int in
                    withUnsafeMutablePointer(to: &from) { p in
                        p.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                            Int(recvfrom(socket, ptr.baseAddress, bufCount, 0, $0, &len))
                        }
                    }
                }
                if r > 0 {
                    replyCount += 1
                    let sourceIP = inet_ntoa(from.sin_addr).map { String(cString: $0) }
                    handleReply(Data(buf[0..<r]), sourceIP: sourceIP, id: id)
                }
            }
        }
        stateLock.lock()
        let ownsSocket = scanID == id && sock == socket
        if ownsSocket { sock = -1; scanID = nil }
        stateLock.unlock()
        if ownsSocket { close(socket) }
    }

    private func sendBroadcast(socket: Int32) {
        var addr = sockaddr_in()
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_port = Proto.discoveryPort.bigEndian
        addr.sin_addr.s_addr = in_addr_t(0xFFFFFFFF)   // 255.255.255.255
        let msg = Proto.discoveryRequest
        var result = -1
        msg.withCString { c in
            result = withUnsafePointer(to: &addr) { p -> Int in
                p.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                    Int(sendto(socket, c, msg.count, 0, $0, socklen_t(MemoryLayout<sockaddr_in>.size)))
                }
            }
        }
        if result > 0 {
            sentCount += 1
        } else {
            onLog?("UDP 探测: 广播发送失败 (errno \(errno))")
        }
    }

    private func handleReply(_ data: Data, sourceIP: String?, id: UUID) {
        guard let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              obj["type"] as? String == TcpMsg.hostDiscovery,
              (obj["protocolVersion"] as? NSNumber)?.uint8Value == Proto.protocolVersion,
              let advertisedIPs = obj["ips"] as? [String], !advertisedIPs.isEmpty else { return }

        // 应答包的来源地址一定是手机当前可达的那张 Windows 网卡；多网卡机器上
        // 不应盲选服务器枚举出的第一个地址（它可能属于 VPN/虚拟交换机）。
        var ips: [String] = []
        if let sourceIP, !sourceIP.isEmpty { ips.append(sourceIP) }
        for ip in advertisedIPs where !ips.contains(ip) { ips.append(ip) }

        let udpValue = obj["udpPort"] as? Int ?? Int(Proto.defaultUdpPort)
        let tcpValue = obj["tcpPort"] as? Int ?? Int(Proto.defaultTcpPort)
        let host = Host(name: obj["name"] as? String ?? "",
                        ips: ips,
                        udpPort: UInt16(exactly: udpValue) ?? Proto.defaultUdpPort,
                        tcpPort: UInt16(exactly: tcpValue) ?? Proto.defaultTcpPort)
        DispatchQueue.main.async { [weak self] in
            guard let self, self.isCurrent(id) else { return }
            self.onHost?(host)
        }
    }
}
