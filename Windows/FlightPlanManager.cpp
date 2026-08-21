#include "FlightPlanManager.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <cmath>
#include <filesystem>
#include <cstdint>
#include <cstring>

namespace {

bool ReadFileUtf8(const std::wstring& path, std::string& out) {
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    std::string raw = ss.str();
    if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE) {
        // 按字节解码 UTF-16LE，避免 wchar_t 宽度差异，也保留非 ASCII 航点名。
        auto appendUtf8 = [&](uint32_t cp) {
            if (cp <= 0x7F) out.push_back((char)cp);
            else if (cp <= 0x7FF) {
                out.push_back((char)(0xC0 | (cp >> 6)));
                out.push_back((char)(0x80 | (cp & 0x3F)));
            } else if (cp <= 0xFFFF) {
                out.push_back((char)(0xE0 | (cp >> 12)));
                out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back((char)(0x80 | (cp & 0x3F)));
            } else {
                out.push_back((char)(0xF0 | (cp >> 18)));
                out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back((char)(0x80 | (cp & 0x3F)));
            }
        };
        for (size_t i = 2; i + 1 < raw.size();) {
            uint32_t cp = (unsigned char)raw[i] | ((uint32_t)(unsigned char)raw[i + 1] << 8);
            i += 2;
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < raw.size()) {
                uint32_t low = (unsigned char)raw[i] | ((uint32_t)(unsigned char)raw[i + 1] << 8);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    i += 2;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                }
            }
            appendUtf8(cp);
        }
    } else {
        if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF &&
            (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
            raw = raw.substr(3); // 去掉 UTF-8 BOM
        }
        out = std::move(raw);
    }
    return true;
}

std::string XmlUnescape(std::string value) {
    struct Entity { const char* encoded; const char* decoded; };
    static constexpr Entity entities[] = {
        { "&amp;", "&" }, { "&quot;", "\"" }, { "&apos;", "'" },
        { "&lt;", "<" }, { "&gt;", ">" },
    };
    for (const auto& e : entities) {
        size_t pos = 0;
        while ((pos = value.find(e.encoded, pos)) != std::string::npos) {
            value.replace(pos, strlen(e.encoded), e.decoded);
            pos += strlen(e.decoded);
        }
    }
    return value;
}

std::string TagValue(const std::string& xml, const char* tag) {
    const std::regex re("<" + std::string(tag) + R"(\s*>([^<]*)</)" +
                        std::string(tag) + R"(\s*>)", std::regex_constants::icase);
    std::smatch match;
    return std::regex_search(xml, match, re) ? XmlUnescape(match[1].str()) : "";
}

std::string RunwayName(const std::string& number, const std::string& designator) {
    if (number.empty()) return "";
    std::string suffix;
    if (designator == "LEFT") suffix = "L";
    else if (designator == "RIGHT") suffix = "R";
    else if (designator == "CENTER") suffix = "C";
    else if (!designator.empty() && designator != "NONE") suffix = designator;
    return number + suffix;
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
    if (!departureId_.empty()) return departureId_;
    return wps_.empty() ? "" : wps_.front().ident;
}

std::string FlightPlanManager::Destination() const {
    if (!destinationId_.empty()) return destinationId_;
    return wps_.empty() ? "" : wps_.back().ident;
}

void FlightPlanManager::Clear() {
    wps_.clear();
    departureId_.clear();
    destinationId_.clear();
    departureRunway_.clear();
    departureProcedure_.clear();
    arrivalProcedure_.clear();
    approachType_.clear();
    destinationRunway_.clear();
    cruisingAltitude_ = 0;
}

std::string FlightPlanManager::Summary() const {
    std::string d = Departure(), a = Destination();
    if (d.empty() && a.empty()) return "";
    return d + " -> " + a;
}

void FlightPlanManager::ParseXml(const std::string& xml) {
    Clear();
    departureId_ = TagValue(xml, "DepartureID");
    destinationId_ = TagValue(xml, "DestinationID");
    departureRunway_ = TagValue(xml, "DeparturePosition");
    try {
        const std::string altitude = TagValue(xml, "CruisingAlt");
        if (!altitude.empty()) cruisingAltitude_ = std::stod(altitude);
    } catch (...) {}

    static const std::regex reBlock(
        R"(<ATCWaypoint\b([^>]*)>([\s\S]*?)</ATCWaypoint\s*>)",
        std::regex_constants::icase);
    static const std::regex reId(R"re(\bid\s*=\s*["']([^"']*)["'])re",
                                 std::regex_constants::icase);
    static const std::regex reType(R"(<ATCWaypointType\s*>([^<]+)</ATCWaypointType\s*>)",
                                   std::regex_constants::icase);
    static const std::regex rePos(R"(<WorldPosition\s*>([^<]+)</WorldPosition\s*>)",
                                  std::regex_constants::icase);
    static const std::regex reIdent(R"(<ICAOIdent\s*>([^<]+)</ICAOIdent\s*>)",
                                    std::regex_constants::icase);
    static const std::regex reAltitude(R"(([+-]\d+(?:\.\d+)?)\s*$)");

    int index = 0;
    std::string firstRunway;
    std::string lastRunway;
    for (std::sregex_iterator m(xml.begin(), xml.end(), reBlock);
         m != std::sregex_iterator(); ++m) {
        const std::string attributes = (*m)[1].str();
        const std::string block = (*m)[2].str();

        const std::string departureProcedure = TagValue(block, "DepartureFP");
        const std::string arrivalProcedure = TagValue(block, "ArrivalFP");
        const std::string approachType = TagValue(block, "ApproachTypeFP");
        const std::string runway = RunwayName(TagValue(block, "RunwayNumberFP"),
                                              TagValue(block, "RunwayDesignatorFP"));
        if (!departureProcedure.empty()) departureProcedure_ = departureProcedure;
        if (!arrivalProcedure.empty()) arrivalProcedure_ = arrivalProcedure;
        if (!approachType.empty()) approachType_ = approachType;
        if (!runway.empty()) {
            if (firstRunway.empty()) firstRunway = runway;
            lastRunway = runway;
            if (!departureProcedure.empty()) departureRunway_ = runway;
            if (!arrivalProcedure.empty() || !approachType.empty()) destinationRunway_ = runway;
        }

        Waypoint wp;
        wp.index = index;

        std::smatch tm;
        if (std::regex_search(attributes, tm, reId))
            wp.ident = XmlUnescape(tm[1].str());
        if (std::regex_search(block, tm, reType))
            wp.type = XmlUnescape(tm[1].str());
        // ICAOIdent 比 id 更权威；自定义航点通常只有 id。
        if (std::regex_search(block, tm, reIdent))
            wp.ident = XmlUnescape(tm[1].str());

        bool havePosition = false;
        if (std::regex_search(block, tm, rePos)) {
            double lat = 0, lon = 0;
            if (ParseWorldPosition(tm[1].str(), lat, lon)) {
                wp.lat = lat;
                wp.lon = lon;
                havePosition = true;
            }
            std::string pos = tm[1].str();
            std::smatch am;
            if (std::regex_search(pos, am, reAltitude)) {
                try { wp.alt = std::stod(am[1].str()); } catch (...) {}
            }
        }
        // 无坐标节点不能安全绘制；跳过并保持连续索引。
        if (havePosition) {
            if (wp.ident.empty()) wp.ident = "WP" + std::to_string(index + 1);
            wp.index = index++;
            wps_.push_back(std::move(wp));
        }
    }
    if (departureRunway_.empty()) departureRunway_ = firstRunway;
    if (destinationRunway_.empty() && lastRunway != firstRunway)
        destinationRunway_ = lastRunway;
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
