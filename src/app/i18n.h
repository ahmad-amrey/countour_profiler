// UI language: English (source) or Arabic (right-to-left). The choice is
// stored in QSettings and applied at start-up.
#pragma once
#include <QString>

namespace i18n {

enum class Lang { English = 0, Arabic = 1 };

Lang saved();             // from settings (default: English)
Lang current();
void apply(Lang lang, bool persist = true);  // install translators, set layout direction, save
QString nativeName(Lang lang);
int untranslatedCount(); // strings looked up but missing (diagnostics)

}  // namespace i18n
