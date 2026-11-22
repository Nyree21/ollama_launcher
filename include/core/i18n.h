// Lightweight runtime translation table.
//
// The English string itself is the key, so the source stays readable and a
// missing entry simply falls back to English. Only two languages ship today;
// adding another one means adding a column to the table in i18n.cpp.
#ifndef OL_CORE_I18N_H
#define OL_CORE_I18N_H

#include <QString>

namespace i18n
{
enum class Language { English = 0, Chinese = 1 };

Language language();
void setLanguage(Language lang);

// "en" / "zh", used by the .ini file and the settings dialog.
QString code(Language lang);
Language fromCode(const QString &code);

// Font family that looks right for the active language.
QString uiFontFamily();

// Applies the UI font of the active language to the running application.
void applyFont();

// Translate an English key. Returns the key itself when no entry exists.
QString translate(const QString &english);
} // namespace i18n

// Short alias used throughout the UI code: T("Service control").
inline QString T(const QString &english) { return i18n::translate(english); }

#endif // OL_CORE_I18N_H
