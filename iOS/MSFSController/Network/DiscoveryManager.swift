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

    private var sock: Int32 = -1
    private var running = false
    private var thread: Thread?
    private var onHost: ((Host) -> Void)?

    var isRunning: Bool { running }

    /// 启动探测：每 2 秒广播一次，期间收到的应答通过 onHost 回调（主线程）。
    func start(onHost: @escaping (Host) -> Void) {
        guard !running else { return }
        running = true
        self.onHost = onHost

        sock = socket(AF_INET, SOCK_DGRAM, 0)
        if sock < 0 { running = false; return }

        var broadcast: Int32 = 1
        setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, socklen_t(MemoryLayout<Int32>.size))
        var reuse: Int32 = 1
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, socklen_t(MemoryLayout<Int32>.size))

        var addr = sockaddr_in()
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_port = 0                        // 随机本地端口
        addr.sin_addr.s_addr = INADDR_ANY
        let bound = withUnsafePointer(to: &addr) { p -> Int32 in
            p.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                bind(sock, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        if bound != 0 {
            close(sock)
            sock = -1
            running = false
            return
        }

        let t = Thread { [weak self] in self?.loop() }
        t.name = "msfs.discovery"
        thread = t
        t.start()
    }

    func stop() {
        running = false
        if sock >= 0 {
            close(sock)
            sock = -1
        }
    }

    deinit {
        stop()
    }

    // MARK: - 内部

    private func loop() {
        var lastSend = Date(timeIntervalSince1970: 0)
        var buf = [UInt8](repeating: 0, count: 2048)

        while running {
            if Date().timeIntervalSince(lastSend) >= 2.0 {
                sendBroadcast()
                lastSend = Date()
            }

            var pfd = pollfd(fd: sock, events: Int16(POLLIN), revents: 0)
            let n = poll(&pfd, 1, 400)
            if n > 0 && (pfd.revents & Int16(POLLIN)) != 0 {
                var from = sockaddr_in()
                var len = socklen_t(MemoryLayout<sockaddr_in>.size)
                let r = buf.withUnsafeMutableBytes { ptr -> Int in
                    withUnsafeMutablePointer(to: &from) { p in
                        p.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                            Int(recvfrom(sock, ptr.baseAddress, buf.count, 0, $0, &len))
                        }
                    }
                }
                if r > 0 {
                    handleReply(Data(buf[0..<r]))
                }
            }
        }
        if sock >= 0 {
            close(sock)
            sock = -1
        }
    }

    private func sendBroadcast() {
        guard sock >= 0 else { return }
        var addr = sockaddr_in()
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_port = Proto.discoveryPort.bigEndian
        addr.sin_addr.s_addr = in_addr_t(0xFFFFFFFF)   // 255.255.255.255
        let msg = Proto.discoveryRequest
        msg.withCString { c in
            _ = withUnsafePointer(to: &addr) { p in
                p.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                    sendto(sock, c, msg.count, 0, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
                }
            }
        }
    }

    private func handleReply(_ data: Data) {
        guard let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              obj["type"] as? String == TcpMsg.hostDiscovery,
              let ips = obj["ips"] as? [String], !ips.isEmpty else { return }

        let host = Host(name: obj["name"] as? String ?? "",
                        ips: ips,
                        udpPort: UInt16(obj["udpPort"] as? Int ?? Int(Proto.defaultUdpPort)),
                        tcpPort: UInt16(obj["tcpPort"] as? Int ?? Int(Proto.defaultTcpPort)))
        DispatchQueue.main.async { [weak self] in
            self?.onHost?(host)
        }
    }
}
