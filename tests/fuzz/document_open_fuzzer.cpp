// libFuzzer harness for the nexPDF open/render path (Clang only).
// Build: cmake -DNEXPDF_BUILD_FUZZERS=ON with Clang, then run
//   ./nexpdf_fuzz_open tests/fuzz/seeds -max_len=65536
#include "nexpdf/document_session.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>

#include <cstdint>

static QCoreApplication *ensureApplication()
{
    static QCoreApplication application(0, nullptr);
    return &application;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static QCoreApplication *application = ensureApplication();
    Q_UNUSED(application);

    QTemporaryDir directory;
    if (!directory.isValid()) {
        return 0;
    }
    const QString path = directory.filePath(QStringLiteral("input.pdf"));
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return 0;
        }
        file.write(reinterpret_cast<const char *>(data), static_cast<qint64>(size));
        file.close();
    }

    nexpdf::DocumentSession session;
    QSignalSpy opened(&session, &nexpdf::DocumentSession::opened);
    QSignalSpy failed(&session, &nexpdf::DocumentSession::failed);
    session.open(path, nexpdf::OpenOptions{});
    for (int spins = 0; opened.isEmpty() && failed.isEmpty() && spins < 100; ++spins) {
        opened.wait(10);
        QCoreApplication::processEvents();
    }
    if (opened.isEmpty()) {
        return 0;
    }
    const auto info = qvariant_cast<nexpdf::DocumentInfo>(opened.first().first());
    if (info.pageCount <= 0) {
        return 0;
    }

    QSignalSpy rendered(&session, &nexpdf::DocumentSession::renderReady);
    nexpdf::RenderRequest request;
    request.requestId = 1;
    request.pageIndex = 0;
    request.scale = 1.0;
    session.requestRender(request);
    for (int spins = 0; rendered.isEmpty() && spins < 100; ++spins) {
        rendered.wait(10);
        QCoreApplication::processEvents();
    }
    return 0;
}
