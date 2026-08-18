#include "FlightPlanManager.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <cmath>

namespace {

bool ReadFileUtf8(const std::wstring& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    std::string raw = ss.str();
    if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE) {
        // UTF-16LE BOM -> 转窄字符（PLN 内容基本为 ASCII）
        const wchar_t* w = reinterpret_cast<const wchar_t*>(raw.data() + 2);
        size_t n = (raw.size() - 2) / sizeof(wchar_t);
        for (size_t i = 0; i < n; ++i) out += (char)(w[i] & 0xFF);
    } else {
        if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF &&
            (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
            raw = raw.substr(3); // 去掉 UTF-8 BOM
        }
        out = std::move(raw);
    }
    return true;
}

} // namespace

bool FlightPlanManager::LoadFile(const std::wstring& path) {
    std::string xml;
    if (!ReadFileUtf8(path, xml)) return false;
    ParseXml(xml);
    return !wps_.empty();
}

bool FlightPlanManager::LoadFile(const std::string& path) {
    return LoadFile(std::wstring(path.begin(), path.end()));
}

std::string FlightPlanManager::Departure() const {
    return wps_.empty() ? "" : wps_.front().ident;
}

std::string FlightPlanManager::Destination() const {
    return wps_.empty() ? "" : wps_.back().ident;
}

std::string FlightPlanManager::Summary() const {
    std::string d = Departure(), a = Destination();
    if (d.empty() && a.empty()) return "";
    return d + " -> " + a;
}

void FlightPlanManager::ParseXml(const std::string& xml) {
    wps_.clear();

    static const std::regex reBlock("<ATCWaypoint\\s+id=\"([^\"]*)\"");
    static const std::regex reType("<ATCWaypointType>([^<]+)</ATCWaypointType>");
    static const std::regex rePos("<WorldPosition>([^<]+)</WorldPosition>");
    static const std::regex reIdent("<ICAOIdent>([^<]+)</ICAOIdent>");

    std::string::const_iterator it = xml.begin(), end = xml.end();
    int index = 0;
    for (std::sregex_iterator m(it, end, reBlock); m != std::sregex_iterator(); ++m, ++index) {
        size_t blockStart = m->position(0);
        std::string tail(xml.substr(blockStart));

        Waypoint wp;
        wp.index = index;

        std::smatch tm;
        if (std::regex_search(tail, tm, reType))
            wp.type = tm[1].str();
        if (std::regex_search(tail, tm, reIdent))
            wp.ident = tm[1].str();
        if (std::regex_search(tail, tm, rePos)) {
            double lat = 0, lon = 0;
            if (ParseWorldPosition(tm[1].str(), lat, lon)) {
                wp.lat = lat;
                wp.lon = lon;
            }
            std::string pos = tm[1].str();
            size_t sp = pos.rfind(' ');
            if (sp != std::string::npos) {
                try { wp.alt = std::stod(pos.substr(sp + 1)); } catch (...) {}
            }
        }
        wps_.push_back(std::move(wp));
    }
}

bool FlightPlanManager::ParseWorldPosition(const std::string& token, double& lat, double& lon) {
    // 支持 DMS："N40° 04' 48.00" E116° 35' 04.56" +000123.00"
    // 或小数："N40.0801 E116.5846 +000123.00"
    lat = lon = 0;
    bool haveLat = false, haveLon = false;

    static const std::regex reDMS(R"dms(([NSEW])(\d+)\s*°\s*(\d+)\s*'\s*([\d.]+)")dms");
    static const std::regex reDec(R"(([NSEW])(\d+(?:\.\d+)?))");

    std::string s = token;
    auto it = s.cbegin(), end = s.cend();
    bool dms = std::regex_search(s, reDMS);

    auto assign = [&](char h, double v) {
        if (h == 'S') v = -v;
        if (h == 'W') v = -v;
        if (h == 'N' || h == 'S') { lat = v; haveLat = true; }
        else if (h == 'E' || h == 'W') { lon = v; haveLon = true; }
    };

    if (dms) {
        for (std::sregex_iterator r(it, end, reDMS); r != std::sregex_iterator(); ++r) {
            char h = (*r)[1].str()[0];
            double deg = std::stod((*r)[2].str());
            double min = std::stod((*r)[3].str());
            double sec = std::stod((*r)[4].str());
            assign(h, deg + min / 60.0 + sec / 3600.0);
        }
    } else {
        for (std::sregex_iterator r(it, end, reDec); r != std::sregex_iterator(); ++r) {
            char h = (*r)[1].str()[0];
            assign(h, std::stod((*r)[2].str()));
        }
    }
    return haveLat && haveLon && std::fabs(lat) <= 90.0 && std::fabs(lon) <= 180.0;
}
