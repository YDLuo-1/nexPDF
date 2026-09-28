#include "nexpdf/document_session.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtMath>

#include <algorithm>

#ifdef Q_OS_WIN
#  define NOMINMAX
#  include <windows.h>
#  include <psapi.h>
#elif defined(Q_OS_MACOS)
#  include <sys/resource.h>
#endif

namespace {

double currentRssMiB()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0) {
        return static_cast<double>(counters.WorkingSetSize) / 1048576.0;
    }
#elif defined(Q_OS_MACOS)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return static_cast<double>(usage.ru_maxrss) / 1048576.0;
    }
#else
    QFile status(QStringLiteral("/proc/self/status"));
    if (status.open(QIODevice::ReadOnly)) {
        while (!status.atEnd()) {
            const QByteArray line = status.readLine();
            if (line.startsWith("VmRSS:")) {
                const QList<QByteArray> fields = line.simplified().split(' ');
                if (fields.size() >= 2) return fields[1].toDouble() / 1024.0;
            }
        }
    }
#endif
    return 0.0;
}

double percentile(QVector<double> values, const double quantile)
{
    if (values.isEmpty()) return 0.0;
    std::sort(values.begin(), values.end());
    const qsizetype index = std::clamp<qsizetype>(
        static_cast<qsizetype>(qCeil(quantile * values.size())) - 1, 0, values.size() - 1);
    return values[index];
}

bool waitForEither(QSignalSpy &success, QSignalSpy &failure, const int timeoutMilliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (success.isEmpty() && failure.isEmpty() && timer.elapsed() < timeoutMilliseconds) {
        success.wait(std::min(100, timeoutMilliseconds - static_cast<int>(timer.elapsed())));
    }
    return !success.isEmpty();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("nexpdf_benchmark"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Measured nexPDF corpus renderer benchmark"));
    parser.addHelpOption();
    QCommandLineOption scaleOption(QStringLiteral("scale"), QStringLiteral("Render scale"),
                                   QStringLiteral("scale"), QStringLiteral("1.0"));
    QCommandLineOption cyclesOption(QStringLiteral("cycles"), QStringLiteral("Open/render/close cycles"),
                                    QStringLiteral("count"), QStringLiteral("1"));
    parser.addOptions({scaleOption, cyclesOption});
    parser.addPositionalArgument(QStringLiteral("pdf"), QStringLiteral("One or more corpus PDFs"),
                                 QStringLiteral("pdf..."));
    parser.process(application);

    bool scaleOk = false;
    bool cyclesOk = false;
    const double scale = parser.value(scaleOption).toDouble(&scaleOk);
    const int cycles = parser.value(cyclesOption).toInt(&cyclesOk);
    const QStringList files = parser.positionalArguments();
    if (!scaleOk || scale <= 0.0 || !cyclesOk || cycles < 1 || files.isEmpty()) {
        parser.showHelp(2);
    }

    nexpdf::DocumentSession session;
    QSignalSpy opened(&session, &nexpdf::DocumentSession::opened);
    QSignalSpy rendered(&session, &nexpdf::DocumentSession::renderReady);
    QSignalSpy closed(&session, &nexpdf::DocumentSession::closed);
    QSignalSpy failed(&session, &nexpdf::DocumentSession::failed);
    QVector<double> renderMilliseconds;
    QJsonArray documents;
    int errors = 0;
    int renderedPages = 0;
    const double initialRss = currentRssMiB();
    double maximumRss = initialRss;
    QVector<double> cycleRss;
    QElapsedTimer total;
    total.start();

    for (int cycle = 0; cycle < cycles; ++cycle) {
        for (const QString &path : files) {
            const QString absolutePath = QFileInfo(path).absoluteFilePath();
            opened.clear();
            failed.clear();
            QElapsedTimer openTimer;
            openTimer.start();
            session.open(absolutePath);
            if (!waitForEither(opened, failed, 30000)) {
                ++errors;
                continue;
            }
            const auto info = qvariant_cast<nexpdf::DocumentInfo>(opened.first().first());
            QJsonObject document;
            document.insert(QStringLiteral("file"), QFileInfo(path).fileName());
            document.insert(QStringLiteral("cycle"), cycle + 1);
            document.insert(QStringLiteral("pages"), info.pageCount);
            document.insert(QStringLiteral("open_ms"), openTimer.elapsed());
            documents.append(document);

            for (int page = 0; page < info.pageCount; ++page) {
                rendered.clear();
                failed.clear();
                nexpdf::RenderRequest request;
                request.requestId = static_cast<quint64>(renderedPages + 1);
                request.revision = info.revision;
                request.pageIndex = page;
                request.scale = scale;
                QElapsedTimer pageTimer;
                pageTimer.start();
                session.requestRender(request);
                if (!waitForEither(rendered, failed, 60000)) {
                    ++errors;
                    continue;
                }
                renderMilliseconds.append(static_cast<double>(pageTimer.nsecsElapsed()) / 1000000.0);
                ++renderedPages;
                maximumRss = std::max(maximumRss, currentRssMiB());
            }
            closed.clear();
            session.close();
            if (closed.isEmpty()) (void)closed.wait(30000);
        }
        const double rss = currentRssMiB();
        cycleRss.append(rss);
        maximumRss = std::max(maximumRss, rss);
    }

    const double elapsedSeconds = static_cast<double>(total.elapsed()) / 1000.0;
    QJsonObject report;
    report.insert(QStringLiteral("schema"), QStringLiteral("nexpdf-benchmark-v1"));
    report.insert(QStringLiteral("documents"), documents);
    report.insert(QStringLiteral("rendered_pages"), renderedPages);
    report.insert(QStringLiteral("errors"), errors);
    report.insert(QStringLiteral("render_p50_ms"), percentile(renderMilliseconds, 0.50));
    report.insert(QStringLiteral("render_p95_ms"), percentile(renderMilliseconds, 0.95));
    report.insert(QStringLiteral("throughput_pages_per_second"),
                  elapsedSeconds > 0.0 ? renderedPages / elapsedSeconds : 0.0);
    QJsonArray cycleRssJson;
    for (const double rss : cycleRss) cycleRssJson.append(rss);
    const qsizetype stableIndex = cycleRss.isEmpty()
        ? 0
        : std::min<qsizetype>(4, cycleRss.size() - 1);
    const double stableRss = cycleRss.isEmpty() ? initialRss : cycleRss[stableIndex];
    const double finalRss = cycleRss.isEmpty() ? initialRss : cycleRss.constLast();
    report.insert(QStringLiteral("initial_rss_mib"), initialRss);
    report.insert(QStringLiteral("cycle_rss_mib"), cycleRssJson);
    report.insert(QStringLiteral("stable_baseline_cycle"), cycleRss.isEmpty() ? 0 : stableIndex + 1);
    report.insert(QStringLiteral("stable_baseline_rss_mib"), stableRss);
    report.insert(QStringLiteral("final_rss_mib"), finalRss);
    report.insert(QStringLiteral("rss_growth_after_warmup_mib"), finalRss - stableRss);
    report.insert(QStringLiteral("peak_rss_mib"), maximumRss);
    report.insert(QStringLiteral("elapsed_seconds"), elapsedSeconds);
    QFile output;
    if (!output.open(stdout, QIODevice::WriteOnly)) return 2;
    output.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    return errors == 0 ? 0 : 1;
}
