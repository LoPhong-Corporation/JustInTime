#include "dashconfig.h"
#include "paths.h"
#include "strutil.h"

#include <windows.h>

#include <fstream>
#include <random>
#include <sstream>

namespace jit::dash {

namespace {

// device_id + nhãn (dòng 2, có thể rỗng) - ĐÚNG định dạng device.c/device.cpp
// của agent dùng, để 2 tiến trình luôn đồng nhất device_id.
bool readDeviceFile(const std::filesystem::path& path, std::string& id, std::string& label)
{
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f)
        return false;

    std::string line1, line2;
    std::getline(f, line1);
    std::getline(f, line2);

    auto trim = [](std::string s) {
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' '))
            s.pop_back();
        return s;
    };
    line1 = trim(line1);
    line2 = trim(line2);

    if (line1.rfind("PC-", 0) == 0 && line1.size() == 11)
        id = line1;
    label = line2;
    return !id.empty();
}

std::string generateDeviceId()
{
    unsigned char b[4];
    std::random_device rd;
    for (unsigned char& c : b)
        c = static_cast<unsigned char>(rd());

    char buf[16];
    snprintf(buf, sizeof(buf), "PC-%02X%02X%02X%02X", b[0], b[1], b[2], b[3]);
    return buf;
}

} // namespace

std::string Config::displayLabel() const
{
    const std::filesystem::path path = jit::configFile(L"device.id");
    std::string id, label;
    if (readDeviceFile(path, id, label) && !label.empty())
        return label;

    if (!deviceLabel.empty())
        return deviceLabel;

    std::string suffix = deviceId.size() > 4 ? deviceId.substr(deviceId.size() - 4) : deviceId;
    return "Computer " + suffix;
}

bool setDeviceLabel(const std::string& id, const std::string& label)
{
    return jit::writeFileAtomic(jit::configFile(L"device.id"), id + "\n" + label + "\n");
}

Config load()
{
    Config cfg;

    wchar_t dirBuf[MAX_PATH] = {0};
    // Dùng lại đúng hàm settings_get_config_dir_w() của agent thông qua
    // paths.h - jit::configFile() tự gọi nó, ở đây chỉ cần thư mục.
    const std::filesystem::path dbFile = jit::configFile(L"justintime.db");
    cfg.configDir = dbFile.parent_path().wstring();
    cfg.localDbPathUtf8 = jit::wideToUtf8(dbFile.wstring());

    if (const char* env = std::getenv("JUSTINTIME_PORT"); env && *env)
    {
        int p = 0;
        for (const char* c = env; *c; c++)
        {
            if (*c < '0' || *c > '9') { p = kDefaultPort; break; }
            p = p * 10 + (*c - '0');
        }
        if (p > 0)
            cfg.port = p;
    }

    if (const char* env = std::getenv("JUSTINTIME_DEVICE_ID"); env && *env)
    {
        cfg.deviceId = env;
    }
    else
    {
        std::string id, label;
        const std::filesystem::path devicePath = jit::configFile(L"device.id");
        if (readDeviceFile(devicePath, id, label))
        {
            cfg.deviceId = id;
            cfg.deviceLabel = label;
        }
        else
        {
            cfg.deviceId = generateDeviceId();
            jit::writeFileAtomic(devicePath, cfg.deviceId + "\n\n");
        }
    }

    return cfg;
}

} // namespace jit::dash
