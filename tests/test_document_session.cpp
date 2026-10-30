#include "nexpdf/document_session.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

namespace {

// AES-256 key derivation takes over five seconds in the instrumented Debug build.
constexpr int cryptoTimeoutMs = 30000;

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

static QByteArray multiPageFixture(int pageCount = 1, bool watermarks = false)
{
    QList<QByteArray> objects;
    QByteArray kids;
    for (int p=0;p<pageCount;++p) kids += QByteArray::number(3+p) + " 0 R ";
    objects << "<< /Type /Catalog /Pages 2 0 R >>"
            << "<< /Type /Pages /Kids [" + kids + "] /Count " + QByteArray::number(pageCount) + " >>";
    const int contentId = 3 + pageCount, fontId = contentId + 1;
    for(int p=0;p<pageCount;++p) {
        QByteArray page = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] /Resources << /Font << /F1 "
            + QByteArray::number(fontId) + " 0 R >> >> /Contents " + QByteArray::number(contentId) + " 0 R";
        if(watermarks) page += " /Annots [" + QByteArray::number(fontId+1) + " 0 R "
            + QByteArray::number(fontId+2) + " 0 R " + QByteArray::number(fontId+3) + " 0 R]";
        objects << page + " >>";
    }
    const QByteArray content = "BT /F1 18 Tf 40 330 Td (nexPDF) Tj ET\n";
    objects << "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content + "endstream"
            << "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";
    if(watermarks) {
        for(int x : {20,120,220}) objects << "<< /Type /Annot /Subtype /Watermark /Rect ["
            + QByteArray::number(x) + " 20 " + QByteArray::number(x+40)
            + " 70] /Contents (External Watermark) >>";
    }
    QByteArray pdf = "%PDF-1.7\n";
    QList<qsizetype> offsets;
    for(qsizetype i=0;i<objects.size();++i) {
        offsets << pdf.size();
        pdf += QByteArray::number(i+1)+" 0 obj\n"+objects[i]+"\nendobj\n";
    }
    auto xref=pdf.size();
    pdf += "xref\n0 "+QByteArray::number(objects.size()+1)+"\n0000000000 65535 f \n";
    for(auto offset:offsets) pdf += QByteArray::number(offset).rightJustified(10,'0')+" 00000 n \n";
    pdf += "trailer\n<< /Size "+QByteArray::number(objects.size()+1)+" /Root 1 0 R >>\nstartxref\n"
        +QByteArray::number(xref)+"\n%%EOF\n";
    return pdf;
}
static QString writeMultiPageFixture(QTemporaryDir &dir,int pages=1,bool marks=false)
{
    auto path=dir.filePath(QStringLiteral("input.pdf"));
    QFile f(path);
    if(!f.open(QIODevice::WriteOnly)) return {};
    const auto bytes=multiPageFixture(pages,marks);
    if(f.write(bytes)!=bytes.size()) return {};
    return path;
}

} // namespace

class DocumentSessionTests final : public QObject {
    Q_OBJECT

private slots:
    void protectedExportsRequireAuthorization_data();
    void protectedExportsRequireAuthorization();
    void failedImportPreservesHistory_data();
    void failedImportPreservesHistory();
    void opensRendersAndSearches();
    void undoAfterSaveMustBeDirty();
    void savePreservesRedoAndFailedSaveState();
    void savePreservesCatalogAndMetadata();
    void replacesOpenSourceWithoutLosingUndo();
    void renderRequestsAlwaysFinish();
    void slicedSearchSurvivesReplacementAndEdits();
    void keepAfterEncryptionMustStayEncrypted_data();
    void keepAfterEncryptionMustStayEncrypted();
    void copyRestrictionMustBeHonored();
    void batchRemovalMustKeepUnselectedWatermark();

    void encryptsAndDecryptsWithValidation();
    void enforcesRequiredPasswordsAndAllowsMatchingCredentials();
    void editsPagesAnnotationsAndJournal();
    void appliesPermanentRedaction();
    void rendersAcrossDisplayListLruEviction();
    void scansAndRemovesStandardWatermarkAnnotation();
    void addsScansAndRemovesOwnWatermark();
    void keepsCurrentDocumentWhenOpenFails();
    void enforcesDocumentPermissions();
    void removingWatermarkAfterRedactionKeepsRedaction();
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
    const auto info = qvariant_cast<nexpdf::DocumentInfo>(opened.first().first());
    QCOMPARE(info.pageCount, 1);
    QVERIFY2(info.fingerprint.size() == 64, "Document fingerprint must be 64 hex characters");

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
    QTRY_COMPARE_WITH_TIMEOUT(rendered.size(), 2, 5000);
    QCOMPARE(qvariant_cast<nexpdf::RenderResult>(rendered.last().first()).requestId, quint64(8));
    QCOMPARE(qvariant_cast<nexpdf::RenderResult>(rendered.last().first()).error.code,
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
    QTRY_VERIFY_WITH_TIMEOUT(saved.size() == 1 || failed.size() == 1, cryptoTimeoutMs);
    QVERIFY2(failed.isEmpty(), qPrintable(errorDescription(failed)));
    QVERIFY(QFileInfo::exists(encryptedPath));

    nexpdf::DocumentSession reader;
    QSignalSpy passwordRequired(&reader, &nexpdf::DocumentSession::passwordRequired);
    QSignalSpy encryptedOpened(&reader, &nexpdf::DocumentSession::opened);
    QSignalSpy readerFailed(&reader, &nexpdf::DocumentSession::failed);
    reader.open(encryptedPath);
    QTRY_COMPARE_WITH_TIMEOUT(passwordRequired.size(), 1, cryptoTimeoutMs);

    nexpdf::OpenOptions wrongPassword;
    wrongPassword.password = QStringLiteral("wrong-password");
    reader.open(encryptedPath, wrongPassword);
    QTRY_COMPARE_WITH_TIMEOUT(readerFailed.size(), 1, cryptoTimeoutMs);
    QCOMPARE(qvariant_cast<nexpdf::OperationError>(readerFailed.first().first()).code,
             nexpdf::ErrorCode::IncorrectPassword);

    nexpdf::OpenOptions password;
    password.password = QStringLiteral("用户-123");
    reader.open(encryptedPath, password);
    QTRY_COMPARE_WITH_TIMEOUT(encryptedOpened.size(), 1, cryptoTimeoutMs);

    QSignalSpy decryptedSaved(&reader, &nexpdf::DocumentSession::saved);
    password.password = QStringLiteral("owner-456");
    reader.open(encryptedPath, password);
    QTRY_COMPARE_WITH_TIMEOUT(encryptedOpened.size(), 2, cryptoTimeoutMs);
    nexpdf::SaveOptions decryption;
    decryption.encryption.algorithm = nexpdf::EncryptionAlgorithm::None;
    reader.saveAs(decryptedPath, decryption);
    QTRY_COMPARE_WITH_TIMEOUT(decryptedSaved.size(), 1, cryptoTimeoutMs);

    nexpdf::DocumentSession verification;
    QSignalSpy verified(&verification, &nexpdf::DocumentSession::opened);
    verification.open(decryptedPath);
    QTRY_COMPARE_WITH_TIMEOUT(verified.size(), 1, 5000);

    nexpdf::DocumentSession ownerReader;
    QSignalSpy ownerOpened(&ownerReader, &nexpdf::DocumentSession::opened);
    nexpdf::OpenOptions ownerPassword;
    ownerPassword.password = QStringLiteral("owner-456");
    ownerReader.open(encryptedPath, ownerPassword);
    QTRY_COMPARE_WITH_TIMEOUT(ownerOpened.size(), 1, cryptoTimeoutMs);
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
    const auto editedRender = qvariant_cast<nexpdf::RenderResult>(rendered.first().first()).image;
    QVERIFY(!editedRender.isNull());
    // The FreeText box must not render as a solid color block: MuPDF paints /C
    // as the box background, so the box is white and only the glyphs are red.
    const QRect box((int)resizeText.bounds.right() - 200, (int)resizeText.bounds.bottom() - 50, 200, 50);
    const QImage cropped = editedRender.copy(box);
    qint64 colored = 0;
    for (int y = 0; y < cropped.height(); ++y) {
        for (int x = 0; x < cropped.width(); ++x) {
            const QColor pixel = cropped.pixelColor(x, y);
            if (pixel.red() > 170 && pixel.green() < 110 && pixel.blue() < 110) ++colored;
        }
    }
    QVERIFY2(colored * 2 < (qint64)cropped.width() * cropped.height(),
             "FreeText box must not be filled with the text color");
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

void DocumentSessionTests::keepsCurrentDocumentWhenOpenFails()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = writeFixture(directory.path());
    QVERIFY(!input.isEmpty());

    nexpdf::DocumentSession session;
    QSignalSpy opened(&session, &nexpdf::DocumentSession::opened);
    QSignalSpy closed(&session, &nexpdf::DocumentSession::closed);
    QSignalSpy failed(&session, &nexpdf::DocumentSession::failed);
    QSignalSpy rendered(&session, &nexpdf::DocumentSession::renderReady);
    session.open(input);
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);
    const auto info = qvariant_cast<nexpdf::DocumentInfo>(opened.first().first());

    const QString notPdf = directory.path() + QStringLiteral("/not-a-pdf.pdf");
    {
        QFile file(notPdf);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write(QByteArrayLiteral("this is not a PDF")) > 0);
    }
    session.open(notPdf);
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);

    // A failed open must not close the document that is already open.
    QCOMPARE(closed.size(), 0);
    nexpdf::RenderRequest request;
    request.requestId = 5;
    request.revision = info.revision;
    request.pageIndex = 0;
    request.scale = 1.0;
    session.requestRender(request);
    QTRY_COMPARE_WITH_TIMEOUT(rendered.size(), 1, 5000);
    QVERIFY(!qvariant_cast<nexpdf::RenderResult>(rendered.first().first()).image.isNull());
}

void DocumentSessionTests::enforcesDocumentPermissions()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = writeFixture(directory.path());
    const QString restrictedPath = directory.path() + QStringLiteral("/restricted.pdf");
    const QString annotateOnlyPath = directory.path() + QStringLiteral("/annotate-only.pdf");

    {
        nexpdf::DocumentSession writer;
        QSignalSpy opened(&writer, &nexpdf::DocumentSession::opened);
        QSignalSpy saved(&writer, &nexpdf::DocumentSession::saved);
        QSignalSpy writeFailed(&writer, &nexpdf::DocumentSession::failed);
        writer.open(input);
        QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);

        nexpdf::SaveOptions restricted;
        restricted.encryption.algorithm = nexpdf::EncryptionAlgorithm::Aes128;
        restricted.encryption.userPassword = QStringLiteral("reader-passphrase");
        restricted.encryption.ownerPassword = QStringLiteral("owner-passphrase");
        restricted.encryption.permissions.modify = false;
        restricted.encryption.permissions.annotate = false;
        restricted.encryption.permissions.assemble = false;
        writer.saveAs(restrictedPath, restricted);
        QTRY_VERIFY_WITH_TIMEOUT(saved.size() == 1 || !writeFailed.isEmpty(), 5000);
        QVERIFY2(writeFailed.isEmpty(), qPrintable(errorDescription(writeFailed)));

        nexpdf::SaveOptions annotateOnly;
        annotateOnly.encryption.algorithm = nexpdf::EncryptionAlgorithm::Aes128;
        annotateOnly.encryption.userPassword = QStringLiteral("reader-passphrase");
        annotateOnly.encryption.ownerPassword = QStringLiteral("owner-passphrase");
        annotateOnly.encryption.permissions.modify = false;
        annotateOnly.encryption.permissions.annotate = true;
        annotateOnly.encryption.permissions.assemble = false;
        writer.saveAs(annotateOnlyPath, annotateOnly);
        QTRY_VERIFY_WITH_TIMEOUT(saved.size() == 2 || !writeFailed.isEmpty(), 5000);
        QVERIFY2(writeFailed.isEmpty(), qPrintable(errorDescription(writeFailed)));
    }

    nexpdf::OpenOptions credentials;
    credentials.password = QStringLiteral("reader-passphrase");

    nexpdf::DocumentSession reader;
    QSignalSpy opened(&reader, &nexpdf::DocumentSession::opened);
    QSignalSpy failed(&reader, &nexpdf::DocumentSession::failed);
    reader.open(restrictedPath, credentials);
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);

    nexpdf::EditOperation rotate;
    rotate.kind = nexpdf::EditKind::RotatePage;
    rotate.pageIndex = 0;
    rotate.rotation = 90;
    reader.applyEdit(rotate);
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
    QCOMPARE(qvariant_cast<nexpdf::OperationError>(failed.last().first()).code,
             nexpdf::ErrorCode::PermissionDenied);

    nexpdf::EditOperation highlight;
    highlight.kind = nexpdf::EditKind::AddHighlight;
    highlight.pageIndex = 0;
    highlight.bounds = QRectF(20, 20, 80, 20);
    reader.applyEdit(highlight);
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 2, 5000);
    QCOMPARE(qvariant_cast<nexpdf::OperationError>(failed.last().first()).code,
             nexpdf::ErrorCode::PermissionDenied);

    nexpdf::DocumentSession annotateReader;
    QSignalSpy annotateOpened(&annotateReader, &nexpdf::DocumentSession::opened);
    QSignalSpy annotateState(&annotateReader, &nexpdf::DocumentSession::stateChanged);
    QSignalSpy annotateFailed(&annotateReader, &nexpdf::DocumentSession::failed);
    annotateReader.open(annotateOnlyPath, credentials);
    QTRY_COMPARE_WITH_TIMEOUT(annotateOpened.size(), 1, 5000);

    // Annotation-only permission permits markup...
    nexpdf::EditOperation annotate;
    annotate.kind = nexpdf::EditKind::AddHighlight;
    annotate.pageIndex = 0;
    annotate.bounds = QRectF(20, 20, 80, 20);
    annotate.color = QColor(255, 214, 0);
    annotate.opacity = 0.45;
    const int stateBeforeAnnotate = annotateState.size();
    annotateReader.applyEdit(annotate);
    QTRY_VERIFY_WITH_TIMEOUT(annotateState.size() > stateBeforeAnnotate || !annotateFailed.isEmpty(), 5000);
    QVERIFY2(annotateFailed.isEmpty(), qPrintable(errorDescription(annotateFailed)));

    // ...but not page assembly.
    nexpdf::EditOperation deletePage;
    deletePage.kind = nexpdf::EditKind::DeletePage;
    deletePage.pageIndex = 0;
    annotateReader.applyEdit(deletePage);
    QTRY_COMPARE_WITH_TIMEOUT(annotateFailed.size(), 1, 5000);
    QCOMPARE(qvariant_cast<nexpdf::OperationError>(annotateFailed.last().first()).code,
             nexpdf::ErrorCode::PermissionDenied);
}

void DocumentSessionTests::removingWatermarkAfterRedactionKeepsRedaction()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = writeFixture(directory.path());
    const QString output = directory.path() + QStringLiteral("/watermark-then-redaction.pdf");

    nexpdf::DocumentSession session;
    QSignalSpy opened(&session, &nexpdf::DocumentSession::opened);
    QSignalSpy state(&session, &nexpdf::DocumentSession::stateChanged);
    QSignalSpy scanned(&session, &nexpdf::DocumentSession::watermarksScanned);
    QSignalSpy searched(&session, &nexpdf::DocumentSession::searchFinished);
    QSignalSpy failed(&session, &nexpdf::DocumentSession::failed);
    QSignalSpy saved(&session, &nexpdf::DocumentSession::saved);
    session.open(input);
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);

    session.search(QStringLiteral("nexPDF"));
    QTRY_COMPARE_WITH_TIMEOUT(searched.size(), 1, 5000);
    const auto hits = qvariant_cast<QVector<nexpdf::SearchHit>>(searched.first().first());
    QVERIFY(!hits.isEmpty());
    QVERIFY(!hits.first().quads.isEmpty());

    nexpdf::WatermarkSpec watermark;
    watermark.text = QStringLiteral("CONFIDENTIAL");
    const int stateBeforeWatermark = state.size();
    session.addWatermark(watermark);
    QTRY_VERIFY_WITH_TIMEOUT(state.size() > stateBeforeWatermark, 5000);

    session.scanWatermarks();
    QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, 5000);
    const auto candidates = qvariant_cast<QVector<nexpdf::WatermarkCandidate>>(
        scanned.first().first());
    QCOMPARE(candidates.size(), 1);
    const QString staleId = candidates.first().id;

    nexpdf::EditOperation preview;
    preview.kind = nexpdf::EditKind::AddRedactionPreview;
    preview.pageIndex = 0;
    preview.bounds = hits.first().quads.first().adjusted(-2, -2, 2, 2);
    const int stateBeforePreview = state.size();
    session.applyEdit(preview);
    QTRY_VERIFY_WITH_TIMEOUT(state.size() > stateBeforePreview, 5000);

    nexpdf::EditOperation apply;
    apply.kind = nexpdf::EditKind::ApplyRedactions;
    apply.pageIndex = 0;
    const int stateBeforeApply = state.size();
    session.applyEdit(apply);
    QTRY_VERIFY_WITH_TIMEOUT(state.size() > stateBeforeApply, 5000);

    // Scanned candidates are invalid after a content rewrite, so removal must be
    // rejected instead of restoring the pre-redaction snapshot.
    session.removeWatermarks({staleId});
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
    QCOMPARE(qvariant_cast<nexpdf::OperationError>(failed.first().first()).code,
             nexpdf::ErrorCode::InvalidArgument);

    session.saveAs(output);
    QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 5000);
#ifndef Q_OS_WIN
    // New destinations are created owner-only because the temporary file may
    // contain decrypted content.
    const QFile::Permissions written = QFileInfo(output).permissions();
    QVERIFY(written.testFlag(QFileDevice::ReadOwner));
    QVERIFY(!written.testFlag(QFileDevice::ReadOther));
#endif

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

void DocumentSessionTests::undoAfterSaveMustBeDirty()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    nexpdf::DocumentSession s;
    QSignalSpy opened(&s,&nexpdf::DocumentSession::opened), states(&s,&nexpdf::DocumentSession::stateChanged),
        saved(&s,&nexpdf::DocumentSession::saved), failed(&s,&nexpdf::DocumentSession::failed),
        counts(&s,&nexpdf::DocumentSession::pageCountChanged);
    s.open(writeMultiPageFixture(d)); QTRY_COMPARE(opened.size(),1);
    nexpdf::EditOperation edit; edit.kind=nexpdf::EditKind::InsertBlankPage; edit.pageIndex=1;
    s.applyEdit(edit); QTRY_COMPARE(counts.size(),1);
    s.saveAs(d.filePath(QStringLiteral("saved.pdf"))); QTRY_COMPARE(saved.size(),1);
    QTRY_VERIFY(!states.last().at(1).toBool());
    for(int i=0;i<10 && states.last().at(2).toBool();++i) {
        auto before=states.size(); s.undo(); QTRY_VERIFY(states.size()>before);
    }
    QCOMPARE(failed.size(),0); QCOMPARE(counts.last().first().toInt(),1);
    QVERIFY2(states.last().at(1).toBool(),"Undo changed saved two-page document back to one page but modified=false");
}
void DocumentSessionTests::keepAfterEncryptionMustStayEncrypted_data()
{
    QTest::addColumn<int>("algorithm");
    QTest::newRow("AES-128") << static_cast<int>(nexpdf::EncryptionAlgorithm::Aes128);
    QTest::newRow("AES-256") << static_cast<int>(nexpdf::EncryptionAlgorithm::Aes256);
}

void DocumentSessionTests::keepAfterEncryptionMustStayEncrypted()
{
    QFETCH(int, algorithm);
    QTemporaryDir d; QVERIFY(d.isValid());
    nexpdf::DocumentSession session;
    QSignalSpy opened(&session, &nexpdf::DocumentSession::opened), saved(&session, &nexpdf::DocumentSession::saved),
        failed(&session, &nexpdf::DocumentSession::failed);
    session.open(writeMultiPageFixture(d)); QTRY_COMPARE(opened.size(), 1);
    nexpdf::SaveOptions enc;
    enc.encryption.algorithm = static_cast<nexpdf::EncryptionAlgorithm>(algorithm);
    enc.encryption.userPassword = QStringLiteral("reader-读者");
    enc.encryption.ownerPassword = QStringLiteral("owner-所有者");
    enc.encryption.permissions.copy = false;
    QString path = d.filePath(QStringLiteral("encrypted.pdf"));
    session.saveAs(path, enc); QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, cryptoTimeoutMs);
    for (int i = 0; i < 3; ++i) {
        path = d.filePath(QStringLiteral("keep-%1.pdf").arg(i));
        session.saveAs(path); QTRY_COMPARE_WITH_TIMEOUT(saved.size(), i + 2, cryptoTimeoutMs);
        nexpdf::DocumentSession reader;
        QSignalSpy prompts(&reader, &nexpdf::DocumentSession::passwordRequired),
            ropen(&reader, &nexpdf::DocumentSession::opened), text(&reader, &nexpdf::DocumentSession::textExtracted);
        reader.open(path); QTRY_COMPARE_WITH_TIMEOUT(prompts.size(), 1, cryptoTimeoutMs);
        nexpdf::OpenOptions auth; auth.password = QStringLiteral("owner-所有者");
        reader.open(path, auth); QTRY_COMPARE_WITH_TIMEOUT(ropen.size(), 1, cryptoTimeoutMs);
        QVERIFY(qvariant_cast<nexpdf::DocumentInfo>(ropen.first().first()).canCopy);
        reader.extractText(0, QRectF(0, 0, 300, 400)); QTRY_COMPARE(text.size(), 1);
        QVERIFY(text.first().at(2).toString().contains(QStringLiteral("nexPDF")));
        // A new session must also retain the existing key when saving with Keep.
        QSignalSpy rsaved(&reader, &nexpdf::DocumentSession::saved);
        reader.saveAs(d.filePath(QStringLiteral("reopened-%1.pdf").arg(i)));
        QTRY_COMPARE_WITH_TIMEOUT(rsaved.size(), 1, cryptoTimeoutMs);
    }
    enc.encryption = {};
    enc.encryption.algorithm = nexpdf::EncryptionAlgorithm::None;
    session.saveAs(d.filePath(QStringLiteral("plain.pdf")), enc); QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 5, cryptoTimeoutMs);
    const QString plain = d.filePath(QStringLiteral("plain-keep.pdf"));
    session.saveAs(plain); QTRY_COMPARE(saved.size(), 6);
    nexpdf::DocumentSession reader;
    QSignalSpy plainOpen(&reader, &nexpdf::DocumentSession::opened);
    reader.open(plain); QTRY_COMPARE(plainOpen.size(), 1);
    QVERIFY(!qvariant_cast<nexpdf::DocumentInfo>(plainOpen.first().first()).encrypted);
    QCOMPARE(failed.size(), 0);
    retainArtifact(plain, QStringLiteral("repeated-save-plain-%1.pdf").arg(algorithm));
}

void DocumentSessionTests::copyRestrictionMustBeHonored()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    nexpdf::DocumentSession s;
    QSignalSpy opened(&s,&nexpdf::DocumentSession::opened), saved(&s,&nexpdf::DocumentSession::saved);
    s.open(writeMultiPageFixture(d)); QTRY_COMPARE(opened.size(),1);
    nexpdf::SaveOptions enc; enc.encryption.algorithm=nexpdf::EncryptionAlgorithm::Aes128;
    enc.encryption.userPassword=QStringLiteral("audit-reader"); enc.encryption.ownerPassword=QStringLiteral("audit-owner");
    enc.encryption.permissions.copy=false; enc.encryption.permissions.print=false;
    enc.encryption.permissions.highQualityPrint=false;
    s.saveAs(d.filePath(QStringLiteral("restricted.pdf")),enc); QTRY_COMPARE(saved.size(),1);
    nexpdf::DocumentSession reader;
    QSignalSpy ropen(&reader,&nexpdf::DocumentSession::opened), extracted(&reader,&nexpdf::DocumentSession::textExtracted),
        failed(&reader,&nexpdf::DocumentSession::failed);
    nexpdf::OpenOptions auth; auth.password=QStringLiteral("audit-reader");
    reader.open(d.filePath(QStringLiteral("restricted.pdf")),auth); QTRY_COMPARE(ropen.size(),1);
    const auto info = qvariant_cast<nexpdf::DocumentInfo>(ropen.first().first());
    QVERIFY(!info.canCopy);
    QVERIFY(!info.canPrint);
    QSignalSpy rendered(&reader, &nexpdf::DocumentSession::renderReady);
    nexpdf::RenderRequest request; request.forPrinting = true; request.requestId = 1;
    reader.requestRender(request); QTRY_COMPARE(rendered.size(), 1);
    QCOMPARE(qvariant_cast<nexpdf::RenderResult>(rendered.first().first()).error.code,
             nexpdf::ErrorCode::PermissionDenied);
    reader.extractText(0,QRectF(0,0,300,400)); QTRY_VERIFY(!extracted.isEmpty() || !failed.isEmpty());
    if(!extracted.isEmpty()) qInfo()<<"Extracted despite copy=false:"<<extracted.first().at(2).toString();
    QVERIFY2(extracted.isEmpty(),"Reader-password session exported text despite copy=false");
    enc.encryption.permissions.print = true;
    s.saveAs(d.filePath(QStringLiteral("low-resolution.pdf")), enc); QTRY_COMPARE(saved.size(), 2);
    reader.open(d.filePath(QStringLiteral("low-resolution.pdf")), auth); QTRY_COMPARE(ropen.size(), 2);
    request.requestId = 2; request.scale = 300.0 / 72;
    reader.requestRender(request); QTRY_COMPARE(rendered.size(), 2);
    QCOMPARE(qvariant_cast<nexpdf::RenderResult>(rendered.last().first()).error.code,
             nexpdf::ErrorCode::PermissionDenied);
    request.requestId = 3; request.scale = 150.0 / 72;
    reader.requestRender(request); QTRY_COMPARE(rendered.size(), 3);
    QVERIFY(!qvariant_cast<nexpdf::RenderResult>(rendered.last().first()).image.isNull());
    auth.password = QStringLiteral("audit-owner");
    reader.open(d.filePath(QStringLiteral("restricted.pdf")), auth); QTRY_COMPARE(ropen.size(), 3);
    QVERIFY(qvariant_cast<nexpdf::DocumentInfo>(ropen.last().first()).canPrintHighQuality);
    request.requestId = 4; request.scale = 300.0 / 72;
    reader.requestRender(request); QTRY_COMPARE(rendered.size(), 4);
    QVERIFY(!qvariant_cast<nexpdf::RenderResult>(rendered.last().first()).image.isNull());
}
void DocumentSessionTests::batchRemovalMustKeepUnselectedWatermark()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    nexpdf::DocumentSession s;
    QSignalSpy opened(&s,&nexpdf::DocumentSession::opened), scanned(&s,&nexpdf::DocumentSession::watermarksScanned),
        state(&s,&nexpdf::DocumentSession::stateChanged), failed(&s,&nexpdf::DocumentSession::failed);
    s.open(writeMultiPageFixture(d,1,true)); QTRY_COMPARE(opened.size(),1);
    s.scanWatermarks(); QTRY_COMPARE(scanned.size(),1);
    const auto initial=qvariant_cast<QVector<nexpdf::WatermarkCandidate>>(scanned.first().first());
    QCOMPARE(initial.size(),3); QCOMPARE(initial.at(2).bounds.left(),220.0);
    auto before=state.size(); s.removeWatermarks({initial.at(0).id,initial.at(1).id});
    QTRY_VERIFY(state.size()>before); QCOMPARE(failed.size(),0);
    scanned.clear(); s.scanWatermarks(); QTRY_COMPARE(scanned.size(),1);
    const auto remaining=qvariant_cast<QVector<nexpdf::WatermarkCandidate>>(scanned.first().first());
    QCOMPARE(remaining.size(),1);
    QCOMPARE(remaining.first().bounds.left(),220.0);
}


void DocumentSessionTests::savePreservesRedoAndFailedSaveState()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    nexpdf::DocumentSession s;
    QSignalSpy opened(&s, &nexpdf::DocumentSession::opened), states(&s, &nexpdf::DocumentSession::stateChanged),
        saved(&s, &nexpdf::DocumentSession::saved), failed(&s, &nexpdf::DocumentSession::failed);
    s.open(writeMultiPageFixture(d)); QTRY_COMPARE(opened.size(), 1);
    auto editAndWait = [&](nexpdf::EditKind kind) {
        const auto before = states.size();
        nexpdf::EditOperation edit; edit.kind = kind; edit.pageIndex = 0; edit.rotation = 90;
        s.applyEdit(edit); QTRY_VERIFY(states.size() > before);
    };
    editAndWait(nexpdf::EditKind::InsertBlankPage);
    s.saveAs(d.filePath(QStringLiteral("saved.pdf"))); QTRY_COMPARE(saved.size(), 1);
    auto before = states.size(); s.undo(); QTRY_VERIFY(states.size() > before);
    QVERIFY(states.last().at(1).toBool());
    before = states.size(); s.redo(); QTRY_VERIFY(states.size() > before);
    QVERIFY(!states.last().at(1).toBool());
    before = states.size(); s.undo(); QTRY_VERIFY(states.size() > before);
    // Saving after undo must not destroy the redo branch or add a synthetic save step.
    s.saveAs(d.filePath(QStringLiteral("one-page.pdf"))); QTRY_COMPARE(saved.size(), 2);
    QVERIFY(states.last().at(3).toBool());
    before = states.size(); s.redo(); QTRY_VERIFY(states.size() > before);
    QVERIFY(states.last().at(1).toBool());
    s.saveAs(d.filePath(QStringLiteral("two-pages.pdf"))); QTRY_COMPARE(saved.size(), 3);
    before = states.size(); s.undo(); QTRY_VERIFY(states.size() > before);
    editAndWait(nexpdf::EditKind::RotatePage);
    QVERIFY(states.last().at(1).toBool()); // same journal position, different branch
    const QString blocked = d.filePath(QStringLiteral("directory.pdf"));
    QVERIFY(QDir().mkpath(blocked));
    nexpdf::SaveOptions replace; replace.overwriteConfirmed = true;
    replace.encryption.algorithm = nexpdf::EncryptionAlgorithm::Aes256;
    replace.encryption.userPassword = QStringLiteral("reader");
    replace.encryption.ownerPassword = QStringLiteral("owner");
    s.saveAs(blocked, replace); QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, cryptoTimeoutMs);
    QVERIFY(states.last().at(1).toBool());
    const QString recovered = d.filePath(QStringLiteral("recovered.pdf"));
    s.saveAs(recovered); QTRY_COMPARE(saved.size(), 4);
    nexpdf::DocumentSession reader; QSignalSpy reopened(&reader, &nexpdf::DocumentSession::opened);
    reader.open(recovered); QTRY_COMPARE(reopened.size(), 1);
    QVERIFY(!qvariant_cast<nexpdf::DocumentInfo>(reopened.first().first()).encrypted);
    QCOMPARE(QDir(d.path()).entryList({QStringLiteral(".nexpdf-*.tmp")}, QDir::Files | QDir::Hidden).size(), 0);
}

void DocumentSessionTests::renderRequestsAlwaysFinish()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    nexpdf::DocumentSession s;
    QSignalSpy opened(&s, &nexpdf::DocumentSession::opened), rendered(&s, &nexpdf::DocumentSession::renderReady),
        closed(&s, &nexpdf::DocumentSession::closed);
    s.open(writeMultiPageFixture(d, 33)); QTRY_COMPARE(opened.size(), 1);
    const auto info = qvariant_cast<nexpdf::DocumentInfo>(opened.first().first());
    nexpdf::RenderRequest req; req.requestId = 1; req.revision = info.revision + 1;
    s.requestRender(req); QTRY_COMPARE(rendered.size(), 1);
    QCOMPARE(qvariant_cast<nexpdf::RenderResult>(rendered.last().first()).error.code, nexpdf::ErrorCode::Cancelled);
    req.requestId = 2; req.revision = info.revision; req.scale = std::numeric_limits<double>::infinity();
    s.requestRender(req); QTRY_COMPARE(rendered.size(), 2);
    QCOMPARE(qvariant_cast<nexpdf::RenderResult>(rendered.last().first()).error.code, nexpdf::ErrorCode::InvalidArgument);
    req.requestId = 3; req.scale = 1000;
    s.requestRender(req); QTRY_COMPARE(rendered.size(), 3);
    QVERIFY(qvariant_cast<nexpdf::RenderResult>(rendered.last().first()).error.isError());
    req.scale = 1;
    for (quint64 id = 4; id <= 36; ++id) {
        req.requestId = id; req.pageIndex = static_cast<int>(id - 4);
        s.requestRender(req); s.cancelRender(id);
    }
    s.close(); QTRY_COMPARE(closed.size(), 1);
    QTRY_COMPARE(rendered.size(), 36);
    QSet<quint64> ids;
    for (const auto &event : rendered) ids.insert(qvariant_cast<nexpdf::RenderResult>(event.first()).requestId);
    QCOMPARE(ids.size(), 36);
    s.open(d.filePath(QStringLiteral("input.pdf"))); QTRY_COMPARE(opened.size(), 2);
    req.requestId = 37; req.revision = 0; req.pageIndex = 0; req.scale = 10; req.timeoutMs = 1;
    s.requestRender(req); QTRY_COMPARE(rendered.size(), 37);
    QCOMPARE(qvariant_cast<nexpdf::RenderResult>(rendered.last().first()).error.code, nexpdf::ErrorCode::Cancelled);
}

void DocumentSessionTests::slicedSearchSurvivesReplacementAndEdits()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    nexpdf::DocumentSession s;
    QSignalSpy opened(&s, &nexpdf::DocumentSession::opened), searched(&s, &nexpdf::DocumentSession::searchFinished),
        states(&s, &nexpdf::DocumentSession::stateChanged), failed(&s, &nexpdf::DocumentSession::failed);
    s.open(writeMultiPageFixture(d, 65)); QTRY_COMPARE(opened.size(), 1);
    s.search(QStringLiteral("nexPDF")); QTRY_COMPARE(searched.size(), 1);
    QCOMPARE(qvariant_cast<QVector<nexpdf::SearchHit>>(searched.first().first()).size(), 65);
    searched.clear();
    s.search(QStringLiteral("nexPDF"));
    nexpdf::EditOperation edit; edit.kind = nexpdf::EditKind::DeletePage; edit.pageIndex = 0;
    const auto before = states.size(); s.applyEdit(edit); QTRY_VERIFY(states.size() > before);
    s.search(QStringLiteral("nexPDF")); QTRY_VERIFY(!searched.isEmpty());
    QTRY_COMPARE(qvariant_cast<QVector<nexpdf::SearchHit>>(searched.last().first()).size(), 64);
    QCOMPARE(failed.size(), 0);
}

void DocumentSessionTests::savePreservesCatalogAndMetadata()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    QByteArray bytes = assemblePdf({
        "<< /Type /Catalog /Pages 2 0 R /Outlines 6 0 R /AcroForm 8 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R /Annots [9 0 R] >>",
        "<< /Length 37 >>\nstream\nBT /F1 18 Tf 40 330 Td (nexPDF) Tj ET\nendstream",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        "<< /Type /Outlines /First 7 0 R /Last 7 0 R /Count 1 >>",
        "<< /Title (Saved bookmark) /Parent 6 0 R /Dest [3 0 R /Fit] >>",
        "<< /Fields [9 0 R] /DA (/F1 12 Tf 0 g) /DR << /Font << /F1 5 0 R >> >> >>",
        "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Saved field) /V (Retained value) /Rect [20 20 180 50] /P 3 0 R >>",
        "<< /Title (Preserved title) /Author (nexPDF regression) >>"
    });
    bytes.replace("/Root 1 0 R >>", "/Root 1 0 R /Info 10 0 R >>");
    const QString input = d.filePath(QStringLiteral("graph.pdf"));
    QFile file(input); QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size()); file.close();
    nexpdf::DocumentSession s; QSignalSpy opened(&s, &nexpdf::DocumentSession::opened), saved(&s, &nexpdf::DocumentSession::saved);
    s.open(input); QTRY_COMPARE(opened.size(), 1);
    nexpdf::SaveOptions options; options.compress = false; options.useObjectStreams = false;
    const QString output = d.filePath(QStringLiteral("saved-graph.pdf"));
    s.saveAs(output, options); QTRY_COMPARE(saved.size(), 1);
    QFile result(output); QVERIFY(result.open(QIODevice::ReadOnly));
    const auto content = result.readAll();
    for (const auto &key : {"/Outlines", "/AcroForm", "Saved bookmark", "Saved field", "Retained value", "Preserved title"}) {
        QVERIFY2(content.contains(key), key);
    }
    nexpdf::DocumentSession reopened; QSignalSpy verified(&reopened, &nexpdf::DocumentSession::opened);
    reopened.open(output); QTRY_COMPARE(verified.size(), 1);
    QCOMPARE(qvariant_cast<nexpdf::DocumentInfo>(verified.first().first()).title, QStringLiteral("Preserved title"));
    retainArtifact(output, QStringLiteral("saved-graph.pdf"));
}

void DocumentSessionTests::replacesOpenSourceWithoutLosingUndo()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    const QString input = writeMultiPageFixture(d);
    nexpdf::DocumentSession s;
    QSignalSpy opened(&s, &nexpdf::DocumentSession::opened), states(&s, &nexpdf::DocumentSession::stateChanged),
        saved(&s, &nexpdf::DocumentSession::saved), failed(&s, &nexpdf::DocumentSession::failed);
    s.open(input); QTRY_COMPARE(opened.size(), 1);
    auto before = states.size();
    nexpdf::EditOperation edit; edit.kind = nexpdf::EditKind::InsertBlankPage; edit.pageIndex = 1;
    s.applyEdit(edit); QTRY_VERIFY(states.size() > before);
    nexpdf::SaveOptions options; options.overwriteConfirmed = true;
    s.saveAs(input, options); QTRY_VERIFY(saved.size() == 1 || !failed.isEmpty());
    QVERIFY2(failed.isEmpty(), qPrintable(errorDescription(failed)));
    before = states.size(); s.undo(); QTRY_VERIFY(states.size() > before);
    QVERIFY(states.last().at(1).toBool());
    s.saveAs(input, options); QTRY_VERIFY(saved.size() == 2 || !failed.isEmpty());
    QVERIFY2(failed.isEmpty(), qPrintable(errorDescription(failed)));
    nexpdf::DocumentSession verify; QSignalSpy verified(&verify, &nexpdf::DocumentSession::opened),
        text(&verify, &nexpdf::DocumentSession::textExtracted);
    verify.open(input); QTRY_COMPARE(verified.size(), 1);
    QCOMPARE(qvariant_cast<nexpdf::DocumentInfo>(verified.first().first()).pageCount, 1);
    verify.extractText(0, QRectF(0, 0, 300, 400)); QTRY_COMPARE(text.size(), 1);
    QVERIFY(text.first().at(2).toString().contains(QStringLiteral("nexPDF")));
}

void DocumentSessionTests::protectedExportsRequireAuthorization_data()
{
    QTest::addColumn<int>("operation");
    QTest::addColumn<bool>("emptyUserPassword");
    QTest::newRow("decrypt") << 0 << false;
    QTest::newRow("reencrypt") << 1 << false;
    QTest::newRow("import-pages") << 2 << false;
    QTest::newRow("empty-user-decrypt") << 0 << true;
    QTest::newRow("empty-user-reencrypt") << 1 << true;
    QTest::newRow("empty-user-import") << 2 << true;
}

void DocumentSessionTests::protectedExportsRequireAuthorization()
{
    QFETCH(int, operation);
    QFETCH(bool, emptyUserPassword);
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString input = writeFixture(dir.path());
    const QString encrypted = dir.filePath(QStringLiteral("restricted.pdf"));
    const QString output = dir.filePath(QStringLiteral("export.pdf"));
    nexpdf::DocumentSession writer;
    QSignalSpy opened(&writer, &nexpdf::DocumentSession::opened), saved(&writer, &nexpdf::DocumentSession::saved);
    writer.open(input); QTRY_COMPARE(opened.size(), 1);
    nexpdf::SaveOptions encryption;
    encryption.encryption.algorithm = nexpdf::EncryptionAlgorithm::Aes256;
    encryption.encryption.userPassword = QStringLiteral("reader");
    encryption.encryption.ownerPassword = QStringLiteral("owner");
    encryption.encryption.permissions.copy = false;
    encryption.encryption.permissions.print = false;
    if (emptyUserPassword) {
        QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/empty-user-restricted.pdf"), encrypted));
    } else {
        writer.saveAs(encrypted, encryption); QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, cryptoTimeoutMs);
    }

    nexpdf::DocumentSession reader;
    QSignalSpy ropen(&reader, &nexpdf::DocumentSession::opened), failed(&reader, &nexpdf::DocumentSession::failed),
        exported(&reader, &nexpdf::DocumentSession::saved), states(&reader, &nexpdf::DocumentSession::stateChanged),
        pageCounts(&reader, &nexpdf::DocumentSession::pageCountChanged);
    nexpdf::OpenOptions auth;
    if (!emptyUserPassword) auth.password = QStringLiteral("reader");
    reader.open(operation == 2 ? input : encrypted, operation == 2 ? nexpdf::OpenOptions{} : auth);
    QTRY_COMPARE_WITH_TIMEOUT(ropen.size(), 1, cryptoTimeoutMs);
    QTRY_VERIFY(!states.isEmpty());
    const auto before = states.size();
    nexpdf::EditOperation import;
    import.kind = nexpdf::EditKind::ImportPages;
    import.sourcePath = encrypted; import.sourcePassword = auth.password;
    nexpdf::SaveOptions options = encryption;
    if (operation == 0) options.encryption.algorithm = nexpdf::EncryptionAlgorithm::None;
    if (operation == 2) reader.applyEdit(import); else reader.saveAs(output, options);
    QTRY_VERIFY_WITH_TIMEOUT(!failed.isEmpty() || !exported.isEmpty() || !pageCounts.isEmpty(), cryptoTimeoutMs);
    if (failed.isEmpty()) qInfo() << "Unauthorized operation succeeded:" << operation
                                 << "saved:" << exported.size() << "page-count events:" << pageCounts.size();
    QCOMPARE(failed.size(), 1);
    QCOMPARE(qvariant_cast<nexpdf::OperationError>(failed.first().first()).code, nexpdf::ErrorCode::PermissionDenied);
    QCOMPARE(states.size(), before);
    QVERIFY(!QFileInfo::exists(output));

    // Denied export must leave the session usable; an owner can perform the operation.
    if (operation != 2) {
        reader.saveAs(dir.filePath(QStringLiteral("keep.pdf"))); QTRY_COMPARE_WITH_TIMEOUT(exported.size(), 1, cryptoTimeoutMs);
        auth.password = QStringLiteral("owner");
        reader.open(encrypted, auth); QTRY_COMPARE_WITH_TIMEOUT(ropen.size(), 2, cryptoTimeoutMs);
        reader.saveAs(output, options); QTRY_COMPARE_WITH_TIMEOUT(exported.size(), 2, cryptoTimeoutMs);
    } else {
        import.sourcePassword = QStringLiteral("owner");
        reader.applyEdit(import); QTRY_VERIFY_WITH_TIMEOUT(states.size() > before, cryptoTimeoutMs);
    }
    QCOMPARE(failed.size(), 1);
}

void DocumentSessionTests::failedImportPreservesHistory_data()
{
    QTest::addColumn<bool>("hasPreviousEdit");
    QTest::newRow("from-original") << false;
    QTest::newRow("from-saved-edit") << true;
}

void DocumentSessionTests::failedImportPreservesHistory()
{
    QFETCH(bool, hasPreviousEdit);
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString input = writeFixture(dir.path());
    nexpdf::DocumentSession session;
    QSignalSpy opened(&session, &nexpdf::DocumentSession::opened), states(&session, &nexpdf::DocumentSession::stateChanged),
        failed(&session, &nexpdf::DocumentSession::failed), saved(&session, &nexpdf::DocumentSession::saved),
        counts(&session, &nexpdf::DocumentSession::pageCountChanged);
    session.open(input); QTRY_COMPARE(opened.size(), 1); QTRY_COMPARE(states.size(), 1);
    QVERIFY(!session.hasUnsavedChanges());
    nexpdf::EditOperation edit; edit.kind = nexpdf::EditKind::InsertBlankPage;
    if (hasPreviousEdit) {
        session.applyEdit(edit); QTRY_COMPARE(counts.size(), 1); QTRY_COMPARE(states.size(), 2);
        session.saveAs(dir.filePath(QStringLiteral("baseline.pdf"))); QTRY_COMPARE(saved.size(), 1);
        QTRY_VERIFY(!session.hasUnsavedChanges()); QTRY_COMPARE(states.size(), 3);
    }
    auto beforeState = states.size();
    session.applyEdit(edit); QVERIFY(session.hasUnsavedChanges());
    QTRY_COMPARE(states.size(), beforeState + 1);
    session.undo(); QTRY_COMPARE(states.size(), beforeState + 2);
    QTRY_VERIFY(!session.hasUnsavedChanges());
    QVERIFY(states.last().at(3).toBool());
    beforeState = states.size();
    edit.kind = nexpdf::EditKind::ImportPages;
    edit.sourcePath = input;
    edit.sourcePages = {0, 99}; // Fail after one page has already been grafted.
    session.applyEdit(edit); QTRY_COMPARE(failed.size(), 1);
    QTRY_VERIFY(!session.hasUnsavedChanges());
    edit.kind = nexpdf::EditKind::AddImage;
    edit.imagePath = dir.filePath(QStringLiteral("missing-image.png"));
    edit.bounds = QRectF(20, 20, 50, 50);
    session.applyEdit(edit); QTRY_COMPARE(failed.size(), 2);
    QTRY_VERIFY(!session.hasUnsavedChanges());
    QCOMPARE(states.size(), beforeState);
    const QString output = dir.filePath(QStringLiteral("after-failure.pdf"));
    const auto beforeSave = saved.size();
    session.saveAs(output); QTRY_COMPARE(saved.size(), beforeSave + 1);
    nexpdf::DocumentSession verification;
    QSignalSpy verified(&verification, &nexpdf::DocumentSession::opened);
    verification.open(output); QTRY_COMPARE(verified.size(), 1);
    QCOMPARE(qvariant_cast<nexpdf::DocumentInfo>(verified.first().first()).pageCount, hasPreviousEdit ? 2 : 1);
    const auto before = counts.size();
    session.redo(); QTRY_VERIFY(counts.size() > before);
    QCOMPARE(counts.last().first().toInt(), hasPreviousEdit ? 3 : 2);
    QVERIFY(session.hasUnsavedChanges());
    QCOMPARE(failed.size(), 2);
}

QTEST_MAIN(DocumentSessionTests)
#include "test_document_session.moc"
