#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "TCPServer.h"
#include "Protocol.h"
#include "Json.h"
#include "SimConnectManager.h"
#include "FlightController.h"
#include "FlightPlanManager.h"
#include <ws2tcpip.h>
#include <random>
#include <sstream>
#include <cstring>
#include <chrono>

#pragma comment(lib, "Ws2_32.lib")

namespace {

uint32_t RandomSession() {
    static std::mt19937 gen(std::random_device{}());
    return std::uniform_int_distribution<uint32_t>(1, 0xFFFFFFFE)(gen);
}

std::string BuildWelcome(uint32_t session, bool sim, const std::string& aircraft) {
    std::ostringstream os;
    os << "{\"type\":\"" << proto::kMsgWelcome
       << "\",\"protocolVersion\":" << (int)proto::kProtocolVersion
       << ",\"sessionId\":" << session
       << ",\"serverVersion\":\"" << proto::kServerVersion
       << "\",\"simConnected\":" << (sim ? "true" : "false")
       << ",\"aircraftName\":\"" << Json::escape(aircraft) << "\"}";
    return os.str();
}

std::string BuildStatus(bool sim, const std::string& aircraft) {
    std::ostringstream os;
    os << "{\"type\":\"" << proto::kMsgStatus
       << "\",\"simConnected\":" << (sim ? "true" : "false")
       << ",\"aircraftName\":\"" << Json::escape(aircraft) << "\"}";
    return os.str();
}

std::string BuildRoute(const FlightPlanManager& fp) {
    std::ostringstream os;
    os << "{\"type\":\"" << proto::kMsgRoute
       << "\",\"departure\":\"" << Json::escape(fp.Departure())
       << "\",\"destination\":\"" << Json::escape(fp.Destination())
       << "\",\"waypoints\":[";
    const auto& wps = fp.Waypoints();
    for (size_t i = 0; i < wps.size(); ++i) {
        if (i) os << ",";
        os << "{\"index\":" << wps[i].index
           << ",\"ident\":\"" << Json::escape(wps[i].ident)
           << "\",\"lat\":" << wps[i].lat
           << ",\"lon\":" << wps[i].lon
           << ",\"alt\":" << wps[i].alt << "}";
    }
    os << "]}";
    return os.str();
}

std::string BuildError(int code, const std::string& message) {
    std::ostringstream os;
    os << "{\"type\":\"" << proto::kMsgError
       << "\",\"code\":" << code
       << ",\"message\":\"" << Json::escape(message) << "\"}";
    return os.str();
}

} // namespace

std::string TCPServer::LastError() const {
    std::lock_guard<std::mutex> lock(errorMtx_);
    return lastError_;
}

void TCPServer::SetError(const std::string& message) {
    std::lock_guard<std::mutex> lock(errorMtx_);
    lastError_ = message;
}

void TCPServer::Start(uint16_t port, SimConnectManager* sim, FlightController* fc,
                      FlightPlanManager* fp, StatusGetter status) {
    sim_ = sim;
    fc_ = fc;
    fp_ = fp;
    status_ = std::move(status);
    if (started_.load()) return;
    SetError("");
    ready_ = false;
    started_ = true;
    running_ = true;
    thread_ = std::thread(&TCPServer::AcceptLoop, this, port);
}

void TCPServer::Stop() {
    running_ = false;
    ready_ = false;
    SOCKET l = (SOCKET)listenSock_.exchange(0);
    if (l != 0 && l != INVALID_SOCKET) closesocket(l);
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard<std::mutex> lock(sendMtx_);
        if (clientSock_ != INVALID_SOCKET) {
            closesocket(clientSock_);
            clientSock_ = INVALID_SOCKET;
        }
    }
    if (clientThread_.joinable()) clientThread_.join();
}

void TCPServer::AcceptLoop(uint16_t port) {
    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        SetError("TCP socket failed: " + std::to_string(WSAGetLastError()));
        return;
    }

    BOOL reuse = TRUE;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR ||
        listen(listenSock, 2) == SOCKET_ERROR) {
        SetError("TCP bind/listen failed on port " + std::to_string(port) +
                 ": " + std::to_string(WSAGetLastError()));
        closesocket(listenSock);
        return;
    }
    listenSock_.store((unsigned long long)listenSock);
    ready_ = true;

    while (running_.load()) {
        sockaddr_storage from{};
        int fromLen = (int)sizeof(from);
        SOCKET client = accept(listenSock, (sockaddr*)&from, &fromLen);
        if (client == INVALID_SOCKET) {
            if (!running_.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        if (!running_.load()) {
            closesocket(client);
            break;
        }
        // 第一版单客户端：已有连接则拒绝新连接
        if (clientActive_.load()) {
            SendLine(client, BuildError(2, "already connected"));
            closesocket(client);
            continue;
        }
        if (clientThread_.joinable()) clientThread_.join();
        {
            std::lock_guard<std::mutex> lock(sendMtx_);
            clientSock_ = client;
        }
        clientActive_ = true;
        clientThread_ = std::thread([this, client] { HandleClient(client); });
    }

    SOCKET owned = (SOCKET)listenSock_.exchange(0);
    if (owned == listenSock) closesocket(listenSock);
    ready_ = false;
}

void TCPServer::HandleClient(SOCKET client) {
    std::string buffer;
    char buf[4096];
    while (running_.load()) {
        int len = recv(client, buf, sizeof(buf), 0);
        if (len <= 0) break;
        buffer.append(buf, len);
        size_t nl;
        while ((nl = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, nl);
            buffer.erase(0, nl + 1);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (!line.empty()) ProcessLine(client, line);
        }
    }

    clientActive_ = false;
    sessionId_ = 0;
    bool ownsSocket = false;
    {
        std::lock_guard<std::mutex> lock(sendMtx_);
        if (clientSock_ == client) {
            clientSock_ = INVALID_SOCKET;
            ownsSocket = true;
        }
    }
    if (ownsSocket) closesocket(client);
}

void TCPServer::ProcessLine(SOCKET client, const std::string& line) {
    bool ok = false;
    Json j = Json::parse(line, ok);
    if (!ok || !j.isObject()) {
        SendResponse(client, BuildError(1, "invalid json"));
        return;
    }
    std::string type = j.str("type");
    if (type == proto::kMsgHello) {
        uint32_t sid = RandomSession();
        sessionId_ = sid;
        bool sim = false;
        std::string aircraft;
        if (status_) {
            auto p = status_();
            sim = p.first;
            aircraft = p.second;
        }
        SendResponse(client, BuildWelcome(sid, sim, aircraft));
        return;
    }
    if (type == proto::kMsgCmd) {
        if (!sim_) return;
        std::string name = j.str("name");
        if (name == proto::kCmdFlapsIncr) sim_->EnqueueCommand(kEvFlapsIncr);
        else if (name == proto::kCmdFlapsDecr) sim_->EnqueueCommand(kEvFlapsDecr);
        else if (name == proto::kCmdGear) sim_->EnqueueCommand(kEvGear);
        else if (name == proto::kCmdTrimUp) sim_->EnqueueCommand(kEvTrimUp);
        else if (name == proto::kCmdTrimDn) sim_->EnqueueCommand(kEvTrimDn);
        else if (name == proto::kCmdParking) sim_->EnqueueCommand(kEvParking);
        else if (name == proto::kCmdBrake)
            sim_->EnqueueCommand(j.boolean("value", false) ? kEvBrakeHold : kEvBrakeRelease);
        else SendResponse(client, BuildError(3, "unknown command: " + name));
        return;
    }
    SendResponse(client, BuildError(4, "unknown message type"));
}

bool TCPServer::SendAll(SOCKET s, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        int n = ::send(s, data.data() + sent, (int)(data.size() - sent), 0);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

bool TCPServer::SendLine(SOCKET s, const std::string& data) {
    return SendAll(s, data + "\n");
}

bool TCPServer::SendResponse(SOCKET client, const std::string& data) {
    std::lock_guard<std::mutex> lock(sendMtx_);
    if (clientSock_ != client) return false;
    return SendLine(client, data);
}

void TCPServer::SendToClient(const std::string& json) {
    if (!clientActive_.load()) return;
    std::lock_guard<std::mutex> lock(sendMtx_);
    if (clientSock_ == INVALID_SOCKET) return;
    SendLine(clientSock_, json);
}

void TCPServer::SendStatus(bool simConnected, const std::string& aircraft) {
    if (!clientActive_.load()) return;
    SendToClient(BuildStatus(simConnected, aircraft));
}

void TCPServer::SendRoute(const FlightPlanManager& fp) {
    if (!clientActive_.load()) return;
    SendToClient(BuildRoute(fp));
}
