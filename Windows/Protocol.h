#pragma once
// 两端（Windows 端 + iPhone 端）共享的协议定义。
// 详细说明见 docs/PROTOCOL.md。

#include <cstdint>
#include <cstddef>

namespace proto {

// ---------- 网络端口 ----------
constexpr uint16_t kDefaultUdpPort = 36666;   // 实时控制（UDP）
constexpr uint16_t kDefaultTcpPort = 36667;   // 状态与命令（TCP）
constexpr uint16_t kDiscoveryPort = 36668;    // 自动探测（UDP 广播，iPhone->PC）

// ---------- 版本 ----------
constexpr uint32_t kMagic = 0x4D534631;       // "MSF1"
constexpr uint8_t  kProtocolVersion = 1;
constexpr char     kServerVersion[] = "1.1.1";

// UDP 包类型（第 5 字节，flags/type 字段）
enum UdpType : uint8_t {
    kUdpControl = 1,   // 实时控制（Aileron/Elevator/Rudder/Throttle）
    kUdpPing    = 2,   // iPhone -> PC 延迟探测
    kUdpPong    = 3,   // PC -> iPhone 延迟应答（回显 timestamp）
};

// axisMask 位定义
constexpr uint16_t kAxisAileron  = 1 << 0;
constexpr uint16_t kAxisElevator = 1 << 1;
constexpr uint16_t kAxisRudder   = 1 << 2;
constexpr uint16_t kAxisThrottle = 1 << 3;

// MSFS 轴事件取值范围
constexpr int16_t  kAxisMin      = -16383;
constexpr int16_t  kAxisMax      = 16384;
constexpr int16_t  kRudderMin    = -16383;
constexpr int16_t  kRudderMax    = 16383;
constexpr uint16_t kThrottleMax  = 16383;

// UDP 固定二进制包（32 字节，小端，1 字节对齐）
#pragma pack(push, 1)
struct UdpPacket {
    uint32_t magic;        // 0:  kMagic
    uint8_t  type;         // 4:  kUdpControl / kUdpPing / kUdpPong
    uint8_t  version;      // 5:  kProtocolVersion
    uint32_t sequence;     // 6:  单调递增序号，丢弃乱序旧包
    uint64_t timestampMs;  // 10: 发送方单调时钟(ms)
    uint32_t sessionId;    // 18: PC 在 WELCOME 中分配的会话
    uint16_t axisMask;     // 22: 有效轴位掩码
    int16_t  aileron;      // 24
    int16_t  elevator;     // 26
    int16_t  rudder;       // 28
    uint16_t throttle;     // 30: 0..16383
};
#pragma pack(pop)
static_assert(sizeof(UdpPacket) == 32, "UdpPacket layout must be 32 bytes");

// ---------- TCP 消息（JSON，一行一个，'\n' 结尾） ----------
// iPhone -> PC
constexpr char kMsgHello[] = "hello";     // {type, protocolVersion, appVersion, deviceName}
constexpr char kMsgCmd[]   = "cmd";       // {type, name, value?}
// PC -> iPhone
constexpr char kMsgWelcome[]  = "welcome";    // {type, protocolVersion, sessionId, serverVersion, simConnected, aircraftName}
constexpr char kMsgStatus[]   = "status";     // {type, simConnected, aircraftName}
constexpr char kMsgTelemetry[]= "telemetry";  // {type, lat, lon, alt, ...}
constexpr char kMsgRoute[]    = "route";      // {type, departure?, destination?, waypoints[]}
constexpr char kMsgError[]    = "error";      // {type, code, message}

// 命令名称
constexpr char kCmdFlapsIncr[] = "flaps_incr";
constexpr char kCmdFlapsDecr[] = "flaps_decr";
constexpr char kCmdGear[]      = "gear";
constexpr char kCmdTrimUp[]    = "trim_up";
constexpr char kCmdTrimDn[]    = "trim_dn";
constexpr char kCmdParking[]   = "parking_brake";
constexpr char kCmdBrake[]     = "brake";     // 需携带 value(bool)：按住 true / 松开 false
constexpr char kCmdThrottleTakeover[] = "throttle_takeover"; // 手动控制前断开 A/THR
constexpr char kCmdThrottleSet[] = "throttle_set"; // value: 0..16383，可靠提交最终值
constexpr char kCmdThrottleIdle[] = "throttle_idle"; // 明确断开 A/THR 并将全部发动机置于 idle
constexpr char kCmdAutopilot[] = "autopilot"; // 需携带 value(bool)：明确开启 / 关闭
constexpr char kCmdAutopilotMode[] = "autopilot_mode";       // value: heading/nav/off
constexpr char kCmdAutopilotHeading[] = "autopilot_heading"; // value: 0..359 度
constexpr char kCmdNavigationSource[] = "navigation_source"; // value: gps/nav1
constexpr char kCmdAutopilotAltitude[] = "autopilot_altitude"; // value: feet
constexpr char kCmdAutopilotVerticalMode[] = "autopilot_vertical_mode"; // hold/vs/flc/off
constexpr char kCmdAutopilotVerticalSpeed[] = "autopilot_vertical_speed"; // ft/min
constexpr char kCmdAutopilotSpeed[] = "autopilot_speed"; // knots
constexpr char kCmdAutopilotApproach[] = "autopilot_approach"; // value: bool
constexpr char kCmdSyncFlightPlan[] = "sync_flight_plan";

// 自动探测（UDP 36668）
constexpr char kDiscoveryRequest[]  = "MSFS_DISCOVER";  // iPhone 广播探测
constexpr char kDiscoveryReplyType[] = "msfs_host";     // PC 应答 type
constexpr char kDiscoveryReply[]     = "MSFS iPhone Controller";

// 安全看门狗：实时控制包超过该时长未到达则回中
constexpr int kControlTimeoutMs = 250;

} // namespace proto
