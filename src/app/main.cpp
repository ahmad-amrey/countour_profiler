#include <QApplication>
#include <QCommandLineParser>
#include <QImageReader>
#include <QTimer>

#include "mainwindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Contour Profiler");
    QApplication::setOrganizationName("ContourProfiler");
    QApplication::setApplicationVersion("0.1.0");
    QImageReader::setAllocationLimit(1024);  // MB; phone photos can exceed the 256 MB default

    QCommandLineParser cli;
    cli.setApplicationDescription("Extract 2D profiles from perspective photos of a bent wire on a pattern.");
    cli.addHelpOption();
    cli.addVersionOption();
    cli.addPositionalArgument("photo", "Photo to open.");
    QCommandLineOption shotOpt("screenshot", "Save a screenshot of the window after processing, then quit.", "file");
    QCommandLineOption viewOpt("view", "View for --screenshot: original | rectified.", "view", "rectified");
    cli.addOption(shotOpt);
    cli.addOption(viewOpt);
    cli.process(app);

    MainWindow w;
    w.show();
    if (!cli.positionalArguments().isEmpty()) w.loadImage(cli.positionalArguments().first());
    if (cli.isSet(shotOpt)) {
        const QString file = cli.value(shotOpt);
        const bool orig = cli.value(viewOpt) == "original";
        QTimer::singleShot(300, &w, [&w, file, orig] {
            w.showView(orig);
            QTimer::singleShot(200, &w, [&w, file] {
                w.grab().save(file);
                QApplication::quit();
            });
        });
    }
    return app.exec();
}
