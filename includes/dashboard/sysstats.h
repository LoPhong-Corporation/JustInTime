//
// sysstats.h
// Cổng từ internal/sysstats/sysstats.go. Bản Go dùng gopsutil (thư viện
// đa nền tảng); bản C++ này gọi thẳng WinAPI tương đương cho từng chỉ số
// (xem chú thích tại từng hàm trong sysstats.cpp để biết API nào thay
// cho hàm gopsutil nào) - vì dự án chỉ nhắm Windows nên không cần lớp
// trừu tượng đa hệ điều hành nữa.
//
#ifndef DASHBOARD_SYSSTATS_H
#define DASHBOARD_SYSSTATS_H

#include <nlohmann/json.hpp>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace jit::dash {

struct DiskInfo { std::string device, mountpoint, fstype; double totalGB = 0; };

struct MachineInfo {
    std::string hostname, os, osVersion, architecture, processor;
    int cpuCoresPhysical = 0, cpuCoresLogical = 0;
    std::optional<int> cpuFreqMHz;
    double ramTotalGB = 0;
    std::vector<DiskInfo> disks;
    uint64_t bootTime = 0; // unix seconds
};

struct NetInterface { std::string name, ipv4; bool isUp = false; int speedMbps = 0; };

struct DiskLive { std::string mountpoint; double percent = 0, usedGB = 0, totalGB = 0; };

struct LiveStats {
    double timestamp = 0;
    double cpuPercent = 0;
    std::vector<double> cpuPerCore;
    double ramPercent = 0, ramUsedGB = 0, ramAvailableGB = 0, ramTotalGB = 0;
    double swapPercent = 0, swapUsedGB = 0, swapTotalGB = 0;
    double diskPercent = 0, diskUsedGB = 0, diskTotalGB = 0;
    std::vector<DiskLive> disks;
    double diskReadBps = 0, diskWriteBps = 0;
    double netUploadBps = 0, netDownloadBps = 0;
};

struct ProcessInfo { int32_t pid = 0; std::string name, status; double cpuPercent = 0, memPercent = 0; };

MachineInfo getMachineInfo();
std::vector<NetInterface> getNetworkInterfaces();
std::vector<ProcessInfo> getProcesses(int limit);

// Giữ trạng thái (bộ đếm/PDH query) cần cho các chỉ số kiểu "tốc độ" -
// tương đương struct Collector bên Go. KHÔNG thread-safe nội tại theo
// từng lời gọi con (xem sysstats.cpp) nên tự khoá mutex ở live().
class Collector {
public:
    Collector();
    ~Collector();
    Collector(const Collector&) = delete;

    LiveStats live();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::mutex mu_;
};

void to_json(nlohmann::json& j, const DiskInfo& d);
void to_json(nlohmann::json& j, const MachineInfo& m);
void to_json(nlohmann::json& j, const NetInterface& n);
void to_json(nlohmann::json& j, const DiskLive& d);
void to_json(nlohmann::json& j, const LiveStats& s);
void to_json(nlohmann::json& j, const ProcessInfo& p);

} // namespace jit::dash

#endif
