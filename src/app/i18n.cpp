#include "i18n.h"

#include <QApplication>
#include <QHash>
#include <QLibraryInfo>
#include <QSet>
#include <QSettings>
#include <QTranslator>

namespace i18n {
namespace {

struct Entry {
    const char* en;
    const char* ar;
};
const Entry kArabic[] = {
#include "i18n_ar.inc"
};

// Translator backed by the in-source table (no .qm build step needed).
class TableTranslator : public QTranslator {
public:
    TableTranslator() {
        for (const Entry& e : kArabic) map_.insert(QString::fromUtf8(e.en), QString::fromUtf8(e.ar));
    }
    bool isEmpty() const override { return false; }
    QString translate(const char*, const char* source, const char*, int) const override {
        if (!source) return {};
        const QString key = QString::fromUtf8(source);
        auto it = map_.constFind(key);
        if (it != map_.constEnd()) return *it;
        if (!missing_.contains(key) && qEnvironmentVariableIsSet("CP_I18N_DEBUG"))
            qWarning("untranslated: %s", source);
        missing_.insert(key);
        return {};
    }
    int missing() const { return int(missing_.size()); }

private:
    QHash<QString, QString> map_;
    mutable QSet<QString> missing_;
};

TableTranslator* g_table = nullptr;
QTranslator* g_qt = nullptr;  // Qt's own strings (standard buttons), if installed
Lang g_lang = Lang::English;

}  // namespace

Lang saved() {
    QSettings s;
    return s.value("ui/language", "en").toString() == "ar" ? Lang::Arabic : Lang::English;
}

Lang current() { return g_lang; }

QString nativeName(Lang lang) { return lang == Lang::Arabic ? QStringLiteral("العربية") : QStringLiteral("English"); }

void apply(Lang lang, bool persist) {
    if (g_table) {
        QApplication::removeTranslator(g_table);
        delete g_table;
        g_table = nullptr;
    }
    if (g_qt) {
        QApplication::removeTranslator(g_qt);
        delete g_qt;
        g_qt = nullptr;
    }
    if (lang == Lang::Arabic) {
        g_qt = new QTranslator;
        if (g_qt->load("qtbase_ar", QLibraryInfo::path(QLibraryInfo::TranslationsPath)) ||
            g_qt->load("qtbase_ar", QApplication::applicationDirPath() + "/translations"))
            QApplication::installTranslator(g_qt);
        else {
            delete g_qt;
            g_qt = nullptr;
        }
        g_table = new TableTranslator;
        QApplication::installTranslator(g_table);
        QApplication::setLayoutDirection(Qt::RightToLeft);
    } else {
        QApplication::setLayoutDirection(Qt::LeftToRight);
    }
    g_lang = lang;
    if (!persist) return;
    QSettings s;
    s.setValue("ui/language", lang == Lang::Arabic ? "ar" : "en");
}

int untranslatedCount() { return g_table ? g_table->missing() : 0; }

}  // namespace i18n
