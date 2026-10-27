// UI language (Polish / English). Texts are written in place as tr("polski", "English").
#pragma once

#include <string>

namespace i18n {

enum class Lang { Polish, English };

void setLanguage(Lang lang);
Lang language();
bool english();
const char* tr(const char* pl, const char* en);
// Number with the matching noun form: Polish has three forms (1 / 2-4 / 5+), English two.
std::string plural(long long n, const char* onePl, const char* fewPl, const char* manyPl, const char* oneEn,
                   const char* manyEn);
// Language code used in settings.json ("pl" / "en") and back.
const char* code(Lang lang);
Lang fromCode(const std::string& code);

} // namespace i18n

using i18n::tr;
