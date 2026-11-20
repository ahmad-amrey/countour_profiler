#include <QApplication>
#include <QCommandLineParser>
#include <QImageReader>
#include <QTimer>

#include "mainwindow.h"
#include "i18n.h"
#include "theme.h"
#include "widgets.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Contour Profiler");
    QApplication::setOrganizationName("ContourProfiler");
    QApplication::setApplicationVersion("0.2.0");
    QImageReader::setAllocationLimit(1024);  // MB; phone photos can exceed the 256 MB default
    theme::apply();

    QCommandLineParser cli;
    cli.setApplicationDescription("Extract 2D profiles from perspective photos of a bent wire on a grid.");
    cli.addHelpOption();
    cli.addVersionOption();
    cli.addPositionalArgument("photo", "Photo to open.");
    QCommandLineOption shotOpt("screenshot", "Save a screenshot of the window after processing, then quit.", "file");
    QCommandLineOption viewOpt("view", "View for --screenshot: original | rectified.", "view", "rectified");
    QCommandLineOption tierOpt("tier", "Tier for --screenshot: simple | details | arcs.", "tier", "simple");
    cli.addOption(shotOpt);
    cli.addOption(viewOpt);
    QCommandLineOption langOpt("lang", "UI language for this run: en | ar (not saved).", "lang");
    cli.addOption(tierOpt);
    cli.addOption(langOpt);
    cli.process(app);
    if (cli.isSet(langOpt))
        i18n::apply(cli.value(langOpt) == "ar" ? i18n::Lang::Arabic : i18n::Lang::English, false);
    else
        i18n::apply(i18n::saved(), false);

    auto* wp = new MainWindow;  // replaced by a new window when the language changes
    wp->setAttribute(Qt::WA_DeleteOnClose);
    MainWindow& w = *wp;
    if (cli.isSet(shotOpt)) {
        w.setAttribute(Qt::WA_DontShowOnScreen, true);  // render without a visible window
        w.resize(1440, 900);
    }
    w.show();
    if (!cli.positionalArguments().isEmpty()) w.loadImage(cli.positionalArguments().first());
    if (cli.isSet(shotOpt)) {
        const QString file = cli.value(shotOpt);
        const bool orig = cli.value(viewOpt) == "original";
        const QString tier = cli.value(tierOpt);
        QTimer::singleShot(300, &w, [&w, file, orig, tier] {
            if (tier == "details") w.showDetails(true);
            if (tier == "arcs") w.showArcs();
            if (tier == "switch") {
                // Open details, then switch language live and capture the new window.
                w.showDetails(true);
                MainWindow* nw = w.switchLanguage(
                    i18n::current() == i18n::Lang::Arabic ? i18n::Lang::English : i18n::Lang::Arabic, true);
                nw->resize(1440, 900);
                QTimer::singleShot(400, nw, [nw, file] {
                    nw->grab().save(file);
                    QApplication::quit();
                });
                return;
            }
            if (tier == "print") {
                auto* dlg = new PrintSheetDialog(&w, 10);
                dlg->setAttribute(Qt::WA_DontShowOnScreen, true);
                dlg->show();
                QTimer::singleShot(250, &w, [dlg, file] {
                    dlg->grab().save(file);
                    QApplication::quit();
                });
                return;
            }
            w.showView(orig);
            QTimer::singleShot(250, &w, [&w, file] {
                w.grab().save(file);
                QApplication::quit();
            });
        });
    }
    return app.exec();
}
