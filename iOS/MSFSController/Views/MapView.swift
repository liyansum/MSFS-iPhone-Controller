import SwiftUI
import MapKit

// 地图页：当前位置 / 实际航迹 / 计划航线 / 航点。只读。

struct MapView: View {
    @EnvironmentObject var conn: ConnectionManager

    @State private var camera: MapCameraPosition = .automatic
    @State private var track: [CLLocationCoordinate2D] = []
    @State private var lastSample: (coord: CLLocationCoordinate2D, date: Date)?
    @State private var autoFollow = true

    var body: some View {
        VStack(spacing: 0) {
            header

            Map(position: $camera) {
                Annotation("", coordinate: aircraftCoord) {
                    AircraftMarker(heading: conn.aircraft.heading)
                }
                .annotationTitles(.hidden)

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
                        Text(wp.ident)
                            .font(.caption2.bold())
                            .padding(.horizontal, 6)
                            .padding(.vertical, 3)
                            .background(Capsule().fill(Color.white))
                            .overlay(Capsule().stroke(Color.blue, lineWidth: 1))
                    }
                }
            }
            .mapStyle(.standard(elevation: .flat))
            .onChange(of: conn.aircraft) { _, _ in sampleTrack() }
            .onAppear { sampleTrack(); follow() }

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
                    track.removeAll()
                    lastSample = nil
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

    private func sampleTrack() {
        let a = conn.aircraft
        guard a.lat != 0 || a.lon != 0 else { return }
        let coord = CLLocationCoordinate2D(latitude: a.lat, longitude: a.lon)
        let now = Date()
        if let last = lastSample {
            let dist = last.coord.distance(to: coord)
            if now.timeIntervalSince(last.date) < 1 && dist < 50 { return }
        }
        lastSample = (coord, now)
        track.append(coord)
        if track.count > 4000 { track.removeFirst(track.count - 4000) }
        if autoFollow { follow() }
    }

    private func follow() {
        let a = conn.aircraft
        guard a.lat != 0 || a.lon != 0 else { return }
        let center = CLLocationCoordinate2D(latitude: a.lat, longitude: a.lon)
        withAnimation(.easeInOut(duration: 0.5)) {
            camera = .region(MKCoordinateRegion(center: center,
                                                latitudinalMeters: 4000,
                                                longitudinalMeters: 4000))
        }
    }

    // MARK: - 坐标读数

    private var readoutBar: some View {
        HStack(spacing: 12) {
            Text(String(format: "LAT %.6f", conn.aircraft.lat))
            Text(String(format: "LON %.6f", conn.aircraft.lon))
            Text(String(format: "HDG %.0f°", conn.aircraft.heading))
            Text(String(format: "ALT %.0f ft", conn.aircraft.altitude))
        }
        .font(.caption.monospacedDigit())
        .frame(maxWidth: .infinity)
        .padding(.vertical, 6)
        .background(Color(.systemGray6))
    }
}

// 飞机 Marker：按真实航向旋转
struct AircraftMarker: View {
    let heading: Double

    var body: some View {
        Image(systemName: "paperplane.fill")
            .font(.title2)
            .foregroundColor(.blue)
            .background(Circle().fill(Color.white).frame(width: 34, height: 34))
            .rotationEffect(.degrees(heading))
            .shadow(radius: 2)
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
