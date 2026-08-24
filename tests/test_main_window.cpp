#include "main_window.h"
#include "pdf_canvas.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolBar>
#include <QWidgetAction>
#include <QtTest>

#include <algorithm>

namespace {

QByteArray minimalPdf()
{
    const QByteArray content =
        "BT /F1 28 Tf 72 680 Td (nexPDF UI smoke test) Tj "
        "0 -48 Td /F1 16 Tf (Local PDF processing) Tj ET\n";
    const QList<QByteArray> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
        "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content + "endstream",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"
    };
    QByteArray pdf = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    QList<qsizetype> offsets;
    for (qsizetype index = 0; index < objects.size(); ++index) {
        offsets.append(pdf.size());
        pdf += QByteArray::number(index + 1) + " 0 obj\n" + objects[index] + "\nendobj\n";
    }
    const qsizetype xref = pdf.size();
    pdf += "xref\n0 " + QByteArray::number(objects.size() + 1) + "\n";
    pdf += "0000000000 65535 f \n";
    for (const qsizetype offset : offsets) {
        pdf += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
    }
    pdf += "trailer\n<< /Size " + QByteArray::number(objects.size() + 1)
        + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
    return pdf;
}

bool hasPageIndicator(const MainWindow &window)
{
    const auto labels = window.findChildren<QLabel *>();
    return std::any_of(labels.cbegin(), labels.cend(), [](const QLabel *label) {
        return label->text() == QStringLiteral("1 / 1");
    });
}

} // namespace

class MainWindowTests final : public QObject {
    Q_OBJECT

private slots:
    void opensFixtureAndCapturesUi();
};

void MainWindowTests::opensFixtureAndCapturesUi()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QSettings settings;
    settings.setValue(QStringLiteral("ui/language"), QStringLiteral("en"));
    settings.sync();

    const QString inputPath = directory.path() + QStringLiteral("/ui-fixture.pdf");
    QFile fixture(inputPath);
    QVERIFY(fixture.open(QIODevice::WriteOnly));
    QCOMPARE(fixture.write(minimalPdf()), minimalPdf().size());
    fixture.close();

    MainWindow window;
    QVERIFY(!window.windowIcon().isNull());
    for (const QString &name : {QStringLiteral("mainToolbar"), QStringLiteral("editToolbar")}) {
        auto *toolbar = window.findChild<QToolBar *>(name);
        QVERIFY2(toolbar != nullptr, qPrintable(name));
        QCOMPARE(toolbar->toolButtonStyle(), Qt::ToolButtonIconOnly);
        for (QAction *action : toolbar->actions()) {
            if (action->isSeparator() || qobject_cast<QWidgetAction *>(action) != nullptr) continue;
            QVERIFY2(!action->icon().isNull(), qPrintable(action->text()));
            QVERIFY2(!action->toolTip().isEmpty(), qPrintable(action->text()));
        }
    }
    for (const QString &name : {QStringLiteral("addTextWatermarkButton"),
                                QStringLiteral("addImageWatermarkButton"),
                                QStringLiteral("scanWatermarkButton"),
                                QStringLiteral("removeWatermarkButton")}) {
        auto *button = window.findChild<QPushButton *>(name);
        QVERIFY2(button != nullptr, qPrintable(name));
        QVERIFY(!button->icon().isNull());
        QVERIFY(!button->toolTip().isEmpty());
    }
    window.setWindowOpacity(0.0);
    window.show();
    window.openFile(inputPath);
    QTRY_VERIFY_WITH_TIMEOUT(hasPageIndicator(window), 10000);
    auto *canvas = window.findChild<PdfCanvas *>();
    QVERIFY(canvas != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->width() >= 600 && canvas->height() >= 800, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasRenderedContent(), 10000);
    QTest::qWait(500);

    const QPixmap screenshot = window.grab();
    QVERIFY(!screenshot.isNull());
    QCOMPARE(screenshot.deviceIndependentSize(), QSizeF(window.size()));
    const QString output = qEnvironmentVariable("NEXPDF_TEST_OUTPUT_DIR");
    if (!output.isEmpty()) {
        QVERIFY(QDir().mkpath(output));
        QVERIFY(screenshot.save(output + QStringLiteral("/ui-main-window.png")));
    }
}

QTEST_MAIN(MainWindowTests)
#include "test_main_window.moc"
