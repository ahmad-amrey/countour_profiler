#include <QApplication>
#include <QCommandLineParser>
#include <QImageReader>
#include <QTimer>

#include "mainwindow.h"
#include "theme.h"

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
    cli.addOption(tierOpt);
    cli.process(app);

    MainWindow w;
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
            w.showView(orig);
            QTimer::singleShot(250, &w, [&w, file] {
                w.grab().save(file);
                QApplication::quit();
            });
        });
    }
    return app.exec();
}
