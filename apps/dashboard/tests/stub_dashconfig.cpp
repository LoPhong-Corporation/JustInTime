// STUB CHỈ ĐỂ TEST UI NATIVE (Linux) - KHÔNG dùng để build thật cho Windows.
#include "dashconfig.h"
namespace jit::dash {
std::string Config::displayLabel() const { return deviceLabel.empty() ? "Test-PC-Overview" : deviceLabel; }
bool setDeviceLabel(const std::string&, const std::string&) { return true; }
Config load()
{
    Config c;
    c.deviceId = "PC-TESTFAKE1";
    c.deviceLabel = "Test-PC-Overview";
    c.localDbPathUtf8 = "/tmp/justintime_stub_test.db";
    return c;
}
}
