#include "UDPServer.h"
#include "Protocol.h"
#include "FlightController.h"
#include "SafetyWatchdog.h"
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <chrono>

#pragma comment(lib, "Ws2_32.lib")

void UDPServer::Start(uint16_t port, SessionCheck check, FlightController* fc, SafetyWatchdog* wd) {
    check_ = std::move(check);
    fc_ = fc;
    wd_ = wd;
    if (running_.load()) return;
    running_ = true;
    thread_ = std::thread(&UDPServer::ThreadMain, this, port);
}

void UDPServer::Stop() {
    running_ = false;
    // 关闭套接字以解除阻塞 recvfrom
    SOCKET s = (SOCKET)sock_.exchange(0);
    if (s != INVALID_SOCKET) closesocket(s);
    if (thread_.joinable()) thread_.join();
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
