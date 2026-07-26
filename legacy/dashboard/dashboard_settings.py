"""
Cau hinh rieng cua dashboard (khac voi settings.ini cua app
JustInTime.exe): font, ngon ngu, nguong canh bao CPU/RAM/Disk.
Luu tai %APPDATA%\\JustInTime\\dashboard_settings.json
"""

import os
import json

DEFAULTS = {
    "font": "mono",          # "mono" | "sans" | "serif"
    "language": "en",        # "en" | "vi"
    "cpu_threshold": 85,
    "ram_threshold": 85,
    "disk_threshold": 90,
}

FONT_STACKS = {
    "mono": "'Cascadia Code', 'Cascadia Mono', 'Consolas', 'SFMono-Regular', monospace",
    "sans": "'Segoe UI', 'Inter', -apple-system, sans-serif",
    "serif": "'Georgia', 'Times New Roman', serif",
}


def _get_path():
    appdata = os.environ.get("APPDATA")

    if not appdata:
        return None

    config_dir = os.path.join(appdata, "JustInTime")
    os.makedirs(config_dir, exist_ok=True)

    return os.path.join(config_dir, "dashboard_settings.json")


def load():
    path = _get_path()

    if not path or not os.path.exists(path):
        return dict(DEFAULTS)

    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except (json.JSONDecodeError, OSError):
        return dict(DEFAULTS)

    merged = dict(DEFAULTS)
    merged.update(data)

    return merged


def save(settings):
    path = _get_path()

    if not path:
        return False

    merged = dict(DEFAULTS)
    merged.update(settings)

    with open(path, "w", encoding="utf-8") as f:
        json.dump(merged, f, indent=2)

    return True
