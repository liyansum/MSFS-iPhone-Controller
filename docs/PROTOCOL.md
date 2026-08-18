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
| 10 | 8  | timestampMs | 发送方单调时钟（毫秒） |
| 18 | 4  | sessionId | WELCOME 分配的会话；不匹配的控制包被 PC 忽略 |
| 22 | 2  | axisMask | 有效轴位掩码（见下） |
| 24 | 2  | aileron | -16383 .. +16384 |
| 26 | 2  | elevator | -16383 .. +16384 |
| 28 | 2  | rudder | -16383 .. +16384 |
| 30 | 2  | throttle | 0 .. 16383 |

`axisMask` 位定义：

```text
bit0 AILERON    bit1 ELEVATOR    bit2 RUDDER    bit3 THROTTLE
```

- **Control**：只发送当前有效的轴。例如陀螺仪已 ARM、未触摸油门/方向舵时，只带 aileron+elevator，PC 不会覆盖 throttle/rudder。
- **Ping**：iPhone 周期发送（含 timestamp），PC 回显为 **Pong**，iPhone 据此计算 RTT。
- **安全**：PC 若 250ms 未收到新的 Control 包，Aileron/Elevator/Rudder 回中；Throttle/Trim/Flaps/Gear 保持。
- **DISARM/后台**：iPhone 立即发送一个 aileron+elevator+rudder 全 0 的 Control 包让 PC 即时回中（不等 250ms）。

## TCP 36667 — 会话与命令

每行一个 JSON 对象，以 `\n` 结尾。

### iPhone → PC

```json
{"type":"hello","protocolVersion":1,"appVersion":"1.0.0","deviceName":"iPhone"}
```

```json
{"type":"cmd","name":"flaps_incr"}
{"type":"cmd","name":"flaps_decr"}
{"type":"cmd","name":"gear"}
{"type":"cmd","name":"trim_up"}
{"type":"cmd","name":"trim_dn"}
{"type":"cmd","name":"parking_brake"}
{"type":"cmd","name":"brake","value":true}
```

`brake.value=true` 表示按住刹车，`false` 表示释放。

### PC → iPhone

```json
{"type":"welcome","protocolVersion":1,"sessionId":1234,"serverVersion":"1.0.0","simConnected":true,"aircraftName":"Cessna 172"}
```

```json
{"type":"status","simConnected":true,"aircraftName":"Cessna 172"}
```

```json
{"type":"telemetry","lat":31.23,"lon":121.47,"alt":1524.0,"altAgl":500.0,"hdg":273.0,"pitch":1.2,"roll":-2.3,"gs":56.7,"ias":61.2,"vs":-3.1,"flaps":10.0,"trim":0.18,"throttle":0.63,"gear":false,"parkingBrake":true,"onGround":false,"seq":42,"aircraft":"Cessna 172"}
```

遥测字段单位：`alt/altAgl` 英尺，`hdg/pitch/roll` 度，`gs/ias/vs` 米/秒，`flaps` 0..100，`trim` -1..1，`throttle` 0..1。

```json
{"type":"route","departure":"ZBAA","destination":"ZSPD","waypoints":[{"index":0,"ident":"ZBAA","lat":40.0801,"lon":116.5846,"alt":0},{"index":1,"ident":"RENOB","lat":39.823,"lon":116.312,"alt":7200}]}
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
