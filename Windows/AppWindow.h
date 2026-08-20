#pragma once
// PC 端自适应状态面板。网络/SimConnect 线程只写 StatusStore；
// UI 线程定时获取快照并重绘，避免跨线程访问窗口对象。

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
    void CreateControls(HINSTANCE instance);
    void LayoutControls();
    void RefreshUi();
    void Paint(HDC dc);
    void DrawOwnerButton(const DRAWITEMSTRUCT& item);
    void RecreateFonts(UINT dpi);
    void ReleaseGraphics();
    bool CopyRecommendedIp();
    bool StartWithWindows(bool enabled);
    bool IsStartWithWindows();
    bool RepairFirewall(HWND owner);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
    HWND startupCheck_ = nullptr;
    HWND copyButton_ = nullptr;
    HWND firewallButton_ = nullptr;
    StatusStore* status_ = nullptr;
    StatusStore::Snapshot snapshot_;
    std::wstring recommendedIp_;
    std::wstring allIps_;

    HFONT titleFont_ = nullptr;
    HFONT subtitleFont_ = nullptr;
    HFONT sectionFont_ = nullptr;
    HFONT bodyFont_ = nullptr;
    HFONT valueFont_ = nullptr;
    HBRUSH backgroundBrush_ = nullptr;
    HBRUSH controlBrush_ = nullptr;
    UINT dpi_ = 96;

    bool trayAdded_ = false;
    bool minimizedToTray_ = false;
    static AppWindow* self_;
};
