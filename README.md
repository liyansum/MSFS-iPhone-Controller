# MSFS iPhone Controller

[![Build](https://github.com/liyansum/MSFS-iPhone-Controller/actions/workflows/build.yml/badge.svg)](https://github.com/liyansum/MSFS-iPhone-Controller/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/liyansum/MSFS-iPhone-Controller)](https://github.com/liyansum/MSFS-iPhone-Controller/releases)

将 iPhone 变成 Microsoft Flight Simulator 2020 的无线飞行控制器。手机通过局域网连接 Windows 伴侣程序，使用陀螺仪控制副翼和升降舵，并提供油门、方向舵、襟翼、刹车、起落架、自动驾驶开关、地图和飞行计划显示。

## 功能

### iPhone 控制端

- CoreMotion 姿态控制：左侧低向左滚转、右侧低向右滚转，手机倾角增大拉起、减小下压。
- `GYRO ARM`、一键回中、死区、灵敏度、Expo、平滑及单轴反转设置。
- 持续占用目标值的油门滑杆、自动回中的方向舵、升降舵配平和机型原生襟翼档位。
- 按住式左右轮刹车、驻车制动、起落架及自动驾驶总开关。
- 自动驾驶开启时暂停手机姿态轴，关闭后恢复原有 GYRO ARM 状态。
- MapKit 飞行地图、飞机位置与航向、实际航迹、MSFS 当前飞行计划和航点信息。
- 前台飞行控制期间保持屏幕常亮，避免自动锁屏造成断连。

### Windows 伴侣程序

- 通过 SimConnect 连接 MSFS 2020，转发控制并以 10 Hz 回传飞机遥测。
- 自动发现局域网地址，同时显示推荐 IP、全部 IPv4 地址和三个服务端口状态。
- 一键添加仅限本地子网的 Windows 防火墙规则。
- 自适应深色状态界面、系统托盘、开机启动和连接诊断。
- 250 ms 实时控制看门狗；断线、App 后台或会话切换时自动回中并释放刹车。

## 系统要求

- Windows 10/11 x64。
- Microsoft Flight Simulator 2020。
- iPhone：iOS 17 或更高版本，并允许“本地网络”权限。
- Windows 与 iPhone 位于同一局域网；建议关闭两端 VPN、代理和访客 Wi-Fi 隔离。
- 从源码构建 iOS App 需要 macOS、Xcode 16+ 和可用于真机签名的 Apple 开发者身份。

## 安装

### Windows

1. 从 [Releases](https://github.com/liyansum/MSFS-iPhone-Controller/releases) 下载 Windows 压缩包并解压。
2. 保持 `MSFSiPhoneController.exe` 和 `SimConnect.dll` 位于同一目录。
3. 启动 MSFS 并进入航班，然后运行 `MSFSiPhoneController.exe`。
4. 首次运行点击“一键放行局域网”，在 UAC 窗口中确认。

### iPhone

Release 中的 `MSFSController.ipa` 是未签名安装包，需要使用自己的 Apple ID 重新签名，可选择：

- Xcode 真机运行或重新签名安装；
- Sideloadly；
- AltStore / AltServer。

iOS 首次启动会请求本地网络和运动传感器权限，必须允许，否则无法发现 Windows 或使用姿态控制。

## 使用方法

1. 将 Windows PC 与 iPhone 连接到同一个路由器。
2. 启动 MSFS 并进入航班，再启动 Windows 伴侣程序，确认窗口中 `MSFS` 显示“已连接”。
3. 在 iPhone 的 SETTINGS 页点击自动探测；也可填写 Windows 窗口显示的“推荐 IP”。默认端口无需修改。
4. 点击 `Test Connection`，成功后选择“保存并连接”。CONTROL 页应同时显示 PC 和 MSFS 已连接。
5. 将手机保持在舒适的横屏姿势，点击 `GYRO ARM`。当前姿势会自动成为中立位置；需要重新校准时点击 `RECENTER`。
6. 切换到 MAP 或 SETTINGS 不会解除 GYRO ARM。App 进入后台、手动锁屏、网络断开或 MSFS 离线时会安全解除并回中。

### 控件说明

| 控件 | 操作 |
| --- | --- |
| GYRO ARM | 开启或手动解除手机姿态控制 |
| RECENTER | 将当前握持姿势设为新的中立点 |
| THROTTLE | 上下拖动设置 0–100% 油门；操作后持续保持该目标，直至后台或断线 |
| RUDDER | 左右拖动方向舵，松手自动回中 |
| TRIM | 增减升降舵配平 |
| FLAPS − / + | 按飞机自身设计切换到上一个或下一个合法襟翼档位 |
| BRAKE | 按住时持续施加左右轮刹车，松手重复发送释放并显示实际制动百分比 |
| PARKING / PARKED | 切换驻车制动，显示状态来自 MSFS 回读 |
| GEAR | 切换起落架 |
| AP ON / AP OFF | 开启或关闭自动驾驶总开关，状态来自 MSFS 回读 |

复杂第三方飞机可能使用自定义机模事件，因此个别襟翼、自动驾驶或系统开关可能不响应标准 SimConnect 事件。标准验收机型为 Cessna 172。

## 无法连接时

依次检查：

1. Windows 与 iPhone 是否位于同一 IPv4 子网，且没有启用 VPN、访客网络或 AP 隔离。
2. Windows 程序是否显示 TCP `36667`、UDP `36666` 和发现端口 `36668` 正常监听。
3. 是否已点击 Windows 程序中的“一键放行局域网”。
4. iOS“设置 → 隐私与安全性 → 本地网络”中是否允许本 App。
5. 自动探测失败时，手动输入 Windows 窗口中的推荐 IP；多网卡电脑可在“所有地址”中选择与手机同网段的地址。

## 网络与安全设计

```text
iPhone ── UDP 36666 实时姿态/油门/方向舵 ──▶ Windows ── SimConnect ──▶ MSFS
   ▲                                          │
   ├──── TCP 36667 命令、状态、遥测和航线 ────┘
   └──── UDP 36668 局域网主机自动发现
```

- UDP 控制包使用会话 ID 和递增序号，Windows 会拒绝旧会话、重复包和乱序包。
- TCP 承载需要可靠送达的命令、状态、遥测与飞行计划。
- Windows 是显示状态的权威来源，iPhone 不会仅凭按钮点击猜测飞机状态。
- 防火墙快捷操作仅允许 `LocalSubnet` 访问程序，不向公网开放端口。

协议字段与范围见 [docs/PROTOCOL.md](docs/PROTOCOL.md)，真机验收步骤见 [docs/VERIFICATION.md](docs/VERIFICATION.md)。

## 从源码构建

### Windows x64

仓库内置构建所需的 SimConnect 头文件、导入库和运行库：

```bat
cmake -S Windows -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

也可以打开 `Windows/MSFSiPhoneController.sln`，选择 `x64 / Release` 构建。若要使用本机 MSFS SDK，可通过 `SIMCONNECT_SDK_PATH` 指定包含 `include` 和 `lib` 的 SimConnect SDK 目录。

### iOS

```bash
open iOS/MSFSController.xcodeproj
```

在 Xcode 的 Signing & Capabilities 中选择自己的 Team，然后选择 iOS 17+ 真机运行。CoreMotion 和局域网连接不能仅靠模拟器完成验收。

## 项目结构

```text
Windows/    Windows 伴侣程序、SimConnect 接入与核心测试
iOS/        SwiftUI iPhone App 与 Xcode 工程
docs/       网络协议和发布前验收清单
.github/    Windows/iOS 自动构建工作流
```

当前发布版本：`1.0.1`。
