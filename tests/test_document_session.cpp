#include "nexpdf/document_session.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QByteArray assemblePdf(const QList<QByteArray> &objects)
{
    QByteArray pdf = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    QList<qsizetype> offsets;
    for (qsizetype i = 0; i < objects.size(); ++i) {
        offsets.append(pdf.size());
        pdf += QByteArray::number(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
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

QByteArray minimalPdf()
{
    return assemblePdf({
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
        "<< /Length 38 >>\nstream\nBT /F1 18 Tf 40 330 Td (nexPDF) Tj ET\nendstream",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"
    });
}

QByteArray externalWatermarkPdf()
{
    return assemblePdf({
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R /Annots [6 0 R] >>",
        "<< /Length 38 >>\nstream\nBT /F1 18 Tf 40 330 Td (nexPDF) Tj ET\nendstream",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        "<< /Type /Annot /Subtype /Watermark /Rect [20 20 180 70] /Contents (External Watermark) /NM (external-watermark) >>"
    });
}

QString writeFixture(const QString &directory)
{
    const QString path = directory + QStringLiteral("/input.pdf");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(minimalPdf()) < 0) {
        return {};
    }
    return path;
}

QString writeExternalWatermarkFixture(const QString &directory)
{
    const QString path = directory + QStringLiteral("/external-watermark.pdf");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(externalWatermarkPdf()) < 0) {
        return {};
    }
    return path;
}

void retainArtifact(const QString &path, const QString &name)
{
    const QString output = qEnvironmentVariable("NEXPDF_TEST_OUTPUT_DIR");
    if (output.isEmpty()) return;
    QDir().mkpath(output);
    QFile::remove(output + QLatin1Char('/') + name);
    QVERIFY(QFile::copy(path, output + QLatin1Char('/') + name));
}

QString errorDescription(const QSignalSpy &failed)
{
    if (failed.isEmpty()) return QStringLiteral("No error signal was emitted.");
    const auto error = qvariant_cast<nexpdf::OperationError>(failed.last().first());
    return QStringLiteral("%1: %2 (%3)").arg(error.operation, error.message, error.detail);
}

void retainImage(const QImage &image, const QString &name)
{
    const QString output = qEnvironmentVariable("NEXPDF_TEST_OUTPUT_DIR");
    if (output.isEmpty()) return;
    QDir().mkpath(output);
    QVERIFY(image.save(output + QLatin1Char('/') + name));
}

} // namespace

class DocumentSessionTests final : public QObject {
    Q_OBJECT

private slots:
    void opensRendersAndSearches();
    void encryptsAndDecryptsWithValidation();
    void enforcesRequiredPasswordsAndAllowsMatchingCredentials();
    void editsPagesAnnotationsAndJournal();
    void appliesPermanentRedaction();
    void rendersAcrossDisplayListLruEviction();
    void scansAndRemovesStandardWatermarkAnnotation();
    void addsScansAndRemovesOwnWatermark();
};

void DocumentSessionTests::opensRendersAndSearches()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = writeFixture(directory.path());
    QVERIFY(!input.isEmpty());

    nexpdf::DocumentSession session;
    QSignalSpy opened(&session, &nexpdf::DocumentSession::opened);
    QSignalSpy rendered(&session, &nexpdf::DocumentSession::renderReady);
    QSignalSpy searched(&session, &nexpdf::DocumentSession::searchFinished);
    QSignalSpy extracted(&session, &nexpdf::DocumentSession::textExtracted);
    QSignalSpy failed(&session, &nexpdf::DocumentSession::failed);

    session.open(input);
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);
    QCOMPARE(qvariant_cast<nexpdf::DocumentInfo>(opened.first().first()).pageCount, 1);

    nexpdf::RenderRequest request;
    request.requestId = 7;
    request.pageIndex = 0;
    request.scale = 1.0;
    session.requestRender(request);
    QTRY_COMPARE_WITH_TIMEOUT(rendered.size(), 1, 5000);
    const auto result = qvariant_cast<nexpdf::RenderResult>(rendered.first().first());
    QCOMPARE(result.requestId, quint64(7));
    QVERIFY(!result.image.isNull());

    session.search(QStringLiteral("nexPDF"));
    QTRY_COMPARE_WITH_TIMEOUT(searched.size(), 1, 5000);
    QVERIFY(!qvariant_cast<QVector<nexpdf::SearchHit>>(searched.first().first()).isEmpty());
    session.extractText(0, QRectF(30, 40, 240, 340));
    QTRY_COMPARE_WITH_TIMEOUT(extracted.size(), 1, 5000);
    QVERIFY(extracted.first().at(2).toString().contains(QStringLiteral("nexPDF")));
    QCOMPARE(failed.size(), 0);

    nexpdf::RenderRequest outsideTile;
    outsideTile.requestId = 8;
    outsideTile.pageIndex = 0;
    outsideTile.scale = 1.0;
    outsideTile.tilePixels = QRect(1000, 1000, 10, 10);
    session.requestRender(outsideTile);
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
    QCOMPARE(qvariant_cast<nexpdf::OperationError>(failed.first().first()).code,
             nexpdf::ErrorCode::EngineError);
}

void DocumentSessionTests::encryptsAndDecryptsWithValidation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = writeFixture(directory.path());
    const QString encryptedPath = directory.path() + QStringLiteral("/encrypted.pdf");
    const QString decryptedPath = directory.path() + QStringLiteral("/decrypted.pdf");

    nexpdf::DocumentSession writer;
    QSignalSpy opened(&writer, &nexpdf::DocumentSession::opened);
    QSignalSpy saved(&writer, &nexpdf::DocumentSession::saved);
    QSignalSpy failed(&writer, &nexpdf::DocumentSession::failed);
    writer.open(input);
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);

    nexpdf::SaveOptions encryption;
    encryption.encryption.algorithm = nexpdf::EncryptionAlgorithm::Aes256;
    encryption.encryption.userPassword = QStringLiteral("用户-123");
    encryption.encryption.ownerPassword = QStringLiteral("owner-456");
    writer.saveAs(encryptedPath, encryption);
    QTRY_VERIFY_WITH_TIMEOUT(saved.size() == 1 || failed.size() == 1, 5000);
    QVERIFY2(failed.isEmpty(), qPrintable(errorDescription(failed)));
    QVERIFY(QFileInfo::exists(encryptedPath));

    nexpdf::DocumentSession reader;
    QSignalSpy passwordRequired(&reader, &nexpdf::DocumentSession::passwordRequired);
    QSignalSpy encryptedOpened(&reader, &nexpdf::DocumentSession::opened);
    QSignalSpy readerFailed(&reader, &nexpdf::DocumentSession::failed);
    reader.open(encryptedPath);
    QTRY_COMPARE_WITH_TIMEOUT(passwordRequired.size(), 1, 5000);

    nexpdf::OpenOptions wrongPassword;
    wrongPassword.password = QStringLiteral("wrong-password");
    reader.open(encryptedPath, wrongPassword);
    QTRY_COMPARE_WITH_TIMEOUT(readerFailed.size(), 1, 5000);
    QCOMPARE(qvariant_cast<nexpdf::OperationError>(readerFailed.first().first()).code,
             nexpdf::ErrorCode::IncorrectPassword);

    nexpdf::OpenOptions password;
    password.password = QStringLiteral("用户-123");
    reader.open(encryptedPath, password);
    QTRY_COMPARE_WITH_TIMEOUT(encryptedOpened.size(), 1, 5000);

    QSignalSpy decryptedSaved(&reader, &nexpdf::DocumentSession::saved);
    nexpdf::SaveOptions decryption;
    decryption.encryption.algorithm = nexpdf::EncryptionAlgorithm::None;
    reader.saveAs(decryptedPath, decryption);
    QTRY_COMPARE_WITH_TIMEOUT(decryptedSaved.size(), 1, 5000);

    nexpdf::DocumentSession verification;
    QSignalSpy verified(&verification, &nexpdf::DocumentSession::opened);
    verification.open(decryptedPath);
    QTRY_COMPARE_WITH_TIMEOUT(verified.size(), 1, 5000);

    nexpdf::DocumentSession ownerReader;
    QSignalSpy ownerOpened(&ownerReader, &nexpdf::DocumentSession::opened);
    nexpdf::OpenOptions ownerPassword;
    ownerPassword.password = QStringLiteral("owner-456");
    ownerReader.open(encryptedPath, ownerPassword);
    QTRY_COMPARE_WITH_TIMEOUT(ownerOpened.size(), 1, 5000);
    retainArtifact(encryptedPath, QStringLiteral("aes256-unicode.pdf"));
    retainArtifact(decryptedPath, QStringLiteral("decrypted.pdf"));
}

void DocumentSessionTests::enforcesRequiredPasswordsAndAllowsMatchingCredentials()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = writeFixture(directory.path());
    const QString missingUserPath = directory.path() + QStringLiteral("/missing-user.pdf");
    const QString missingOwnerPath = directory.path() + QStringLiteral("/missing-owner.pdf");
    const QString encryptedPath = directory.path() + QStringLiteral("/aes128-matching-passwords.pdf");

    nexpdf::DocumentSession writer;
    QSignalSpy opened(&writer, &nexpdf::DocumentSession::opened);
    QSignalSpy saved(&writer, &nexpdf::DocumentSession::saved);
    QSignalSpy failed(&writer, &nexpdf::DocumentSession::failed);
    writer.open(input);
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);

    nexpdf::SaveOptions options;
    options.encryption.algorithm = nexpdf::EncryptionAlgorithm::Aes128;
    options.encryption.ownerPassword = QStringLiteral("owner-passphrase-123");
    options.encryption.permissions.copy = false;
    options.encryption.permissions.print = false;
    writer.saveAs(missingUserPath, options);
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
    QCOMPARE(qvariant_cast<nexpdf::OperationError>(failed.last().first()).code,
             nexpdf::ErrorCode::InvalidArgument);
    QVERIFY(!QFileInfo::exists(missingUserPath));

    options.encryption.userPassword = QStringLiteral("user-passphrase-123");
    options.encryption.ownerPassword.clear();
    writer.saveAs(missingOwnerPath, options);
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 2, 5000);
    QCOMPARE(qvariant_cast<nexpdf::OperationError>(failed.last().first()).code,
             nexpdf::ErrorCode::InvalidArgument);
    QVERIFY(!QFileInfo::exists(missingOwnerPath));

    options.encryption.userPassword = QStringLiteral("same-strong-password");
    options.encryption.ownerPassword = options.encryption.userPassword;
    writer.saveAs(encryptedPath, options);
    QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 5000);
    QCOMPARE(failed.size(), 2);

    nexpdf::DocumentSession reader;
    QSignalSpy readerOpened(&reader, &nexpdf::DocumentSession::opened);
    QSignalSpy passwordRequired(&reader, &nexpdf::DocumentSession::passwordRequired);
    QSignalSpy readerFailed(&reader, &nexpdf::DocumentSession::failed);
    reader.open(encryptedPath);
    QTRY_COMPARE_WITH_TIMEOUT(passwordRequired.size(), 1, 5000);
    QCOMPARE(readerOpened.size(), 0);

    nexpdf::OpenOptions password;
    password.password = QStringLiteral("same-strong-password");
    reader.open(encryptedPath, password);
    QTRY_VERIFY_WITH_TIMEOUT(!readerOpened.isEmpty() || !readerFailed.isEmpty(), 5000);
    QVERIFY2(readerFailed.isEmpty(), qPrintable(errorDescription(readerFailed)));
    QVERIFY(qvariant_cast<nexpdf::DocumentInfo>(readerOpened.first().first()).encrypted);
    retainArtifact(encryptedPath, QStringLiteral("aes128-matching-passwords.pdf"));
}

void DocumentSessionTests::editsPagesAnnotationsAndJournal()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = writeFixture(directory.path());
    const QString output = directory.path() + QStringLiteral("/edited.pdf");
    const QString imagePath = directory.path() + QStringLiteral("/edit-image.png");
    QImage editImage(32, 24, QImage::Format_RGB32);
    editImage.fill(Qt::blue);
    QVERIFY(editImage.save(imagePath));

    nexpdf::DocumentSession session;
    QSignalSpy opened(&session, &nexpdf::DocumentSession::opened);
    QSignalSpy pageCountChanged(&session, &nexpdf::DocumentSession::pageCountChanged);
    QSignalSpy stateChanged(&session, &nexpdf::DocumentSession::stateChanged);
    QSignalSpy failed(&session, &nexpdf::DocumentSession::failed);
    session.open(input);
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);

    nexpdf::EditOperation insert;
    insert.kind = nexpdf::EditKind::InsertBlankPage;
    insert.pageIndex = 1;
    session.applyEdit(insert);
    QTRY_VERIFY_WITH_TIMEOUT(!pageCountChanged.isEmpty(), 5000);
    QCOMPARE(pageCountChanged.last().first().toInt(), 2);

    session.undo();
    QTRY_VERIFY_WITH_TIMEOUT(pageCountChanged.size() >= 2, 5000);
    QCOMPARE(pageCountChanged.last().first().toInt(), 1);
    session.redo();
    QTRY_VERIFY_WITH_TIMEOUT(pageCountChanged.size() >= 3, 5000);
    QCOMPARE(pageCountChanged.last().first().toInt(), 2);

    nexpdf::EditOperation rotate;
    rotate.kind = nexpdf::EditKind::RotatePage;
    rotate.pageIndex = 0;
    rotate.rotation = 90;
    int expectedStateCount = stateChanged.size() + 1;
    session.applyEdit(rotate);
    QTRY_VERIFY_WITH_TIMEOUT(stateChanged.size() >= expectedStateCount || !failed.isEmpty(), 5000);
    QVERIFY2(failed.isEmpty(), qPrintable(errorDescription(failed)));

    nexpdf::EditOperation freeText;
    freeText.kind = nexpdf::EditKind::AddFreeText;
    freeText.pageIndex = 0;
    freeText.bounds = QRectF(35, 250, 180, 45);
    freeText.text = QStringLiteral("Edited by nexPDF");
    freeText.color = Qt::red;
    freeText.fontSize = 18;
    expectedStateCount = stateChanged.size() + 1;
    session.applyEdit(freeText);
    QTRY_VERIFY_WITH_TIMEOUT(stateChanged.size() >= expectedStateCount || !failed.isEmpty(), 5000);
    QVERIFY2(failed.isEmpty(), qPrintable(errorDescription(failed)));

    nexpdf::EditOperation resizeText;
    resizeText.kind = nexpdf::EditKind::ResizeObject;
    resizeText.pageIndex = 0;
    resizeText.sourceBounds = freeText.bounds;
    resizeText.bounds = QRectF(45, 240, 220, 55);
    expectedStateCount = stateChanged.size() + 1;
    session.applyEdit(resizeText);
    QTRY_VERIFY_WITH_TIMEOUT(stateChanged.size() >= expectedStateCount || !failed.isEmpty(), 5000);
    QVERIFY2(failed.isEmpty(), qPrintable(errorDescription(failed)));

    nexpdf::EditOperation image;
    image.kind = nexpdf::EditKind::AddImage;
    image.pageIndex = 0;
    image.bounds = QRectF(35, 140, 96, 72);
    image.imagePath = imagePath;
    expectedStateCount = stateChanged.size() + 1;
    session.applyEdit(image);
    QTRY_VERIFY_WITH_TIMEOUT(stateChanged.size() >= expectedStateCount || !failed.isEmpty(), 5000);
    QVERIFY2(failed.isEmpty(), qPrintable(errorDescription(failed)));

    nexpdf::EditOperation ink;
    ink.kind = nexpdf::EditKind::AddInk;
    ink.pageIndex = 0;
    ink.bounds = QRectF(40, 105, 100, 20);
    ink.points = {QPointF(40, 115), QPointF(70, 105), QPointF(105, 120), QPointF(140, 110)};
    ink.color = Qt::red;
    expectedStateCount = stateChanged.size() + 1;
    session.applyEdit(ink);
    QTRY_VERIFY_WITH_TIMEOUT(stateChanged.size() >= expectedStateCount || !failed.isEmpty(), 5000);
    QVERIFY2(failed.isEmpty(), qPrintable(errorDescription(failed)));

    nexpdf::EditOperation highlight;
    highlight.kind = nexpdf::EditKind::AddHighlight;
    highlight.pageIndex = 0;
    highlight.bounds = QRectF(35, 50, 120, 30);
    highlight.opacity = 0.45;
    expectedStateCount = stateChanged.size() + 1;
    session.applyEdit(highlight);
    QTRY_VERIFY_WITH_TIMEOUT(stateChanged.size() >= expectedStateCount || !failed.isEmpty(), 5000);
    QVERIFY2(failed.isEmpty(), qPrintable(errorDescription(failed)));

    QSignalSpy saved(&session, &nexpdf::DocumentSession::saved);
    session.saveAs(output);
    QTRY_VERIFY_WITH_TIMEOUT(!saved.isEmpty() || !failed.isEmpty(), 5000);
    QVERIFY2(failed.isEmpty(), qPrintable(errorDescription(failed)));

    nexpdf::DocumentSession verification;
    QSignalSpy verified(&verification, &nexpdf::DocumentSession::opened);
    QSignalSpy rendered(&verification, &nexpdf::DocumentSession::renderReady);
    verification.open(output);
    QTRY_COMPARE_WITH_TIMEOUT(verified.size(), 1, 5000);
    const auto info = qvariant_cast<nexpdf::DocumentInfo>(verified.first().first());
    QCOMPARE(info.pageCount, 2);
    nexpdf::RenderRequest request;
    request.requestId = 99;
    request.revision = info.revision;
    request.pageIndex = 0;
    verification.requestRender(request);
    QTRY_COMPARE_WITH_TIMEOUT(rendered.size(), 1, 5000);
    QVERIFY(!qvariant_cast<nexpdf::RenderResult>(rendered.first().first()).image.isNull());
    retainArtifact(output, QStringLiteral("edited.pdf"));
}

void DocumentSessionTests::appliesPermanentRedaction()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = writeFixture(directory.path());
    const QString output = directory.path() + QStringLiteral("/redacted.pdf");

    nexpdf::DocumentSession session;
    QSignalSpy opened(&session, &nexpdf::DocumentSession::opened);
    QSignalSpy searched(&session, &nexpdf::DocumentSession::searchFinished);
    QSignalSpy stateChanged(&session, &nexpdf::DocumentSession::stateChanged);
    QSignalSpy saved(&session, &nexpdf::DocumentSession::saved);
    QSignalSpy failed(&session, &nexpdf::DocumentSession::failed);
    session.open(input);
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);

    session.search(QStringLiteral("nexPDF"));
    QTRY_COMPARE_WITH_TIMEOUT(searched.size(), 1, 5000);
    const auto hits = qvariant_cast<QVector<nexpdf::SearchHit>>(searched.first().first());
    QVERIFY(!hits.isEmpty());
    QVERIFY(!hits.first().quads.isEmpty());

    nexpdf::EditOperation preview;
    preview.kind = nexpdf::EditKind::AddRedactionPreview;
    preview.pageIndex = 0;
    preview.bounds = hits.first().quads.first().adjusted(-2, -2, 2, 2);
    int expectedStateCount = stateChanged.size() + 1;
    session.applyEdit(preview);
    QTRY_VERIFY_WITH_TIMEOUT(stateChanged.size() >= expectedStateCount || !failed.isEmpty(), 5000);
    QVERIFY2(failed.isEmpty(), qPrintable(errorDescription(failed)));

    nexpdf::EditOperation apply;
    apply.kind = nexpdf::EditKind::ApplyRedactions;
    apply.pageIndex = 0;
    expectedStateCount = stateChanged.size() + 1;
    session.applyEdit(apply);
    QTRY_VERIFY_WITH_TIMEOUT(stateChanged.size() >= expectedStateCount || !failed.isEmpty(), 5000);
    QVERIFY2(failed.isEmpty(), qPrintable(errorDescription(failed)));

    session.saveAs(output);
    QTRY_VERIFY_WITH_TIMEOUT(!saved.isEmpty() || !failed.isEmpty(), 5000);
    QVERIFY2(failed.isEmpty(), qPrintable(errorDescription(failed)));
    retainArtifact(output, QStringLiteral("redacted.pdf"));

    nexpdf::DocumentSession verification;
    QSignalSpy verified(&verification, &nexpdf::DocumentSession::opened);
    QSignalSpy verificationSearch(&verification, &nexpdf::DocumentSession::searchFinished);
    verification.open(output);
    QTRY_COMPARE_WITH_TIMEOUT(verified.size(), 1, 5000);
    verification.search(QStringLiteral("nexPDF"));
    QTRY_COMPARE_WITH_TIMEOUT(verificationSearch.size(), 1, 5000);
    QVERIFY(qvariant_cast<QVector<nexpdf::SearchHit>>(
        verificationSearch.first().first()).isEmpty());
}

void DocumentSessionTests::rendersAcrossDisplayListLruEviction()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = writeFixture(directory.path());

    nexpdf::DocumentSession session;
    QSignalSpy opened(&session, &nexpdf::DocumentSession::opened);
    QSignalSpy pageCountChanged(&session, &nexpdf::DocumentSession::pageCountChanged);
    QSignalSpy rendered(&session, &nexpdf::DocumentSession::renderReady);
    QSignalSpy failed(&session, &nexpdf::DocumentSession::failed);
    session.open(input);
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);

    constexpr int pageCount = 12;
    for (int page = 1; page < pageCount; ++page) {
        nexpdf::EditOperation insert;
        insert.kind = nexpdf::EditKind::InsertBlankPage;
        insert.pageIndex = page;
        session.applyEdit(insert);
        QTRY_COMPARE_WITH_TIMEOUT(pageCountChanged.size(), page, 5000);
    }

    quint64 requestId = 1000;
    for (int page = 0; page < pageCount; ++page) {
        rendered.clear();
        nexpdf::RenderRequest request;
        request.requestId = requestId++;
        request.pageIndex = page;
        request.scale = 0.5;
        session.requestRender(request);
        QTRY_COMPARE_WITH_TIMEOUT(rendered.size(), 1, 5000);
        QVERIFY(!qvariant_cast<nexpdf::RenderResult>(rendered.first().first()).image.isNull());
    }

    rendered.clear();
    nexpdf::RenderRequest firstPageAgain;
    firstPageAgain.requestId = requestId;
    firstPageAgain.pageIndex = 0;
    firstPageAgain.scale = 0.5;
    session.requestRender(firstPageAgain);
    QTRY_COMPARE_WITH_TIMEOUT(rendered.size(), 1, 5000);
    QVERIFY(!qvariant_cast<nexpdf::RenderResult>(rendered.first().first()).image.isNull());
    QVERIFY2(failed.isEmpty(), qPrintable(errorDescription(failed)));
}

void DocumentSessionTests::scansAndRemovesStandardWatermarkAnnotation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = writeExternalWatermarkFixture(directory.path());
    QVERIFY(!input.isEmpty());

    nexpdf::DocumentSession session;
    QSignalSpy opened(&session, &nexpdf::DocumentSession::opened);
    QSignalSpy scanned(&session, &nexpdf::DocumentSession::watermarksScanned);
    QSignalSpy stateChanged(&session, &nexpdf::DocumentSession::stateChanged);
    QSignalSpy failed(&session, &nexpdf::DocumentSession::failed);
    session.open(input);
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);

    session.scanWatermarks();
    QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, 5000);
    const auto candidates = qvariant_cast<QVector<nexpdf::WatermarkCandidate>>(
        scanned.first().first());
    QCOMPARE(candidates.size(), 1);
    QCOMPARE(candidates.first().safety, nexpdf::WatermarkRemovalSafety::ReviewRequired);
    QVERIFY(!candidates.first().createdByNexPDF);
    QVERIFY(!candidates.first().bounds.isEmpty());

    const int expectedStateCount = stateChanged.size() + 1;
    session.removeWatermarks({candidates.first().id});
    QTRY_VERIFY_WITH_TIMEOUT(stateChanged.size() >= expectedStateCount || !failed.isEmpty(), 5000);
    QVERIFY2(failed.isEmpty(), qPrintable(errorDescription(failed)));

    const QString output = directory.path() + QStringLiteral("/external-watermark-removed.pdf");
    QSignalSpy saved(&session, &nexpdf::DocumentSession::saved);
    session.saveAs(output);
    QTRY_VERIFY_WITH_TIMEOUT(!saved.isEmpty() || !failed.isEmpty(), 5000);
    QVERIFY2(failed.isEmpty(), qPrintable(errorDescription(failed)));
    retainArtifact(output, QStringLiteral("external-watermark-removed.pdf"));

    session.scanWatermarks();
    QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 2, 5000);
    QVERIFY(qvariant_cast<QVector<nexpdf::WatermarkCandidate>>(
        scanned.last().first()).isEmpty());
}

void DocumentSessionTests::addsScansAndRemovesOwnWatermark()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = writeFixture(directory.path());

    nexpdf::DocumentSession session;
    QSignalSpy opened(&session, &nexpdf::DocumentSession::opened);
    QSignalSpy state(&session, &nexpdf::DocumentSession::stateChanged);
    QSignalSpy scanned(&session, &nexpdf::DocumentSession::watermarksScanned);
    QSignalSpy rendered(&session, &nexpdf::DocumentSession::renderReady);
    session.open(input);
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);

    nexpdf::RenderRequest render;
    render.requestId = 1;
    render.pageIndex = 0;
    render.scale = 1.0;
    session.requestRender(render);
    QTRY_COMPARE_WITH_TIMEOUT(rendered.size(), 1, 5000);
    const QImage original = qvariant_cast<nexpdf::RenderResult>(rendered.takeFirst().first()).image;

    nexpdf::WatermarkSpec watermark;
    watermark.text = QStringLiteral("CONFIDENTIAL / 机密");
    session.addWatermark(watermark);
    QTRY_VERIFY_WITH_TIMEOUT(state.size() >= 2, 5000);
    render.requestId = 2;
    session.requestRender(render);
    QTRY_COMPARE_WITH_TIMEOUT(rendered.size(), 1, 5000);
    const QImage marked = qvariant_cast<nexpdf::RenderResult>(rendered.takeFirst().first()).image;
    QVERIFY(marked != original);
    session.scanWatermarks();
    QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, 5000);
    const auto candidates = qvariant_cast<QVector<nexpdf::WatermarkCandidate>>(scanned.first().first());
    QCOMPARE(candidates.size(), 1);
    QVERIFY(candidates.first().createdByNexPDF);
    QCOMPARE(candidates.first().safety, nexpdf::WatermarkRemovalSafety::Exact);

    const QString markedPath = directory.path() + QStringLiteral("/watermark-marked.pdf");
    QSignalSpy markedSaved(&session, &nexpdf::DocumentSession::saved);
    session.saveAs(markedPath);
    QTRY_COMPARE_WITH_TIMEOUT(markedSaved.size(), 1, 5000);
    retainArtifact(markedPath, QStringLiteral("watermark-marked.pdf"));

    session.removeWatermarks({candidates.first().id});
    QTRY_VERIFY_WITH_TIMEOUT(state.size() >= 3, 5000);
    render.requestId = 3;
    session.requestRender(render);
    QTRY_COMPARE_WITH_TIMEOUT(rendered.size(), 1, 5000);
    const QImage restored = qvariant_cast<nexpdf::RenderResult>(rendered.takeFirst().first()).image;
    retainImage(original, QStringLiteral("watermark-original.png"));
    retainImage(marked, QStringLiteral("watermark-marked.png"));
    retainImage(restored, QStringLiteral("watermark-restored.png"));
    QCOMPARE(restored, original);
    session.scanWatermarks();
    QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 2, 5000);
    QVERIFY(qvariant_cast<QVector<nexpdf::WatermarkCandidate>>(scanned.last().first()).isEmpty());
    const QString restoredPath = directory.path() + QStringLiteral("/watermark-restored.pdf");
    QSignalSpy saved(&session, &nexpdf::DocumentSession::saved);
    session.saveAs(restoredPath);
    QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 5000);
    retainArtifact(input, QStringLiteral("watermark-original.pdf"));
    retainArtifact(restoredPath, QStringLiteral("watermark-restored.pdf"));
}

QTEST_MAIN(DocumentSessionTests)
#include "test_document_session.moc"
