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
#include <cmath>

#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#endif

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
       << "\",\"departureRunway\":\"" << Json::escape(fp.DepartureRunway())
       << "\",\"departureProcedure\":\"" << Json::escape(fp.DepartureProcedure())
       << "\",\"arrivalProcedure\":\"" << Json::escape(fp.ArrivalProcedure())
       << "\",\"approachType\":\"" << Json::escape(fp.ApproachType())
       << "\",\"destinationRunway\":\"" << Json::escape(fp.DestinationRunway())
       << "\",\"cruisingAltitude\":" << fp.CruisingAltitude()
       << ",\"waypoints\":[";
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
                      StatusGetter status) {
    sim_ = sim;
    fc_ = fc;
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
    if (sim_ && sim_->IsSimConnected()) sim_->EnqueueCommand(kEvBrakeRelease);
    if (fc_) fc_->ResetSession();
    SOCKET l = (SOCKET)listenSock_.exchange(0);
    if (l != 0 && l != INVALID_SOCKET) closesocket(l);
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard<std::mutex> lock(sendMtx_);
        if (clientSock_ != INVALID_SOCKET) {
            shutdown(clientSock_, SD_BOTH);
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
        // 避免遥测发送在对端不再读取时永久阻塞 SimConnect/遥测线程。
        DWORD sendTimeoutMs = 1000;
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<const char*>(&sendTimeoutMs), sizeof(sendTimeoutMs));
        // 单客户端，但新连接应替换旧连接。iPhone 修改主机或网络切换时，
        // 旧 TCP 会话可能尚未被 Windows 检测为断开；直接拒绝会令客户端陷入
        // "already connected" 循环。
        if (clientActive_.load()) {
            if (sim_ && sim_->IsSimConnected()) sim_->EnqueueCommand(kEvBrakeRelease);
            if (fc_) fc_->ResetSession();
            {
                std::lock_guard<std::mutex> lock(sendMtx_);
                if (clientSock_ != INVALID_SOCKET) {
                    shutdown(clientSock_, SD_BOTH);
                    closesocket(clientSock_);
                    clientSock_ = INVALID_SOCKET;
                }
            }
            if (clientThread_.joinable()) clientThread_.join();
            clientActive_ = false;
            sessionId_ = 0;
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
        if (buffer.size() > 1024 * 1024) {
            SendResponse(client, BuildError(7, "message too large"));
            break;
        }
        size_t nl;
        while ((nl = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, nl);
            buffer.erase(0, nl + 1);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (!line.empty()) ProcessLine(client, line);
        }
    }

    bool ownsSocket = false;
    {
        std::lock_guard<std::mutex> lock(sendMtx_);
        if (clientSock_ == client) {
            clientSock_ = INVALID_SOCKET;
            ownsSocket = true;
        }
    }
    if (ownsSocket) {
        clientActive_ = false;
        sessionId_ = 0;
        if (fc_) fc_->ResetSession();
        if (sim_ && sim_->IsSimConnected()) sim_->EnqueueCommand(kEvBrakeRelease);
        closesocket(client);
    }
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
        if ((int)j.num("protocolVersion", -1) != (int)proto::kProtocolVersion) {
            SendResponse(client, BuildError(5, "unsupported protocol version"));
            return;
        }
        uint32_t sid = RandomSession();
        if (fc_) fc_->ResetSession();
        sessionId_ = sid;
        bool sim = false;
        std::string aircraft;
        if (status_) {
            auto p = status_();
            sim = p.first;
            aircraft = p.second;
        }
        SendResponse(client, BuildWelcome(sid, sim, aircraft));
        // 飞行计划可能早于 iPhone 建立连接；新会话也要收到当前缓存路线。
        std::string route;
        {
            std::lock_guard<std::mutex> lock(routeMtx_);
            route = lastRouteJson_;
        }
        if (!route.empty()) SendResponse(client, route);
        return;
    }
    if (type == proto::kMsgCmd) {
        if (sessionId_.load() == 0) {
            SendResponse(client, BuildError(6, "hello required"));
            return;
        }
        if (!sim_ || !sim_->IsSimConnected()) {
            SendResponse(client, BuildError(8, "MSFS is not connected"));
            return;
        }
        std::string name = j.str("name");
        int command = -1;
        if (name == proto::kCmdFlapsIncr) command = kEvFlapsIncr;
        else if (name == proto::kCmdFlapsDecr) command = kEvFlapsDecr;
        else if (name == proto::kCmdGear) command = kEvGear;
        else if (name == proto::kCmdTrimUp) command = kEvTrimUp;
        else if (name == proto::kCmdTrimDn) command = kEvTrimDn;
        else if (name == proto::kCmdParking) command = kEvParking;
        else if (name == proto::kCmdBrake)
            command = j.boolean("value", false) ? kEvBrakeHold : kEvBrakeRelease;
        else if (name == proto::kCmdThrottleIncr) command = kEvThrottleIncr;
        else if (name == proto::kCmdThrottleDecr) command = kEvThrottleDecr;
        else if (name == proto::kCmdThrottleTakeover) command = kEvThrottleTakeover;
        else if (name == proto::kCmdThrottleSet) {
            const double value = j.num("value", -1);
            if (value < 0 || value > proto::kThrottleMax) {
                SendResponse(client, BuildError(7, "throttle must be 0..16383"));
                return;
            }
            if (!sim_->EnqueueCommand(kEvThrottleSet, static_cast<int>(value + 0.5)))
                SendResponse(client, BuildError(8, "MSFS disconnected before command execution"));
            return;
        }
        else if (name == proto::kCmdThrottleIdle) command = kEvThrottleIdle;
        else if (name == proto::kCmdAutopilot)
            command = j.boolean("value", false) ? kEvAutopilotOn : kEvAutopilotOff;
        else if (name == proto::kCmdAutopilotMode) {
            const std::string mode = j.str("value");
            if (mode == "heading") command = kEvAutopilotHeadingMode;
            else if (mode == "nav") command = kEvAutopilotNavMode;
            else if (mode == "off") command = kEvAutopilotLateralOff;
            else {
                SendResponse(client, BuildError(7, "invalid autopilot mode"));
                return;
            }
        } else if (name == proto::kCmdAutopilotHeading) {
            const double rawHeading = j.num("value", -1);
            if (rawHeading < 0 || rawHeading >= 360) {
                SendResponse(client, BuildError(7, "autopilot heading must be 0..359"));
                return;
            }
            command = kEvAutopilotHeadingSet;
            if (!sim_->EnqueueCommand(command, static_cast<int>(rawHeading + 0.5)))
                SendResponse(client, BuildError(8, "MSFS disconnected before command execution"));
            return;
        } else if (name == proto::kCmdNavigationSource) {
            const std::string source = j.str("value");
            if (source == "gps") command = kEvNavigationSourceGps;
            else if (source == "nav1") command = kEvNavigationSourceNav1;
            else {
                SendResponse(client, BuildError(7, "invalid navigation source"));
                return;
            }
        } else if (name == proto::kCmdAutopilotAltitude) {
            const double value = j.num("value", -1);
            if (value < 0 || value > 60000) {
                SendResponse(client, BuildError(7, "autopilot altitude must be 0..60000 feet"));
                return;
            }
            command = kEvAutopilotAltitudeSet;
            if (!sim_->EnqueueCommand(command, static_cast<int>(value + 0.5)))
                SendResponse(client, BuildError(8, "MSFS disconnected before command execution"));
            return;
        } else if (name == proto::kCmdAutopilotVerticalSpeed) {
            const double value = j.num("value", 99999);
            if (value < -6000 || value > 6000) {
                SendResponse(client, BuildError(7, "autopilot vertical speed must be -6000..6000"));
                return;
            }
            command = kEvAutopilotVerticalSpeedSet;
            if (!sim_->EnqueueCommand(command, static_cast<int>(std::round(value))))
                SendResponse(client, BuildError(8, "MSFS disconnected before command execution"));
            return;
        } else if (name == proto::kCmdAutopilotSpeed) {
            const double value = j.num("value", -1);
            if (value < 40 || value > 400) {
                SendResponse(client, BuildError(7, "autopilot speed must be 40..400 knots"));
                return;
            }
            command = kEvAutopilotSpeedSet;
            if (!sim_->EnqueueCommand(command, static_cast<int>(value + 0.5)))
                SendResponse(client, BuildError(8, "MSFS disconnected before command execution"));
            return;
        } else if (name == proto::kCmdAutopilotVerticalMode) {
            const std::string mode = j.str("value");
            if (mode == "hold") command = kEvAutopilotAltitudeHold;
            else if (mode == "vs") command = kEvAutopilotVerticalSpeedMode;
            else if (mode == "flc") command = kEvAutopilotFlightLevelChangeMode;
            else if (mode == "off") command = kEvAutopilotVerticalOff;
            else {
                SendResponse(client, BuildError(7, "invalid autopilot vertical mode"));
                return;
            }
        } else if (name == proto::kCmdAutopilotApproach) {
            command = j.boolean("value", false)
                ? kEvAutopilotApproachOn : kEvAutopilotApproachOff;
        } else if (name == proto::kCmdSyncFlightPlan) {
            if (!sim_->HasActiveFlightPlan()) {
                SendResponse(client, BuildError(9, "no active flight plan to synchronize"));
                return;
            }
            command = kEvSyncFlightPlan;
        }
        else {
            SendResponse(client, BuildError(3, "unknown command: " + name));
            return;
        }
        if (!sim_->EnqueueCommand(command))
            SendResponse(client, BuildError(8, "MSFS disconnected before command execution"));
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
    if (!SendLine(clientSock_, json)) shutdown(clientSock_, SD_BOTH);
}

void TCPServer::SendStatus(bool simConnected, const std::string& aircraft) {
    if (!clientActive_.load()) return;
    SendToClient(BuildStatus(simConnected, aircraft));
}

void TCPServer::SendRoute(const FlightPlanManager& fp) {
    std::string route = BuildRoute(fp);
    {
        std::lock_guard<std::mutex> lock(routeMtx_);
        lastRouteJson_ = route;
    }
    if (clientActive_.load()) SendToClient(route);
}
