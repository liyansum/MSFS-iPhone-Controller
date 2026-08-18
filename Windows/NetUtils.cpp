#include "NetUtils.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <algorithm>
#include <vector>

#pragma comment(lib, "Iphlpapi.lib")

namespace {

bool IsVirtualAdapter(const std::wstring& desc) {
    static const wchar_t* markers[] = {
        L"virtualbox", L"vmware", L"hyper-v", L"wsl", L"vEthernet",
        L"tailscale", L"docker", L"hamachi", L"zerotier", L"tap-",
        L"wireguard", L"tunnel", L"loopback", L"ndis", L"virtual",
        L"toad", L"kryptonet",
    };
    std::wstring d = desc;
    for (auto& c : d) c = (wchar_t)towlower(c);
    for (const wchar_t* m : markers) {
        if (d.find(m) != std::wstring::npos) return true;
    }
    return false;
}

} // namespace

std::vector<NetAddrInfo> GetLocalIpv4Addresses() {
    std::vector<NetAddrInfo> out;
    ULONG size = 0;
    GetAdaptersAddresses(AF_INET,
                         GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                         nullptr, nullptr, &size);
    if (size == 0) return out;
    std::vector<BYTE> buf(size);
    PIP_ADAPTER_ADDRESSES p = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
    ULONG hr = GetAdaptersAddresses(AF_INET,
                                    GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                    nullptr, p, &size);
    if (hr != NO_ERROR) return out;

    for (PIP_ADAPTER_ADDRESSES a = p; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

        bool virt = a->Description ? IsVirtualAdapter(a->Description) : false;
        bool gw = (a->FirstGatewayAddress != nullptr && a->FirstGatewayAddress->Address.lpSockaddr != nullptr);

        for (PIP_ADAPTER_UNICAST_ADDRESS u = a->FirstUnicastAddress; u; u = u->Next) {
            sockaddr_in* sa = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
            if (!sa || sa->sin_family != AF_INET) continue;
            char ip[INET_ADDRSTRLEN] = { 0 };
            inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));

            std::string name;
            if (a->FriendlyName) {
                int n = WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1, nullptr, 0, nullptr, nullptr);
                if (n > 0) {
                    name.resize(n);
                    WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1, name.data(), n, nullptr, nullptr);
                    name.resize(n - 1);
                }
            }
            out.push_back({ ip, name, virt, gw });
        }
    }
    return out;
}

std::string RecommendLocalIp() {
    std::vector<NetAddrInfo> list = GetLocalIpv4Addresses();
    for (const auto& a : list)
        if (!a.virtualAdapter && a.hasGateway) return a.ip;
    for (const auto& a : list)
        if (!a.virtualAdapter) return a.ip;
    if (!list.empty()) return list.front().ip;
    return "127.0.0.1";
}

std::vector<std::string> LocalIpList() {
    std::vector<std::string> ips;
    std::vector<NetAddrInfo> addresses = GetLocalIpv4Addresses();
    std::stable_sort(addresses.begin(), addresses.end(), [](const NetAddrInfo& a, const NetAddrInfo& b) {
        auto rank = [](const NetAddrInfo& item) {
            if (!item.virtualAdapter && item.hasGateway) return 0;
            if (!item.virtualAdapter) return 1;
            return 2;
        };
        return rank(a) < rank(b);
    });
    for (const auto& a : addresses) {
        if (std::find(ips.begin(), ips.end(), a.ip) == ips.end()) ips.push_back(a.ip);
    }
    return ips;
}
