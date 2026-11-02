#include "i18n.h"
namespace jit::dash {
static nlohmann::json g_empty = nlohmann::json::object();
void loadI18n(const std::string&) {}
const nlohmann::json& i18nDict(const std::string&) { return g_empty; }
}
