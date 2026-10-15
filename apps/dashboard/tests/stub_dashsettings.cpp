#include "dashsettings.h"
namespace jit::dash {
std::string fontStack(const std::string& name)
{
    if (name == "mono") return "monospace";
    if (name == "serif") return "serif";
    return "sans-serif";
}
static DashSettings g_settings;
DashSettings loadDashSettings() { return g_settings; }
bool saveDashSettings(const DashSettings& s) { g_settings = s; return true; }
}
