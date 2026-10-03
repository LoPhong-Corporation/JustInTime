#include "sysstats.h"
#include <cmath>
#include <ctime>
namespace jit::dash {
MachineInfo getMachineInfo()
{
    MachineInfo m;
    m.hostname = "TEST-PC"; m.os = "Windows 11 Pro"; m.osVersion = "26100"; m.architecture = "x86_64";
    m.processor = "Intel(R) Core(TM) i7-12700K CPU @ 3.60GHz";
    m.cpuCoresPhysical = 12; m.cpuCoresLogical = 20; m.cpuFreqMHz = 3600;
    m.ramTotalGB = 32.0;
    m.disks = {{"C:", "C:\\", "NTFS", 476.9}, {"D:", "D:\\", "NTFS", 931.5}};
    m.bootTime = static_cast<uint64_t>(time(nullptr)) - 3600 * 5;
    return m;
}
std::vector<NetInterface> getNetworkInterfaces()
{
    return {{"Ethernet", "192.168.1.42", true, 0}, {"Wi-Fi", "N/A", false, 0}};
}
std::vector<ProcessInfo> getProcesses(int limit)
{
    std::vector<ProcessInfo> out = {
        {1234, "chrome.exe", "running", 18.4, 12.1}, {5678, "code.exe", "running", 9.2, 7.5},
        {9012, "Discord.exe", "running", 3.1, 2.8}, {3456, "explorer.exe", "running", 1.2, 3.3},
        {7890, "dashboard.exe", "running", 0.8, 1.1},
    };
    if (static_cast<int>(out.size()) > limit) out.resize(static_cast<size_t>(limit));
    return out;
}
struct Collector::Impl { double t = 0; };
Collector::Collector() : impl_(std::make_unique<Impl>()) {}
Collector::~Collector() = default;
LiveStats Collector::live()
{
    std::lock_guard<std::mutex> lock(mu_);
    impl_->t += 0.3;
    LiveStats s;
    s.timestamp = time(nullptr);
    s.cpuPercent = 40 + 25 * std::sin(impl_->t);
    for (int i = 0; i < 20; i++) s.cpuPerCore.push_back(30 + 40 * std::fabs(std::sin(impl_->t + i)));
    s.ramPercent = 55 + 10 * std::sin(impl_->t * 0.5);
    s.ramUsedGB = 32.0 * s.ramPercent / 100.0; s.ramAvailableGB = 32.0 - s.ramUsedGB; s.ramTotalGB = 32.0;
    s.swapPercent = 12; s.swapUsedGB = 0.6; s.swapTotalGB = 5.0;
    s.diskPercent = 97.1; s.diskUsedGB = 463.0; s.diskTotalGB = 476.9;
    s.disks = {{"C:\\", 97.1, 463.0, 476.9}, {"D:\\", 41.2, 384.0, 931.5}};
    s.diskReadBps = 1200000 + 900000 * std::fabs(std::sin(impl_->t * 2));
    s.diskWriteBps = 300000;
    s.netUploadBps = 45000 + 20000 * std::fabs(std::sin(impl_->t * 3));
    s.netDownloadBps = 820000 + 400000 * std::fabs(std::cos(impl_->t * 2));
    return s;
}
}
