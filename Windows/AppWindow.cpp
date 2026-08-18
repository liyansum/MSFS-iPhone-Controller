#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <shellscalingapi.h>
#include <iphlpapi.h>

#include "AppWindow.h"
#include "Protocol.h"
#include "NetUtils.h"
#include <shellapi.h>
#include <sstream>
#include <cstring>
#include <vector>

#pragma comment(lib, "Iphlpapi.lib")

AppWindow* AppWindow::self_ = nullptr;

// 控件 ID
enum {
    IDC_LABEL_TITLE = 1001,
    IDC_VAL_MSFS,
    IDC_VAL_IPHONE,
    IDC_VAL_IP,
    IDC_VAL_IPLIST,
    IDC_VAL_UDP,
    IDC_VAL_TCP,
    IDC_VAL_NETWORK,
    IDC_VAL_RATE,
    IDC_VAL_RTT,
    IDC_VAL_AIRCRAFT,
    IDC_VAL_FP,
    IDC_CHK_STARTUP,
    IDC_BTN_FIREWALL,
    IDC_VAL_STATUS,
};

static const UINT WM_TRAYICON = WM_APP + 10;
static const UINT WM_REFRESH  = WM_APP + 11;

namespace {

// 全部 IPv4 地址（逗号分隔，带适配器名）
std::string AllLocalIps() {
    std::vector<NetAddrInfo> list = GetLocalIpv4Addresses();
    std::string s;
    for (const auto& a : list) {
        if (!s.empty()) s += ", ";
        s += a.ip;
        if (!a.adapter.empty()) s += "(" + a.adapter + ")";
    }
    return s.empty() ? "127.0.0.1" : s;
}

void EnableHighDPI() {
    HMODULE hShcore = LoadLibraryA("Shcore.dll");
    if (hShcore) {
        typedef HRESULT(WINAPI* SetProcessDpiAwareness_t)(PROCESS_DPI_AWARENESS);
        SetProcessDpiAwareness_t fn = (SetProcessDpiAwareness_t)GetProcAddress(hShcore, "SetProcessDpiAwareness");
        if (fn) fn(PROCESS_PER_MONITOR_DPI_AWARE);
        FreeLibrary(hShcore);
    }
}
} // namespace

// ---------------- StatusStore ----------------

void StatusStore::SetSim(bool v) { std::lock_guard<std::mutex> l(mtx_); snap_.simConnected = v; }
void StatusStore::SetIphone(bool v) { std::lock_guard<std::mutex> l(mtx_); snap_.iphoneConnected = v; }
void StatusStore::SetAircraft(const std::string& v) { std::lock_guard<std::mutex> l(mtx_); snap_.aircraft = v; }
void StatusStore::SetFlightPlan(const std::string& v) { std::lock_guard<std::mutex> l(mtx_); snap_.flightPlan = v; }
void StatusStore::SetControlRate(int v) { std::lock_guard<std::mutex> l(mtx_); snap_.controlRate = v; }
void StatusStore::SetLastControlAge(long long v) { std::lock_guard<std::mutex> l(mtx_); snap_.lastControlAgeMs = v; }
void StatusStore::SetWatchdogFired(bool v) { std::lock_guard<std::mutex> l(mtx_); snap_.watchdogFired = v; }
void StatusStore::SetStatus(const std::string& v) { std::lock_guard<std::mutex> l(mtx_); snap_.status = v; }
void StatusStore::SetNetwork(const std::string& v) { std::lock_guard<std::mutex> l(mtx_); snap_.network = v; }

StatusStore::Snapshot StatusStore::Take() const {
    std::lock_guard<std::mutex> l(mtx_);
    return snap_;
}

// ---------------- AppWindow ----------------

bool AppWindow::Create(HINSTANCE inst, StatusStore* status) {
    status_ = status;
    self_ = this;

    EnableHighDPI();

    WNDCLASSW wc{};
    wc.lpszClassName = L"MSFSiPhoneControllerWnd";
    wc.lpfnWndProc = &AppWindow::WndProc;
    wc.hInstance = inst;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(107));
    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(0, L"MSFSiPhoneControllerWnd", L"MSFS iPhone Controller",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, 700, 530,
                            nullptr, nullptr, inst, nullptr);
    return hwnd_ != nullptr;
}

void AppWindow::Show(int nCmdShow) {
    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);
}

void AppWindow::Destroy() {
    if (trayAdded_) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd_;
        nid.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        trayAdded_ = false;
    }
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
}

bool AppWindow::StartWithWindows(bool enabled) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return false;
    bool ok;
    if (enabled) {
        wchar_t exe[512];
        GetModuleFileNameW(nullptr, exe, 512);
        ok = RegSetValueExW(key, L"MSFSiPhoneController", 0, REG_SZ,
                            (const BYTE*)exe, (DWORD)(wcslen(exe) + 1) * sizeof(wchar_t)) == ERROR_SUCCESS;
    } else {
        ok = RegDeleteValueW(key, L"MSFSiPhoneController") == ERROR_SUCCESS ||
             GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(key);
    return ok;
}

bool AppWindow::IsStartWithWindows() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    bool exists = RegQueryValueExW(key, L"MSFSiPhoneController", nullptr, nullptr,
                                  nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(key);
    return exists;
}

bool AppWindow::RepairFirewall(HWND owner) {
    wchar_t exePath[32768]{};
    DWORD length = GetModuleFileNameW(nullptr, exePath, 32768);
    if (length == 0 || length >= 32768) {
        MessageBoxW(owner, L"无法读取当前程序路径。", L"防火墙修复失败", MB_OK | MB_ICONERROR);
        return false;
    }

    // 规则只允许同一子网访问当前 exe，且只作用于 Windows“专用”网络。
    std::wstring params =
        L"advfirewall firewall add rule name=\"MSFS iPhone Controller (Private LAN)\" "
        L"dir=in action=allow enable=yes profile=private remoteip=LocalSubnet program=\"";
    params += exePath;
    params += L"\"";

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.hwnd = owner;
    info.lpVerb = L"runas";
    info.lpFile = L"netsh.exe";
    info.lpParameters = params.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) {
        if (GetLastError() != ERROR_CANCELLED) {
            MessageBoxW(owner, L"无法启动 Windows 防火墙配置工具。", L"防火墙修复失败",
                        MB_OK | MB_ICONERROR);
        }
        return false;
    }

    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(info.hProcess, &exitCode);
    CloseHandle(info.hProcess);
    if (exitCode != 0) {
        MessageBoxW(owner, L"Windows 未能添加防火墙规则。请确认当前网络已设置为“专用网络”。",
                    L"防火墙修复失败", MB_OK | MB_ICONERROR);
        return false;
    }

    MessageBoxW(owner,
                L"已允许本程序接收同一专用局域网内的连接。现在请在 iPhone 上重新测试连接。\n\n"
                L"如果仍无回复，请在 Windows“网络和 Internet”设置中确认当前网络类型为“专用网络”。",
                L"防火墙已放行", MB_OK | MB_ICONINFORMATION);
    return true;
}

void AppWindow::RefreshUi(HWND hwnd) {
    StatusStore::Snapshot s = status_->Take();

    std::ostringstream msfs;
    msfs << (s.simConnected ? "Connected" : "Disconnected");
    std::ostringstream iphone;
    iphone << (s.iphoneConnected ? "Connected" : "Disconnected");
    std::ostringstream rate;
    rate << s.controlRate << " Hz";
    std::ostringstream rtt;
    if (s.lastControlAgeMs < 0) rtt << "--";
    else rtt << s.lastControlAgeMs << " ms";

    SetDlgItemTextA(hwnd, IDC_VAL_MSFS, msfs.str().c_str());
    SetDlgItemTextA(hwnd, IDC_VAL_IPHONE, iphone.str().c_str());
    SetDlgItemTextA(hwnd, IDC_VAL_IP, RecommendLocalIp().c_str());
    SetDlgItemTextA(hwnd, IDC_VAL_IPLIST, AllLocalIps().c_str());
    SetDlgItemTextA(hwnd, IDC_VAL_UDP, std::to_string(proto::kDefaultUdpPort).c_str());
    SetDlgItemTextA(hwnd, IDC_VAL_TCP, std::to_string(proto::kDefaultTcpPort).c_str());
    SetDlgItemTextA(hwnd, IDC_VAL_NETWORK, s.network.empty() ? "Starting..." : s.network.c_str());
    SetDlgItemTextA(hwnd, IDC_VAL_RATE, rate.str().c_str());
    SetDlgItemTextA(hwnd, IDC_VAL_RTT, rtt.str().c_str());
    SetDlgItemTextA(hwnd, IDC_VAL_AIRCRAFT, s.aircraft.empty() ? "--" : s.aircraft.c_str());
    SetDlgItemTextA(hwnd, IDC_VAL_FP, s.flightPlan.empty() ? "--" : s.flightPlan.c_str());

    std::string statusText = s.status;
    if (!s.simConnected) statusText = "等待 MSFS 启动...";
    SetDlgItemTextA(hwnd, IDC_VAL_STATUS, statusText.c_str());
}

LRESULT CALLBACK AppWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppWindow* self = self_;
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
        HFONT hBold = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");
        HFONT hNormal = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");

        auto add = [&](const wchar_t* text, int id, int x, int y, int w, int h) {
            HWND c = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                                     x, y, w, h, hwnd, (HMENU)(INT_PTR)id, hInst, nullptr);
            SendMessageW(c, WM_SETFONT, (WPARAM)hNormal, TRUE);
            return c;
        };

        HWND title = CreateWindowExW(0, L"STATIC", L"MSFS iPhone Controller",
                                     WS_CHILD | WS_VISIBLE, 16, 12, 300, 26,
                                     hwnd, (HMENU)IDC_LABEL_TITLE, hInst, nullptr);
        SendMessageW(title, WM_SETFONT, (WPARAM)hBold, TRUE);

        int y = 56, row = 28, lx = 24, lw = 110, vx = 140, vw = 450;
        add(L"MSFS:", IDC_LABEL_TITLE + 0, lx, y, lw, 20);
        add(L"", IDC_VAL_MSFS, vx, y, vw, 20); y += row;
        add(L"iPhone:", IDC_LABEL_TITLE + 0, lx, y, lw, 20);
        add(L"", IDC_VAL_IPHONE, vx, y, vw, 20); y += row;
        add(L"Local IP:", IDC_LABEL_TITLE + 0, lx, y, lw, 20);
        add(L"", IDC_VAL_IP, vx, y, vw, 20); y += row;
        add(L"All IPs:", IDC_LABEL_TITLE + 0, lx, y, lw, 20);
        add(L"", IDC_VAL_IPLIST, vx, y, vw, 20); y += row;
        add(L"UDP Port:", IDC_LABEL_TITLE + 0, lx, y, lw, 20);
        add(L"", IDC_VAL_UDP, vx, y, vw, 20); y += row;
        add(L"TCP Port:", IDC_LABEL_TITLE + 0, lx, y, lw, 20);
        add(L"", IDC_VAL_TCP, vx, y, vw, 20); y += row;
        add(L"Network:", IDC_LABEL_TITLE + 0, lx, y, lw, 20);
        add(L"", IDC_VAL_NETWORK, vx, y, vw, 20); y += row;
        add(L"Control Rate:", IDC_LABEL_TITLE + 0, lx, y, lw, 20);
        add(L"", IDC_VAL_RATE, vx, y, vw, 20); y += row;
        add(L"Control Age:", IDC_LABEL_TITLE + 0, lx, y, lw, 20);
        add(L"", IDC_VAL_RTT, vx, y, vw, 20); y += row;
        add(L"Aircraft:", IDC_LABEL_TITLE + 0, lx, y, lw, 20);
        add(L"", IDC_VAL_AIRCRAFT, vx, y, vw, 20); y += row;
        add(L"Flight Plan:", IDC_LABEL_TITLE + 0, lx, y, lw, 20);
        add(L"", IDC_VAL_FP, vx, y, vw, 20); y += row + 6;

        HWND chk = CreateWindowExW(0, L"BUTTON", L"Start with Windows",
                                   WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                   lx, y, 200, 22, hwnd, (HMENU)IDC_CHK_STARTUP, hInst, nullptr);
        SendMessageW(chk, WM_SETFONT, (WPARAM)hNormal, TRUE);
        if (self) SendMessageW(chk, BM_SETCHECK, self->IsStartWithWindows() ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND firewall = CreateWindowExW(0, L"BUTTON", L"Allow iPhone through Firewall",
                                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        300, y - 4, 270, 30, hwnd,
                                        (HMENU)IDC_BTN_FIREWALL, hInst, nullptr);
        SendMessageW(firewall, WM_SETFONT, (WPARAM)hNormal, TRUE);

        y += 40;
        add(L"Status: Ready", IDC_VAL_STATUS, lx, y, 320, 20);

        SetTimer(hwnd, 1, 200, nullptr);

        // 托盘
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(107));
        wcscpy_s(nid.szTip, L"MSFS iPhone Controller");
        if (self) {
            self->trayAdded_ = Shell_NotifyIconW(NIM_ADD, &nid);
            self->RefreshUi(hwnd);
        }
        break;
    }

    case WM_TIMER:
        if (self) self->RefreshUi(hwnd);
        break;

    case WM_TRAYICON:
        if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            if (self) self->minimizedToTray_ = false;
        } else if (LOWORD(lParam) == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"打开");
            AppendMenuW(menu, MF_STRING, 2, L"退出");
            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
            if (cmd == 1) {
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
                if (self) self->minimizedToTray_ = false;
            } else if (cmd == 2) {
                PostMessage(hwnd, WM_CLOSE, 0, 0);
            }
        }
        break;

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED && self) {
            self->minimizedToTray_ = true;
            ShowWindow(hwnd, SW_HIDE);
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_CHK_STARTUP && HIWORD(wParam) == BN_CLICKED) {
            if (self) {
                bool on = SendMessageW((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED;
                self->StartWithWindows(on);
            }
        } else if (LOWORD(wParam) == IDC_BTN_FIREWALL && HIWORD(wParam) == BN_CLICKED) {
            if (self && self->RepairFirewall(hwnd) && self->status_) {
                self->status_->SetStatus("Firewall rule installed; retry the iPhone connection");
            }
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (self && self->trayAdded_) {
            NOTIFYICONDATAW nid{};
            nid.cbSize = sizeof(nid);
            nid.hWnd = hwnd;
            nid.uID = 1;
            Shell_NotifyIconW(NIM_DELETE, &nid);
            self->trayAdded_ = false;
        }
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
