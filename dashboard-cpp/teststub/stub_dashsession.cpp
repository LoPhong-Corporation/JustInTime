#include "dashsession.h"
namespace jit::dash {
static std::optional<DashSession> g_session = DashSession{"tok-abc", "rt-abc", "user-1", "demo@justintime.test"};
bool saveDashSession(const DashSession& s) { g_session = s; return true; }
std::optional<DashSession> loadDashSession() { return g_session; }
void clearDashSession() { g_session.reset(); }
}
