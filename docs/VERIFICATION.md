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
   确认 PC/MSFS 双状态和 RTT 每秒更新。Windows 的“实时控制包”在有姿态或油门
   输入时应显示很小的本机收包年龄；输入停止并触发看门狗后应显示“无实时输入”，
   不得把 iPhone 与 Windows 的单调时钟直接相减或无限显示巨大的毫秒数。
3. ARM 后保持手机不动，副翼和升降舵应为中立；向左/右倾斜只控制副翼，
   前/后倾斜只控制升降舵。分别在 Landscape Left/Right 下验证方向一致。
4. RECENTER 后当前握持姿势应立即成为中立；Deadzone、Expo、Sensitivity、
   Smoothing 与 Invert 设置应即时生效。
5. 拖动 Rudder 后松手必须回中。点击 Throttle − / + 时，每次应先断开 A/THR，
   再将全部发动机油门减小/增大 10%；按住约 0.4 秒后应连续变化，松手、断线或切页
   必须立即停止。中间百分比必须始终显示 `SIM` 实际回读。
   停止点击后 iPhone 不得继续发送油门轴或保存目标，驾驶舱、实体油门和 A/THR 的
   后续变化应正常回显。关闭 GYRO 后按钮仍必须有效。Trim、Flaps、Gear、Parking
   Brake、Autopilot 状态必须以 MSFS 回读为准。Flaps± 应切换机型合法档位。
6. 按住 Brake 后松手必须释放，按钮显示的左右轮实际制动应回到 5% 以下。按住期间
   切页、锁屏、断开 TCP 或关闭手机 Wi-Fi，刹车必须释放，Aileron/Elevator/Rudder
   必须在 250 ms 左右回中。
7. GYRO ARM 后切换到 Map/Settings 必须保持 ARM 和姿态控制；Rudder/Brake 等
   按住型控件离开页面时必须释放。开启 AP 后姿态轴暂停输出，关闭 AP 后自动恢复，
   不要求重新 ARM。
8. MSFS 退出或重启时，iPhone 控件应禁用并 DISARM；重连后不得补执行离线期间
   点击的命令，必须重新 ARM 才能恢复姿态控制。
9. AUTOPILOT 页应依次显示“飞机状态、起飞阶段、飞行阶段、降落阶段”。设置目标航向并进入 HDG 后，飞机航向游标、HDG 指示和手机回读应一致；
   分别选择 GPS/NAV1 并核对机内 CDI 来源，切到 NAV 后 HDG 应关闭、NAV 应激活，
   切到 OFF 只关闭横向模式。AP Master 状态必须由模拟器回读确认。至少用
   老款 Asobo/Legacy A320neo 验证完整流程；再用 Cessna 172 验证标准事件降级流程。
10. 分别执行“保持当前高度”“按升降率前往目标”“按速度前往目标”，核对目标高度、
    VS/FLC、目标速度及 ALT ARM/ACTIVE 的模拟器回读。目标在当前高度以下时，快捷 VS
    必须自动使用负值；FLC 飞机没有自动油门时，App 必须提示手动调油门。
11. APP 按下后先显示预位；只有 `AUTOPILOT APPROACH ACTIVE` 和
    `AUTOPILOT GLIDESLOPE ACTIVE` 回读后，才能分别显示 LOC/GS 已截获。

## 真机地图与航路验收

1. 地图飞机位置、经纬度、高度和 Heading 应与 MSFS 一致，机头图标随 Heading
   旋转；拖动地图后不应被下一条遥测强制拉回，点击“跟随”才恢复自动跟随。
2. 实际航迹约每秒或移动 50 m 采样；跨机场瞬移、新航路、飞机变化或新航班
   起飞时不应画出跨场直线，手动“清除航迹”应立即生效。
3. 在 MSFS 激活一份包含机场、导航台与 User Waypoint 的 `.PLN`；计划航线应在
   iPhone 出现。逐个点击航点，核对名称、经纬度和计划高度。
4. Windows 或 iPhone 重连后，当前已缓存航路应再次发送；激活另一份 `.PLN`
   后应自动替换旧航路。
5. AUTOPILOT 页应显示与 MAP 相同的标准 `.PLN` 起点、终点和航点；不得把该列表
   标成 A320 MCDU 内部航路，也不应声称可以通用编辑第三方客机的专有 FMC。
6. 老款 Asobo/Legacy A320neo：在世界地图激活包含 SID/STAR/进近的 `.PLN`，App
   应识别为“老款 A320neo · 标准航路适配”。点击“同步并跟随 GPS 航路”后人工核对
   机载 F-PLN 航点，并等待 NAV 实际回读；由于 `SimConnect_FlightPlanLoad` 没有完成
   回执，不得仅凭点击显示同步成功。A320neo V2/A32NX/Fenix 应显示专有 FMS 提示，
   不得误称已把标准 `.PLN` 写入其 MCDU。

## 长时间与异常验收

- 连续运行至少 60 分钟，观察 TCP/UDP 状态、内存、地图航迹和操纵延迟。
- 分别测试 Wi-Fi 丢包、路由器漫游、PC 网络切换、PC 程序重启、MSFS 重启、
  iPhone 锁屏和 App 前后台切换。
- 主要验收机型使用 MSFS 2020 老款 Asobo/Legacy A320neo；另用 Cessna 172 验证
  标准事件降级路径。
  复杂第三方飞机若覆盖标准 SimConnect 事件，需要单独的 Aircraft Profile。
