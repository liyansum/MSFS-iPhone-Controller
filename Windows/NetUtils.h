#pragma once
// 本地 IPv4 网卡枚举（GetAdaptersAddresses），供窗口显示与自动探测应答共用。

#include <string>
#include <vector>

struct NetAddrInfo {
    std::string ip;
    std::string adapter;     // 友好名
    bool virtualAdapter = false;
    bool hasGateway = false;
};

// 枚举所有启用的 IPv4 地址
std::vector<NetAddrInfo> GetLocalIpv4Addresses();

// 推荐 IP：优先“非虚拟 + 有网关”的真实网卡
std::string RecommendLocalIp();

// 全部 IPv4 地址（字符串列表，用于自动探测应答）
std::vector<std::string> LocalIpList();
