#pragma once

#include <QColor>
#include <QImage>
#include <QMetaType>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace nexpdf {

enum class ErrorCode {
    None,
    InvalidArgument,
    FileNotFound,
    UnsupportedFormat,
    PasswordRequired,
    IncorrectPassword,
    PermissionDenied,
    SignedDocumentRequiresSaveAs,
    UnsafeWatermarkRemoval,
    Cancelled,
    IoError,
    ValidationFailed,
    EngineError
};

struct OperationError {
    ErrorCode code = ErrorCode::None;
    QString message;
    QString detail;
    QString operation;

    [[nodiscard]] bool isError() const noexcept { return code != ErrorCode::None; }
};

struct OpenOptions {
    QString password;
    bool allowRepair = true;
};

struct DocumentPermissions {
    bool print = true;
    bool copy = true;
    bool annotate = true;
    bool fillForms = true;
    bool assemble = true;
    bool modify = true;
    bool accessibility = true;
    bool highQualityPrint = true;
};

enum class EncryptionAlgorithm { Keep, None, Aes128, Aes256 };

struct EncryptionOptions {
    EncryptionAlgorithm algorithm = EncryptionAlgorithm::Keep;
    QString userPassword;
    QString ownerPassword;
    DocumentPermissions permissions;
};

enum class EditKind {
    ImportPages,
    InsertBlankPage,
    DeletePage,
    MovePage,
    RotatePage,
    AddText,
    AddImage,
    AddHighlight,
    AddUnderline,
    AddStrikeOut,
    AddFreeText,
    AddRectangle,
    AddEllipse,
    AddInk,
    AddRedactionPreview,
    ApplyRedactions,
    MoveObject,
    ResizeObject,
    DeleteObject
};

struct EditOperation {
    EditKind kind = EditKind::InsertBlankPage;
    int pageIndex = 0;
    int destinationIndex = 0;
    int rotation = 0;
    QRectF sourceBounds;
    QRectF bounds;
    QVector<QPointF> points;
    QString text;
    QString objectId;
    QString sourcePath;
    QString sourcePassword;
    QVector<int> sourcePages;
    QString imagePath;
    QColor color = QColor(255, 220, 0);
    qreal opacity = 1.0;
    qreal fontSize = 12.0;
};

struct RenderRequest {
    quint64 requestId = 0;
    quint64 revision = 0;
    int pageIndex = 0;
    qreal scale = 1.0;
    int rotation = 0;
    QRect tilePixels;
    QColor background = Qt::white;
    int priority = 0;
};

struct RenderResult {
    quint64 requestId = 0;
    quint64 revision = 0;
    int pageIndex = 0;
    QRect tilePixels;
    QSize pagePixelSize;
    QImage image;
};

enum class WatermarkKind { Text, Image };
enum class WatermarkLayer { Background, Foreground };
enum class WatermarkRemovalSafety { Exact, ReviewRequired, Unsupported };

struct WatermarkSpec {
    WatermarkKind kind = WatermarkKind::Text;
    QString text;
    QString imagePath;
    QString fontFamily;
    QColor color = QColor(128, 128, 128);
    qreal opacity = 0.25;
    qreal rotation = -35.0;
    qreal scale = 0.45;
    QPointF position = QPointF(0.5, 0.5);
    QVector<int> pages;
    WatermarkLayer layer = WatermarkLayer::Foreground;
};

struct WatermarkCandidate {
    QString id;
    QString label;
    QVector<int> pages;
    QRectF bounds;
    qreal confidence = 0.0;
    WatermarkRemovalSafety safety = WatermarkRemovalSafety::ReviewRequired;
    bool createdByNexPDF = false;
};

struct SaveOptions {
    bool overwriteConfirmed = false;
    bool garbageCollect = true;
    bool compress = true;
    bool useObjectStreams = true;
    EncryptionOptions encryption;
};

struct SearchHit {
    int pageIndex = 0;
    QVector<QRectF> quads;
    QString preview;
};

struct DocumentInfo {
    QString path;
    QString title;
    int pageCount = 0;
    bool encrypted = false;
    bool signedDocument = false;
    quint64 revision = 0;
};

} // namespace nexpdf

Q_DECLARE_METATYPE(nexpdf::OperationError)
Q_DECLARE_METATYPE(nexpdf::OpenOptions)
Q_DECLARE_METATYPE(nexpdf::EncryptionOptions)
Q_DECLARE_METATYPE(nexpdf::EditOperation)
Q_DECLARE_METATYPE(nexpdf::RenderRequest)
Q_DECLARE_METATYPE(nexpdf::RenderResult)
Q_DECLARE_METATYPE(nexpdf::WatermarkSpec)
Q_DECLARE_METATYPE(nexpdf::WatermarkCandidate)
Q_DECLARE_METATYPE(QVector<nexpdf::WatermarkCandidate>)
Q_DECLARE_METATYPE(nexpdf::SaveOptions)
Q_DECLARE_METATYPE(nexpdf::SearchHit)
Q_DECLARE_METATYPE(QVector<nexpdf::SearchHit>)
Q_DECLARE_METATYPE(nexpdf::DocumentInfo)
