# MSFS iPhone Controller

[![build](https://github.com/liyansum/MSFS-iPhone-Controller/actions/workflows/build.yml/badge.svg)](https://github.com/liyansum/MSFS-iPhone-Controller/actions/workflows/build.yml)

面向 **Microsoft Flight Simulator 2020** 的无线 iPhone 飞行控制器。

- **iPhone App**（SwiftUI / CoreMotion / Network.framework / MapKit）：
  陀螺仪姿态控制、油门、方向舵、配平、襟翼、刹车、起落架、导航地图。
- **Windows Companion**（C++ / Win32 / Winsock / SimConnect）：
  局域网通信 + SimConnect + 状态同步 + 飞行计划解析 + 安全保护。

架构（详见 `docs/PROTOCOL.md`）：

```text
iPhone  ── UDP 36666 实时控制 ──▶ Windows  ── SimConnect ──▶ MSFS 2020
    ▲                              │  ▲
    └────────── TCP 36667 状态/命令/遥测/航线 ◀──┘
```

Windows 是权威状态端；所有控件显示以 MSFS 回读为准，iPhone 只负责输入与显示。

## 构建

GitHub Actions（`.github/workflows/build.yml`）会在每次 push 时自动构建：

- **Windows**：windows-latest + CMake（x64 Release），产出 `MSFSiPhoneController.exe`
- **iOS**：macos-15 + xcodebuild（无签名），产出 `MSFSController.app`

Windows 构建使用仓库内置的 SimConnect SDK（`Windows/ThirdParty/SimConnect`），
无需额外下载；若你本地装了官方 SDK，可用 `SIMCONNECT_SDK_PATH` 覆盖。

## 目录

```text
Windows/    PC 端（CMake + VS2022 工程）
iOS/        iPhone 端（Xcode 工程）
docs/       协议文档
.github/    CI 工作流
```

## 快速开始

### Windows 端

1. 安装 MSFS SDK，设置环境变量 `SIMCONNECT_SDK_PATH` 指向 `MSFS SDK/SimConnect SDK`。
2. 打开 `Windows/MSFSiPhoneController.sln`（x64 + Release）或用 CMake 构建。
3. 启动 MSFS 进入航班，运行程序，记下窗口中的 `Local IP`。
   （详见 `Windows/README.md`）

### iPhone 端

1. 用 Xcode 打开 `iOS/MSFSController.xcodeproj`（iOS 17+，真机）。
2. 在 SETTINGS 填入 PC 的 IP，`Test Connection` → `保存并连接`。
3. CONTROL 页点击 `GYRO ARM` 开始飞行。
   （详见 `iOS/README.md`）

## V1 功能

- 连接：手动 IP、自动保存主机、PC/MSFS 双状态、RTT 显示
- 姿态控制：Roll / Pitch、RECENTER、ARM/DISARM、Deadzone/Expo/Smoothing/Invert
- 飞控：Rudder（自动回中）、Throttle（触摸期间控制）、Trim（增量）、Flaps±、Brake、Gear
- 网络：UDP 实时控制 + TCP 可靠命令、sessionId 鉴权、250ms 看门狗
- 地图：当前位置、航向、经纬度、实际航迹、计划航线、航点详情
- 安全：切页/后台自动 DISARM，断线回中，持久状态（Throttle/Trim/Flaps/Gear）保持

## 开发顺序参考

V0.1 PC 基础 → V0.2 通信 → V0.3 Gyro → V0.4 完整控制 → V0.5 双向状态 → V0.6 地图 → V0.7 航线 → V1.0 稳定测试。

## 参考项目

SimConnect 管理部分参考 [MSFS-SimGPstoAndroid](https://github.com/JinShichang/MSFS-SimGPstoAndroid)。
本项目的网络层简化为纯局域网模式，飞控直接使用标准 SimConnect 事件，不依赖 ViGEm/虚拟手柄。
