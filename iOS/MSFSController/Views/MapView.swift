import SwiftUI
import MapKit

// 地图页：当前位置 / 实际航迹 / 计划航线 / 航点。只读。

struct MapView: View {
    @EnvironmentObject var conn: ConnectionManager

    @State private var camera: MapCameraPosition = .automatic
    @State private var track: [CLLocationCoordinate2D] = []
    @State private var lastSample: (coord: CLLocationCoordinate2D, date: Date)?
    @State private var autoFollow = true
    @State private var selectedDetail: MapDetail?
    @State private var lastTelemetrySeq: UInt64?
    @State private var lastAircraftName = ""
    @State private var groundedSince: Date?
    @State private var clearOnNextDeparture = false

    var body: some View {
        VStack(spacing: 0) {
            header

            Map(position: $camera) {
                if conn.hasTelemetry {
                    Annotation("", coordinate: aircraftCoord) {
                        Button { selectedDetail = .aircraft } label: {
                            AircraftMarker(heading: conn.aircraft.heading)
                        }
                        .buttonStyle(.plain)
                    }
                    .annotationTitles(.hidden)
                }

                if track.count > 1 {
                    MapPolyline(coordinates: track)
                        .stroke(Color.orange, lineWidth: 2)
                }
                if flightPlanCoords.count > 1 {
                    MapPolyline(coordinates: flightPlanCoords)
                        .stroke(Color.blue, lineWidth: 1.5)
                }
                ForEach(conn.flightPlan.waypoints) { wp in
                    Annotation(wp.ident, coordinate: wp.coord) {
                        Button { selectedDetail = .waypoint(wp) } label: {
                            Text(wp.ident)
                                .font(.caption2.bold())
                                .foregroundStyle(.blue)
                                .padding(.horizontal, 6)
                                .padding(.vertical, 3)
                                .background(Capsule().fill(Color.white))
                                .overlay(Capsule().stroke(Color.blue, lineWidth: 1))
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
            .mapStyle(.standard(elevation: .flat))
            .onChange(of: conn.aircraft) { _, _ in sampleTrack() }
            .onChange(of: routeSignature) { oldValue, newValue in
                if !oldValue.isEmpty && oldValue != newValue { clearTrack() }
            }
            .simultaneousGesture(DragGesture(minimumDistance: 4).onChanged { _ in
                autoFollow = false
            })
            .simultaneousGesture(MagnifyGesture().onChanged { _ in
                autoFollow = false
            })
            .onAppear { sampleTrack(); follow() }
            .sheet(item: $selectedDetail) { detail in
                switch detail {
                case .aircraft:
                    AircraftDetail(aircraft: conn.aircraft, name: conn.aircraftName)
                        .presentationDetents([.height(260)])
                case .waypoint(let waypoint):
                    WaypointDetail(waypoint: waypoint)
                        .presentationDetents([.height(220)])
                }
            }

            readoutBar

            HStack {
                Button {
                    autoFollow = true
                    follow()
                } label: {
                    Label("跟随", systemImage: "location.fill")
                }
                Spacer()
                Button("清除航迹") {
                    clearTrack()
                }
            }
            .font(.caption)
            .padding(.horizontal, 12)
            .padding(.vertical, 6)
        }
    }

    // MARK: - 头部

    private var header: some View {
        HStack {
            Text("MAP")
                .font(.headline)
            Spacer()
            Text(titleText)
                .font(.caption)
                .foregroundColor(.secondary)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
    }

    private var titleText: String {
        if !conn.flightPlan.waypoints.isEmpty {
            return "\(conn.flightPlan.departure) → \(conn.flightPlan.destination)"
        }
        return conn.aircraftName.isEmpty ? "Not Connected" : conn.aircraftName
    }

    // MARK: - 数据

    private var aircraftCoord: CLLocationCoordinate2D {
        CLLocationCoordinate2D(latitude: conn.aircraft.lat, longitude: conn.aircraft.lon)
    }

    private var flightPlanCoords: [CLLocationCoordinate2D] {
        conn.flightPlan.waypoints.map { $0.coord }
    }

    private var routeSignature: String {
        guard !conn.flightPlan.waypoints.isEmpty else { return "" }
        return conn.flightPlan.waypoints.map {
            "\($0.ident):\(String(format: "%.5f", $0.lat)):\(String(format: "%.5f", $0.lon))"
        }.joined(separator: "|")
    }

    private func sampleTrack() {
        let a = conn.aircraft
        guard conn.hasTelemetry,
              (-90.0...90.0).contains(a.lat),
              (-180.0...180.0).contains(a.lon) else { return }
        let coord = CLLocationCoordinate2D(latitude: a.lat, longitude: a.lon)
        let now = Date()

        if let lastSeq = lastTelemetrySeq, a.seq < lastSeq { clearTrack() }
        lastTelemetrySeq = a.seq

        if !lastAircraftName.isEmpty, !conn.aircraftName.isEmpty,
           lastAircraftName != conn.aircraftName { clearTrack() }
        if !conn.aircraftName.isEmpty { lastAircraftName = conn.aircraftName }

        if a.onGround && a.groundSpeed < 2 {
            if groundedSince == nil { groundedSince = now }
            if let since = groundedSince, now.timeIntervalSince(since) >= 30 {
                clearOnNextDeparture = true
            }
        } else if !a.onGround {
            groundedSince = nil
            if clearOnNextDeparture {
                clearTrack()
                clearOnNextDeparture = false
            }
        } else {
            groundedSince = nil
        }

        if let last = lastSample {
            let dist = last.coord.distance(to: coord)
            // 更换机场/加载新航班时的瞬移不应画出跨洲直线。
            if dist > 100_000 { clearTrack() }
            if now.timeIntervalSince(last.date) < 1 && dist < 50 { return }
        }
        lastSample = (coord, now)
        track.append(coord)
        if track.count > 4000 { track.removeFirst(track.count - 4000) }
        if autoFollow { follow() }
    }

    private func follow() {
        let a = conn.aircraft
        guard conn.hasTelemetry else { return }
        let center = CLLocationCoordinate2D(latitude: a.lat, longitude: a.lon)
        withAnimation(.easeInOut(duration: 0.5)) {
            camera = .region(MKCoordinateRegion(center: center,
                                                latitudinalMeters: 4000,
                                                longitudinalMeters: 4000))
        }
    }

    private func clearTrack() {
        track.removeAll()
        lastSample = nil
    }

    // MARK: - 坐标读数

    private var readoutBar: some View {
        HStack(spacing: 12) {
            if conn.hasTelemetry {
                Text(String(format: "LAT %.6f", conn.aircraft.lat))
                Text(String(format: "LON %.6f", conn.aircraft.lon))
                Text(String(format: "HDG %.0f°", conn.aircraft.heading))
                Text(String(format: "ALT %.0f ft", conn.aircraft.altitude))
            } else {
                Text("等待 MSFS 遥测…")
                    .foregroundStyle(.secondary)
            }
        }
        .font(.caption.monospacedDigit())
        .frame(maxWidth: .infinity)
        .padding(.vertical, 6)
        .background(Color(.systemGray6))
    }
}

enum MapDetail: Identifiable {
    case aircraft
    case waypoint(Waypoint)

    var id: String {
        switch self {
        case .aircraft: return "aircraft"
        case .waypoint(let waypoint): return "waypoint-\(waypoint.id)"
        }
    }
}

// 飞机 Marker：按真实航向旋转
struct AircraftMarker: View {
    let heading: Double

    var body: some View {
        Image(systemName: "location.north.fill")
            .font(.title2)
            .foregroundColor(.blue)
            .background(Circle().fill(Color.white).frame(width: 34, height: 34))
            .rotationEffect(.degrees(heading))
            .shadow(radius: 2)
    }
}

struct WaypointDetail: View {
    let waypoint: Waypoint

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(waypoint.ident).font(.title2.bold())
            LabeledContent("序号", value: "\(waypoint.index + 1)")
            LabeledContent("纬度", value: String(format: "%.6f", waypoint.lat))
            LabeledContent("经度", value: String(format: "%.6f", waypoint.lon))
            LabeledContent("计划高度", value: String(format: "%.0f ft", waypoint.alt))
        }
        .monospacedDigit()
        .padding(20)
    }
}

struct AircraftDetail: View {
    let aircraft: AircraftState
    let name: String

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(name.isEmpty ? "Aircraft" : name).font(.title2.bold())
            LabeledContent("纬度", value: String(format: "%.6f", aircraft.lat))
            LabeledContent("经度", value: String(format: "%.6f", aircraft.lon))
            LabeledContent("高度", value: String(format: "%.0f ft", aircraft.altitude))
            LabeledContent("航向", value: String(format: "%.0f°", aircraft.heading))
            LabeledContent("地速", value: String(format: "%.1f m/s", aircraft.groundSpeed))
        }
        .monospacedDigit()
        .padding(20)
    }
}

extension Waypoint {
    var coord: CLLocationCoordinate2D {
        CLLocationCoordinate2D(latitude: lat, longitude: lon)
    }
}

extension CLLocationCoordinate2D {
    func distance(to other: CLLocationCoordinate2D) -> CLLocationDistance {
        let a = CLLocation(latitude: latitude, longitude: longitude)
        let b = CLLocation(latitude: other.latitude, longitude: other.longitude)
        return a.distance(from: b)
    }
}
