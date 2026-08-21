import SwiftUI
import Foundation

// 阶段式自动驾驶页：飞机状态 → 起飞准备 → 飞行控制 → 进近/降落。
// 命令是否真正生效一律由 MSFS 遥测回读确认。

struct FlightView: View {
    @EnvironmentObject var conn: ConnectionManager

    @State private var selectedHeading = 0
    @State private var targetAltitude = 5_000
    @State private var targetSpeed = 120
    @State private var climbRate = 1_000
    @State private var headingEdited = false
    @State private var altitudeEdited = false
    @State private var speedEdited = false
    @State private var mcduConfirmed = false
    @State private var approachConfirmed = false

    var body: some View {
        VStack(spacing: 10) {
            header
            ScrollView(.horizontal, showsIndicators: true) {
                HStack(alignment: .top, spacing: 12) {
                    aircraftStatusCard.frame(width: 300)
                    takeoffCard.frame(width: 360)
                    flightCard.frame(width: 510)
                    landingCard.frame(width: 390)
                }
                .padding(.bottom, 4)
            }
        }
        .padding(10)
        .onAppear { synchronizeTargets() }
        .onChange(of: conn.aircraft.autopilotHeading) { _, value in
            let actual = normalizedHeading(Int(value.rounded()))
            if !headingEdited { selectedHeading = actual }
            else if headingDistance(actual, selectedHeading) <= 1 { headingEdited = false }
        }
        .onChange(of: conn.aircraft.autopilotAltitude) { _, value in
            if !altitudeEdited && value > 0 { targetAltitude = roundedAltitude(value) }
        }
        .onChange(of: conn.aircraft.autopilotSpeed) { _, value in
            if !speedEdited && value >= 40 { targetSpeed = Int(value.rounded()) }
        }
        .onChange(of: conn.aircraftName) { _, _ in
            mcduConfirmed = false
            approachConfirmed = false
        }
    }

    private var header: some View {
        HStack(spacing: 9) {
            Label("自动驾驶", systemImage: "airplane")
                .font(.headline)
            Text(flightPhase)
                .font(.caption.bold())
                .foregroundColor(.cyan)
            statusBadge("AP", active: conn.aircraft.autopilot)
            statusBadge("A/THR", active: conn.aircraft.autothrottleActive)
            statusBadge("NAV", active: conn.aircraft.autopilotNavLock)
            statusBadge("APP", active: approachEngaged)
            Spacer()
            Text(conn.aircraftProfileLabel)
                .font(.caption.bold())
                .foregroundColor(conn.isLegacyA320neo ? .green :
                    (conn.usesExternalFmsRoute ? .orange : .secondary))
            Button(conn.aircraft.autopilot ? "自动驾驶 ON" : "自动驾驶 OFF") {
                conn.setAutopilot(!conn.aircraft.autopilot)
            }
            .buttonStyle(.borderedProminent)
            .tint(conn.aircraft.autopilot ? .green : .blue)
            .disabled(!conn.simConnected)
        }
    }

    // MARK: - 飞机状态

    private var aircraftStatusCard: some View {
        stageCard(number: "状态", title: "飞机状态", icon: "airplane.circle.fill") {
            VStack(alignment: .leading, spacing: 9) {
                HStack {
                    valueBlock("高度", String(format: "%.0f ft", conn.aircraft.altitude))
                    Spacer()
                    valueBlock("航向", String(format: "%03.0f°", conn.aircraft.magneticHeading), trailing: true)
                }
                HStack {
                    valueBlock("空速", String(format: "%.0f kt", indicatedKnots))
                    Spacer()
                    valueBlock("升降率", String(format: "%+.0f ft/min", verticalFeetPerMinute), trailing: true)
                }
                HStack {
                    valueBlock("模拟器油门", String(format: "%.0f%%", conn.aircraft.throttle * 100))
                    Spacer()
                    valueBlock("襟翼", String(format: "%.0f%%", conn.aircraft.flapsPercent), trailing: true)
                }

                if conn.aircraft.autothrottleActive {
                    Label("A/THR 正在管理推力；手机拉到 0% 会明确断开并强制 IDLE。",
                          systemImage: "exclamationmark.triangle.fill")
                        .font(.caption2)
                        .foregroundColor(.orange)
                } else if conn.aircraft.autothrottleArmed {
                    Label("A/THR 已预位但尚未接管", systemImage: "speedometer")
                        .font(.caption2)
                        .foregroundColor(.secondary)
                }

                Divider()
                statusLine(icon: "location.north.fill", title: directionStatus,
                           active: lateralActive)
                statusLine(icon: verticalIcon, title: verticalStatus,
                           active: verticalActive)
                statusLine(icon: "airplane.arrival", title: approachStatus,
                           active: approachEngaged)

                if let waypoint = activeWaypoint {
                    Divider()
                    Text(conn.usesExternalFmsRoute ? "地图/标准 GPS 下一航点" : "下一航点")
                        .font(.caption2)
                        .foregroundColor(.secondary)
                    HStack {
                        Text(waypoint.ident.isEmpty ? "WPT" : waypoint.ident)
                            .font(.headline)
                        Spacer()
                        Text(String(format: "%.1f NM", conn.aircraft.gpsWaypointDistance))
                            .font(.subheadline.monospacedDigit())
                    }
                }
            }
        }
    }

    // MARK: - 起飞阶段

    private var takeoffCard: some View {
        stageCard(number: "起飞阶段", title: "航路设置", icon: "airplane.departure") {
            ScrollView(.vertical, showsIndicators: true) {
                VStack(alignment: .leading, spacing: 9) {
                    routeSummary

                    if conn.usesExternalFmsRoute {
                        Text("App 中的航点来自 MSFS 世界地图，不代表已经写入 MCDU。")
                            .font(.caption2)
                            .foregroundColor(.orange)
                        checkRow("EFB 已填写 SimBrief Pilot ID", complete: mcduConfirmed)
                        checkRow("MCDU INIT → INIT REQUEST 已完成", complete: mcduConfirmed)
                        checkRow("F-PLN 航点、SID/STAR 与断点已检查", complete: mcduConfirmed)
                        Button(mcduConfirmed ? "✓ MCDU 航路已确认" : "确认 MCDU 航路已准备") {
                            mcduConfirmed.toggle()
                        }
                        .buttonStyle(.borderedProminent)
                        .tint(mcduConfirmed ? .green : .blue)
                        Text("这是飞行员确认项；SimConnect 无法读取专有 FMGS/FMC 内部航路。")
                            .font(.caption2)
                            .foregroundColor(.secondary)
                    } else {
                        Button {
                            conn.syncFlightPlan()
                        } label: {
                            Label("同步 .PLN 到标准 GPS", systemImage: "arrow.triangle.2.circlepath")
                                .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.borderedProminent)
                        .disabled(conn.flightPlan.waypoints.isEmpty)
                        if !conn.routeSyncMessage.isEmpty {
                            Text(conn.routeSyncMessage)
                                .font(.caption2)
                                .foregroundColor(.orange)
                        }
                    }

                    Divider()
                    checkRow("起落架放下", complete: conn.aircraft.gearDown)
                    checkRow(conn.aircraft.parkingBrake ? "驻车制动已接通" : "驻车制动已释放",
                             complete: !conn.aircraft.parkingBrake)
                    checkRow(String(format: "襟翼 %.0f%%", conn.aircraft.flapsPercent),
                             complete: conn.aircraft.flapsPercent > 0)
                    Text("起飞由飞行员完成；稳定爬升后再在“飞行阶段”接通 NAV/AP。")
                        .font(.caption2)
                        .foregroundColor(.secondary)
                }
            }
        }
    }

    @ViewBuilder private var routeSummary: some View {
        if conn.flightPlan.waypoints.isEmpty {
            Text("没有可见的世界地图航路")
                .font(.headline)
            Text(conn.usesExternalFmsRoute ? "请先在该机型的 EFB/FMS 中建立航路。" :
                    "请先在 MSFS 世界地图建立并激活飞行计划。")
                .font(.caption)
                .foregroundColor(.secondary)
        } else {
            Text("\(routeEndpoint(conn.flightPlan.departure)) → \(routeEndpoint(conn.flightPlan.destination))")
                .font(.title3.bold())
            Text(routeDetails)
                .font(.caption)
                .foregroundColor(.secondary)
        }
    }

    // MARK: - 飞行阶段

    private var flightCard: some View {
        stageCard(number: "飞行阶段", title: "高度、方向与导航", icon: "point.topleft.down.to.point.bottomright.curvepath") {
            ScrollView(.vertical, showsIndicators: true) {
                VStack(alignment: .leading, spacing: 10) {
                    Button {
                        conn.followRoute(syncFirst: !conn.usesExternalFmsRoute)
                    } label: {
                        Label(conn.usesExternalFmsRoute ? "跟随机载 FMS 航路 · NAV" : "同步并跟随 GPS 航路 · NAV",
                              systemImage: "location.north.line.fill")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)
                    .tint(.green)
                    .disabled(routeFollowDisabled)

                    if conn.usesExternalFmsRoute {
                        Text(mcduConfirmed ? "导航源：A320 FMGS/MCDU（已由飞行员确认）" :
                                "请先在“起飞阶段”确认 MCDU F-PLN。")
                            .font(.caption2)
                            .foregroundColor(mcduConfirmed ? .secondary : .orange)
                    } else {
                        HStack {
                            Text("导航源").font(.caption2).foregroundColor(.secondary)
                            sourceButton("GPS 航路", gps: true)
                            sourceButton("NAV1 电台", gps: false)
                        }
                    }

                    Divider()
                    Text("指定航向 · HDG").font(.caption).foregroundColor(.secondary)
                    HStack(spacing: 6) {
                        stepButton("−10", action: { changeHeading(-10) })
                        stepButton("−1", action: { changeHeading(-1) })
                        Text(String(format: "%03d°", selectedHeading))
                            .font(.system(size: 27, weight: .semibold, design: .monospaced))
                            .frame(maxWidth: .infinity)
                        stepButton("+1", action: { changeHeading(1) })
                        stepButton("+10", action: { changeHeading(10) })
                        Button("飞向") { conn.flyHeading(selectedHeading) }
                            .buttonStyle(.borderedProminent)
                    }
                    Slider(value: Binding(get: { Double(selectedHeading) }, set: {
                        selectedHeading = normalizedHeading(Int($0.rounded()))
                        headingEdited = true
                    }), in: 0...359, step: 1)

                    Divider()
                    HStack {
                        valueBlock("当前高度", String(format: "%.0f ft", conn.aircraft.altitude))
                        Spacer()
                        valueBlock("目标高度", "\(targetAltitude) ft", trailing: true)
                    }
                    HStack(spacing: 6) {
                        stepButton("−1000", action: { changeAltitude(-1_000) })
                        stepButton("−100", action: { changeAltitude(-100) })
                        Spacer()
                        stepButton("+100", action: { changeAltitude(100) })
                        stepButton("+1000", action: { changeAltitude(1_000) })
                    }
                    HStack(spacing: 6) {
                        Button("保持当前高度 · ALT") { conn.holdCurrentAltitude() }
                            .buttonStyle(.borderedProminent)
                        rateButton("舒适", value: 500)
                        rateButton("正常", value: 1_000)
                        rateButton("快速", value: 1_500)
                    }
                    Button("按 \(signedClimbRate >= 0 ? "爬升" : "下降")率 \(abs(signedClimbRate)) ft/min 前往目标 · VS") {
                        conn.flyVerticalSpeed(targetAltitude: targetAltitude,
                                              feetPerMinute: signedClimbRate)
                    }
                    .buttonStyle(.borderedProminent)

                    HStack {
                        Text("FLC 速度").font(.caption)
                        stepButton("−5", action: { changeSpeed(-5) })
                        Text("\(targetSpeed) kt")
                            .font(.headline.monospacedDigit())
                            .frame(maxWidth: .infinity)
                        stepButton("+5", action: { changeSpeed(5) })
                        Button("按速度前往") {
                            conn.flyFlightLevelChange(targetAltitude: targetAltitude,
                                                      speedKnots: targetSpeed)
                        }
                        .buttonStyle(.bordered)
                    }
                    Text("A320 使用 A/THR 时推力可能由飞机管理；状态卡显示真实 A/THR 状态。")
                        .font(.caption2)
                        .foregroundColor(.secondary)
                }
            }
        }
    }

    // MARK: - 降落阶段

    private var landingCard: some View {
        stageCard(number: "降落阶段", title: "自动进近 / 降落", icon: "airplane.arrival") {
            ScrollView(.vertical, showsIndicators: true) {
                VStack(alignment: .leading, spacing: 9) {
                    if conn.usesExternalFmsRoute {
                        checkRow("MCDU ARRIVAL 已选择跑道、STAR 与进近", complete: approachConfirmed)
                        checkRow("F-PLN 无意外断点，ILS/RAD NAV 已核对", complete: approachConfirmed)
                        Button(approachConfirmed ? "✓ MCDU 进近已确认" : "确认 MCDU 进近已准备") {
                            approachConfirmed.toggle()
                        }
                        .buttonStyle(.bordered)
                        .tint(approachConfirmed ? .green : .blue)
                    } else {
                        checkRow("游戏内已选择进近、跑道并调谐频率",
                                 complete: conn.aircraft.nav1Frequency > 100)
                    }

                    Divider()
                    checkRow(localizerFrequencyText,
                             complete: conn.aircraft.nav1HasLocalizer,
                             waiting: conn.aircraft.nav1Frequency > 100)
                    checkRow("等待截获跑道中心线 · LOC",
                             complete: conn.aircraft.autopilotApproachActive,
                             waiting: conn.aircraft.autopilotApproachArm)
                    checkRow("等待截获下滑道 · GS",
                             complete: conn.aircraft.autopilotGlideslopeActive,
                             waiting: conn.aircraft.autopilotGlideslopeArm)

                    Button(approachEngaged ? "取消自动进近" : "预位自动进近 · APP") {
                        approachEngaged ? conn.setAutopilotApproach(false) : conn.prepareApproach()
                    }
                    .buttonStyle(.borderedProminent)
                    .tint(approachEngaged ? .orange : .green)
                    .disabled(approachButtonDisabled)

                    Text(approachStatus)
                        .font(.headline)
                        .foregroundColor(approachEngaged ? .green : .secondary)
                    Text("建议从下滑道下方、以较小夹角截获。APP 只负责预位并显示 LOC/GS；襟翼、起落架、速度、AP2、拉平和落地滑跑仍需飞行员确认，不能仅凭本 App 声称已自动着陆。")
                        .font(.caption2)
                        .foregroundColor(.secondary)
                }
            }
        }
    }

    // MARK: - 通用组件与状态

    private func stageCard<Content: View>(number: String, title: String, icon: String,
                                          @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text(number).font(.caption.bold()).foregroundColor(.cyan)
                Spacer()
                Image(systemName: icon).foregroundColor(.cyan)
            }
            Text(title).font(.title3.bold())
            content()
            Spacer(minLength: 0)
        }
        .padding(13)
        .frame(maxHeight: .infinity, alignment: .top)
        .background(RoundedRectangle(cornerRadius: 14).fill(Color(.secondarySystemBackground)))
        .disabled(!conn.simConnected)
        .opacity(conn.simConnected ? 1 : 0.55)
    }

    private func valueBlock(_ label: String, _ value: String, trailing: Bool = false) -> some View {
        VStack(alignment: trailing ? .trailing : .leading, spacing: 2) {
            Text(label).font(.caption2).foregroundColor(.secondary)
            Text(value).font(.headline.monospacedDigit())
        }
    }

    private func stepButton(_ title: String, action: @escaping () -> Void) -> some View {
        Button(title, action: action)
            .buttonStyle(.bordered)
            .font(.caption.bold().monospacedDigit())
    }

    private func rateButton(_ title: String, value: Int) -> some View {
        Button(title) { climbRate = value }
            .buttonStyle(.borderedProminent)
            .tint(climbRate == value ? .blue : Color(.systemGray3))
            .font(.caption)
    }

    private func sourceButton(_ title: String, gps: Bool) -> some View {
        let active = conn.aircraft.gpsDrivesNav1 == gps
        return Button(title) { conn.setNavigationSource(gps ? "gps" : "nav1") }
            .buttonStyle(.borderedProminent)
            .tint(active ? .green : Color(.systemGray3))
            .font(.caption2)
    }

    private func statusBadge(_ title: String, active: Bool) -> some View {
        Text(title)
            .font(.caption.bold())
            .foregroundColor(active ? .black : .secondary)
            .padding(.horizontal, 7)
            .padding(.vertical, 3)
            .background(Capsule().fill(active ? Color.green : Color(.systemGray5)))
    }

    private func statusLine(icon: String, title: String, active: Bool) -> some View {
        HStack(spacing: 8) {
            Image(systemName: icon).foregroundColor(active ? .green : .secondary)
            Text(title).font(.subheadline).lineLimit(2)
        }
    }

    private func checkRow(_ title: String, complete: Bool, waiting: Bool = false) -> some View {
        HStack(spacing: 8) {
            Image(systemName: complete ? "checkmark.circle.fill" :
                    (waiting ? "circle.dotted" : "circle"))
                .foregroundColor(complete ? .green : (waiting ? .orange : .secondary))
            Text(complete ? title.replacingOccurrences(of: "等待截获", with: "已截获") : title)
                .font(.caption)
        }
    }

    private var flightPhase: String {
        if conn.aircraft.onGround { return "起飞准备" }
        if approachEngaged || (conn.aircraft.altAgl > 0 && conn.aircraft.altAgl < 2_500 && conn.aircraft.verticalSpeed < 0) {
            return "进近 / 降落"
        }
        return "飞行中"
    }

    private var lateralActive: Bool {
        conn.aircraft.autopilotHeadingLock || conn.aircraft.autopilotNavLock
    }

    private var verticalActive: Bool {
        conn.aircraft.autopilotAltitudeLock || conn.aircraft.autopilotVerticalHold ||
        conn.aircraft.autopilotFlightLevelChange
    }

    private var approachEngaged: Bool {
        conn.aircraft.autopilotApproachArm || conn.aircraft.autopilotApproachActive ||
        conn.aircraft.autopilotGlideslopeArm || conn.aircraft.autopilotGlideslopeActive
    }

    private var routeFollowDisabled: Bool {
        conn.usesExternalFmsRoute ? !mcduConfirmed : conn.flightPlan.waypoints.isEmpty
    }

    private var approachButtonDisabled: Bool {
        if approachEngaged { return false }
        return conn.usesExternalFmsRoute ? !approachConfirmed : conn.aircraft.nav1Frequency <= 100
    }

    private var directionStatus: String {
        if conn.aircraft.autopilotApproachActive { return "已截获跑道中心线" }
        if conn.aircraft.autopilotNavLock { return "正在跟随 NAV 航路" }
        if conn.aircraft.autopilotHeadingLock {
            return String(format: "正在飞向 %03.0f°", conn.aircraft.autopilotHeading)
        }
        return "没有方向引导"
    }

    private var verticalStatus: String {
        if conn.aircraft.autopilotGlideslopeActive { return "正在沿下滑道下降" }
        if conn.aircraft.autopilotAltitudeLock {
            return String(format: "保持 %.0f ft", conn.aircraft.autopilotAltitude)
        }
        if conn.aircraft.autopilotFlightLevelChange {
            return String(format: "按速度前往 %.0f ft", conn.aircraft.autopilotAltitude)
        }
        if conn.aircraft.autopilotVerticalHold {
            return String(format: "%+.0f ft/min → %.0f ft",
                          conn.aircraft.autopilotVerticalSpeed,
                          conn.aircraft.autopilotAltitude)
        }
        if conn.aircraft.autopilotAltitudeArm {
            return String(format: "等待截获 %.0f ft", conn.aircraft.autopilotAltitude)
        }
        return "没有高度引导"
    }

    private var verticalIcon: String {
        if conn.aircraft.autopilotGlideslopeActive { return "arrow.down.right" }
        if conn.aircraft.autopilotVerticalSpeed > 50 ||
            (conn.aircraft.autopilotFlightLevelChange && conn.aircraft.autopilotAltitude > conn.aircraft.altitude) {
            return "arrow.up"
        }
        if conn.aircraft.autopilotVerticalSpeed < -50 || conn.aircraft.autopilotFlightLevelChange {
            return "arrow.down"
        }
        return "arrow.left.and.right"
    }

    private var approachStatus: String {
        if conn.aircraft.autopilotGlideslopeActive { return "LOC 与 GS 已捕获，正在自动进近" }
        if conn.aircraft.autopilotGlideslopeArm { return "LOC 已捕获，等待 GS" }
        if conn.aircraft.autopilotApproachActive { return "LOC 已捕获" }
        if conn.aircraft.autopilotApproachArm { return "APP 已预位，等待 LOC/GS" }
        return "自动进近未预位"
    }

    private var localizerFrequencyText: String {
        if conn.aircraft.nav1Frequency > 100 {
            return String(format: "NAV1 %.2f MHz%@", conn.aircraft.nav1Frequency,
                          conn.aircraft.nav1HasLocalizer ? " · ILS" : "")
        }
        return "等待 FMGS/飞行员调谐 ILS 频率"
    }

    private var activeWaypoint: Waypoint? {
        let index = conn.aircraft.gpsWaypointIndex
        guard conn.flightPlan.waypoints.indices.contains(index) else { return nil }
        return conn.flightPlan.waypoints[index]
    }

    private var indicatedKnots: Double { conn.aircraft.indicatedAirspeed * 1.943844 }
    private var verticalFeetPerMinute: Double { conn.aircraft.verticalSpeed * 196.850394 }

    private var signedClimbRate: Int {
        targetAltitude >= Int(conn.aircraft.altitude) ? climbRate : -climbRate
    }

    private func synchronizeTargets() {
        selectedHeading = normalizedHeading(Int(conn.aircraft.autopilotHeading.rounded()))
        let altitude = conn.aircraft.autopilotAltitude > 0
            ? conn.aircraft.autopilotAltitude : conn.aircraft.altitude
        targetAltitude = roundedAltitude(altitude)
        if conn.aircraft.autopilotSpeed >= 40 {
            targetSpeed = Int(conn.aircraft.autopilotSpeed.rounded())
        }
    }

    private func changeHeading(_ delta: Int) {
        selectedHeading = normalizedHeading(selectedHeading + delta)
        headingEdited = true
    }

    private func changeAltitude(_ delta: Int) {
        targetAltitude = min(max(targetAltitude + delta, 0), 60_000)
        altitudeEdited = true
    }

    private func changeSpeed(_ delta: Int) {
        targetSpeed = min(max(targetSpeed + delta, 40), 400)
        speedEdited = true
    }

    private func normalizedHeading(_ value: Int) -> Int {
        ((value % 360) + 360) % 360
    }

    private func headingDistance(_ lhs: Int, _ rhs: Int) -> Int {
        let direct = abs(normalizedHeading(lhs) - normalizedHeading(rhs))
        return min(direct, 360 - direct)
    }

    private func roundedAltitude(_ value: Double) -> Int {
        min(max(Int((value / 100).rounded() * 100), 0), 60_000)
    }

    private func routeEndpoint(_ value: String) -> String {
        value.isEmpty ? "----" : value
    }

    private var routeDetails: String {
        var parts = ["\(conn.flightPlan.waypoints.count) 个航点"]
        if !conn.flightPlan.departureRunway.isEmpty {
            parts.append("起飞跑道 \(conn.flightPlan.departureRunway)")
        }
        if !conn.flightPlan.departureProcedure.isEmpty {
            parts.append("SID \(conn.flightPlan.departureProcedure)")
        }
        if !conn.flightPlan.arrivalProcedure.isEmpty {
            parts.append("STAR \(conn.flightPlan.arrivalProcedure)")
        }
        if !conn.flightPlan.approachType.isEmpty || !conn.flightPlan.destinationRunway.isEmpty {
            parts.append("\(conn.flightPlan.approachType) \(conn.flightPlan.destinationRunway)"
                .trimmingCharacters(in: .whitespaces))
        }
        return parts.joined(separator: " · ")
    }
}
