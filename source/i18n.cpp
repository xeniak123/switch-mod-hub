#include "i18n.h"

namespace i18n {
namespace {
Lang g_lang = Lang::Polish;
} // namespace

void setLanguage(Lang lang) { g_lang = lang; }
Lang language() { return g_lang; }
bool english() { return g_lang == Lang::English; }
const char* tr(const char* pl, const char* en) { return g_lang == Lang::English ? en : pl; }

std::string plural(long long n, const char* onePl, const char* fewPl, const char* manyPl, const char* oneEn,
                   const char* manyEn) {
    const long long a = n < 0 ? -n : n;
    const char* w = nullptr;
    if (g_lang == Lang::English) {
        w = a == 1 ? oneEn : manyEn;
    } else {
        w = manyPl;
        if (a == 1) w = onePl;
        else if (a % 10 >= 2 && a % 10 <= 4 && (a % 100 < 12 || a % 100 > 14)) w = fewPl;
    }
    return std::to_string(n) + " " + w;
}

const char* code(Lang lang) { return lang == Lang::English ? "en" : "pl"; }
Lang fromCode(const std::string& c) { return c == "en" ? Lang::English : Lang::Polish; }

} // namespace i18n
