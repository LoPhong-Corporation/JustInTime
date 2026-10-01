#include "dashsettings.h"
#include "paths.h"
#include "strutil.h"
#include <nlohmann/json.hpp>

#include <windows.h>
#include <wincrypt.h>

#include <fstream>
#include <sstream>

#pragma comment(lib, "crypt32.lib")

namespace jit::dash {

using json = nlohmann::json;

namespace {

std::filesystem::path settingsPath() { return jit::configFile(L"dashboard_settings.json"); }

// DPAPI scoped theo user Windows hiện tại - CÙNG cơ chế auth.cpp của
// agent dùng cho session.dat, ở đây tách riêng biệt (mô tả khác) để 2
// secret không lẫn vào nhau nếu 1 trong 2 bị lộ.
std::string dpapiProtect(const std::string& plain)
{
    if (plain.empty())
        return "";

    DATA_BLOB in{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    DATA_BLOB out{};

    if (!CryptProtectData(&in, L"JustInTime dashboard secret", nullptr, nullptr, nullptr, 0, &out))
        return "";

    std::string result(reinterpret_cast<char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return result;
}

std::string dpapiUnprotect(const std::string& enc)
{
    if (enc.empty())
        return "";

    DATA_BLOB in{static_cast<DWORD>(enc.size()), reinterpret_cast<BYTE*>(const_cast<char*>(enc.data()))};
    DATA_BLOB out{};

    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out))
        return "";

    std::string result(reinterpret_cast<char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}

std::string toBase64(const std::string& bin)
{
    DWORD outLen = 0;
    CryptBinaryToStringA(reinterpret_cast<const BYTE*>(bin.data()), static_cast<DWORD>(bin.size()),
                          CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &outLen);
    std::string out(outLen, '\0');
    CryptBinaryToStringA(reinterpret_cast<const BYTE*>(bin.data()), static_cast<DWORD>(bin.size()),
                          CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out.data(), &outLen);
    if (!out.empty() && out.back() == '\0')
        out.resize(outLen);
    return out;
}

std::string fromBase64(const std::string& b64)
{
    if (b64.empty())
        return "";
    DWORD outLen = 0;
    if (!CryptStringToBinaryA(b64.data(), static_cast<DWORD>(b64.size()), CRYPT_STRING_BASE64,
                               nullptr, &outLen, nullptr, nullptr))
        return "";
    std::string out(outLen, '\0');
    CryptStringToBinaryA(b64.data(), static_cast<DWORD>(b64.size()), CRYPT_STRING_BASE64,
                          reinterpret_cast<BYTE*>(out.data()), &outLen, nullptr, nullptr);
    return out;
}

} // namespace

std::string fontStack(const std::string& name)
{
    if (name == "mono")  return "'Cascadia Code', 'Cascadia Mono', 'Consolas', 'SFMono-Regular', monospace";
    if (name == "serif") return "'Georgia', 'Times New Roman', serif";
    return "'Segoe UI', 'Inter', system-ui, -apple-system, sans-serif"; // "sans" (mặc định)
}

DashSettings loadDashSettings()
{
    DashSettings out;

    std::ifstream f(settingsPath(), std::ios::in | std::ios::binary);
    if (!f)
        return out;

    std::ostringstream ss;
    ss << f.rdbuf();

    json data;
    try { data = json::parse(ss.str()); } catch (...) { return out; }
    if (!data.is_object())
        return out;

    if (data.contains("font") && data["font"].is_string()) out.font = data["font"];
    if (data.contains("language") && data["language"].is_string()) out.language = data["language"];
    if (data.contains("cpu_threshold") && data["cpu_threshold"].is_number()) out.cpuThreshold = data["cpu_threshold"];
    if (data.contains("ram_threshold") && data["ram_threshold"].is_number()) out.ramThreshold = data["ram_threshold"];
    if (data.contains("disk_threshold") && data["disk_threshold"].is_number()) out.diskThreshold = data["disk_threshold"];
    if (data.contains("time_format") && data["time_format"].is_string()) out.timeFormat = data["time_format"];
    if (data.contains("default_period") && data["default_period"].is_string()) out.defaultPeriod = data["default_period"];

    if (data.contains("gemini_api_key_enc") && data["gemini_api_key_enc"].is_string())
    {
        const std::string enc = fromBase64(data["gemini_api_key_enc"].get<std::string>());
        if (!enc.empty())
            out.geminiApiKey = dpapiUnprotect(enc); // lỗi giải mã (đổi máy/đổi user) -> im lặng để trống
    }
    else if (data.contains("gemini_api_key") && data["gemini_api_key"].is_string())
    {
        // MIGRATION: bản cũ lưu key dạng PLAINTEXT ở trường "gemini_api_key".
        // Đọc 1 lần cho không mất key người dùng đã nhập; lần save() kế
        // tiếp sẽ ghi lại dưới dạng mã hoá và bỏ hẳn trường plaintext cũ.
        out.geminiApiKey = data["gemini_api_key"].get<std::string>();
    }

    return out;
}

bool saveDashSettings(const DashSettings& s)
{
    json disk = {
        {"font", s.font}, {"language", s.language},
        {"cpu_threshold", s.cpuThreshold}, {"ram_threshold", s.ramThreshold}, {"disk_threshold", s.diskThreshold},
        {"time_format", s.timeFormat}, {"default_period", s.defaultPeriod},
    };

    if (!s.geminiApiKey.empty())
    {
        const std::string enc = dpapiProtect(s.geminiApiKey);
        if (enc.empty())
            return false;
        disk["gemini_api_key_enc"] = toBase64(enc);
    }

    return jit::writeFileAtomic(settingsPath(), disk.dump(2));
}

} // namespace jit::dash
