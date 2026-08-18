# MSFS iPhone Controller — Windows 端

MSFS 2020 的局域网伴侣程序：通过 SimConnect 与 MSFS 通信，向 iPhone 提供
UDP 实时控制通道（36666）与 TCP 状态/命令通道（36667）。

## 环境要求

- Windows 10 / 11（x64）
- Visual Studio 2022（含 C++ 桌面开发工作负载）
- [Microsoft Flight Simulator SDK](https://flightsimulator.zendesk.com/hc/en-us/articles/4404162967316-SDK-Documentation)（含 SimConnect SDK）

## 构建准备

仓库已内置 SimConnect SDK 头文件与导入库（`Windows/ThirdParty/SimConnect`），
**没有官方 SDK 也能直接构建**（CI 即用此方式）。若你安装了官方 SDK 则优先使用它：

1. 安装 MSFS SDK，记下 **SimConnect SDK** 目录，它应包含：

   ```
   ...\SimConnect SDK\
       ├── include\SimConnect.h
       └── lib\SimConnect.lib
   ```

2. 设置环境变量 `SIMCONNECT_SDK_PATH` 指向该目录，例如：

   ```
   SIMCONNECT_SDK_PATH = D:\MSFS SDK\SimConnect SDK
   ```

   > 注意：环境变量设置后需要重新打开 VS / 终端才生效。
   > 不设置时自动回退到内置的 `ThirdParty\SimConnect`。

## 方式 A：Visual Studio 工程

1. 双击打开 `MSFSiPhoneController.sln`。
2. 默认使用内置 SimConnect SDK；如需官方 SDK，设置
   `SIMCONNECT_SDK_PATH` 环境变量后再打开工程。
3. 选择 **x64 + Release**，生成。

## 方式 B：CMake

```bat
:: 使用官方 SDK（可选）
cmake -B build -DSIMCONNECT_SDK_PATH="D:\MSFS SDK\SimConnect SDK"
cmake --build build --config Release

:: 或直接使用内置 SDK
cmake -B build -A x64
cmake --build build --config Release
```

## 运行

1. 启动 MSFS 2020 并进入任意航班。
2. 运行 `MSFSiPhoneController.exe`。窗口显示 `MSFS: Connected`。
3. 记下窗口中的 `Local IP`，在 iPhone App 设置页填入。
4. 最小化到托盘即可，也可勾选 `Start with Windows` 开机自启。

## 多网卡说明

UDP(36666) 与 TCP(36667) 服务器默认绑定 `INADDR_ANY`，即**同时监听所有网卡**，
任意一张网卡上到达的连接都能接收。

窗口中的 `Local IP` 为自动推荐：优先「非虚拟适配器 + 有网关」的真实网卡
（自动排除 VirtualBox / VMware / WSL / Tailscale 等虚拟与隧道网卡）；
`All IPs` 列出全部 IPv4 地址（含适配器名），便于你为手机所在的子网挑选正确 IP。

**自动探测**：程序额外监听 UDP `36668`，响应 iPhone 的 `MSFS_DISCOVER` 广播
并返回本机全部 IP。iPhone 设置页点「自动探测」即可发现主机，无需手输 IP。

> 若手机连不上，除确认使用正确 IP 外，还需在 Windows 防火墙中允许本程序
> 「专用网络」入站（首次运行时弹出的防火墙提示请勾选专用网络）。

## 模块说明

| 文件 | 职责 |
| ---- | ---- |
| `Protocol.h` | 两端共享协议（UDP 二进制包 / TCP JSON） |
| `Json.h` | 极简 JSON 解析器（TCP 文本协议用） |
| `SimConnectManager` | SimConnect 连接/自动重连、SIM_FRAME 遥测、事件发送 |
| `FlightController` | 最新控制状态共享区（UDP→SimConnect 线程） |
| `SafetyWatchdog` | 250ms 控制包超时 → 回中 Aileron/Elevator/Rudder |
| `TelemetryManager` | 10 Hz 遥测打包推送 |
| `FlightPlanManager` | .PLN 飞行计划解析 |
| `UDPServer` | UDP 36666 实时控制 |
| `TCPServer` | TCP 36667 会话/命令/状态 |
| `AppWindow` | 状态面板 + 托盘 + 开机自启 |

## 说明

- 首版不依赖 ViGEm / 虚拟 Xbox 手柄，直接通过标准 SimConnect 事件
  （`AXIS_AILERONS_SET` 等）控制飞控。
- 控制输出与状态回读均以 MSFS 为准；iPhone 只负责输入与显示。
