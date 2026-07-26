"""
Doc thong so he thong (CPU/RAM/Disk/Network) va cau hinh
may bang psutil - chi doc, khong can quyen admin.
"""

import platform
import socket
import time

import psutil

_last_net_io = None
_last_net_time = None
_last_net_pernic = None
_last_disk_io = None
_last_disk_io_time = None


def get_machine_info():
    """Thong tin cau hinh may (goi 1 lan, it thay doi)."""
    freq = psutil.cpu_freq()
    mem = psutil.virtual_memory()

    disks = []
    for part in psutil.disk_partitions(all=False):
        try:
            usage = psutil.disk_usage(part.mountpoint)
            disks.append({
                "device": part.device,
                "mountpoint": part.mountpoint,
                "fstype": part.fstype,
                "total_gb": round(usage.total / (1024 ** 3), 1),
            })
        except (PermissionError, OSError):
            continue

    return {
        "hostname": socket.gethostname(),
        "os": f"{platform.system()} {platform.release()}",
        "os_version": platform.version(),
        "architecture": platform.machine(),
        "processor": platform.processor() or "N/A",
        "cpu_cores_physical": psutil.cpu_count(logical=False),
        "cpu_cores_logical": psutil.cpu_count(logical=True),
        "cpu_freq_mhz": round(freq.current) if freq else None,
        "cpu_freq_min_mhz": round(freq.min) if freq and freq.min else None,
        "cpu_freq_max_mhz": round(freq.max) if freq and freq.max else None,
        "ram_total_gb": round(mem.total / (1024 ** 3), 1),
        "disks": disks,
        "boot_time": psutil.boot_time(),
    }


def get_network_interfaces_info():
    """Danh sach interface mang: IP, trang thai, toc do link."""
    addrs = psutil.net_if_addrs()
    stats = psutil.net_if_stats()

    interfaces = []
    for name, addr_list in addrs.items():
        ipv4 = next((a.address for a in addr_list if a.family == socket.AF_INET), None)
        stat = stats.get(name)

        interfaces.append({
            "name": name,
            "ipv4": ipv4 or "N/A",
            "is_up": stat.isup if stat else False,
            "speed_mbps": stat.speed if stat else 0,
        })

    return interfaces


def get_live_stats():
    """
    Thong so real-time day du cho tat ca cac tab: CPU (tong +
    tung nhan), RAM (chi tiet + swap), Disk (tung partition +
    toc do doc/ghi), Network (tong + tung interface).
    """
    global _last_net_io, _last_net_time, _last_net_pernic
    global _last_disk_io, _last_disk_io_time

    now = time.time()

    # ---- CPU ----
    cpu_percent = psutil.cpu_percent(interval=None)
    per_core = psutil.cpu_percent(interval=None, percpu=True)

    # ---- RAM ----
    mem = psutil.virtual_memory()
    swap = psutil.swap_memory()

    # ---- Disk (tat ca partition, khong chi C:) ----
    disks = []
    for part in psutil.disk_partitions(all=False):
        try:
            usage = psutil.disk_usage(part.mountpoint)
            disks.append({
                "mountpoint": part.mountpoint,
                "percent": usage.percent,
                "used_gb": round(usage.used / (1024 ** 3), 1),
                "total_gb": round(usage.total / (1024 ** 3), 1),
            })
        except (PermissionError, OSError):
            continue

    disk_read_bps = 0.0
    disk_write_bps = 0.0

    try:
        disk_io = psutil.disk_io_counters()
        if _last_disk_io is not None and _last_disk_io_time is not None:
            elapsed = max(now - _last_disk_io_time, 0.001)
            disk_read_bps = (disk_io.read_bytes - _last_disk_io.read_bytes) / elapsed
            disk_write_bps = (disk_io.write_bytes - _last_disk_io.write_bytes) / elapsed
        _last_disk_io = disk_io
        _last_disk_io_time = now
    except (OSError, AttributeError):
        pass

    primary_disk = disks[0] if disks else {"percent": 0, "used_gb": 0, "total_gb": 0}

    # ---- Network (tong + tung interface) ----
    net_io = psutil.net_io_counters()
    net_pernic = psutil.net_io_counters(pernic=True)

    upload_speed = 0.0
    download_speed = 0.0
    pernic_speeds = {}

    if _last_net_io is not None and _last_net_time is not None:
        elapsed = max(now - _last_net_time, 0.001)
        upload_speed = (net_io.bytes_sent - _last_net_io.bytes_sent) / elapsed
        download_speed = (net_io.bytes_recv - _last_net_io.bytes_recv) / elapsed

        if _last_net_pernic:
            for name, counters in net_pernic.items():
                prev = _last_net_pernic.get(name)
                if prev:
                    pernic_speeds[name] = {
                        "upload_bps": round((counters.bytes_sent - prev.bytes_sent) / elapsed, 1),
                        "download_bps": round((counters.bytes_recv - prev.bytes_recv) / elapsed, 1),
                    }

    _last_net_io = net_io
    _last_net_pernic = net_pernic
    _last_net_time = now

    return {
        "timestamp": now,

        "cpu_percent": cpu_percent,
        "cpu_per_core": per_core,

        "ram_percent": mem.percent,
        "ram_used_gb": round(mem.used / (1024 ** 3), 2),
        "ram_available_gb": round(mem.available / (1024 ** 3), 2),
        "ram_total_gb": round(mem.total / (1024 ** 3), 2),
        "swap_percent": swap.percent,
        "swap_used_gb": round(swap.used / (1024 ** 3), 2),
        "swap_total_gb": round(swap.total / (1024 ** 3), 2),

        "disk_percent": primary_disk["percent"],
        "disk_used_gb": primary_disk["used_gb"],
        "disk_total_gb": primary_disk["total_gb"],
        "disks": disks,
        "disk_read_bps": round(disk_read_bps, 1),
        "disk_write_bps": round(disk_write_bps, 1),

        "net_upload_bps": round(upload_speed, 1),
        "net_download_bps": round(download_speed, 1),
        "net_pernic": pernic_speeds,
    }
