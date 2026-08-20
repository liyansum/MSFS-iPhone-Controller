# V1 功能验收清单

本清单以 `repo.txt` 的 V1 定义为基线。自动化检查用于阻止协议、解析、构建和
范围回归；标记为“真机”的项目必须在同一局域网内使用 Windows + MSFS 2020 +
iPhone 完成，不能用静态检查替代。

## 自动化检查

Windows x64：

```bat
cmake -S Windows -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

核心测试覆盖：JSON 握手解析、UDP 新会话序号重置、重复/乱序/回绕保护、
SimConnect 轴范围钳制、PLN 航点块隔离、自定义航点名、DMS/十进制坐标和高度。

iOS 无签名构建：

```bash
xcodebuild -project iOS/MSFSController.xcodeproj \
  -scheme MSFSController -configuration Release \
  -destination 'generic/platform=iOS' \
  CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO build
```

## 真机连接与安全验收

1. PC 与 iPhone 接入同一路由器，关闭 VPN；Windows 程序应显示 TCP 36667、
   UDP 36666/36668 均已监听。若 Windows 防火墙未放行，点击“一键放行局域网”。
2. iPhone 设置页自动探测应显示 Windows 主机；手动输入推荐 IPv4 也应能连接。
   确认 PC/MSFS 双状态和 RTT 每秒更新。
3. ARM 后保持手机不动，副翼和升降舵应为中立；向左/右倾斜只控制副翼，
   前/后倾斜只控制升降舵。分别在 Landscape Left/Right 下验证方向一致。
4. RECENTER 后当前握持姿势应立即成为中立；Deadzone、Expo、Sensitivity、
   Smoothing 与 Invert 设置应即时生效。
5. 拖动 Rudder 后松手必须回中；拖动 Throttle 时持续控制，松手后停止覆盖并
   跟随 MSFS 回读。Trim、Flaps、Gear、Parking Brake 状态必须以 MSFS 回读为准。
6. 按住 Brake 后松手必须释放。按住期间切页、锁屏、断开 TCP 或关闭手机 Wi-Fi，
   刹车必须释放，Aileron/Elevator/Rudder 必须在 250 ms 左右回中，Throttle 保持。
7. 在 Gyro、Rudder 或 Throttle 活动时切换到 Map/Settings，再回到 Control；
   App 必须保持 DISARMED，且不能恢复旧拖动值。
8. MSFS 退出或重启时，iPhone 控件应禁用并 DISARM；重连后不得补执行离线期间
   点击的命令，必须重新 ARM 才能恢复姿态控制。

## 真机地图与航路验收

1. 地图飞机位置、经纬度、高度和 Heading 应与 MSFS 一致，机头图标随 Heading
   旋转；拖动地图后不应被下一条遥测强制拉回，点击“跟随”才恢复自动跟随。
2. 实际航迹约每秒或移动 50 m 采样；跨机场瞬移、新航路、飞机变化或新航班
   起飞时不应画出跨场直线，手动“清除航迹”应立即生效。
3. 在 MSFS 激活一份包含机场、导航台与 User Waypoint 的 `.PLN`；计划航线应在
   iPhone 出现。逐个点击航点，核对名称、经纬度和计划高度。
4. Windows 或 iPhone 重连后，当前已缓存航路应再次发送；激活另一份 `.PLN`
   后应自动替换旧航路。

## 长时间与异常验收

- 连续运行至少 60 分钟，观察 TCP/UDP 状态、内存、地图航迹和操纵延迟。
- 分别测试 Wi-Fi 丢包、路由器漫游、PC 网络切换、PC 程序重启、MSFS 重启、
  iPhone 锁屏和 App 前后台切换。
- 标准验收机型使用 Cessna 172。复杂第三方飞机若覆盖标准 SimConnect 事件，
  需要单独的 Aircraft Profile，不属于 V1 标准事件兼容范围。
