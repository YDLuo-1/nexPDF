#include "main_window.h"
#include "version.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QMetaObject>

#include <cstdio>
#include <cstring>

int main(int argc, char *argv[])
{
    for (int index = 1; index < argc; ++index) {
        if (argv[index] != nullptr
            && (std::strcmp(argv[index], "--version") == 0
                || std::strcmp(argv[index], "-v") == 0)) {
            std::printf("nexPDF %s\n", NEXPDF_VERSION);
            std::fflush(stdout);
            return 0;
        }
    }

    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("nexPDF"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(NEXPDF_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("nexPDF"));
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

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
