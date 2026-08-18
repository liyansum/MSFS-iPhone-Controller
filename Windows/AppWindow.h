#pragma once
// PC 端主窗口：状态面板 + 系统托盘 + 开机自启选项。
// 各线程只写 StatusStore，UI 定时器读取刷新，避免跨线程直接操作窗口。

#include <windows.h>
#include <string>
#include <mutex>

class StatusStore {
public:
    struct Snapshot {
        bool simConnected = false;
        bool iphoneConnected = false;
        std::string aircraft;
        std::string flightPlan;
        int controlRate = 60;
        long long lastControlAgeMs = -1;
        bool watchdogFired = false;
        std::string status;
        std::string network;
    };

    void SetSim(bool v);
    void SetIphone(bool v);
    void SetAircraft(const std::string& v);
    void SetFlightPlan(const std::string& v);
    void SetControlRate(int v);
    void SetLastControlAge(long long v);
    void SetWatchdogFired(bool v);
    void SetStatus(const std::string& v);
    void SetNetwork(const std::string& v);
    Snapshot Take() const;

private:
    mutable std::mutex mtx_;
    Snapshot snap_;
};

class AppWindow {
public:
    bool Create(HINSTANCE inst, StatusStore* status);
    void Show(int nCmdShow);
    void Destroy();
    HWND Handle() const { return hwnd_; }

private:
    void RefreshUi(HWND hwnd);
    bool StartWithWindows(bool enabled);
    bool IsStartWithWindows();
    bool RepairFirewall(HWND owner);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
    StatusStore* status_ = nullptr;
    bool trayAdded_ = false;
    bool minimizedToTray_ = false;

    static AppWindow* self_;
};
