//
// sysstats.cpp
//
// Mỗi hàm ghi chú API gopsutil (Go) tương ứng đã được thay bằng API
// WinAPI nào - để tiện đối chiếu khi cần so sánh hành vi với bản gốc.
//
#include "sysstats.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "strutil.h" // sau winsock2.h: strutil.h kéo theo <windows.h> nên phải để winsock2.h đi trước
#include <iphlpapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <winternl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <vector>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ntdll.lib")

namespace jit::dash {

namespace {

constexpr double kGB = 1024.0 * 1024.0 * 1024.0;

double round1(double v) { return std::floor(v * 10.0 + 0.5) / 10.0; }
double round2(double v) { return std::floor(v * 100.0 + 0.5) / 100.0; }

// registry helper: HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion - nguồn
// đáng tin cậy nhất cho tên/phiên bản OS mà không cần manifest đặc biệt
// (GetVersionEx bị Windows "giả vờ" trả về Windows 8 nếu thiếu manifest).
std::string regReadString(HKEY root, const wchar_t* subkey, const wchar_t* value)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return "";

    wchar_t buf[256] = {0};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    std::string out;
    if (RegQueryValueExW(key, value, nullptr, &type, reinterpret_cast<BYTE*>(buf), &size) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ))
    {
        out = jit::wideToUtf8(buf);
    }
    RegCloseKey(key);
    return out;
}

DWORD regReadDword(HKEY root, const wchar_t* subkey, const wchar_t* value, DWORD fallback)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return fallback;

    DWORD out = fallback, size = sizeof(out), type = 0;
    if (RegQueryValueExW(key, value, nullptr, &type, reinterpret_cast<BYTE*>(&out), &size) != ERROR_SUCCESS ||
        type != REG_DWORD)
        out = fallback;
    RegCloseKey(key);
    return out;
}

} // namespace

// ---------------------------------------------------------------- Machine --

// gopsutil host.Info() -> registry CurrentVersion + GetComputerNameExW.
// gopsutil cpu.Counts()/cpu.Info() -> GetLogicalProcessorInformationEx +
// registry CentralProcessor\0 (tên CPU, tần số).
// gopsutil mem.VirtualMemory().Total -> GlobalMemoryStatusEx.
// gopsutil disk.Partitions()+Usage() -> GetLogicalDrives + GetDiskFreeSpaceExW.
MachineInfo getMachineInfo()
{
    MachineInfo info;

    wchar_t hostBuf[256] = {0};
    DWORD hostLen = 256;
    if (GetComputerNameExW(ComputerNamePhysicalDnsHostname, hostBuf, &hostLen))
        info.hostname = jit::wideToUtf8(hostBuf);

    const wchar_t* kCurVer = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    const std::string productName = regReadString(HKEY_LOCAL_MACHINE, kCurVer, L"ProductName");
    const std::string displayVersion = regReadString(HKEY_LOCAL_MACHINE, kCurVer, L"DisplayVersion");
    const DWORD buildNumber = regReadDword(HKEY_LOCAL_MACHINE, kCurVer, L"CurrentBuildNumber", 0) != 0
        ? 0 // CurrentBuildNumber is REG_SZ, read separately below
        : 0;
    (void)buildNumber;
    const std::string buildStr = regReadString(HKEY_LOCAL_MACHINE, kCurVer, L"CurrentBuildNumber");
    info.os = productName.empty() ? "Windows" : productName;
    if (!displayVersion.empty())
        info.os += " " + displayVersion;
    info.osVersion = buildStr;

    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture)
    {
        case PROCESSOR_ARCHITECTURE_AMD64: info.architecture = "x86_64"; break;
        case PROCESSOR_ARCHITECTURE_ARM64: info.architecture = "arm64"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: info.architecture = "x86"; break;
        default: info.architecture = "unknown";
    }
    info.cpuCoresLogical = static_cast<int>(si.dwNumberOfProcessors);

    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    if (len > 0)
    {
        std::vector<BYTE> buf(len);
        if (GetLogicalProcessorInformationEx(RelationProcessorCore,
                reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data()), &len))
        {
            int physical = 0;
            size_t offset = 0;
            while (offset < len)
            {
                auto* p = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data() + offset);
                if (p->Relationship == RelationProcessorCore)
                    physical++;
                offset += p->Size;
            }
            info.cpuCoresPhysical = physical;
        }
    }
    if (info.cpuCoresPhysical == 0)
        info.cpuCoresPhysical = info.cpuCoresLogical; // fallback hợp lý nếu API trên thất bại

    info.processor = regReadString(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
    if (info.processor.empty())
        info.processor = "N/A";
    else
    {
        // chuỗi registry hay có khoảng trắng thừa ở đầu/cuối
        while (!info.processor.empty() && info.processor.front() == ' ') info.processor.erase(0, 1);
        while (!info.processor.empty() && info.processor.back() == ' ') info.processor.pop_back();
    }
    const DWORD mhz = regReadDword(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz", 0);
    if (mhz > 0)
        info.cpuFreqMHz = static_cast<int>(mhz);

    MEMORYSTATUSEX mem = {sizeof(mem)};
    if (GlobalMemoryStatusEx(&mem))
        info.ramTotalGB = round1(static_cast<double>(mem.ullTotalPhys) / kGB);

    const DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; i++)
    {
        if (!(drives & (1u << i)))
            continue;
        wchar_t root[4] = {static_cast<wchar_t>(L'A' + i), L':', L'\\', 0};
        if (GetDriveTypeW(root) != DRIVE_FIXED)
            continue;

        ULARGE_INTEGER totalBytes{};
        if (!GetDiskFreeSpaceExW(root, nullptr, &totalBytes, nullptr))
            continue;

        wchar_t fsName[32] = {0};
        GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr, nullptr, fsName, 32);

        DiskInfo d;
        d.device = jit::wideToUtf8(std::wstring(root, 2)); // "C:"
        d.mountpoint = jit::wideToUtf8(root);               // "C:\"
        d.fstype = jit::wideToUtf8(fsName);
        d.totalGB = round1(static_cast<double>(totalBytes.QuadPart) / kGB);
        info.disks.push_back(std::move(d));
    }

    ULONGLONG uptimeMs = GetTickCount64();
    info.bootTime = static_cast<uint64_t>(time(nullptr)) - uptimeMs / 1000;

    return info;
}

// gopsutil net.Interfaces() -> GetAdaptersAddresses (địa chỉ IPv4 + trạng
// thái lên/xuống của từng card mạng).
std::vector<NetInterface> getNetworkInterfaces()
{
    std::vector<NetInterface> out;

    ULONG size = 0;
    GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, nullptr, &size);
    if (size == 0)
        return out;

    std::vector<BYTE> buf(size);
    auto* addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, addrs, &size) != NO_ERROR)
        return out;

    for (IP_ADAPTER_ADDRESSES* a = addrs; a; a = a->Next)
    {
        NetInterface iface;
        iface.name = a->AdapterName ? a->AdapterName : "";
        // dashboard.js hiển thị tên card cho người dùng đọc - FriendlyName dễ
        // hiểu hơn GUID của AdapterName.
        if (a->FriendlyName)
            iface.name = jit::wideToUtf8(a->FriendlyName);

        iface.ipv4 = "N/A";
        for (IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next)
        {
            if (u->Address.lpSockaddr->sa_family == AF_INET)
            {
                char ipBuf[INET_ADDRSTRLEN] = {0};
                auto* sin = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
                inet_ntop(AF_INET, &sin->sin_addr, ipBuf, sizeof(ipBuf));
                iface.ipv4 = ipBuf;
                break;
            }
        }

        iface.isUp = (a->OperStatus == IfOperStatusUp);
        // Không có API "chuẩn" đa dụng cho tốc độ link ở đây - giữ 0 ("N/A"
        // ở FE) giống hệt gopsutil bản Go, thay vì đoán mò.
        out.push_back(std::move(iface));
    }

    return out;
}

// ------------------------------------------------------------- Processes --

// gopsutil process.Pids()/Name()/CPUPercent()/MemoryPercent()/Status() ->
// CreateToolhelp32Snapshot cho danh sách PID/tên, GetProcessTimes (chênh
// lệch giữa 2 lần gọi, lưu trạng thái tĩnh giữa các lần - CÙNG cách
// gopsutil tự làm nội bộ) cho %CPU, GetProcessMemoryInfo cho %RAM.
std::vector<ProcessInfo> getProcesses(int limit)
{
    struct PrevTimes { ULONGLONG kernel = 0, user = 0; };
    // Trạng thái tĩnh liên-lần-gọi (như gopsutil giữ trong *Process nội bộ) -
    // lần gọi ĐẦU sẽ báo ~0% cho mọi process, tự sửa đúng từ lần gọi kế tiếp,
    // giống hệt docstring GetProcesses() bên Go.
    static std::map<DWORD, PrevTimes> s_prev;
    static ULONGLONG s_prevWall = 0;

    std::vector<ProcessInfo> out;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return out;

    MEMORYSTATUSEX mem = {sizeof(mem)};
    GlobalMemoryStatusEx(&mem);

    FILETIME nowFt;
    GetSystemTimeAsFileTime(&nowFt);
    const ULONGLONG nowWall = (static_cast<ULONGLONG>(nowFt.dwHighDateTime) << 32) | nowFt.dwLowDateTime;
    const double elapsedSec = s_prevWall ? static_cast<double>(nowWall - s_prevWall) / 1e7 : 0;

    PROCESSENTRY32W pe = {sizeof(pe)};
    std::map<DWORD, PrevTimes> curTimes;

    if (Process32FirstW(snap, &pe))
    {
        do
        {
            const DWORD pid = pe.th32ProcessID;
            if (pid == 0)
                continue;

            ProcessInfo p;
            p.pid = static_cast<int32_t>(pid);
            p.name = jit::wideToUtf8(pe.szExeFile);
            p.status = "running"; // Windows không có khái niệm sleeping/zombie như POSIX

            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (h)
            {
                FILETIME create, exit, kernel, user;
                if (GetProcessTimes(h, &create, &exit, &kernel, &user))
                {
                    const ULONGLONG k = (static_cast<ULONGLONG>(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime;
                    const ULONGLONG u = (static_cast<ULONGLONG>(user.dwHighDateTime) << 32) | user.dwLowDateTime;
                    curTimes[pid] = {k, u};

                    auto it = s_prev.find(pid);
                    if (it != s_prev.end() && elapsedSec > 0)
                    {
                        const double usedSec = static_cast<double>((k - it->second.kernel) + (u - it->second.user)) / 1e7;
                        p.cpuPercent = round1(std::max(0.0, usedSec / elapsedSec * 100.0));
                    }
                }

                PROCESS_MEMORY_COUNTERS pmc;
                if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc)) && mem.ullTotalPhys > 0)
                    p.memPercent = round1(static_cast<double>(pmc.WorkingSetSize) / static_cast<double>(mem.ullTotalPhys) * 100.0);

                CloseHandle(h);
            }

            out.push_back(std::move(p));
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    s_prev = std::move(curTimes);
    s_prevWall = nowWall;

    std::sort(out.begin(), out.end(), [](const ProcessInfo& a, const ProcessInfo& b) { return a.cpuPercent > b.cpuPercent; });
    if (static_cast<int>(out.size()) > limit)
        out.resize(static_cast<size_t>(limit));
    return out;
}

// -------------------------------------------------------------- Collector --

struct Collector::Impl {
    // ---- CPU: NtQuerySystemInformation(SystemProcessorPerformanceInformation) ----
    // gopsutil cpu.Percent() làm việc này nội bộ bằng /proc (Linux) hay
    // NtQuerySystemInformation (Windows) - ở đây gọi thẳng, giữ mẫu trước để
    // tính chênh lệch, y hệt cách gopsutil tự làm.
    std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> prevCpu;

    // ---- Disk I/O throughput: PDH (Performance Data Helper) ----
    // gopsutil disk.IOCounters() đọc bộ đếm luỹ kế của hệ điều hành rồi TỰ
    // trừ giữa 2 lần gọi ở phía server.go; PDH counter kiểu "Bytes/sec" đã
    // tự làm phép trừ đó bên trong (cần đúng 1 HQUERY sống suốt vòng đời
    // Collector, không mở lại mỗi lần gọi).
    PDH_HQUERY pdhQuery = nullptr;
    PDH_HCOUNTER pdhDiskRead = nullptr, pdhDiskWrite = nullptr;
    bool pdhFirstSampleDone = false;

    // ---- Network throughput: GetIfTable2 (bộ đếm luỹ kế) + tự trừ ----
    ULONGLONG lastNetSent = 0, lastNetRecv = 0;
    ULONGLONG lastNetTimeMs = 0;

    Impl()
    {
        if (PdhOpenQueryW(nullptr, 0, &pdhQuery) == ERROR_SUCCESS)
        {
            PdhAddEnglishCounterW(pdhQuery, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", 0, &pdhDiskRead);
            PdhAddEnglishCounterW(pdhQuery, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", 0, &pdhDiskWrite);
        }
    }

    ~Impl()
    {
        if (pdhQuery)
            PdhCloseQuery(pdhQuery);
    }
};

Collector::Collector() : impl_(std::make_unique<Impl>()) {}
Collector::~Collector() = default;

LiveStats Collector::live()
{
    std::lock_guard<std::mutex> lock(mu_);
    LiveStats s;

    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    const ULONGLONG now100ns = (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    s.timestamp = static_cast<double>(now100ns) / 1e7 - 11644473600.0; // FILETIME epoch -> Unix epoch

    // ---- CPU ----
    ULONG len = 0;
    NtQuerySystemInformation(SystemProcessorPerformanceInformation, nullptr, 0, &len);
    if (len > 0)
    {
        std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> cur(len / sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION));
        if (NtQuerySystemInformation(SystemProcessorPerformanceInformation, cur.data(), len, &len) == 0 /*STATUS_SUCCESS*/)
        {
            if (impl_->prevCpu.size() == cur.size())
            {
                double sumBusyPct = 0;
                for (size_t i = 0; i < cur.size(); i++)
                {
                    const auto& p = cur[i];
                    const auto& q = impl_->prevCpu[i];
                    const long long idle = p.IdleTime.QuadPart - q.IdleTime.QuadPart;
                    const long long kernel = p.KernelTime.QuadPart - q.KernelTime.QuadPart; // đã BAO GỒM idle, như NT báo cáo
                    const long long user = p.UserTime.QuadPart - q.UserTime.QuadPart;
                    const long long total = kernel + user;
                    const long long busy = total - idle;
                    const double pct = total > 0 ? round1(std::clamp(static_cast<double>(busy) / static_cast<double>(total) * 100.0, 0.0, 100.0)) : 0.0;
                    s.cpuPerCore.push_back(pct);
                    sumBusyPct += pct;
                }
                s.cpuPercent = s.cpuPerCore.empty() ? 0.0 : round1(sumBusyPct / static_cast<double>(s.cpuPerCore.size()));
            }
            impl_->prevCpu = std::move(cur);
        }
    }

    // ---- RAM ----
    MEMORYSTATUSEX mem = {sizeof(mem)};
    if (GlobalMemoryStatusEx(&mem))
    {
        s.ramPercent = round1(static_cast<double>(mem.dwMemoryLoad));
        const double used = static_cast<double>(mem.ullTotalPhys - mem.ullAvailPhys) / kGB;
        s.ramUsedGB = round2(used);
        s.ramAvailableGB = round2(static_cast<double>(mem.ullAvailPhys) / kGB);
        s.ramTotalGB = round2(static_cast<double>(mem.ullTotalPhys) / kGB);
    }

    // ---- Swap (xấp xỉ, thuật toán giống psutil trên Windows: dùng phần
    // commit vượt quá RAM vật lý làm ước lượng swap - Windows không tách
    // riêng "swap" như Linux/macOS) ----
    PERFORMANCE_INFORMATION perf = {sizeof(perf)};
    if (GetPerformanceInfo(&perf, sizeof(perf)) && mem.ullTotalPhys > 0)
    {
        const double pageSize = static_cast<double>(perf.PageSize);
        const double commitTotal = static_cast<double>(perf.CommitTotal) * pageSize;
        const double commitLimit = static_cast<double>(perf.CommitLimit) * pageSize;
        const double physTotal = static_cast<double>(mem.ullTotalPhys);

        const double swapTotal = std::max(0.0, commitLimit - physTotal);
        const double swapUsed = std::clamp(commitTotal - physTotal, 0.0, swapTotal);

        s.swapTotalGB = round2(swapTotal / kGB);
        s.swapUsedGB = round2(swapUsed / kGB);
        s.swapPercent = swapTotal > 0 ? round1(swapUsed / swapTotal * 100.0) : 0.0;
    }

    // ---- Disk usage per-partition ----
    const DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; i++)
    {
        if (!(drives & (1u << i)))
            continue;
        wchar_t root[4] = {static_cast<wchar_t>(L'A' + i), L':', L'\\', 0};
        if (GetDriveTypeW(root) != DRIVE_FIXED)
            continue;

        ULARGE_INTEGER freeBytes{}, totalBytes{};
        if (!GetDiskFreeSpaceExW(root, nullptr, &totalBytes, &freeBytes))
            continue;

        DiskLive d;
        d.mountpoint = jit::wideToUtf8(root);
        d.totalGB = round1(static_cast<double>(totalBytes.QuadPart) / kGB);
        const double usedBytes = static_cast<double>(totalBytes.QuadPart - freeBytes.QuadPart);
        d.usedGB = round1(usedBytes / kGB);
        d.percent = totalBytes.QuadPart > 0 ? round1(usedBytes / static_cast<double>(totalBytes.QuadPart) * 100.0) : 0.0;
        s.disks.push_back(d);
    }
    if (!s.disks.empty())
    {
        s.diskPercent = s.disks[0].percent;
        s.diskUsedGB = s.disks[0].usedGB;
        s.diskTotalGB = s.disks[0].totalGB;
    }

    // ---- Disk I/O throughput (PDH) ----
    if (impl_->pdhQuery)
    {
        PdhCollectQueryData(impl_->pdhQuery);
        if (impl_->pdhFirstSampleDone) // PDH cần 2 mẫu mới có giá trị rate hợp lệ
        {
            PDH_FMT_COUNTERVALUE val;
            if (impl_->pdhDiskRead && PdhGetFormattedCounterValue(impl_->pdhDiskRead, PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS)
                s.diskReadBps = round1(val.doubleValue);
            if (impl_->pdhDiskWrite && PdhGetFormattedCounterValue(impl_->pdhDiskWrite, PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS)
                s.diskWriteBps = round1(val.doubleValue);
        }
        impl_->pdhFirstSampleDone = true;
    }

    // ---- Network throughput (tự trừ giữa 2 lần gọi, như Go bản gốc) ----
    MIB_IF_TABLE2* table = nullptr;
    if (GetIfTable2(&table) == NO_ERROR && table)
    {
        ULONGLONG sent = 0, recv = 0;
        for (ULONG i = 0; i < table->NumEntries; i++)
        {
            const MIB_IF_ROW2& row = table->Table[i];
            // Bỏ qua interface ảo/loopback để không cộng trùng traffic nội bộ,
            // giống lọc "is_up" (không hoàn toàn giống gopsutil - gopsutil chỉ
            // lấy counters[0] - nhưng cộng dồn mọi NIC thật là mô tả đúng hơn
            // tổng băng thông máy đang dùng).
            if (row.OperStatus != IfOperStatusUp || row.Type == IF_TYPE_SOFTWARE_LOOPBACK)
                continue;
            sent += row.OutOctets;
            recv += row.InOctets;
        }
        FreeMibTable(table);

        const ULONGLONG nowMs = GetTickCount64();
        if (impl_->lastNetTimeMs != 0)
        {
            const double elapsed = static_cast<double>(nowMs - impl_->lastNetTimeMs) / 1000.0;
            if (elapsed > 0)
            {
                s.netUploadBps = round1(static_cast<double>(sent - impl_->lastNetSent) / elapsed);
                s.netDownloadBps = round1(static_cast<double>(recv - impl_->lastNetRecv) / elapsed);
            }
        }
        impl_->lastNetSent = sent;
        impl_->lastNetRecv = recv;
        impl_->lastNetTimeMs = nowMs;
    }

    return s;
}

// ------------------------------------------------------------------ JSON --

void to_json(nlohmann::json& j, const DiskInfo& d)
{
    j = {{"device", d.device}, {"mountpoint", d.mountpoint}, {"fstype", d.fstype}, {"total_gb", d.totalGB}};
}

void to_json(nlohmann::json& j, const MachineInfo& m)
{
    j = {
        {"hostname", m.hostname}, {"os", m.os}, {"os_version", m.osVersion}, {"architecture", m.architecture},
        {"processor", m.processor}, {"cpu_cores_physical", m.cpuCoresPhysical}, {"cpu_cores_logical", m.cpuCoresLogical},
        {"cpu_freq_mhz", m.cpuFreqMHz ? nlohmann::json(*m.cpuFreqMHz) : nlohmann::json(nullptr)},
        {"cpu_freq_min_mhz", nullptr}, {"cpu_freq_max_mhz", nullptr}, // không có nguồn đáng tin cậy - Go cũng luôn để trống
        {"ram_total_gb", m.ramTotalGB}, {"disks", m.disks}, {"boot_time", m.bootTime},
    };
}

void to_json(nlohmann::json& j, const NetInterface& n)
{
    j = {{"name", n.name}, {"ipv4", n.ipv4}, {"is_up", n.isUp}, {"speed_mbps", n.speedMbps}};
}

void to_json(nlohmann::json& j, const DiskLive& d)
{
    j = {{"mountpoint", d.mountpoint}, {"percent", d.percent}, {"used_gb", d.usedGB}, {"total_gb", d.totalGB}};
}

void to_json(nlohmann::json& j, const LiveStats& s)
{
    j = {
        {"timestamp", s.timestamp}, {"cpu_percent", s.cpuPercent}, {"cpu_per_core", s.cpuPerCore},
        {"ram_percent", s.ramPercent}, {"ram_used_gb", s.ramUsedGB}, {"ram_available_gb", s.ramAvailableGB}, {"ram_total_gb", s.ramTotalGB},
        {"swap_percent", s.swapPercent}, {"swap_used_gb", s.swapUsedGB}, {"swap_total_gb", s.swapTotalGB},
        {"disk_percent", s.diskPercent}, {"disk_used_gb", s.diskUsedGB}, {"disk_total_gb", s.diskTotalGB}, {"disks", s.disks},
        {"disk_read_bps", s.diskReadBps}, {"disk_write_bps", s.diskWriteBps},
        {"net_upload_bps", s.netUploadBps}, {"net_download_bps", s.netDownloadBps},
    };
}

void to_json(nlohmann::json& j, const ProcessInfo& p)
{
    j = {{"pid", p.pid}, {"name", p.name}, {"cpu_percent", p.cpuPercent}, {"mem_percent", p.memPercent}, {"status", p.status}};
}

} // namespace jit::dash
