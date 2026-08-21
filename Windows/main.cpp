#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "AppWindow.h"
#include "Protocol.h"
#include "FlightController.h"
#include "SimConnectManager.h"
#include "SafetyWatchdog.h"
#include "TelemetryManager.h"
#include "FlightPlanManager.h"
#include "UDPServer.h"
#include "TCPServer.h"

#include <thread>
#include <string>
#include <chrono>
#include <atomic>
#include <algorithm>

#pragma comment(lib, "Ws2_32.lib")

namespace {
long long NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrev*/, LPWSTR /*lpCmd*/, int nCmdShow) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;

    StatusStore status;
    FlightController fc;
    SimConnectManager sim;
    SafetyWatchdog watchdog;
    TelemetryManager telemetry;
    FlightPlanManager flightPlan;
    UDPServer udp;
    TCPServer tcp;

    // ---------- 安全看门狗 ----------
    watchdog.Start([&]() {
        fc.NeutralizeAxes();
        status.SetWatchdogFired(true);
        status.SetStatus("Watchdog: control timeout, axes centered");
    });

    // ---------- SimConnect ----------
    sim.Start(&fc,
        [&](const AircraftTelemetry& t) {
            telemetry.Push(t);
        },
        [&](bool connected, const std::string& aircraft) {
            status.SetSim(connected);
            telemetry.SetSimConnected(connected);
            if (!aircraft.empty()) {
                status.SetAircraft(aircraft);
                telemetry.SetAircraftName(aircraft);
            } else if (!connected) {
                status.SetAircraft("");
                telemetry.SetAircraftName("");
                flightPlan.Clear();
                tcp.SendRoute(flightPlan);
                status.SetFlightPlan("");
            }
            tcp.SendStatus(connected, aircraft);
        },
        [&](const std::wstring& plnFile) {
            if (plnFile.empty()) {
                flightPlan.Clear();
                tcp.SendRoute(flightPlan);
                status.SetFlightPlan("");
            } else if (flightPlan.LoadFile(plnFile)) {
                tcp.SendRoute(flightPlan);
                status.SetFlightPlan(flightPlan.Summary());
            } else {
                flightPlan.Clear();
                tcp.SendRoute(flightPlan);
                status.SetFlightPlan("Unable to parse active flight plan");
            }
        });

    // ---------- 遥测 ----------
    telemetry.Start([&](const std::string& json) { tcp.SendToClient(json); });

    // ---------- UDP 实时控制 ----------
    udp.Start(proto::kDefaultUdpPort,
              [&](uint32_t sid) { return sid != 0 && sid == tcp.SessionId(); },
              [&]() { return sim.IsSimConnected(); },
              &fc, &watchdog);

    // ---------- TCP 状态与命令 ----------
    tcp.Start(proto::kDefaultTcpPort, &sim, &fc,
              [&]() {
                  StatusStore::Snapshot s = status.Take();
                  return std::make_pair(s.simConnected, s.aircraft);
              });

    // ---------- 状态监视线程：刷新连接状态 / 控制包延迟 ----------
    std::atomic<bool> running{ true };
    std::thread monitor([&]() {
        while (running.load()) {
            bool phone = tcp.SessionId() != 0;
            status.SetIphone(phone);

            if (tcp.IsReady() && udp.IsControlReady() && udp.IsDiscoveryReady()) {
                status.SetNetwork("Listening (TCP 36667 / UDP 36666,36668), discovery replies: " +
                                  std::to_string(udp.DiscoveryReplies()));
            } else {
                std::string error;
                auto appendError = [&](const std::string& value) {
                    if (value.empty()) return;
                    if (!error.empty()) error += " | ";
                    error += value;
                };
                appendError(tcp.LastError());
                appendError(udp.ControlError());
                appendError(udp.DiscoveryError());
                status.SetNetwork(error.empty() ? "Starting listeners..." : "ERROR: " + error);
            }

            const long long last = watchdog.LastControlMs();
            if (phone && last > 0) {
                // timestampMs 来自 iPhone，仅用于 Pong 原样回显；两台设备的
                // steady clock 没有共同起点。控制包年龄必须完全使用 Windows
                // 本地 Touch 时间，并避免异常负值。
                status.SetLastControlAge(std::max(0LL, NowMs() - last));
            } else {
                status.SetLastControlAge(-1);
            }
            status.SetWatchdogFired(watchdog.IsExpired());
            if (phone) {
                status.SetStatus(watchdog.IsExpired()
                    ? "Realtime control idle; transient axes centered"
                    : "Ready");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    // ---------- 主窗口 ----------
    AppWindow window;
    if (!window.Create(hInstance, &status)) {
        running = false;
        monitor.join();
        watchdog.Stop(); udp.Stop(); tcp.Stop(); telemetry.Stop(); sim.Stop();
        WSACleanup();
        return -1;
    }
    window.Show(nCmdShow);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // ---------- 关闭 ----------
    running = false;
    if (monitor.joinable()) monitor.join();
    watchdog.Stop();
    udp.Stop();
    tcp.Stop();
    telemetry.Stop();
    sim.Stop();
    WSACleanup();
    return (int)msg.wParam;
}
