#include "dashi18n.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace jit::dash {

namespace {
nlohmann::json g_translations;
}

void loadI18n(const std::string& webRootUtf8)
{
    const std::string path = webRootUtf8 + "/i18n.json";
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f)
        throw std::runtime_error("i18n: cannot open " + path);

    std::ostringstream ss;
    ss << f.rdbuf();
    g_translations = nlohmann::json::parse(ss.str()); // ném nếu JSON hỏng - cố ý, xem i18n.h
}

const nlohmann::json& i18nDict(const std::string& language)
{
    if (g_translations.contains(language))
        return g_translations[language];
    return g_translations["en"];
}

} // namespace jit::dash
