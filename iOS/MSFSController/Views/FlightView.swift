import SwiftUI
import Foundation

// 面向新手的意图式自动驾驶页：用户只选择方向、高度和到达方式，
// 底层再映射为标准 SimConnect AP 模式。所有状态均来自模拟器回读。

struct FlightView: View {
    @EnvironmentObject var conn: ConnectionManager

    @State private var selectedHeading = 0
    @State private var targetAltitude = 5_000
    @State private var targetSpeed = 120
    @State private var climbRate = 1_000
    @State private var headingEdited = false
    @State private var altitudeEdited = false
    @State private var speedEdited = false

    var body: some View {
        VStack(spacing: 10) {
            header
            ScrollView(.horizontal, showsIndicators: true) {
                HStack(alignment: .top, spacing: 12) {
                    statusCard.frame(width: 260)
                    directionCard.frame(width: 330)
                    altitudeCard.frame(width: 370)
                    approachCard.frame(width: 350)
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
    }

    private var header: some View {
        HStack(spacing: 10) {
            Label("自动驾驶", systemImage: "airplane")
                .font(.headline)
            statusBadge("AP", active: conn.aircraft.autopilot)
            statusBadge("方向", active: lateralActive)
            statusBadge("高度", active: verticalActive)
            statusBadge("进近", active: approachEngaged)
            Spacer()
            Text(conn.isA320neoV2 ? "A320neo V2 适配" : "标准 SimConnect 模式")
                .font(.caption.bold())
                .foregroundColor(conn.isA320neoV2 ? .cyan : .secondary)
            Button(conn.aircraft.autopilot ? "自动驾驶 ON" : "自动驾驶 OFF") {
                conn.setAutopilot(!conn.aircraft.autopilot)
            }
            .buttonStyle(.borderedProminent)
            .tint(conn.aircraft.autopilot ? .green : .blue)
            .disabled(!conn.simConnected)
        }
    }

    private var statusCard: some View {
        intentCard(title: "飞机正在做什么", icon: "airplane.circle.fill") {
            VStack(alignment: .leading, spacing: 10) {
                statusLine(icon: "location.north.fill", title: directionStatus,
                           active: lateralActive)
                statusLine(icon: verticalIcon, title: verticalStatus,
                           active: verticalActive)
                statusLine(icon: "gauge.with.dots.needle.50percent",
                           title: speedStatus, active: conn.aircraft.autopilotFlightLevelChange)
                statusLine(icon: "airplane.arrival", title: approachStatus,
                           active: approachEngaged)
                Divider()
                if let waypoint = activeWaypoint {
                    Text("下一航点")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    HStack {
                        Text(waypoint.ident.isEmpty ? "WPT" : waypoint.ident)
                            .font(.title3.bold())
                        Spacer()
                        Text(String(format: "%.1f NM", conn.aircraft.gpsWaypointDistance))
                            .font(.subheadline.monospacedDigit())
                    }
                } else {
                    Text("没有可识别的下一航点")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                Text(conn.aircraftName.isEmpty ? "等待 MSFS…" : conn.aircraftName)
                    .font(.caption2)
                    .foregroundStyle(.secondary)
                    .lineLimit(2)
            }
        }
    }

    private var directionCard: some View {
        intentCard(title: "往哪里飞", icon: "location.north.circle") {
            VStack(alignment: .leading, spacing: 9) {
                Text("飞向指定航向 · HDG")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                HStack(spacing: 6) {
                    stepButton("−10", action: { changeHeading(-10) })
                    stepButton("−1", action: { changeHeading(-1) })
                    Text(String(format: "%03d°", selectedHeading))
                        .font(.system(size: 30, weight: .semibold, design: .monospaced))
                        .frame(maxWidth: .infinity)
                    stepButton("+1", action: { changeHeading(1) })
                    stepButton("+10", action: { changeHeading(10) })
                }
                Slider(value: Binding(get: { Double(selectedHeading) }, set: {
                    selectedHeading = normalizedHeading(Int($0.rounded()))
                    headingEdited = true
                }), in: 0...359, step: 1)
                HStack {
                    Button("当前航向") {
                        selectedHeading = normalizedHeading(Int(conn.aircraft.magneticHeading.rounded()))
                        headingEdited = true
                    }
                    .buttonStyle(.bordered)
                    Button("飞向 \(String(format: "%03d°", selectedHeading))") {
                        conn.flyHeading(selectedHeading)
                    }
                    .buttonStyle(.borderedProminent)
                }

                Divider()

                Button {
                    conn.followRoute(syncFirst: true)
                } label: {
                    Label("同步并跟随航线", systemImage: "point.topleft.down.to.point.bottomright.curvepath")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .tint(.green)
                .disabled(conn.flightPlan.waypoints.isEmpty)

                HStack {
                    Text("NAV 来源")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                    sourceButton("GPS 航路", gps: true)
                    sourceButton("NAV1 电台", gps: false)
                }
                Button("停止方向引导") { conn.setAutopilotMode("off") }
                    .buttonStyle(.bordered)
            }
        }
    }

    private var altitudeCard: some View {
        intentCard(title: "飞多高", icon: "arrow.up.and.down.circle") {
            VStack(alignment: .leading, spacing: 9) {
                HStack {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("当前高度")
                            .font(.caption2)
                            .foregroundStyle(.secondary)
                        Text(String(format: "%.0f ft", conn.aircraft.altitude))
                            .font(.headline.monospacedDigit())
                    }
                    Spacer()
                    VStack(alignment: .trailing, spacing: 2) {
                        Text("目标高度")
                            .font(.caption2)
                            .foregroundStyle(.secondary)
                        Text("\(targetAltitude) ft")
                            .font(.title3.bold().monospacedDigit())
                    }
                }

                HStack(spacing: 6) {
                    stepButton("−1000", action: { changeAltitude(-1_000) })
                    stepButton("−100", action: { changeAltitude(-100) })
                    Spacer()
                    stepButton("+100", action: { changeAltitude(100) })
                    stepButton("+1000", action: { changeAltitude(1_000) })
                }

                Button("保持当前高度 · ALT") { conn.holdCurrentAltitude() }
                    .buttonStyle(.borderedProminent)
                    .frame(maxWidth: .infinity)

                Text(targetAltitude >= Int(conn.aircraft.altitude) ? "爬升方式" : "下降方式")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                HStack(spacing: 6) {
                    rateButton("舒适", value: 500)
                    rateButton("正常", value: 1_000)
                    rateButton("快速", value: 1_500)
                }
                Button("按升降率前往目标 · VS") {
                    conn.flyVerticalSpeed(targetAltitude: targetAltitude,
                                          feetPerMinute: signedClimbRate)
                }
                .buttonStyle(.borderedProminent)

                HStack {
                    Text("保持速度")
                        .font(.caption)
                    stepButton("−5", action: { changeSpeed(-5) })
                    Text("\(targetSpeed) kt")
                        .font(.headline.monospacedDigit())
                        .frame(maxWidth: .infinity)
                    stepButton("+5", action: { changeSpeed(5) })
                }
                Button("按速度前往目标 · FLC") {
                    conn.flyFlightLevelChange(targetAltitude: targetAltitude,
                                              speedKnots: targetSpeed)
                }
                .buttonStyle(.bordered)
                Text("FLC 通过俯仰保持速度；没有自动油门的飞机仍需手动调油门。")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
            }
        }
    }

    private var approachCard: some View {
        intentCard(title: "准备降落", icon: "airplane.arrival") {
            VStack(alignment: .leading, spacing: 9) {
                if conn.flightPlan.waypoints.isEmpty {
                    Text("没有活动航路")
                        .font(.headline)
                    Text("请先在 MSFS 世界地图中选择目的地、跑道和进近程序。")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                } else {
                    Text("\(routeEndpoint(conn.flightPlan.departure)) → \(routeEndpoint(conn.flightPlan.destination))")
                        .font(.title3.bold())
                    Text(routeDetails)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    if conn.isA320neoV2 && !a320ApproachPlanComplete {
                        Text("世界地图航路未检测到完整的目标跑道和进近程序；自动进近前请补全并重新进入航班。")
                            .font(.caption2)
                            .foregroundStyle(.orange)
                    }
                }

                Button {
                    conn.syncFlightPlan()
                } label: {
                    Label("同步航路到机载导航", systemImage: "arrow.triangle.2.circlepath")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .disabled(conn.flightPlan.waypoints.isEmpty)

                if !conn.routeSyncMessage.isEmpty {
                    Text(conn.routeSyncMessage)
                        .font(.caption2)
                        .foregroundStyle(.orange)
                }

                Divider()
                approachCheck("航路已加载", complete: !conn.flightPlan.waypoints.isEmpty,
                              waiting: false)
                approachCheck(localizerFrequencyText,
                              complete: conn.aircraft.nav1HasLocalizer,
                              waiting: conn.aircraft.nav1Frequency > 100)
                approachCheck("等待截获跑道中心线 · LOC",
                              complete: conn.aircraft.autopilotApproachActive,
                              waiting: conn.aircraft.autopilotApproachArm)
                approachCheck("等待截获下滑道 · GS",
                              complete: conn.aircraft.autopilotGlideslopeActive,
                              waiting: conn.aircraft.autopilotGlideslopeArm)

                Button(approachEngaged ? "取消自动进近" : "准备自动进近 · APP") {
                    approachEngaged ? conn.setAutopilotApproach(false) : conn.prepareApproach()
                }
                .buttonStyle(.borderedProminent)
                .tint(approachEngaged ? .orange : .green)
                .disabled(conn.flightPlan.waypoints.isEmpty && !approachEngaged)

                Text(approachHelp)
                    .font(.caption2)
                    .foregroundStyle(.secondary)
            }
        }
    }

    private func intentCard<Content: View>(title: String, icon: String,
                                           @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            Label(title, systemImage: icon).font(.headline)
            content()
            Spacer(minLength: 0)
        }
        .padding(13)
        .frame(maxHeight: .infinity, alignment: .top)
        .background(RoundedRectangle(cornerRadius: 14).fill(Color(.secondarySystemBackground)))
        .disabled(!conn.simConnected)
        .opacity(conn.simConnected ? 1 : 0.55)
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
            .frame(maxWidth: .infinity)
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

    private func approachCheck(_ title: String, complete: Bool, waiting: Bool) -> some View {
        HStack(spacing: 8) {
            Image(systemName: complete ? "checkmark.circle.fill" :
                    (waiting ? "circle.dotted" : "circle"))
                .foregroundColor(complete ? .green : (waiting ? .orange : .secondary))
            Text(complete ? title.replacingOccurrences(of: "等待截获", with: "已截获") : title)
                .font(.caption)
        }
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

    private var directionStatus: String {
        if conn.aircraft.autopilotApproachActive { return "已对准跑道中心线" }
        if conn.aircraft.autopilotNavLock { return "正在跟随航线" }
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

    private var speedStatus: String {
        conn.aircraft.autopilotFlightLevelChange
            ? String(format: "保持 %.0f kt（FLC）", conn.aircraft.autopilotSpeed)
            : "速度由飞行员/自动油门管理"
    }

    private var approachStatus: String {
        if conn.aircraft.autopilotGlideslopeActive { return "已捕获 LOC 和下滑道" }
        if conn.aircraft.autopilotGlideslopeArm { return "已对准，等待下滑道" }
        if conn.aircraft.autopilotApproachActive { return "已捕获跑道中心线" }
        if conn.aircraft.autopilotApproachArm { return "进近已预位，等待截获" }
        return "自动进近未准备"
    }

    private var approachHelp: String {
        if conn.isA320neoV2 {
            return "A320neo V2：先同步世界地图中已选择的跑道和进近，再核对 MCDU/RAD NAV；App 会区分 APP 预位、LOC 和 GS 捕获。"
        }
        return "其他机型：请先在游戏内选择进近程序、跑道并调好频率，App 负责 APP 预位及 LOC/GS 状态显示。"
    }

    private var a320ApproachPlanComplete: Bool {
        !conn.flightPlan.destinationRunway.isEmpty && !conn.flightPlan.approachType.isEmpty
    }

    private var localizerFrequencyText: String {
        if conn.aircraft.nav1Frequency > 100 {
            return String(format: "NAV1 %.2f MHz%@", conn.aircraft.nav1Frequency,
                          conn.aircraft.nav1HasLocalizer ? " · ILS" : "")
        }
        return "等待 A320 FMGS/飞行员调谐 ILS 频率"
    }

    private var activeWaypoint: Waypoint? {
        let index = conn.aircraft.gpsWaypointIndex
        guard conn.flightPlan.waypoints.indices.contains(index) else { return nil }
        return conn.flightPlan.waypoints[index]
    }

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
            parts.append("\(conn.flightPlan.approachType) \(conn.flightPlan.destinationRunway)".trimmingCharacters(in: .whitespaces))
        }
        return parts.joined(separator: " · ")
    }
}
