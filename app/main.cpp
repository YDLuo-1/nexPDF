#include "app_icons.h"
#include "crash_handler.h"
#include "main_window.h"

#include "nexpdf/version.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QMetaObject>

#include <cstdio>
#include <cstring>

int main(int argc, char *argv[])
{
    // QStandardPaths needs these before the crash handler resolves its
    // directory; both setters work without a QCoreApplication instance.
    QCoreApplication::setApplicationName(QStringLiteral("nexPDF"));
    QCoreApplication::setOrganizationName(QStringLiteral("nexPDF"));
    nexpdf::crash::install();
    for (int index = 1; index < argc; ++index) {
        if (argv[index] != nullptr
            && (std::strcmp(argv[index], "--version") == 0
                || std::strcmp(argv[index], "-v") == 0)) {
            std::printf("nexPDF %s\n", NEXPDF_VERSION);
            std::fflush(stdout);
            return 0;
        }
        // Hidden verification aid for the crash reporter: deliberately fault so
        // the produced dump/report can be checked locally.
        if (argv[index] != nullptr && std::strcmp(argv[index], "--crash-test") == 0) {
            volatile int *fault = nullptr;
            *fault = 1;
            return 2;
        }
    }

    // The rounding policy only takes effect before QApplication is constructed.
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("nexPDF"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(NEXPDF_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("nexPDF"));
    QApplication::setWindowIcon(nexpdf::icons::applicationIcon());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("nexPDF — local cross-platform PDF tools"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("PDF file to open"), QStringLiteral("[file]"));
    parser.process(application);

    MainWindow window;
    window.show();
    if (!parser.positionalArguments().isEmpty()) {
        const QString path = QFileInfo(parser.positionalArguments().first()).absoluteFilePath();
        QMetaObject::invokeMethod(&window, [&window, path] { window.openFile(path); });
    }
    return application.exec();
}
