# MSFS iPhone Controller — iOS 端

MSFS 2020 的无线飞行控制器 App（SwiftUI）。横屏使用，仅限局域网。

## 环境要求

- Xcode 16+（macOS）
- iOS 17+ 的真机（CoreMotion / 局域网权限均需真机）

## 打开工程

### 方式 A：直接打开 Xcode 工程（推荐）

```bash
open iOS/MSFSController.xcodeproj
```

选择你的开发者 Team（Signing & Capabilities），然后运行到 iPhone。

### 方式 B：XcodeGen 重新生成

```bash
cd iOS
xcodegen generate
```

## 使用

1. 在 iPhone 首次打开 App，进入 SETTINGS 填入 Windows 端显示的主机 IP。
2. 点击 `Test Connection`；状态显示 `PC 已连接` / `MSFS 已连接` 后点 `保存并连接`。
3. 回到 CONTROL 页：点击 `GYRO ARM`（自动执行一次 RECENTER）开始姿态控制。
4. 地图页自动跟随飞机；切回控制页需要重新 ARM。

## CI 产物（无签名 .ipa）

GitHub Actions 产出的 `MSFSController.ipa` 为**未签名**包（`Payload/MSFSController.app`）。
用以下任一方式安装到 iPhone：

- **Sideloadly**（Windows/macOS）：拖入 .ipa 用你的 Apple ID 签名安装
- **AltStore / AltServer**：以个人 Apple ID 侧载
- **Xcode 自签**：Window > Devices 手动安装

> 需要 iOS 17+ 真机；CoreMotion、局域网权限均需真机。

## 模块说明

| 目录 | 内容 |
| ---- | ---- |
| `App/` | 入口、根视图、横屏、后台处理、触觉反馈 |
| `Motion/` | CoreMotion 采样、姿态零点、Deadzone+Expo 曲线 |
| `Network/` | UDP 控制、TCP 会话、控制引擎、协议编解码 |
| `Models/` | 控制状态 / 飞机状态 / 航线 / 连接状态 |
| `Views/` | 控制页 / 地图页 / 设置页 / 组件 |
| `Storage/` | 设置持久化 |

## 安全行为

- 切到地图/设置页自动 DISARM；App 进入后台立即尝试发送回中。
- 断线保护由 Windows 端 250ms 看门狗兜底。
- 再次进入 App 必须先 RECENTER + ARM 才能控制。
