#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include "AppWindow.h"
#include "Protocol.h"
#include "NetUtils.h"
#include "resource.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "UxTheme.lib")
#endif

AppWindow* AppWindow::self_ = nullptr;

namespace {

constexpr int IDC_STARTUP = 2001;
constexpr int IDC_COPY_IP = 2002;
constexpr int IDC_FIREWALL = 2003;
constexpr UINT WM_TRAYICON = WM_APP + 10;

constexpr COLORREF kBackground = RGB(11, 17, 28);
constexpr COLORREF kHeader = RGB(15, 23, 38);
constexpr COLORREF kCard = RGB(23, 33, 51);
constexpr COLORREF kCardBorder = RGB(43, 57, 78);
constexpr COLORREF kText = RGB(235, 241, 249);
constexpr COLORREF kMuted = RGB(148, 163, 184);
constexpr COLORREF kAccent = RGB(75, 161, 255);
constexpr COLORREF kGood = RGB(52, 211, 153);
constexpr COLORREF kWarn = RGB(251, 191, 36);
constexpr COLORREF kBad = RGB(248, 113, 113);

int Scale(int value, UINT dpi) { return MulDiv(value, static_cast<int>(dpi), 96); }

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                    value.data(), static_cast<int>(value.size()), nullptr, 0);
    UINT codePage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (count <= 0) {
        // SimConnect 的部分旧机模名称可能仍使用系统 ANSI 编码。
        codePage = CP_ACP;
        flags = 0;
        count = MultiByteToWideChar(codePage, flags, value.data(),
                                    static_cast<int>(value.size()), nullptr, 0);
    }
    if (count <= 0) return L"--";
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(codePage, flags, value.data(), static_cast<int>(value.size()),
                        result.data(), count);
    return result;
}

std::wstring AllLocalIps() {
    std::vector<NetAddrInfo> list = GetLocalIpv4Addresses();
    std::wstring result;
    for (const auto& address : list) {
        if (!result.empty()) result += L"  ·  ";
        result += Utf8ToWide(address.ip);
        if (!address.adapter.empty()) {
            result += L" (";
            result += Utf8ToWide(address.adapter);
            result += L")";
        }
    }
    return result.empty() ? L"127.0.0.1" : result;
}

std::wstring CurrentRunCommand() {
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable,
                                            static_cast<DWORD>(std::size(executable)));
    if (!length || length >= std::size(executable)) return {};
    return L"\"" + std::wstring(executable, length) + L"\"";
}

void EnableHighDpi() {
    if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    SetProcessDPIAware();
}

void FillRound(HDC dc, const RECT& rect, int radius, COLORREF color, COLORREF border) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawTextLine(HDC dc, const std::wstring& text, RECT rect, HFONT font,
                  COLORREF color, UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, format);
    SelectObject(dc, oldFont);
}

void DrawStatusRow(HDC dc, const RECT& card, int y, int rowHeight, int labelWidth,
                   const wchar_t* label, const std::wstring& value,
                   HFONT bodyFont, HFONT valueFont, COLORREF valueColor = kText) {
    RECT labelRect{ card.left + 18, y, labelWidth, y + rowHeight };
    RECT valueRect{ card.left + labelWidth, y, card.right - 18, y + rowHeight };
    DrawTextLine(dc, label, labelRect, bodyFont, kMuted);
    DrawTextLine(dc, value.empty() ? L"--" : value, valueRect, valueFont, valueColor);
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
StatusStore::Snapshot StatusStore::Take() const { std::lock_guard<std::mutex> l(mtx_); return snap_; }

// ---------------- AppWindow ----------------

bool AppWindow::Create(HINSTANCE instance, StatusStore* status) {
    status_ = status;
    self_ = this;
    EnableHighDpi();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpszClassName = L"MSFSiPhoneControllerWnd";
    wc.lpfnWndProc = &AppWindow::WndProc;
    wc.hInstance = instance;
    wc.hbrBackground = nullptr;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_ICON1));
    wc.hIconSm = wc.hIcon;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"MSFS iPhone Controller",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, 900, 610,
                            nullptr, nullptr, instance, nullptr);
    return hwnd_ != nullptr;
}

void AppWindow::Show(int nCmdShow) {
    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);
}

void AppWindow::Destroy() {
    if (hwnd_) DestroyWindow(hwnd_);
}

void AppWindow::CreateControls(HINSTANCE instance) {
    startupCheck_ = CreateWindowExW(0, L"BUTTON", L"随 Windows 启动",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 1, 1, hwnd_, reinterpret_cast<HMENU>(IDC_STARTUP), instance, nullptr);
    copyButton_ = CreateWindowExW(0, L"BUTTON", L"复制推荐 IP",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 1, 1, hwnd_, reinterpret_cast<HMENU>(IDC_COPY_IP), instance, nullptr);
    firewallButton_ = CreateWindowExW(0, L"BUTTON", L"一键放行局域网",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 1, 1, hwnd_, reinterpret_cast<HMENU>(IDC_FIREWALL), instance, nullptr);

    for (HWND control : { startupCheck_, copyButton_, firewallButton_ }) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont_), TRUE);
    }
    SendMessageW(startupCheck_, BM_SETCHECK, IsStartWithWindows() ? BST_CHECKED : BST_UNCHECKED, 0);
}

void AppWindow::LayoutControls() {
    if (!hwnd_ || !startupCheck_) return;
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int margin = Scale(20, dpi_);
    const int gap = Scale(10, dpi_);
    const int height = Scale(36, dpi_);
    const int y = std::max(0, static_cast<int>(client.bottom) - margin - height);
    const int copyWidth = Scale(142, dpi_);
    const int firewallWidth = Scale(168, dpi_);
    const int checkWidth = Scale(180, dpi_);

    MoveWindow(startupCheck_, margin, y, checkWidth, height, TRUE);
    MoveWindow(firewallButton_, client.right - margin - firewallWidth, y,
               firewallWidth, height, TRUE);
    MoveWindow(copyButton_, client.right - margin - firewallWidth - gap - copyWidth, y,
               copyWidth, height, TRUE);
}

void AppWindow::RefreshUi() {
    if (!status_) return;
    snapshot_ = status_->Take();
    recommendedIp_ = Utf8ToWide(RecommendLocalIp());
    allIps_ = AllLocalIps();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::Paint(HDC dc) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    FillRect(dc, &client, backgroundBrush_);

    const int margin = Scale(20, dpi_);
    const int headerHeight = Scale(88, dpi_);
    RECT header{ 0, 0, client.right, headerHeight };
    HBRUSH headerBrush = CreateSolidBrush(kHeader);
    FillRect(dc, &header, headerBrush);
    DeleteObject(headerBrush);

    RECT titleRect{ margin, Scale(14, dpi_), client.right - margin, Scale(49, dpi_) };
    DrawTextLine(dc, L"MSFS iPhone Controller", titleRect, titleFont_, kText);
    RECT subtitleRect{ margin, Scale(50, dpi_), client.right - margin, Scale(76, dpi_) };
    DrawTextLine(dc, L"局域网飞行控制服务  ·  保持此程序运行即可连接 iPhone",
                 subtitleRect, subtitleFont_, kMuted);

    const int buttonArea = Scale(76, dpi_);
    const int cardsTop = headerHeight + Scale(16, dpi_);
    const int cardsBottom = std::max(cardsTop + Scale(260, dpi_),
                                     static_cast<int>(client.bottom) - buttonArea);
    const int gap = Scale(16, dpi_);
    const int available = std::max(0, static_cast<int>(client.right) - margin * 2 - gap);
    const int leftWidth = available * 47 / 100;
    RECT left{ margin, cardsTop, margin + leftWidth, cardsBottom };
    RECT right{ left.right + gap, cardsTop, client.right - margin, cardsBottom };
    FillRound(dc, left, Scale(14, dpi_), kCard, kCardBorder);
    FillRound(dc, right, Scale(14, dpi_), kCard, kCardBorder);

    RECT leftTitle{ left.left + Scale(18, dpi_), left.top + Scale(12, dpi_),
                    left.right - Scale(18, dpi_), left.top + Scale(44, dpi_) };
    DrawTextLine(dc, L"连接状态", leftTitle, sectionFont_, kText);
    RECT rightTitle{ right.left + Scale(18, dpi_), right.top + Scale(12, dpi_),
                     right.right - Scale(18, dpi_), right.top + Scale(44, dpi_) };
    DrawTextLine(dc, L"局域网服务", rightTitle, sectionFont_, kText);

    int rowHeight = std::max(Scale(28, dpi_),
                             static_cast<int>(left.bottom - left.top - Scale(55, dpi_)) / 6);
    int y = left.top + Scale(48, dpi_);
    int labelWidth = static_cast<int>(left.left) +
                     std::min(Scale(124, dpi_), static_cast<int>(left.right - left.left) / 2);
    DrawStatusRow(dc, left, y, rowHeight, labelWidth, L"MSFS",
                  snapshot_.simConnected ? L"● 已连接" : L"● 等待模拟器",
                  bodyFont_, valueFont_, snapshot_.simConnected ? kGood : kWarn); y += rowHeight;
    DrawStatusRow(dc, left, y, rowHeight, labelWidth, L"iPhone",
                  snapshot_.iphoneConnected ? L"● 已连接" : L"● 等待连接",
                  bodyFont_, valueFont_, snapshot_.iphoneConnected ? kGood : kMuted); y += rowHeight;
    DrawStatusRow(dc, left, y, rowHeight, labelWidth, L"飞机",
                  Utf8ToWide(snapshot_.aircraft), bodyFont_, valueFont_); y += rowHeight;
    DrawStatusRow(dc, left, y, rowHeight, labelWidth, L"飞行计划",
                  Utf8ToWide(snapshot_.flightPlan), bodyFont_, valueFont_); y += rowHeight;
    std::wstring age;
    if (snapshot_.lastControlAgeMs < 0) age = L"--";
    else if (snapshot_.watchdogFired) age = L"无实时输入";
    else age = std::to_wstring(snapshot_.lastControlAgeMs) + L" ms";
    DrawStatusRow(dc, left, y, rowHeight, labelWidth, L"实时控制包", age,
                  bodyFont_, valueFont_,
                  snapshot_.lastControlAgeMs >= 0 && snapshot_.watchdogFired ? kWarn : kText); y += rowHeight;
    std::wstring statusText = snapshot_.simConnected
        ? Utf8ToWide(snapshot_.status.empty() ? "Ready" : snapshot_.status)
        : L"等待 MSFS 启动…";
    DrawStatusRow(dc, left, y, rowHeight, labelWidth, L"安全状态", statusText,
                  bodyFont_, valueFont_);

    rowHeight = std::max(Scale(26, dpi_),
                         static_cast<int>(right.bottom - right.top - Scale(83, dpi_)) / 6);
    y = right.top + Scale(48, dpi_);
    labelWidth = static_cast<int>(right.left) +
                 std::min(Scale(118, dpi_), static_cast<int>(right.right - right.left) / 2);
    DrawStatusRow(dc, right, y, rowHeight, labelWidth, L"推荐 IP", recommendedIp_,
                  bodyFont_, valueFont_, kAccent); y += rowHeight;
    DrawStatusRow(dc, right, y, rowHeight, labelWidth, L"TCP 可靠通道",
                  std::to_wstring(proto::kDefaultTcpPort), bodyFont_, valueFont_); y += rowHeight;
    DrawStatusRow(dc, right, y, rowHeight, labelWidth, L"UDP 实时控制",
                  std::to_wstring(proto::kDefaultUdpPort), bodyFont_, valueFont_); y += rowHeight;
    DrawStatusRow(dc, right, y, rowHeight, labelWidth, L"UDP 自动发现",
                  std::to_wstring(proto::kDiscoveryPort), bodyFont_, valueFont_); y += rowHeight;
    DrawStatusRow(dc, right, y, rowHeight, labelWidth, L"控制频率",
                  std::to_wstring(snapshot_.controlRate) + L" Hz", bodyFont_, valueFont_); y += rowHeight;

    COLORREF networkColor = snapshot_.network.rfind("ERROR:", 0) == 0 ? kBad : kGood;
    DrawStatusRow(dc, right, y, rowHeight, labelWidth, L"监听状态",
                  Utf8ToWide(snapshot_.network.empty() ? "Starting..." : snapshot_.network),
                  bodyFont_, valueFont_, networkColor);

    // 全部地址放在卡片底部提示条；可用宽度变化时自动省略，不覆盖相邻控件。
    RECT ipsRect{ right.left + Scale(18, dpi_), right.bottom - Scale(28, dpi_),
                  right.right - Scale(18, dpi_), right.bottom - Scale(7, dpi_) };
    DrawTextLine(dc, L"所有地址：" + allIps_, ipsRect, subtitleFont_, kMuted);
}

void AppWindow::DrawOwnerButton(const DRAWITEMSTRUCT& item) {
    RECT rect = item.rcItem;
    InflateRect(&rect, -1, -1);
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const COLORREF fill = disabled ? kHeader : (pressed ? RGB(31, 75, 119) : kCard);
    const COLORREF border = pressed ? kAccent : kCardBorder;
    FillRound(item.hDC, rect, Scale(9, dpi_), fill, border);

    wchar_t label[128]{};
    GetWindowTextW(item.hwndItem, label, static_cast<int>(std::size(label)));
    DrawTextLine(item.hDC, label, rect, bodyFont_, disabled ? kMuted : kText,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if ((item.itemState & ODS_FOCUS) != 0) {
        RECT focus = rect;
        InflateRect(&focus, -Scale(4, dpi_), -Scale(4, dpi_));
        DrawFocusRect(item.hDC, &focus);
    }
}

void AppWindow::DrawStartupToggle(const DRAWITEMSTRUCT& item) {
    RECT rect = item.rcItem;
    HBRUSH background = CreateSolidBrush(kBackground);
    FillRect(item.hDC, &rect, background);
    DeleteObject(background);

    const bool checked = SendMessageW(item.hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const int trackWidth = Scale(38, dpi_);
    const int trackHeight = Scale(20, dpi_);
    const int trackTop = rect.top + (rect.bottom - rect.top - trackHeight) / 2;
    RECT track{ rect.left + Scale(2, dpi_), trackTop,
                rect.left + Scale(2, dpi_) + trackWidth, trackTop + trackHeight };
    FillRound(item.hDC, track, trackHeight, checked ? kAccent : kCard,
              checked ? kAccent : kCardBorder);

    const int knobSize = Scale(14, dpi_);
    const int knobTop = track.top + (trackHeight - knobSize) / 2;
    const int knobLeft = checked
        ? track.right - knobSize - Scale(3, dpi_)
        : track.left + Scale(3, dpi_);
    HBRUSH knobBrush = CreateSolidBrush(pressed ? kMuted : kText);
    HGDIOBJ oldBrush = SelectObject(item.hDC, knobBrush);
    HPEN knobPen = CreatePen(PS_NULL, 0, kText);
    HGDIOBJ oldPen = SelectObject(item.hDC, knobPen);
    Ellipse(item.hDC, knobLeft, knobTop, knobLeft + knobSize, knobTop + knobSize);
    SelectObject(item.hDC, oldPen);
    SelectObject(item.hDC, oldBrush);
    DeleteObject(knobPen);
    DeleteObject(knobBrush);

    wchar_t label[128]{};
    GetWindowTextW(item.hwndItem, label, static_cast<int>(std::size(label)));
    RECT textRect{ track.right + Scale(10, dpi_), rect.top, rect.right, rect.bottom };
    DrawTextLine(item.hDC, label, textRect, bodyFont_, kText);

    if ((item.itemState & ODS_FOCUS) != 0) {
        RECT focus = textRect;
        InflateRect(&focus, -Scale(2, dpi_), -Scale(5, dpi_));
        DrawFocusRect(item.hDC, &focus);
    }
}

void AppWindow::RecreateFonts(UINT dpi) {
    ReleaseGraphics();
    dpi_ = dpi ? dpi : 96;
    auto font = [&](int points, int weight) {
        return CreateFontW(-MulDiv(points, static_cast<int>(dpi_), 72), 0, 0, 0, weight,
                           FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
    };
    titleFont_ = font(20, FW_SEMIBOLD);
    subtitleFont_ = font(9, FW_NORMAL);
    sectionFont_ = font(13, FW_SEMIBOLD);
    bodyFont_ = font(10, FW_NORMAL);
    valueFont_ = font(10, FW_SEMIBOLD);
    backgroundBrush_ = CreateSolidBrush(kBackground);
    controlBrush_ = CreateSolidBrush(kBackground);

    for (HWND control : { startupCheck_, copyButton_, firewallButton_ }) {
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont_), TRUE);
    }
}

void AppWindow::ReleaseGraphics() {
    for (HFONT* font : { &titleFont_, &subtitleFont_, &sectionFont_, &bodyFont_, &valueFont_ }) {
        if (*font) { DeleteObject(*font); *font = nullptr; }
    }
    if (backgroundBrush_) { DeleteObject(backgroundBrush_); backgroundBrush_ = nullptr; }
    if (controlBrush_) { DeleteObject(controlBrush_); controlBrush_ = nullptr; }
}

bool AppWindow::CopyRecommendedIp() {
    if (recommendedIp_.empty() || !OpenClipboard(hwnd_)) return false;
    EmptyClipboard();
    const SIZE_T bytes = (recommendedIp_.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) { CloseClipboard(); return false; }
    void* target = GlobalLock(memory);
    memcpy(target, recommendedIp_.c_str(), bytes);
    GlobalUnlock(memory);
    if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

bool AppWindow::StartWithWindows(bool enabled) {
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    bool ok = false;
    if (enabled) {
        const std::wstring command = CurrentRunCommand();
        ok = !command.empty() &&
             RegSetValueExW(key, L"MSFSiPhoneController", 0, REG_SZ,
                reinterpret_cast<const BYTE*>(command.c_str()),
                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        LSTATUS result = RegDeleteValueW(key, L"MSFSiPhoneController");
        ok = result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(key);
    return ok;
}

bool AppWindow::IsStartWithWindows() {
    const std::wstring expected = CurrentRunCommand();
    if (expected.empty()) return false;
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0;
    DWORD bytes = 0;
    LSTATUS result = RegQueryValueExW(key, L"MSFSiPhoneController", nullptr,
                                     &type, nullptr, &bytes);
    bool matches = false;
    if (result == ERROR_SUCCESS && type == REG_SZ && bytes >= sizeof(wchar_t)) {
        std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 1, L'\0');
        if (RegQueryValueExW(key, L"MSFSiPhoneController", nullptr, &type,
                reinterpret_cast<BYTE*>(value.data()), &bytes) == ERROR_SUCCESS) {
            matches = CompareStringOrdinal(value.data(), -1, expected.c_str(), -1, TRUE)
                == CSTR_EQUAL;
        }
    }
    RegCloseKey(key);
    return matches;
}

bool AppWindow::RepairFirewall(HWND owner) {
    wchar_t executable[32768]{};
    DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
    if (!length || length >= 32768) {
        MessageBoxW(owner, L"无法读取当前程序路径。", L"防火墙修复失败", MB_OK | MB_ICONERROR);
        return false;
    }

    // 仅允许本地子网访问当前程序，但同时覆盖 Public/Private 配置文件。
    // 家用 Wi-Fi 经常被 Windows 错分为“公用网络”，原先只放行 private 会导致
    // 手机与电脑明明在同一 LAN 却始终无法连接。
    std::wstring parameters =
        L"advfirewall firewall add rule name=\"MSFS iPhone Controller (Local LAN)\" "
        L"dir=in action=allow enable=yes profile=any remoteip=LocalSubnet program=\"";
    parameters += executable;
    parameters += L"\"";

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.hwnd = owner;
    info.lpVerb = L"runas";
    info.lpFile = L"netsh.exe";
    info.lpParameters = parameters.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) {
        if (GetLastError() != ERROR_CANCELLED)
            MessageBoxW(owner, L"无法启动 Windows 防火墙配置工具。", L"防火墙修复失败",
                        MB_OK | MB_ICONERROR);
        return false;
    }

    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(info.hProcess, &exitCode);
    CloseHandle(info.hProcess);
    if (exitCode != 0) {
        MessageBoxW(owner, L"Windows 未能添加防火墙规则。", L"防火墙修复失败",
                    MB_OK | MB_ICONERROR);
        return false;
    }
    MessageBoxW(owner,
        L"已允许同一本地子网访问本程序。请回到 iPhone 点击“重新连接”。",
        L"局域网已放行", MB_OK | MB_ICONINFORMATION);
    return true;
}

LRESULT CALLBACK AppWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppWindow* self = self_;
    switch (msg) {
    case WM_CREATE: {
        if (!self) return -1;
        self->hwnd_ = hwnd;
        self->dpi_ = GetDpiForWindow(hwnd);
        self->RecreateFonts(self->dpi_);
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
        HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
        self->CreateControls(instance);
        self->LayoutControls();
        self->RefreshUi();
        SetTimer(hwnd, 1, 250, nullptr);

        NOTIFYICONDATAW tray{};
        tray.cbSize = sizeof(tray);
        tray.hWnd = hwnd;
        tray.uID = 1;
        tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        tray.uCallbackMessage = WM_TRAYICON;
        tray.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_ICON1));
        wcscpy_s(tray.szTip, L"MSFS iPhone Controller");
        self->trayAdded_ = Shell_NotifyIconW(NIM_ADD, &tray) == TRUE;
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        if (self) self->Paint(dc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        if (self && wParam == 1) self->RefreshUi();
        if (self && wParam == 2) {
            SetWindowTextW(self->copyButton_, L"复制推荐 IP");
            KillTimer(hwnd, 2);
        }
        return 0;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED && self) {
            self->minimizedToTray_ = true;
            ShowWindow(hwnd, SW_HIDE);
        } else if (self) {
            self->LayoutControls();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_DPICHANGED:
        if (self) {
            self->RecreateFonts(HIWORD(wParam));
            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            self->LayoutControls();
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        UINT dpi = self ? self->dpi_ : 96;
        limits->ptMinTrackSize.x = Scale(660, dpi);
        limits->ptMinTrackSize.y = Scale(500, dpi);
        return 0;
    }
    case WM_CTLCOLORBTN:
        if (self) {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, kText);
            SetBkColor(dc, kBackground);
            return reinterpret_cast<LRESULT>(self->controlBrush_);
        }
        break;
    case WM_DRAWITEM:
        if (self && wParam == IDC_STARTUP) {
            self->DrawStartupToggle(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        }
        if (self && (wParam == IDC_COPY_IP || wParam == IDC_FIREWALL)) {
            self->DrawOwnerButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if (!self || HIWORD(wParam) != BN_CLICKED) break;
        if (LOWORD(wParam) == IDC_STARTUP) {
            const bool enabled = SendMessageW(self->startupCheck_, BM_GETCHECK, 0, 0)
                != BST_CHECKED;
            if (self->StartWithWindows(enabled)) {
                SendMessageW(self->startupCheck_, BM_SETCHECK,
                             enabled ? BST_CHECKED : BST_UNCHECKED, 0);
                InvalidateRect(self->startupCheck_, nullptr, TRUE);
            } else {
                MessageBoxW(hwnd, L"无法更新开机启动设置。", L"设置失败", MB_OK | MB_ICONERROR);
            }
        } else if (LOWORD(wParam) == IDC_COPY_IP) {
            if (self->CopyRecommendedIp()) {
                SetWindowTextW(self->copyButton_, L"已复制");
                SetTimer(hwnd, 2, 1400, nullptr);
            }
        } else if (LOWORD(wParam) == IDC_FIREWALL) {
            if (self->RepairFirewall(hwnd) && self->status_)
                self->status_->SetStatus("Firewall ready; reconnect the iPhone");
        }
        return 0;
    case WM_TRAYICON:
        if (!self) break;
        if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            self->minimizedToTray_ = false;
        } else if (LOWORD(lParam) == WM_RBUTTONUP) {
            POINT point{};
            GetCursorPos(&point);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"打开");
            AppendMenuW(menu, MF_STRING, 2, L"退出");
            SetForegroundWindow(hwnd);
            int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                         point.x, point.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
            if (command == 1) {
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
                self->minimizedToTray_ = false;
            } else if (command == 2) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        if (self) {
            if (self->trayAdded_) {
                NOTIFYICONDATAW tray{};
                tray.cbSize = sizeof(tray);
                tray.hWnd = hwnd;
                tray.uID = 1;
                Shell_NotifyIconW(NIM_DELETE, &tray);
                self->trayAdded_ = false;
            }
            self->ReleaseGraphics();
            self->hwnd_ = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
