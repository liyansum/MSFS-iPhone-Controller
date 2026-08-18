#include "UDPServer.h"
#include "Protocol.h"
#include "FlightController.h"
#include "SafetyWatchdog.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <cstring>
#include <chrono>
#include <sstream>
#include <vector>
#include "NetUtils.h"

// 部分 Windows SDK 在 _WIN32_WINNT 未达标时不下发 SIO_UDP_CONNRESET，
// 提供数值回退：_WSAIORW(IOC_VENDOR, 12) = IOC_IN|IOC_VENDOR|12
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET 0x9800000Cu
#endif

#pragma comment(lib, "Ws2_32.lib")

void UDPServer::Start(uint16_t port, SessionCheck check, FlightController* fc, SafetyWatchdog* wd) {
    check_ = std::move(check);
    fc_ = fc;
    wd_ = wd;
    if (running_.load()) return;
    running_ = true;
    thread_ = std::thread(&UDPServer::ThreadMain, this, port);
    discoveryThread_ = std::thread(&UDPServer::DiscoveryThread, this, proto::kDiscoveryPort);
}

void UDPServer::Stop() {
    running_ = false;
    // 关闭套接字以解除阻塞 recvfrom
    SOCKET s = (SOCKET)sock_.exchange(0);
    if (s != INVALID_SOCKET) closesocket(s);
    SOCKET ds = (SOCKET)discoverySock_.exchange(0);
    if (ds != INVALID_SOCKET) closesocket(ds);
    if (thread_.joinable()) thread_.join();
    if (discoveryThread_.joinable()) discoveryThread_.join();
}

void UDPServer::ThreadMain(uint16_t port) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) return;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return;
    }
    sock_.store((unsigned long long)sock);

    // 忽略 Windows 下 ICMP 端口不可达导致的 WSAECONNRESET
    DWORD bytesReturned = 0;
    BOOL newBehavior = FALSE;
    WSAIoctl(sock, SIO_UDP_CONNRESET, &newBehavior, sizeof(newBehavior),
             nullptr, 0, &bytesReturned, nullptr, nullptr);

    sockaddr_storage from{};
    int fromLen = (int)sizeof(from);
    char buf[sizeof(proto::UdpPacket) + 8];

    while (running_.load()) {
        fromLen = (int)sizeof(from);
        int len = recvfrom(sock, buf, (int)sizeof(buf), 0, (sockaddr*)&from, &fromLen);
        if (len == SOCKET_ERROR) {
            if (!running_.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        if (len < (int)sizeof(proto::UdpPacket)) continue;

        proto::UdpPacket pkt;
        memcpy(&pkt, buf, sizeof(pkt));
        if (pkt.magic != proto::kMagic || pkt.version != proto::kProtocolVersion) continue;

        if (pkt.type == proto::kUdpControl) {
            if (!check_ || !check_(pkt.sessionId)) continue;
            if (fc_) {
                fc_->UpdateFromControl(pkt.sequence, pkt.timestampMs, pkt.axisMask,
                                       pkt.aileron, pkt.elevator, pkt.rudder, pkt.throttle);
            }
            if (wd_) wd_->Touch();
        } else if (pkt.type == proto::kUdpPing) {
            pkt.type = proto::kUdpPong;
            sendto(sock, (const char*)&pkt, sizeof(pkt), 0, (sockaddr*)&from, fromLen);
        }
    }

    closesocket(sock);
    sock_.store(0);
}

// 自动探测：监听 36668 广播，回复本机信息（供 iPhone 自动发现主机）
void UDPServer::DiscoveryThread(uint16_t port) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) return;

    BOOL reuse = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return;
    }
    discoverySock_.store((unsigned long long)sock);

    char buf[256];
    while (running_.load()) {
        sockaddr_storage from{};
        int fromLen = (int)sizeof(from);
        int len = recvfrom(sock, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fromLen);
        if (len == SOCKET_ERROR) {
            if (!running_.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        buf[len] = '\0';
        if (strncmp(buf, proto::kDiscoveryRequest, strlen(proto::kDiscoveryRequest)) != 0) continue;

        // 构建应答：{"type":"msfs_host","name":...,"ips":[...],"udpPort":...,"tcpPort":...,"protocolVersion":...}
        std::vector<std::string> ips = LocalIpList();
        std::ostringstream os;
        os << "{\"type\":\"" << proto::kDiscoveryReplyType
           << "\",\"name\":\"" << proto::kDiscoveryReply
           << "\",\"ips\":[";
        for (size_t i = 0; i < ips.size(); ++i) {
            if (i) os << ",";
            os << "\"" << ips[i] << "\"";
        }
        os << "],\"udpPort\":" << (int)proto::kDefaultUdpPort
           << ",\"tcpPort\":" << (int)proto::kDefaultTcpPort
           << ",\"protocolVersion\":" << (int)proto::kProtocolVersion << "}";

        std::string reply = os.str();
        sendto(sock, reply.c_str(), (int)reply.size(), 0, (sockaddr*)&from, fromLen);
    }

    closesocket(sock);
    discoverySock_.store(0);
}
