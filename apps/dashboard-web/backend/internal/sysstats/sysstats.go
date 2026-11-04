// Package sysstats is a Go port of system_stats.py: read-only machine
// and live performance stats (CPU/RAM/Disk/Network) using gopsutil,
// mirroring the psutil-based Python implementation field for field so
// the existing dashboard.js (reused as-is) keeps working unmodified.
package sysstats

import (
	"sort"
	"sync"
	"time"

	cpuutil "github.com/shirou/gopsutil/v3/cpu"
	diskutil "github.com/shirou/gopsutil/v3/disk"
	hostutil "github.com/shirou/gopsutil/v3/host"
	memutil "github.com/shirou/gopsutil/v3/mem"
	netutil "github.com/shirou/gopsutil/v3/net"
	processutil "github.com/shirou/gopsutil/v3/process"
)

const gb = 1024 * 1024 * 1024

type DiskInfo struct {
	Device     string  `json:"device"`
	Mountpoint string  `json:"mountpoint"`
	Fstype     string  `json:"fstype"`
	TotalGB    float64 `json:"total_gb"`
}

type MachineInfo struct {
	Hostname         string     `json:"hostname"`
	OS               string     `json:"os"`
	OSVersion        string     `json:"os_version"`
	Architecture     string     `json:"architecture"`
	Processor        string     `json:"processor"`
	CPUCoresPhysical int        `json:"cpu_cores_physical"`
	CPUCoresLogical  int        `json:"cpu_cores_logical"`
	CPUFreqMHz       *int       `json:"cpu_freq_mhz"`
	CPUFreqMinMHz    *int       `json:"cpu_freq_min_mhz"`
	CPUFreqMaxMHz    *int       `json:"cpu_freq_max_mhz"`
	RAMTotalGB       float64    `json:"ram_total_gb"`
	Disks            []DiskInfo `json:"disks"`
	BootTime         uint64     `json:"boot_time"`
}

func GetMachineInfo() MachineInfo {
	info := MachineInfo{}

	if h, err := hostutil.Info(); err == nil {
		info.Hostname = h.Hostname
		info.OS = h.Platform + " " + h.PlatformVersion
		info.OSVersion = h.KernelVersion
		info.Architecture = h.KernelArch
		info.BootTime = h.BootTime
	}

	if physical, err := cpuutil.Counts(false); err == nil {
		info.CPUCoresPhysical = physical
	}
	if logical, err := cpuutil.Counts(true); err == nil {
		info.CPUCoresLogical = logical
	}

	info.Processor = "N/A"
	if cpuInfo, err := cpuutil.Info(); err == nil && len(cpuInfo) > 0 {
		if cpuInfo[0].ModelName != "" {
			info.Processor = cpuInfo[0].ModelName
		}
		if cpuInfo[0].Mhz > 0 {
			mhz := int(cpuInfo[0].Mhz + 0.5)
			info.CPUFreqMHz = &mhz
		}
	}

	if vm, err := memutil.VirtualMemory(); err == nil {
		info.RAMTotalGB = round1(float64(vm.Total) / gb)
	}

	if parts, err := diskutil.Partitions(false); err == nil {
		for _, p := range parts {
			usage, err := diskutil.Usage(p.Mountpoint)
			if err != nil {
				continue
			}
			info.Disks = append(info.Disks, DiskInfo{
				Device:     p.Device,
				Mountpoint: p.Mountpoint,
				Fstype:     p.Fstype,
				TotalGB:    round1(float64(usage.Total) / gb),
			})
		}
	}

	return info
}

type NetInterface struct {
	Name      string `json:"name"`
	IPv4      string `json:"ipv4"`
	IsUp      bool   `json:"is_up"`
	SpeedMbps int    `json:"speed_mbps"`
}

func GetNetworkInterfaces() []NetInterface {
	ifaces, err := netutil.Interfaces()
	if err != nil {
		return nil
	}

	var out []NetInterface
	for _, iface := range ifaces {
		ipv4 := "N/A"
		for _, addr := range iface.Addrs {
			if ip := parseIPv4(addr.Addr); ip != "" {
				ipv4 = ip
				break
			}
		}

		isUp := false
		for _, f := range iface.Flags {
			if f == "up" {
				isUp = true
				break
			}
		}

		// gopsutil has no portable link-speed API; left at 0 ("N/A" in
		// the UI) rather than guessing.
		out = append(out, NetInterface{Name: iface.Name, IPv4: ipv4, IsUp: isUp})
	}
	return out
}

func parseIPv4(cidr string) string {
	// addr.Addr looks like "192.168.1.5/24"; keep only the IP part, and
	// only if it looks like IPv4 (has 3 dots).
	for i := 0; i < len(cidr); i++ {
		if cidr[i] == '/' {
			cidr = cidr[:i]
			break
		}
	}
	dots := 0
	for _, c := range cidr {
		if c == '.' {
			dots++
		}
	}
	if dots == 3 {
		return cidr
	}
	return ""
}

type DiskLive struct {
	Mountpoint string  `json:"mountpoint"`
	Percent    float64 `json:"percent"`
	UsedGB     float64 `json:"used_gb"`
	TotalGB    float64 `json:"total_gb"`
}

type LiveStats struct {
	Timestamp float64 `json:"timestamp"`

	CPUPercent float64   `json:"cpu_percent"`
	CPUPerCore []float64 `json:"cpu_per_core"`

	RAMPercent      float64 `json:"ram_percent"`
	RAMUsedGB       float64 `json:"ram_used_gb"`
	RAMAvailableGB  float64 `json:"ram_available_gb"`
	RAMTotalGB      float64 `json:"ram_total_gb"`
	SwapPercent     float64 `json:"swap_percent"`
	SwapUsedGB      float64 `json:"swap_used_gb"`
	SwapTotalGB     float64 `json:"swap_total_gb"`

	DiskPercent float64    `json:"disk_percent"`
	DiskUsedGB  float64    `json:"disk_used_gb"`
	DiskTotalGB float64    `json:"disk_total_gb"`
	Disks       []DiskLive `json:"disks"`
	DiskReadBps float64    `json:"disk_read_bps"`
	DiskWriteBps float64   `json:"disk_write_bps"`

	NetUploadBps   float64 `json:"net_upload_bps"`
	NetDownloadBps float64 `json:"net_download_bps"`
}

// Collector keeps the small bit of state (previous counters/timestamps)
// needed to compute throughput deltas, mirroring the module-level
// globals in system_stats.py.
type Collector struct {
	mu sync.Mutex

	lastNetTime  time.Time
	lastNetSent  uint64
	lastNetRecv  uint64

	lastDiskTime  time.Time
	lastDiskRead  uint64
	lastDiskWrite uint64
}

func NewCollector() *Collector {
	return &Collector{}
}

func (c *Collector) Live() LiveStats {
	c.mu.Lock()
	defer c.mu.Unlock()

	now := time.Now()
	stats := LiveStats{Timestamp: float64(now.UnixNano()) / 1e9}

	// ---- CPU ----
	if overall, err := cpuutil.Percent(0, false); err == nil && len(overall) > 0 {
		stats.CPUPercent = round1(overall[0])
	}
	if perCore, err := cpuutil.Percent(0, true); err == nil {
		for _, v := range perCore {
			stats.CPUPerCore = append(stats.CPUPerCore, round1(v))
		}
	}

	// ---- RAM ----
	if vm, err := memutil.VirtualMemory(); err == nil {
		stats.RAMPercent = round1(vm.UsedPercent)
		stats.RAMUsedGB = round2(float64(vm.Used) / gb)
		stats.RAMAvailableGB = round2(float64(vm.Available) / gb)
		stats.RAMTotalGB = round2(float64(vm.Total) / gb)
	}
	if sm, err := memutil.SwapMemory(); err == nil {
		stats.SwapPercent = round1(sm.UsedPercent)
		stats.SwapUsedGB = round2(float64(sm.Used) / gb)
		stats.SwapTotalGB = round2(float64(sm.Total) / gb)
	}

	// ---- Disk usage per partition ----
	if parts, err := diskutil.Partitions(false); err == nil {
		for _, p := range parts {
			usage, err := diskutil.Usage(p.Mountpoint)
			if err != nil {
				continue
			}
			stats.Disks = append(stats.Disks, DiskLive{
				Mountpoint: p.Mountpoint,
				Percent:    round1(usage.UsedPercent),
				UsedGB:     round1(float64(usage.Used) / gb),
				TotalGB:    round1(float64(usage.Total) / gb),
			})
		}
	}
	if len(stats.Disks) > 0 {
		stats.DiskPercent = stats.Disks[0].Percent
		stats.DiskUsedGB = stats.Disks[0].UsedGB
		stats.DiskTotalGB = stats.Disks[0].TotalGB
	}

	// ---- Disk I/O throughput ----
	if counters, err := diskutil.IOCounters(); err == nil {
		var readTotal, writeTotal uint64
		for _, io := range counters {
			readTotal += io.ReadBytes
			writeTotal += io.WriteBytes
		}
		if !c.lastDiskTime.IsZero() {
			elapsed := now.Sub(c.lastDiskTime).Seconds()
			if elapsed > 0 {
				stats.DiskReadBps = round1(float64(readTotal-c.lastDiskRead) / elapsed)
				stats.DiskWriteBps = round1(float64(writeTotal-c.lastDiskWrite) / elapsed)
			}
		}
		c.lastDiskRead, c.lastDiskWrite, c.lastDiskTime = readTotal, writeTotal, now
	}

	// ---- Network throughput ----
	if counters, err := netutil.IOCounters(false); err == nil && len(counters) > 0 {
		sent, recv := counters[0].BytesSent, counters[0].BytesRecv
		if !c.lastNetTime.IsZero() {
			elapsed := now.Sub(c.lastNetTime).Seconds()
			if elapsed > 0 {
				stats.NetUploadBps = round1(float64(sent-c.lastNetSent) / elapsed)
				stats.NetDownloadBps = round1(float64(recv-c.lastNetRecv) / elapsed)
			}
		}
		c.lastNetSent, c.lastNetRecv, c.lastNetTime = sent, recv, now
	}

	return stats
}

func round1(v float64) float64 {
	return float64(int(v*10+0.5)) / 10
}

func round2(v float64) float64 {
	return float64(int(v*100+0.5)) / 100
}

// sortedKeys is a small helper kept for potential future per-NIC
// breakdowns; unused today since dashboard.js doesn't read per-NIC
// speed, but harmless to keep for parity/extension.
func sortedKeys(m map[string]struct{}) []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}

// ProcessInfo is a row for the "Processes" panel (see the reference
// screenshot). It's read-only and cheap: gopsutil's process package
// samples /proc (Linux) or the Windows process APIs directly, no
// polling loop needed like the CPU/net counters above.
type ProcessInfo struct {
	PID        int32   `json:"pid"`
	Name       string  `json:"name"`
	CPUPercent float64 `json:"cpu_percent"`
	MemPercent float64 `json:"mem_percent"`
	Status     string  `json:"status"`
}

// GetProcesses returns the top `limit` processes by CPU usage. The
// first call always reports ~0% CPU for every process (gopsutil needs
// two samples to compute a delta); this is expected and self-corrects
// on the next poll from the browser.
func GetProcesses(limit int) []ProcessInfo {
	pids, err := processutil.Pids()
	if err != nil {
		return nil
	}

	var out []ProcessInfo
	for _, pid := range pids {
		p, err := processutil.NewProcess(pid)
		if err != nil {
			continue
		}
		name, err := p.Name()
		if err != nil || name == "" {
			continue
		}

		cpuPct, _ := p.CPUPercent()
		memPct, _ := p.MemoryPercent()
		statusList, _ := p.Status()
		status := "running"
		if len(statusList) > 0 {
			status = statusList[0]
		}

		out = append(out, ProcessInfo{
			PID:        pid,
			Name:       name,
			CPUPercent: round1(cpuPct),
			MemPercent: round1(float64(memPct)),
			Status:     status,
		})
	}

	sort.Slice(out, func(i, j int) bool { return out[i].CPUPercent > out[j].CPUPercent })
	if len(out) > limit {
		out = out[:limit]
	}
	return out
}
