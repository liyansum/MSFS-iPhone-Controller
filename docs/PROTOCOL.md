# 通信协议

MSFS iPhone Controller 使用两条通道：

| 通道 | 端口 | 用途 |
| ---- | ---- | ---- |
| UDP | 36666 | 实时控制（Aileron/Elevator/Rudder/Throttle）、Ping/Pong |
| TCP | 36667 | 会话、可靠命令、遥测、航线 |

## 自动探测（UDP 36668）

iPhone 不需要手动输入 IP 即可发现 Windows 主机：

```text
iPhone  ── UDP 广播 255.255.255.255:36668  "MSFS_DISCOVER\n"  (每 2s) ──▶ 局域网
Windows ── UDP 单播应答（回到来源地址）:
{"type":"msfs_host","name":"MSFS iPhone Controller",
 "ips":["192.168.1.100","192.168.56.1"],
 "udpPort":36666,"tcpPort":36667,"protocolVersion":1}
```

Windows 监听 `36668`，仅对 `MSFS_DISCOVER` 前缀的包应答；`ips` 列出全部 IPv4
地址，iOS 端选择任意一个即可连接。该端口不参与会话鉴权，仅用于发现。

## UDP 36666 — 固定二进制包

32 字节，小端序，1 字节对齐。`magic` 不匹配或 `version` 不匹配的包直接丢弃。

| 偏移 | 大小 | 字段 | 说明 |
| ---- | ---- | ---- | ---- |
| 0  | 4  | magic   | `0x4D534631`（"MSF1"） |
| 4  | 1  | type    | 1=Control, 2=Ping, 3=Pong |
| 5  | 1  | version | 1 |
| 6  | 4  | sequence | 发送方单调递增；PC 丢弃乱序旧包 |
| 10 | 8  | timestampMs | 发送方单调时钟（毫秒；仅用于 Ping/Pong 回显，不与 PC 时钟相减） |
| 18 | 4  | sessionId | WELCOME 分配的会话；不匹配的控制包被 PC 忽略 |
| 22 | 2  | axisMask | 有效轴位掩码（见下） |
| 24 | 2  | aileron | -16383 .. +16384 |
| 26 | 2  | elevator | -16383 .. +16384 |
| 28 | 2  | rudder | -16383 .. +16383 |
| 30 | 2  | throttle | 0 .. 16383 |

`axisMask` 位定义：

```text
bit0 AILERON    bit1 ELEVATOR    bit2 RUDDER    bit3 THROTTLE
```

- **Control**：只发送当前有效的轴。例如陀螺仪已 ARM、未触摸油门/方向舵时，只带 aileron+elevator，PC 不会覆盖 throttle/rudder。
- **Ping**：iPhone 周期发送（含 timestamp），PC 回显为 **Pong**，iPhone 据此计算 RTT。
- **安全**：PC 若 250ms 未收到新的 Control 包，Aileron/Elevator/Rudder 回中；Throttle/Trim/Flaps/Gear 保持。
- **GYRO DISARM**：只将 aileron+elevator 回中，不改变手机已设定的 throttle，也不取消正在操作的 rudder。
- **后台/断线**：iPhone 立即发送 aileron+elevator+rudder 全 0 的 Control 包并停止持续油门覆盖。

## TCP 36667 — 会话与命令

每行一个 JSON 对象，以 `\n` 结尾。

### iPhone → PC

```json
{"type":"hello","protocolVersion":1,"appVersion":"1.1.4","deviceName":"iPhone"}
```

```json
{"type":"cmd","name":"flaps_incr"}
{"type":"cmd","name":"flaps_decr"}
{"type":"cmd","name":"gear"}
{"type":"cmd","name":"trim_up"}
{"type":"cmd","name":"trim_dn"}
{"type":"cmd","name":"parking_brake"}
{"type":"cmd","name":"brake","value":true}
{"type":"cmd","name":"throttle_decr"}
{"type":"cmd","name":"throttle_incr"}
{"type":"cmd","name":"throttle_takeover"}
{"type":"cmd","name":"throttle_set","value":8192}
{"type":"cmd","name":"throttle_idle"}
{"type":"cmd","name":"autopilot","value":true}
{"type":"cmd","name":"autopilot_mode","value":"heading"}
{"type":"cmd","name":"autopilot_mode","value":"nav"}
{"type":"cmd","name":"autopilot_mode","value":"off"}
{"type":"cmd","name":"autopilot_heading","value":273}
{"type":"cmd","name":"navigation_source","value":"gps"}
{"type":"cmd","name":"autopilot_altitude","value":10000}
{"type":"cmd","name":"autopilot_vertical_mode","value":"vs"}
{"type":"cmd","name":"autopilot_vertical_speed","value":1000}
{"type":"cmd","name":"autopilot_speed","value":120}
{"type":"cmd","name":"autopilot_approach","value":true}
{"type":"cmd","name":"sync_flight_plan"}
```

`brake.value=true` 表示按住刹车，`false` 表示释放。Windows 按住期间持续刷新左右轮制动，松开后短时重复发送完全释放值。
`throttle_decr/throttle_incr` 是当前 iOS 界面使用的油门命令。每次点击先断开 A/THR，再通过 MSFS 原生事件减小/增大全部发动机 10%；没有手机目标值，也不会持续占用 UDP 油门轴。
`throttle_takeover/throttle_set/throttle_idle` 仅为兼容 1.1.2 客户端保留；新界面不再发送这些绝对油门命令。
`autopilot.value=true` 表示开启自动驾驶总开关，`false` 表示关闭；显示状态以遥测回读为准。
`autopilot_mode` 在标准 HDG/NAV 横向模式间明确切换，`off` 只关闭横向模式，不关闭 AP Master。
`autopilot_heading` 设置 0..359 度航向游标；iPhone 的“设定并进入 HDG”会先设置游标再进入 HDG。
`navigation_source` 在 `gps`（活动飞行计划）与 `nav1`（NAV1 电台）之间明确选择 NAV 的驱动源。
`autopilot_altitude` 设置 0..60000 ft 高度游标；`autopilot_vertical_mode` 可取 `hold/vs/flc/off`。
`autopilot_vertical_speed` 设置 -6000..6000 ft/min，`autopilot_speed` 设置 40..400 kt 的 FLC 速度游标。
`autopilot_approach` 明确预位或取消 APP。`sync_flight_plan` 用于老款 Asobo A320neo 和其他标准 GPS 机型：它将 Windows 当前缓存的活动 `.PLN` 重新交给 SimConnect 加载；SDK 对此调用不提供完成回执，最终必须以机载航路与 NAV 遥测为准。A320neo V2、A32NX、Fenix 等专有 FMGS/FMC 机型不走此导入路径。
`flaps_incr/flaps_decr` 使用机型原生的上一个/下一个襟翼档位，档位百分比由飞机定义。

### PC → iPhone

```json
{"type":"welcome","protocolVersion":1,"sessionId":1234,"serverVersion":"1.1.4","simConnected":true,"aircraftName":"Cessna 172"}
```

```json
{"type":"status","simConnected":true,"aircraftName":"Cessna 172"}
```

```json
{"type":"telemetry","lat":31.23,"lon":121.47,"alt":1524.0,"altAgl":500.0,"hdg":273.0,"magHdg":269.0,"pitch":1.2,"roll":-2.3,"gs":56.7,"ias":61.2,"vs":-3.1,"flaps":10.0,"trim":0.18,"throttle":0.63,"autothrottleActive":true,"autothrottleArmed":true,"gear":false,"parkingBrake":true,"onGround":false,"autopilot":true,"apHeadingLock":true,"apNavLock":false,"apHeading":269.0,"gpsDrivesNav1":true,"apAltitudeLock":false,"apAltitudeArm":true,"apAltitude":10000,"apVerticalHold":true,"apVerticalSpeed":1000,"apFlc":false,"apSpeed":120,"apApproachArm":false,"apApproachActive":false,"apGlideslopeArm":false,"apGlideslopeActive":false,"gpsWpIndex":3,"gpsWpDistance":12.4,"nav1Frequency":109.50,"nav1HasLocalizer":true,"nav1HasGlideslope":true,"brakeLeft":0.0,"brakeRight":0.0,"seq":42,"aircraft":"Cessna 172"}
```

遥测字段单位：`alt/altAgl/apAltitude` 英尺，`hdg/magHdg/pitch/roll/apHeading` 度，`gs/ias/vs` 米/秒，`apVerticalSpeed` ft/min，`apSpeed` kt，`gpsWpDistance` NM，`nav1Frequency` MHz，`flaps` 0..100，`trim` -1..1，`throttle/brakeLeft/brakeRight` 0..1。`autothrottleActive/autothrottleArmed` 分别表示 A/THR 实际接管和预位状态；`nav1HasLocalizer/nav1HasGlideslope` 表示当前 NAV1 频率是否具备航向道/下滑道。所有 AP/A/THR 模式字段均是模拟器实际回读，不是按钮的乐观状态。

```json
{"type":"route","departure":"ZBAA","destination":"ZSPD","departureRunway":"36L","departureProcedure":"RENOB1","arrivalProcedure":"SASAN2","approachType":"ILS","destinationRunway":"16R","cruisingAltitude":12000,"waypoints":[{"index":0,"ident":"ZBAA","lat":40.0801,"lon":116.5846,"alt":0},{"index":1,"ident":"RENOB","lat":39.823,"lon":116.312,"alt":7200}]}
```

```json
{"type":"error","code":1,"message":"invalid json"}
```

### 会话流程

```text
iPhone --TCP HELLO---------------------------> PC
PC     --TCP WELCOME { sessionId } ---------> iPhone
iPhone --UDP Control(带 sessionId) 60/100Hz --> PC
PC     --TCP telemetry 10Hz --------------> iPhone
```

PC 只接受携带当前 `sessionId` 的 UDP 控制包，避免同局域网其他设备误发指令。
