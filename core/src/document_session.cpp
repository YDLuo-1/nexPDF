#include "nexpdf/document_session.h"

#include "mupdf_runtime.h"

#include <mupdf/pdf.h>

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QPainter>
#include <QPointer>
#include <QSet>
#include <QScopeGuard>
#include <QThreadPool>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <QRunnable>
#include <utility>

#ifdef Q_OS_WIN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <cerrno>
#endif

namespace nexpdf {
namespace {

constexpr std::size_t kDefaultMuPdfStoreBytes = 64ULL * 1024ULL * 1024ULL;
constexpr qsizetype kMaximumCachedDisplayLists = 8;
constexpr int kMaximumTileEdge = 1024;
constexpr auto kNexPdfWatermarkPrefix = "nexPDF:watermark:";

void secureZero(void *data, std::size_t size) noexcept
{
    auto *byte = static_cast<volatile unsigned char *>(data);
    while (size-- > 0) {
        *byte++ = 0;
    }
}

void secureClear(QByteArray &value) noexcept
{
    if (value.isEmpty()) {
        return;
    }
    value.detach();
    secureZero(value.data(), static_cast<std::size_t>(value.size()));
    value.clear();
}

QString engineMessage(fz_context *context)
{
    const char *message = fz_caught_message(context);
    return message == nullptr ? QStringLiteral("Unknown MuPDF error")
                              : QString::fromUtf8(message);
}

fz_rect toFzRect(const QRectF &rect)
{
    return {
        static_cast<float>(rect.left()),
        static_cast<float>(rect.top()),
        static_cast<float>(rect.right()),
        static_cast<float>(rect.bottom())
    };
}

QRectF toQRect(const fz_rect rect)
{
    return QRectF(QPointF(rect.x0, rect.y0), QPointF(rect.x1, rect.y1)).normalized();
}

std::array<float, 3> rgb(const QColor &color)
{
    return {
        static_cast<float>(color.redF()),
        static_cast<float>(color.greenF()),
        static_cast<float>(color.blueF())
    };
}

int normalizedRotation(int rotation)
{
    rotation %= 360;
    if (rotation < 0) {
        rotation += 360;
    }
    return ((rotation + 45) / 90 * 90) % 360;
}

struct RawRenderOutput {
    fz_pixmap *pixmap = nullptr;
    int pageWidth = 0;
    int pageHeight = 0;
    int tileX = 0;
    int tileY = 0;
    int tileWidth = 0;
    int tileHeight = 0;
    char error[256]{};
};

bool renderDisplayList(fz_context *context, fz_display_list *displayList, const fz_rect bounds,
                       const double scale, const int rotation, const int requestedX,
                       const int requestedY, const int requestedWidth, const int requestedHeight,
                       const int backgroundValue, RawRenderOutput *output)
{
    fz_pixmap *pixmap = nullptr;
    fz_device *device = nullptr;
    int failed = 0;
    fz_var(pixmap);
    fz_var(device);
    fz_var(failed);

    fz_try(context) {
        const fz_matrix transform = fz_transform_page(
            bounds, static_cast<float>(72.0 * scale), static_cast<float>(rotation));
        const fz_irect fullPixels = fz_round_rect(fz_transform_rect(bounds, transform));
        output->pageWidth = fullPixels.x1 - fullPixels.x0;
        output->pageHeight = fullPixels.y1 - fullPixels.y0;

        int x0 = requestedWidth > 0 ? requestedX : 0;
        int y0 = requestedHeight > 0 ? requestedY : 0;
        int x1 = requestedWidth > 0 ? requestedX + requestedWidth : output->pageWidth;
        int y1 = requestedHeight > 0 ? requestedY + requestedHeight : output->pageHeight;
        x0 = fz_maxi(0, fz_mini(x0, output->pageWidth));
        y0 = fz_maxi(0, fz_mini(y0, output->pageHeight));
        x1 = fz_maxi(0, fz_mini(x1, output->pageWidth));
        y1 = fz_maxi(0, fz_mini(y1, output->pageHeight));
        if (x1 <= x0 || y1 <= y0) {
            fz_throw(context, FZ_ERROR_ARGUMENT, "Render tile does not intersect the page");
        }
        output->tileX = x0;
        output->tileY = y0;
        output->tileWidth = x1 - x0;
        output->tileHeight = y1 - y0;

        const fz_irect tileBox = {
            fullPixels.x0 + x0, fullPixels.y0 + y0,
            fullPixels.x0 + x1, fullPixels.y0 + y1
        };
        pixmap = fz_new_pixmap_with_bbox(context, fz_device_rgb(context), tileBox, nullptr, 0);
        fz_clear_pixmap_with_value(context, pixmap, backgroundValue);
        device = fz_new_draw_device_with_bbox(context, fz_identity, pixmap, &tileBox);
        fz_run_display_list(context, displayList, device, transform, fz_infinite_rect, nullptr);
        fz_close_device(context, device);
    }
    fz_always(context) {
        fz_drop_device(context, device);
    }
    fz_catch(context) {
        failed = 1;
        const char *message = fz_caught_message(context);
        if (message != nullptr) {
            std::strncpy(output->error, message, sizeof(output->error) - 1);
            output->error[sizeof(output->error) - 1] = '\0';
        }
    }

    if (failed) {
        fz_drop_pixmap(context, pixmap);
        return false;
    }
    output->pixmap = pixmap;
    return true;
}

QImage copyPixmap(fz_context *context, fz_pixmap *pixmap)
{
    const int width = fz_pixmap_width(context, pixmap);
    const int height = fz_pixmap_height(context, pixmap);
    const int stride = fz_pixmap_stride(context, pixmap);
    const unsigned char *samples = fz_pixmap_samples(context, pixmap);

    QImage result(width, height, QImage::Format_RGB888);
    for (int row = 0; row < height; ++row) {
        std::memcpy(result.scanLine(row), samples + row * stride,
                    static_cast<std::size_t>(width) * 3U);
    }
    return result;
}

QImage makeTextWatermarkImage(const WatermarkSpec &spec)
{
    const QString text = spec.text.trimmed();
    QFont font(spec.fontFamily);
    font.setPixelSize(72);
    font.setWeight(QFont::DemiBold);
    const QFontMetrics metrics(font);
    const QSize textSize = metrics.boundingRect(text).size() + QSize(40, 32);

    QImage source(textSize, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::transparent);
    {
        QPainter painter(&source);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setFont(font);
        painter.setPen(spec.color);
        painter.drawText(source.rect(), Qt::AlignCenter, text);
    }

    const QTransform transform = QTransform().rotate(spec.rotation);
    return source.transformed(transform, Qt::SmoothTransformation);
}

bool replaceAtomically(const QString &temporaryPath, const QString &targetPath, QString *detail)
{
#ifdef Q_OS_WIN
    const std::wstring source = QDir::toNativeSeparators(temporaryPath).toStdWString();
    const std::wstring target = QDir::toNativeSeparators(targetPath).toStdWString();
    const DWORD flags = MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH;
    if (MoveFileExW(source.c_str(), target.c_str(), flags) != 0) {
        return true;
    }
    if (detail != nullptr) {
        *detail = QStringLiteral("MoveFileExW failed with error %1").arg(GetLastError());
    }
    return false;
#else
    const QByteArray source = QFile::encodeName(temporaryPath);
    const QByteArray target = QFile::encodeName(targetPath);
    if (::rename(source.constData(), target.constData()) == 0) {
        return true;
    }
    if (detail != nullptr) {
        *detail = QString::fromLocal8Bit(std::strerror(errno));
    }
    return false;
#endif
}

struct QtOutputState {
    QIODevice *device = nullptr;
};

void writeQtOutput(fz_context *context, void *opaque, const void *data, const size_t size)
{
    auto *state = static_cast<QtOutputState *>(opaque);
    const auto written = state->device->write(static_cast<const char *>(data),
                                               static_cast<qint64>(size));
    if (written != static_cast<qint64>(size)) {
        fz_throw(context, FZ_ERROR_SYSTEM, "Unable to write the temporary PDF");
    }
}

void seekQtOutput(fz_context *context, void *opaque, const int64_t offset, const int whence)
{
    auto *state = static_cast<QtOutputState *>(opaque);
    qint64 target = 0;
    switch (whence) {
    case SEEK_SET:
        target = static_cast<qint64>(offset);
        break;
    case SEEK_CUR:
        target = state->device->pos() + static_cast<qint64>(offset);
        break;
    case SEEK_END:
        target = state->device->size() + static_cast<qint64>(offset);
        break;
    default:
        fz_throw(context, FZ_ERROR_ARGUMENT, "Invalid seek mode for temporary PDF");
    }
    if (target < 0 || !state->device->seek(target)) {
        fz_throw(context, FZ_ERROR_SYSTEM, "Unable to seek in the temporary PDF");
    }
}

int64_t tellQtOutput(fz_context *context, void *opaque)
{
    auto *state = static_cast<QtOutputState *>(opaque);
    const qint64 position = state->device->pos();
    if (position < 0) {
        fz_throw(context, FZ_ERROR_SYSTEM, "Unable to tell the temporary PDF position");
    }
    return static_cast<int64_t>(position);
}

void closeQtOutput(fz_context *context, void *opaque)
{
    auto *state = static_cast<QtOutputState *>(opaque);
    auto *file = qobject_cast<QFileDevice *>(state->device);
    if (file != nullptr && !file->flush()) {
        fz_throw(context, FZ_ERROR_SYSTEM, "Unable to flush the temporary PDF");
    }
}

enum pdf_annot_type annotationTypeFor(const EditKind kind)
{
    switch (kind) {
    case EditKind::AddText:
    case EditKind::AddFreeText:
        return PDF_ANNOT_FREE_TEXT;
    case EditKind::AddImage:
        return PDF_ANNOT_STAMP;
    case EditKind::AddHighlight:
        return PDF_ANNOT_HIGHLIGHT;
    case EditKind::AddUnderline:
        return PDF_ANNOT_UNDERLINE;
    case EditKind::AddStrikeOut:
        return PDF_ANNOT_STRIKE_OUT;
    case EditKind::AddRectangle:
        return PDF_ANNOT_SQUARE;
    case EditKind::AddEllipse:
        return PDF_ANNOT_CIRCLE;
    case EditKind::AddInk:
        return PDF_ANNOT_INK;
    case EditKind::AddRedactionPreview:
        return PDF_ANNOT_REDACT;
    default:
        return PDF_ANNOT_UNKNOWN;
    }
}

class DisplayListRenderTask final : public QRunnable {
public:
    DisplayListRenderTask(QPointer<DocumentSession> owner, fz_context *context,
                          fz_display_list *displayList, const fz_rect bounds,
                          RenderRequest request, const quint64 revision)
        : owner_(std::move(owner)), context_(context), displayList_(displayList),
          bounds_(bounds), request_(std::move(request)), revision_(revision)
    {
        setAutoDelete(true);
    }

    ~DisplayListRenderTask() override { cleanup(); }

    void run() override
    {
        RenderResult result;
        result.requestId = request_.requestId;
        result.revision = revision_;
        result.pageIndex = request_.pageIndex;
        RawRenderOutput raw;
        const QRect requestedTile = request_.tilePixels;
        const bool rendered = renderDisplayList(
            context_, displayList_, bounds_, request_.scale,
            normalizedRotation(request_.rotation), requestedTile.x(), requestedTile.y(),
            requestedTile.width(), requestedTile.height(), request_.background.lightness(), &raw);
        QString detail;
        if (rendered) {
            result.pagePixelSize = QSize(raw.pageWidth, raw.pageHeight);
            result.tilePixels = QRect(raw.tileX, raw.tileY, raw.tileWidth, raw.tileHeight);
            result.image = copyPixmap(context_, raw.pixmap);
            fz_drop_pixmap(context_, raw.pixmap);
        } else {
            detail = QString::fromUtf8(raw.error);
        }
        cleanup();

        const QPointer<DocumentSession> owner = owner_;
        if (!owner) {
            return;
        }
        if (!rendered) {
            OperationError error{ErrorCode::EngineError,
                QCoreApplication::translate("DocumentSession", "Unable to render the page."),
                detail, QStringLiteral("render")};
            QMetaObject::invokeMethod(owner.data(), [owner, error] {
                if (owner) emit owner->failed(error);
            }, Qt::QueuedConnection);
        } else {
            QMetaObject::invokeMethod(owner.data(), [owner, result] {
                if (owner) emit owner->renderReady(result);
            }, Qt::QueuedConnection);
        }
    }

private:
    void cleanup()
    {
        if (context_ != nullptr) {
            fz_drop_display_list(context_, displayList_);
            fz_drop_context(context_);
        }
        context_ = nullptr;
        displayList_ = nullptr;
    }

    QPointer<DocumentSession> owner_;
    fz_context *context_ = nullptr;
    fz_display_list *displayList_ = nullptr;
    fz_rect bounds_{};
    RenderRequest request_;
    quint64 revision_ = 0;
};

} // namespace

class DocumentSession::Impl final {
public:
    explicit Impl(DocumentSession *owner) : owner_(owner) {}

    ~Impl() { closeDocument(false); }

    void initialize()
    {
        try {
            runtime_ = std::make_unique<detail::MuPdfRuntime>(kDefaultMuPdfStoreBytes);
            renderPool_.setObjectName(QStringLiteral("nexPDF render pool"));
            renderPool_.setMaxThreadCount(std::max(1, QThread::idealThreadCount() - 1));
        } catch (const std::exception &exception) {
            fail(ErrorCode::EngineError, QStringLiteral("initialize"),
                 tr("Unable to initialize the PDF engine."),
                 QString::fromUtf8(exception.what()));
        }
    }

    void openDocument(const QString &path, const OpenOptions &options)
    {
        if (!runtime_) {
            fail(ErrorCode::EngineError, QStringLiteral("open"),
                 tr("The PDF engine is not available."));
            return;
        }
        const QFileInfo file(path);
        if (!file.isFile()) {
            fail(ErrorCode::FileNotFound, QStringLiteral("open"),
                 tr("The selected PDF does not exist."), path);
            return;
        }

        closeDocument(false);
        fz_context *context = runtime_->context();
        fz_document *candidate = nullptr;
        bool needsPassword = false;
        bool encryptedDocument = false;
        bool incorrectPassword = false;
        int pageCount = 0;
        bool signedDocument = false;
        char title[512]{};
        const QByteArray nativePath = QDir::toNativeSeparators(file.absoluteFilePath()).toUtf8();
        QByteArray password = options.password.toUtf8();
        auto clearPassword = qScopeGuard([&password] { secureClear(password); });
        fz_var(candidate);
        fz_var(needsPassword);
        fz_var(encryptedDocument);
        fz_var(incorrectPassword);
        fz_var(pageCount);
        fz_var(signedDocument);
        fz_var(title);

        fz_try(context) {
            candidate = fz_open_document(context, nativePath.constData());
            pdf_document *candidatePdf = pdf_specifics(context, candidate);
            if (candidatePdf == nullptr) {
                fz_throw(context, FZ_ERROR_UNSUPPORTED, "nexPDF currently edits PDF documents only");
            }
            encryptedDocument = pdf_dict_get(
                context, pdf_trailer(context, candidatePdf), PDF_NAME(Encrypt)) != nullptr;
            needsPassword = fz_needs_password(context, candidate) != 0;
            if (needsPassword && fz_authenticate_password(context, candidate, password.constData()) == 0) {
                incorrectPassword = true;
            } else {
                pdf_disable_js(context, candidatePdf);
                pageCount = fz_count_pages(context, candidate);
                signedDocument = pdf_count_signatures(context, candidatePdf) > 0;
                (void)fz_lookup_metadata(context, candidate, FZ_META_INFO_TITLE, title, sizeof(title));
                pdf_enable_journal(context, candidatePdf);
            }
        }
        fz_catch(context) {
            const QString detail = engineMessage(context);
            fz_drop_document(context, candidate);
            fail(ErrorCode::EngineError, QStringLiteral("open"),
                 tr("Unable to open the PDF."), detail);
            return;
        }

        if (needsPassword && incorrectPassword) {
            fz_drop_document(context, candidate);
            if (password.isEmpty()) {
                post([path](DocumentSession *owner) { emit owner->passwordRequired(path); });
            } else {
                fail(ErrorCode::IncorrectPassword, QStringLiteral("open"),
                     tr("The password is incorrect."));
            }
            return;
        }

        document_ = candidate;
        pdf_ = pdf_specifics(context, document_);
        path_ = file.absoluteFilePath();
        encrypted_ = encryptedDocument;
        password_ = password;
        signedDocument_ = signedDocument;
        pageCount_ = pageCount;
        modified_ = false;
        ++revision_;

        DocumentInfo info;
        info.path = path_;
        info.title = QString::fromUtf8(title);
        if (info.title.trimmed().isEmpty()) {
            info.title = file.completeBaseName();
        }
        info.pageCount = pageCount_;
        info.encrypted = encrypted_;
        info.signedDocument = signedDocument_;
        info.revision = revision_;
        post([info](DocumentSession *owner) { emit owner->opened(info); });
        emitState();
    }

    void closeDocument(const bool notify = true)
    {
        renderPool_.clear();
        renderPool_.waitForDone();
        dropDisplayLists();
        if (runtime_ && document_ != nullptr) {
            fz_drop_document(runtime_->context(), document_);
        }
        document_ = nullptr;
        pdf_ = nullptr;
        path_.clear();
        secureClear(password_);
        pageCount_ = 0;
        encrypted_ = false;
        signedDocument_ = false;
        modified_ = false;
        candidates_.clear();
        if (notify) {
            post([](DocumentSession *owner) { emit owner->closed(); });
        }
    }

    void render(const RenderRequest &request)
    {
        if (!requireDocument(QStringLiteral("render"))) {
            return;
        }
        if (request.revision != 0 && request.revision != revision_) {
            return;
        }
        if (request.pageIndex < 0 || request.pageIndex >= pageCount_ || request.scale <= 0.0) {
            fail(ErrorCode::InvalidArgument, QStringLiteral("render"),
                 tr("The render request is outside the document."));
            return;
        }
        if (!request.tilePixels.isEmpty()
            && (request.tilePixels.width() > kMaximumTileEdge
                || request.tilePixels.height() > kMaximumTileEdge)) {
            fail(ErrorCode::InvalidArgument, QStringLiteral("render"),
                 tr("A render tile cannot be larger than 1024 pixels per edge."));
            return;
        }

        fz_context *context = runtime_->context();
        fz_page *page = nullptr;
        fz_display_list *list = displayLists_.value(request.pageIndex, nullptr);
        fz_display_list *jobList = nullptr;
        fz_context *jobContext = nullptr;
        fz_rect bounds{};
        bool createdList = false;
        bool setupFailed = false;
        QString detail;

        if (list != nullptr) {
            bounds = displayListBounds_.value(request.pageIndex);
        }
        fz_var(page);
        fz_var(list);
        fz_var(jobList);
        fz_var(jobContext);
        fz_var(bounds);
        fz_var(createdList);

        fz_try(context) {
            if (list == nullptr) {
                page = fz_load_page(context, document_, request.pageIndex);
                bounds = fz_bound_page(context, page);
                list = fz_new_display_list_from_page(context, page);
                createdList = true;
            }
            jobList = fz_keep_display_list(context, list);
            jobContext = fz_clone_context(context);
            if (jobContext == nullptr) {
                fz_throw(context, FZ_ERROR_SYSTEM, "Unable to clone the MuPDF render context");
            }
        }
        fz_always(context) { fz_drop_page(context, page); }
        fz_catch(context) {
            setupFailed = true;
            detail = engineMessage(context);
            fz_drop_display_list(context, jobList);
            fz_drop_context(jobContext);
            if (createdList) {
                fz_drop_display_list(context, list);
            }
        }
        if (setupFailed) {
            fail(ErrorCode::EngineError, QStringLiteral("render"),
                 tr("Unable to render the page."), detail);
            return;
        }
        if (createdList) {
            displayLists_.insert(request.pageIndex, list);
            displayListBounds_.insert(request.pageIndex, bounds);
        }
        touchDisplayList(request.pageIndex);
        renderPool_.start(new DisplayListRenderTask(owner_, jobContext, jobList, bounds,
                                                    request, revision_), request.priority);
    }

    void search(const QString &needle)
    {
        if (!requireDocument(QStringLiteral("search"))) {
            return;
        }
        if (needle.trimmed().isEmpty()) {
            post([](DocumentSession *owner) { emit owner->searchFinished({}); });
            return;
        }

        QVector<SearchHit> results;
        const QByteArray utf8 = needle.toUtf8();
        fz_context *context = runtime_->context();
        bool failedSearch = false;
        QString detail;
        for (int pageIndex = 0; pageIndex < pageCount_ && !failedSearch; ++pageIndex) {
            fz_page *page = nullptr;
            std::array<fz_quad, 256> hits{};
            std::array<int, 256> marks{};
            int count = 0;
            fz_var(page);
            fz_var(hits);
            fz_var(marks);
            fz_var(count);
            fz_try(context) {
                page = fz_load_page(context, document_, pageIndex);
                count = fz_search_page(context, page, utf8.constData(), marks.data(),
                                       hits.data(), static_cast<int>(hits.size()));
            }
            fz_always(context) { fz_drop_page(context, page); }
            fz_catch(context) {
                failedSearch = true;
                detail = engineMessage(context);
            }
            for (int i = 0; i < count; ++i) {
                const fz_quad &quad = hits[static_cast<std::size_t>(i)];
                SearchHit hit;
                hit.pageIndex = pageIndex;
                hit.preview = needle;
                hit.quads.append(QRectF(QPointF(quad.ul.x, quad.ul.y),
                                        QPointF(quad.lr.x, quad.lr.y)).normalized());
                results.append(std::move(hit));
            }
            post([pageIndex, total = pageCount_](DocumentSession *owner) {
                emit owner->progressChanged(QStringLiteral("search"), pageIndex + 1, total);
            });
        }
        if (failedSearch) {
            fail(ErrorCode::EngineError, QStringLiteral("search"),
                 tr("Text search failed."), detail);
            return;
        }
        post([results](DocumentSession *owner) { emit owner->searchFinished(results); });
    }

    void extractText(const int pageIndex, const QRectF &bounds)
    {
        if (!requireDocument(QStringLiteral("extract-text"))) {
            return;
        }
        if (pageIndex < 0 || pageIndex >= pageCount_ || bounds.isEmpty()) {
            fail(ErrorCode::InvalidArgument, QStringLiteral("extract-text"),
                 tr("The selected page does not exist."));
            return;
        }
        fz_context *context = runtime_->context();
        fz_page *page = nullptr;
        fz_stext_page *textPage = nullptr;
        QString selected;
        bool failed = false;
        QString detail;
        const fz_rect selection = toFzRect(bounds.normalized());
        fz_var(page);
        fz_var(textPage);
        fz_try(context) {
            page = fz_load_page(context, document_, pageIndex);
            textPage = fz_new_stext_page_from_page(context, page, nullptr);
        }
        fz_catch(context) {
            failed = true;
            detail = engineMessage(context);
            fz_drop_stext_page(context, textPage);
            fz_drop_page(context, page);
        }
        if (failed) {
            fail(ErrorCode::EngineError, QStringLiteral("extract-text"),
                 tr("Text selection failed."), detail);
            return;
        }
        for (fz_stext_block *block = textPage->first_block; block != nullptr; block = block->next) {
            if (block->type != FZ_STEXT_BLOCK_TEXT) continue;
            for (fz_stext_line *line = block->u.t.first_line; line != nullptr; line = line->next) {
                QString lineText;
                for (fz_stext_char *character = line->first_char; character != nullptr;
                     character = character->next) {
                    const fz_rect characterBounds = fz_rect_from_quad(character->quad);
                    if (fz_is_empty_rect(fz_intersect_rect(selection, characterBounds))) continue;
                    const char32_t codepoint = static_cast<char32_t>(character->c);
                    lineText.append(QString::fromUcs4(&codepoint, 1));
                }
                if (!lineText.isEmpty()) {
                    if (!selected.isEmpty()) selected.append(QLatin1Char('\n'));
                    selected.append(lineText);
                }
            }
        }
        fz_drop_stext_page(context, textPage);
        fz_drop_page(context, page);
        post([pageIndex, bounds, selected](DocumentSession *owner) {
            emit owner->textExtracted(pageIndex, bounds, selected);
        });
    }

    void edit(const EditOperation &operation)
    {
        if (!requirePdf(QStringLiteral("edit"))) {
            return;
        }
        const bool insertion = operation.kind == EditKind::InsertBlankPage
            || operation.kind == EditKind::ImportPages;
        if (operation.pageIndex < 0 || operation.pageIndex > pageCount_
            || (!insertion && operation.pageIndex == pageCount_)) {
            fail(ErrorCode::InvalidArgument, QStringLiteral("edit"),
                 tr("The selected page does not exist."));
            return;
        }
        if (!fz_has_permission(runtime_->context(), document_, FZ_PERMISSION_EDIT)
            && !fz_has_permission(runtime_->context(), document_, FZ_PERMISSION_ANNOTATE)) {
            fail(ErrorCode::PermissionDenied, QStringLiteral("edit"),
                 tr("This document does not grant editing permission."));
            return;
        }

        fz_context *context = runtime_->context();
        pdf_page *page = nullptr;
        pdf_obj *pageObject = nullptr;
        pdf_obj *resources = nullptr;
        fz_buffer *contents = nullptr;
        fz_image *image = nullptr;
        fz_document *sourceDocument = nullptr;
        pdf_graft_map *graftMap = nullptr;
        bool operationStarted = false;
        bool ok = false;
        bool ownsPageObject = false;
        int pageCountDelta = 0;
        const QByteArray title = QByteArrayLiteral("nexPDF edit");
        const QByteArray text = operation.text.toUtf8();
        const QByteArray imagePath = QDir::toNativeSeparators(operation.imagePath).toUtf8();
        const QByteArray sourcePath = QDir::toNativeSeparators(operation.sourcePath).toUtf8();
        const QByteArray sourcePassword = operation.sourcePassword.toUtf8();
        const QByteArray objectId = operation.objectId.isEmpty()
            ? QByteArrayLiteral("nexPDF:object:") + QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8()
            : operation.objectId.toUtf8();
        const fz_rect operationBounds = toFzRect(operation.bounds.normalized());
        const fz_rect selectionBounds = toFzRect(
            (operation.sourceBounds.isEmpty() ? operation.bounds : operation.sourceBounds).normalized());
        const std::array<float, 3> annotationColor = rgb(operation.color);
        const QRectF markupBounds = operation.bounds.normalized();
        const fz_quad markupQuad = {
            {static_cast<float>(markupBounds.left()), static_cast<float>(markupBounds.top())},
            {static_cast<float>(markupBounds.right()), static_cast<float>(markupBounds.top())},
            {static_cast<float>(markupBounds.left()), static_cast<float>(markupBounds.bottom())},
            {static_cast<float>(markupBounds.right()), static_cast<float>(markupBounds.bottom())}
        };
        QVector<fz_point> inkPoints;
        if (operation.kind == EditKind::AddInk) {
            inkPoints.reserve(operation.points.size());
            for (const QPointF &point : operation.points) {
                inkPoints.append({static_cast<float>(point.x()), static_cast<float>(point.y())});
            }
        }

        QVector<int> pageOrder;
        if (operation.kind == EditKind::MovePage) {
            const int destination = std::clamp(operation.destinationIndex, 0, pageCount_ - 1);
            for (int i = 0; i < pageCount_; ++i) {
                if (i != operation.pageIndex) {
                    pageOrder.append(i);
                }
            }
            pageOrder.insert(destination, operation.pageIndex);
        }

        fz_var(page);
        fz_var(pageObject);
        fz_var(resources);
        fz_var(contents);
        fz_var(image);
        fz_var(sourceDocument);
        fz_var(graftMap);
        fz_var(operationStarted);
        fz_var(ok);
        fz_var(ownsPageObject);
        fz_var(pageCountDelta);

        fz_try(context) {
            pdf_begin_operation(context, pdf_, title.constData());
            operationStarted = true;

            switch (operation.kind) {
            case EditKind::ImportPages: {
                sourceDocument = fz_open_document(context, sourcePath.constData());
                if (fz_needs_password(context, sourceDocument)
                    && fz_authenticate_password(context, sourceDocument, sourcePassword.constData()) == 0) {
                    fz_throw(context, FZ_ERROR_ARGUMENT, "The source PDF password is incorrect");
                }
                pdf_document *sourcePdf = pdf_specifics(context, sourceDocument);
                if (sourcePdf == nullptr) {
                    fz_throw(context, FZ_ERROR_UNSUPPORTED, "Only PDF pages can be imported");
                }
                const int sourceCount = fz_count_pages(context, sourceDocument);
                graftMap = pdf_new_graft_map(context, pdf_);
                int insertionIndex = operation.pageIndex;
                if (operation.sourcePages.isEmpty()) {
                    for (int sourcePage = 0; sourcePage < sourceCount; ++sourcePage) {
                        pdf_graft_mapped_page(context, graftMap, insertionIndex++, sourcePdf, sourcePage);
                        ++pageCountDelta;
                    }
                } else {
                    for (qsizetype index = 0; index < operation.sourcePages.size(); ++index) {
                        const int sourcePage = operation.sourcePages.at(index);
                        if (sourcePage < 0 || sourcePage >= sourceCount) {
                            fz_throw(context, FZ_ERROR_ARGUMENT, "A source page is outside the PDF");
                        }
                        pdf_graft_mapped_page(context, graftMap, insertionIndex++, sourcePdf, sourcePage);
                        ++pageCountDelta;
                    }
                }
                break;
            }
            case EditKind::InsertBlankPage:
                resources = pdf_new_dict(context, pdf_, 1);
                contents = fz_new_buffer(context, 0);
                pageObject = pdf_add_page(context, pdf_, fz_make_rect(0, 0, 595, 842), 0,
                                          resources, contents);
                ownsPageObject = true;
                pdf_insert_page(context, pdf_, operation.pageIndex, pageObject);
                pageCountDelta = 1;
                break;
            case EditKind::DeletePage:
                if (pageCount_ <= 1) {
                    fz_throw(context, FZ_ERROR_ARGUMENT, "A PDF must keep at least one page");
                }
                pdf_delete_page(context, pdf_, operation.pageIndex);
                pageCountDelta = -1;
                break;
            case EditKind::MovePage:
                pdf_rearrange_pages(context, pdf_, pageOrder.size(), pageOrder.constData(),
                                    PDF_CLEAN_STRUCTURE_KEEP);
                break;
            case EditKind::RotatePage:
                pageObject = pdf_lookup_page_obj(context, pdf_, operation.pageIndex);
                pdf_dict_put_int(context, pageObject, PDF_NAME(Rotate),
                                 normalizedRotation(pdf_dict_get_inheritable_int(
                                     context, pageObject, PDF_NAME(Rotate)) + operation.rotation));
                break;
            case EditKind::ApplyRedactions: {
                page = pdf_load_page(context, pdf_, operation.pageIndex);
                pdf_annot *annot = pdf_first_annot(context, page);
                while (annot != nullptr) {
                    pdf_annot *next = pdf_next_annot(context, annot);
                    if (pdf_annot_type(context, annot) == PDF_ANNOT_REDACT) {
                        pdf_redact_options redaction{};
                        redaction.black_boxes = 1;
                        redaction.image_method = PDF_REDACT_IMAGE_PIXELS;
                        redaction.line_art = PDF_REDACT_LINE_ART_REMOVE_IF_TOUCHED;
                        redaction.text = PDF_REDACT_TEXT_REMOVE;
                        (void)pdf_apply_redaction(context, annot, &redaction);
                    }
                    annot = next;
                }
                break;
            }
            case EditKind::MoveObject:
            case EditKind::ResizeObject:
            case EditKind::DeleteObject: {
                page = pdf_load_page(context, pdf_, operation.pageIndex);
                for (pdf_annot *annot = pdf_first_annot(context, page); annot != nullptr;
                     annot = pdf_next_annot(context, annot)) {
                    const char *name = pdf_annot_name(context, annot);
                    const bool toolObject = name != nullptr
                        && std::strncmp(name, "nexPDF:object:", 14) == 0;
                    const bool hasRect = toolObject && pdf_annot_has_rect(context, annot);
                    bool matches = false;
                    if (toolObject && !operation.objectId.isEmpty()) {
                        matches = std::strcmp(name, objectId.constData()) == 0;
                    } else if (hasRect) {
                        matches = !fz_is_empty_rect(fz_intersect_rect(
                            pdf_annot_rect(context, annot), selectionBounds));
                    }
                    if (matches) {
                        if (operation.kind == EditKind::DeleteObject) {
                            pdf_delete_annot(context, page, annot);
                        } else if (!hasRect) {
                            fz_throw(context, FZ_ERROR_UNSUPPORTED,
                                     "This annotation geometry cannot be resized with Rect");
                        } else {
                            pdf_set_annot_rect(context, annot, operationBounds);
                        }
                        break;
                    }
                }
                break;
            }
            default: {
                const enum pdf_annot_type type = annotationTypeFor(operation.kind);
                if (type == PDF_ANNOT_UNKNOWN) {
                    fz_throw(context, FZ_ERROR_ARGUMENT, "Unsupported edit operation");
                }
                page = pdf_load_page(context, pdf_, operation.pageIndex);
                pdf_annot *annot = pdf_create_annot(context, page, type);
                pdf_set_annot_name(context, annot, objectId.constData());
                pdf_set_annot_author(context, annot, "nexPDF");
                // Text-markup and ink annotations derive their bounding box from
                // QuadPoints/InkList. MuPDF deliberately rejects Rect for those
                // subtypes, so only set it for annotations whose geometry is Rect.
                if (pdf_annot_has_rect(context, annot)) {
                    pdf_set_annot_rect(context, annot, operationBounds);
                }
                pdf_set_annot_opacity(context, annot,
                                      static_cast<float>(std::clamp(operation.opacity, 0.0, 1.0)));
                pdf_set_annot_color(context, annot, 3, annotationColor.data());

                if (type == PDF_ANNOT_FREE_TEXT) {
                    pdf_set_annot_contents(context, annot, text.constData());
                    pdf_set_annot_default_appearance(context, annot, "Helv",
                        static_cast<float>(std::max<qreal>(1.0, operation.fontSize)),
                        3, annotationColor.data());
                } else if (type == PDF_ANNOT_STAMP) {
                    image = fz_new_image_from_file(context, imagePath.constData());
                    pdf_set_annot_stamp_image(context, annot, image);
                } else if (type == PDF_ANNOT_INK) {
                    if (!inkPoints.isEmpty()) {
                        pdf_add_annot_ink_list(context, annot, inkPoints.size(), inkPoints.data());
                    }
                } else if (type == PDF_ANNOT_HIGHLIGHT || type == PDF_ANNOT_UNDERLINE
                           || type == PDF_ANNOT_STRIKE_OUT) {
                    pdf_set_annot_quad_points(context, annot, 1, &markupQuad);
                }
                pdf_update_page(context, page);
                break;
            }
            }

            pdf_end_operation(context, pdf_);
            operationStarted = false;
            ok = true;
        }
        fz_always(context) {
            fz_drop_image(context, image);
            pdf_drop_graft_map(context, graftMap);
            fz_drop_document(context, sourceDocument);
            pdf_drop_page(context, page);
            fz_drop_buffer(context, contents);
            pdf_drop_obj(context, resources);
            if (ownsPageObject) {
                pdf_drop_obj(context, pageObject);
            }
        }
        fz_catch(context) {
            const QString detail = engineMessage(context);
            if (operationStarted) {
                fz_try(context) { pdf_abandon_operation(context, pdf_); }
                fz_catch(context) { /* Preserve the original error. */ }
            }
            fail(ErrorCode::EngineError, QStringLiteral("edit"),
                 tr("The edit could not be applied."), detail);
            return;
        }

        if (ok) {
            pageCount_ += pageCountDelta;
            if (pageCountDelta != 0) {
                const int count = pageCount_;
                post([count](DocumentSession *owner) { emit owner->pageCountChanged(count); });
            }
            changed();
        }
    }

    void addWatermark(const WatermarkSpec &spec)
    {
        if (!requirePdf(QStringLiteral("add-watermark"))) {
            return;
        }
        if (spec.kind == WatermarkKind::Text && spec.text.trimmed().isEmpty()) {
            fail(ErrorCode::InvalidArgument, QStringLiteral("add-watermark"),
                 tr("Watermark text cannot be empty."));
            return;
        }
        if (spec.kind == WatermarkKind::Image && !QFileInfo::exists(spec.imagePath)) {
            fail(ErrorCode::FileNotFound, QStringLiteral("add-watermark"),
                 tr("The watermark image does not exist."));
            return;
        }

        QImage watermarkImage;
        QByteArray encodedWatermarkImage;
        if (spec.kind == WatermarkKind::Text) {
            watermarkImage = makeTextWatermarkImage(spec);
        } else {
            watermarkImage.load(spec.imagePath);
            if (watermarkImage.isNull()) {
                fail(ErrorCode::UnsupportedFormat, QStringLiteral("add-watermark"),
                     tr("The watermark image format is not supported."));
                return;
            }
            if (!qFuzzyIsNull(spec.rotation)) {
                watermarkImage = watermarkImage.transformed(
                    QTransform().rotate(spec.rotation), Qt::SmoothTransformation);
            }
        }
        {
            QBuffer encoded(&encodedWatermarkImage);
            encoded.open(QIODevice::WriteOnly);
            if (!watermarkImage.save(&encoded, "PNG")) {
                fail(ErrorCode::IoError, QStringLiteral("add-watermark"),
                     tr("The watermark image could not be prepared."));
                return;
            }
        }

        QVector<int> pages = spec.pages;
        if (pages.isEmpty()) {
            pages.reserve(pageCount_);
            for (int i = 0; i < pageCount_; ++i) {
                pages.append(i);
            }
        }
        std::sort(pages.begin(), pages.end());
        pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
        if (std::any_of(pages.cbegin(), pages.cend(), [this](int page) {
                return page < 0 || page >= pageCount_;
            })) {
            fail(ErrorCode::InvalidArgument, QStringLiteral("add-watermark"),
                 tr("The watermark page range is invalid."));
            return;
        }

        fz_context *context = runtime_->context();
        fz_image *image = nullptr;
        fz_buffer *imageBuffer = nullptr;
        fz_buffer *formBuffer = nullptr;
        fz_buffer *pageBuffer = nullptr;
        pdf_obj *imageRef = nullptr;
        pdf_obj *formDict = nullptr;
        pdf_obj *formRef = nullptr;
        pdf_obj *graphicsState = nullptr;
        pdf_obj *graphicsStateRef = nullptr;
        pdf_obj *ocgDict = nullptr;
        pdf_obj *ocgRef = nullptr;
        pdf_obj *pageResources = nullptr;
        pdf_obj *resourceSubdict = nullptr;
        pdf_obj *contentsArray = nullptr;
        pdf_obj *streamDict = nullptr;
        pdf_obj *streamRef = nullptr;
        bool operationStarted = false;
        const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QByteArray watermarkId = QByteArray(kNexPdfWatermarkPrefix) + uuid.toUtf8();
        const QByteArray resourceBase = QByteArrayLiteral("NXWM") + uuid.left(12).toLatin1();
        const QByteArray formName = resourceBase + QByteArrayLiteral("F");
        const QByteArray stateName = resourceBase + QByteArrayLiteral("G");
        const QByteArray ocgName = resourceBase + QByteArrayLiteral("O");
        const QByteArray label = spec.kind == WatermarkKind::Text
            ? spec.text.toUtf8() : QByteArrayLiteral("Image watermark");

        fz_var(image);
        fz_var(imageBuffer);
        fz_var(formBuffer);
        fz_var(pageBuffer);
        fz_var(imageRef);
        fz_var(formDict);
        fz_var(formRef);
        fz_var(graphicsState);
        fz_var(graphicsStateRef);
        fz_var(ocgDict);
        fz_var(ocgRef);
        fz_var(pageResources);
        fz_var(resourceSubdict);
        fz_var(contentsArray);
        fz_var(streamDict);
        fz_var(streamRef);
        fz_var(operationStarted);

        fz_try(context) {
            pdf_begin_operation(context, pdf_, "Add nexPDF watermark");
            operationStarted = true;
            imageBuffer = fz_new_buffer_from_copied_data(context,
                reinterpret_cast<const unsigned char *>(encodedWatermarkImage.constData()),
                static_cast<size_t>(encodedWatermarkImage.size()));
            image = fz_new_image_from_buffer(context, imageBuffer);
            imageRef = pdf_add_image(context, pdf_, image);

            formDict = pdf_new_dict(context, pdf_, 5);
            pdf_dict_put(context, formDict, PDF_NAME(Type), PDF_NAME(XObject));
            pdf_dict_put(context, formDict, PDF_NAME(Subtype), PDF_NAME(Form));
            pdf_dict_put_rect(context, formDict, PDF_NAME(BBox), fz_make_rect(0, 0, 1, 1));
            pdf_obj *formResources = pdf_dict_put_dict(context, formDict, PDF_NAME(Resources), 1);
            pdf_obj *formImages = pdf_dict_put_dict(context, formResources, PDF_NAME(XObject), 1);
            pdf_dict_puts(context, formImages, "Im0", imageRef);
            formBuffer = fz_new_buffer(context, 32);
            fz_append_string(context, formBuffer, "q 1 0 0 1 0 0 cm /Im0 Do Q\n");
            formRef = pdf_add_stream(context, pdf_, formBuffer, formDict, 1);

            graphicsState = pdf_new_dict(context, pdf_, 4);
            pdf_dict_put(context, graphicsState, PDF_NAME(Type), PDF_NAME(ExtGState));
            pdf_dict_put_real(context, graphicsState, PDF_NAME(ca), std::clamp(spec.opacity, 0.0, 1.0));
            pdf_dict_put_real(context, graphicsState, PDF_NAME(CA), std::clamp(spec.opacity, 0.0, 1.0));
            graphicsStateRef = pdf_add_object(context, pdf_, graphicsState);

            ocgDict = pdf_new_dict(context, pdf_, 3);
            pdf_dict_put(context, ocgDict, PDF_NAME(Type), PDF_NAME(OCG));
            pdf_dict_puts_drop(context, ocgDict, "Name", pdf_new_text_string(context, watermarkId.constData()));
            ocgRef = pdf_add_object(context, pdf_, ocgDict);
            pdf_obj *catalog = pdf_dict_get(context, pdf_trailer(context, pdf_), PDF_NAME(Root));
            pdf_obj *ocProperties = pdf_dict_get(context, catalog, PDF_NAME(OCProperties));
            if (!pdf_is_dict(context, ocProperties)) {
                ocProperties = pdf_dict_put_dict(context, catalog, PDF_NAME(OCProperties), 3);
            }
            pdf_obj *ocgs = pdf_dict_get(context, ocProperties, PDF_NAME(OCGs));
            if (!pdf_is_array(context, ocgs)) {
                ocgs = pdf_dict_put_array(context, ocProperties, PDF_NAME(OCGs), 1);
            }
            pdf_array_push(context, ocgs, ocgRef);
            pdf_obj *defaultConfig = pdf_dict_get(context, ocProperties, PDF_NAME(D));
            if (!pdf_is_dict(context, defaultConfig)) {
                defaultConfig = pdf_dict_put_dict(context, ocProperties, PDF_NAME(D), 3);
            }
            pdf_obj *order = pdf_dict_get(context, defaultConfig, PDF_NAME(Order));
            if (!pdf_is_array(context, order)) {
                order = pdf_dict_put_array(context, defaultConfig, PDF_NAME(Order), 1);
            }
            pdf_array_push(context, order, ocgRef);
            pdf_obj *enabled = pdf_dict_get(context, defaultConfig, PDF_NAME(ON));
            if (!pdf_is_array(context, enabled)) {
                enabled = pdf_dict_put_array(context, defaultConfig, PDF_NAME(ON), 1);
            }
            pdf_array_push(context, enabled, ocgRef);

            for (const int pageIndex : pages) {
                pdf_obj *pageObject = pdf_lookup_page_obj(context, pdf_, pageIndex);
                const fz_rect pageBounds = pdf_dict_get_inheritable_rect(
                    context, pageObject, PDF_NAME(CropBox));
                fz_rect effectiveBounds = pageBounds;
                if (fz_is_empty_rect(effectiveBounds)) {
                    effectiveBounds = pdf_dict_get_inheritable_rect(context, pageObject, PDF_NAME(MediaBox));
                }
                const float pageWidth = effectiveBounds.x1 - effectiveBounds.x0;
                const float pageHeight = effectiveBounds.y1 - effectiveBounds.y0;
                const float targetWidth = pageWidth * static_cast<float>(std::clamp(spec.scale, 0.02, 2.0));
                const float aspect = static_cast<float>(image->h)
                    / static_cast<float>(std::max(1, image->w));
                const float targetHeight = targetWidth * aspect;
                const float centerX = effectiveBounds.x0 + pageWidth * static_cast<float>(spec.position.x());
                const float centerY = effectiveBounds.y0 + pageHeight * static_cast<float>(spec.position.y());
                const float x = centerX - targetWidth / 2.0F;
                const float y = centerY - targetHeight / 2.0F;

                if (pdf_dict_gets(context, pageObject,
                                  "nexPDFOriginalPageStateStored") == nullptr) {
                    pdf_obj *originalResources = pdf_dict_get(
                        context, pageObject, PDF_NAME(Resources));
                    pdf_obj *originalContents = pdf_dict_get(
                        context, pageObject, PDF_NAME(Contents));
                    pdf_dict_puts(context, pageObject,
                                  "nexPDFOriginalPageStateStored", PDF_TRUE);
                    if (originalResources != nullptr) {
                        pdf_dict_puts(context, pageObject,
                                      "nexPDFOriginalResources", originalResources);
                    }
                    if (originalContents != nullptr) {
                        pdf_dict_puts(context, pageObject,
                                      "nexPDFOriginalContents", originalContents);
                    }
                }

                pdf_obj *inheritedResources = pdf_dict_get_inheritable(
                    context, pageObject, PDF_NAME(Resources));
                pageResources = pdf_is_dict(context, inheritedResources)
                    ? pdf_copy_dict(context, inheritedResources) : pdf_new_dict(context, pdf_, 4);

                pdf_obj *existing = pdf_dict_get(context, pageResources, PDF_NAME(XObject));
                resourceSubdict = pdf_is_dict(context, existing)
                    ? pdf_copy_dict(context, existing) : pdf_new_dict(context, pdf_, 1);
                pdf_dict_puts(context, resourceSubdict, formName.constData(), formRef);
                pdf_dict_put(context, pageResources, PDF_NAME(XObject), resourceSubdict);
                pdf_drop_obj(context, resourceSubdict);
                resourceSubdict = nullptr;

                existing = pdf_dict_get(context, pageResources, PDF_NAME(ExtGState));
                resourceSubdict = pdf_is_dict(context, existing)
                    ? pdf_copy_dict(context, existing) : pdf_new_dict(context, pdf_, 1);
                pdf_dict_puts(context, resourceSubdict, stateName.constData(), graphicsStateRef);
                pdf_dict_put(context, pageResources, PDF_NAME(ExtGState), resourceSubdict);
                pdf_drop_obj(context, resourceSubdict);
                resourceSubdict = nullptr;

                existing = pdf_dict_get(context, pageResources, PDF_NAME(Properties));
                resourceSubdict = pdf_is_dict(context, existing)
                    ? pdf_copy_dict(context, existing) : pdf_new_dict(context, pdf_, 1);
                pdf_dict_puts(context, resourceSubdict, ocgName.constData(), ocgRef);
                pdf_dict_put(context, pageResources, PDF_NAME(Properties), resourceSubdict);
                pdf_drop_obj(context, resourceSubdict);
                resourceSubdict = nullptr;
                pdf_dict_put(context, pageObject, PDF_NAME(Resources), pageResources);
                pdf_drop_obj(context, pageResources);
                pageResources = nullptr;

                pageBuffer = fz_new_buffer(context, 256);
                fz_append_printf(context, pageBuffer,
                    "/Artifact <</Subtype /Watermark>> BDC /OC /%s BDC q /%s gs %.8g 0 0 %.8g %.8g %.8g cm /%s Do Q EMC EMC\n",
                    ocgName.constData(), stateName.constData(), targetWidth, targetHeight,
                    x, y, formName.constData());
                streamDict = pdf_new_dict(context, pdf_, 5);
                pdf_dict_puts_drop(context, streamDict, "nexPDFWatermarkId",
                                   pdf_new_text_string(context, watermarkId.constData()));
                pdf_dict_puts_drop(context, streamDict, "nexPDFWatermarkLabel",
                                   pdf_new_text_string(context, label.constData()));
                pdf_dict_puts_drop(context, streamDict, "nexPDFResourceBase",
                                   pdf_new_text_string(context, resourceBase.constData()));
                streamRef = pdf_add_stream(context, pdf_, pageBuffer, streamDict, 1);

                pdf_obj *oldContents = pdf_dict_get(context, pageObject, PDF_NAME(Contents));
                contentsArray = pdf_new_array(context, pdf_, pdf_is_array(context, oldContents)
                    ? pdf_array_len(context, oldContents) + 1 : 2);
                if (spec.layer == WatermarkLayer::Background) {
                    pdf_array_push(context, contentsArray, streamRef);
                }
                if (pdf_is_array(context, oldContents)) {
                    const int count = pdf_array_len(context, oldContents);
                    for (int index = 0; index < count; ++index) {
                        pdf_array_push(context, contentsArray, pdf_array_get(context, oldContents, index));
                    }
                } else if (oldContents != nullptr) {
                    pdf_array_push(context, contentsArray, oldContents);
                }
                if (spec.layer == WatermarkLayer::Foreground) {
                    pdf_array_push(context, contentsArray, streamRef);
                }
                pdf_dict_put(context, pageObject, PDF_NAME(Contents), contentsArray);

                pdf_drop_obj(context, contentsArray);
                contentsArray = nullptr;
                pdf_drop_obj(context, streamRef);
                streamRef = nullptr;
                pdf_drop_obj(context, streamDict);
                streamDict = nullptr;
                fz_drop_buffer(context, pageBuffer);
                pageBuffer = nullptr;
            }
            pdf_end_operation(context, pdf_);
            operationStarted = false;
        }
        fz_always(context) {
            pdf_drop_obj(context, streamRef);
            pdf_drop_obj(context, streamDict);
            pdf_drop_obj(context, contentsArray);
            pdf_drop_obj(context, resourceSubdict);
            pdf_drop_obj(context, pageResources);
            pdf_drop_obj(context, ocgRef);
            pdf_drop_obj(context, ocgDict);
            pdf_drop_obj(context, graphicsStateRef);
            pdf_drop_obj(context, graphicsState);
            pdf_drop_obj(context, formRef);
            pdf_drop_obj(context, formDict);
            pdf_drop_obj(context, imageRef);
            fz_drop_buffer(context, pageBuffer);
            fz_drop_buffer(context, formBuffer);
            fz_drop_image(context, image);
            fz_drop_buffer(context, imageBuffer);
        }
        fz_catch(context) {
            const QString detail = engineMessage(context);
            if (operationStarted) {
                fz_try(context) { pdf_abandon_operation(context, pdf_); }
                fz_catch(context) { }
            }
            fail(ErrorCode::EngineError, QStringLiteral("add-watermark"),
                 tr("The watermark could not be added."), detail);
            return;
        }
        changed();
    }

    void scanWatermarks()
    {
        if (!requirePdf(QStringLiteral("scan-watermarks"))) {
            return;
        }
        candidates_.clear();
        fz_context *context = runtime_->context();
        bool scanFailed = false;
        QString detail;
        struct RepeatedContent {
            QString label;
            QSet<int> pages;
            QRectF bounds;
            qreal maximumAreaRatio = 0.0;
            int minimumAlpha = 255;
            bool tilted = false;
            bool image = false;
        };
        QHash<QString, RepeatedContent> repeatedContent;

        for (int pageIndex = 0; pageIndex < pageCount_ && !scanFailed; ++pageIndex) {
            pdf_page *page = nullptr;
            fz_stext_page *textPage = nullptr;
            fz_var(page);
            fz_var(textPage);
            fz_try(context) {
                page = pdf_load_page(context, pdf_, pageIndex);
                pdf_obj *pageObject = pdf_lookup_page_obj(context, pdf_, pageIndex);
                pdf_obj *contents = pdf_dict_get(context, pageObject, PDF_NAME(Contents));
                const int contentCount = pdf_is_array(context, contents)
                    ? pdf_array_len(context, contents) : (pdf_is_stream(context, contents) ? 1 : 0);
                for (int contentIndex = 0; contentIndex < contentCount; ++contentIndex) {
                    pdf_obj *stream = pdf_is_array(context, contents)
                        ? pdf_array_get(context, contents, contentIndex) : contents;
                    if (!pdf_is_stream(context, stream)) {
                        continue;
                    }
                    pdf_obj *idObject = pdf_dict_gets(context, stream, "nexPDFWatermarkId");
                    const char *idText = idObject != nullptr
                        ? pdf_to_text_string(context, idObject) : nullptr;
                    const QString id = QString::fromUtf8(idText != nullptr ? idText : "");
                    if (!id.startsWith(QString::fromLatin1(kNexPdfWatermarkPrefix))) {
                        continue;
                    }
                    pdf_obj *labelObject = pdf_dict_gets(context, stream, "nexPDFWatermarkLabel");
                    pdf_obj *resourceObject = pdf_dict_gets(context, stream, "nexPDFResourceBase");
                    const char *labelText = labelObject != nullptr
                        ? pdf_to_text_string(context, labelObject) : nullptr;
                    const char *resourceText = resourceObject != nullptr
                        ? pdf_to_text_string(context, resourceObject) : nullptr;
                    auto existing = std::find_if(candidates_.begin(), candidates_.end(),
                        [&id](const CandidateRecord &candidate) { return candidate.publicValue.id == id; });
                    if (existing == candidates_.end()) {
                        CandidateRecord record;
                        record.publicValue.id = id;
                        record.publicValue.label = labelText != nullptr && *labelText != '\0'
                            ? QString::fromUtf8(labelText) : tr("nexPDF watermark");
                        record.publicValue.confidence = 1.0;
                        record.publicValue.safety = WatermarkRemovalSafety::Exact;
                        record.publicValue.createdByNexPDF = true;
                        record.publicValue.bounds = toQRect(pdf_bound_page(context, page, FZ_CROP_BOX));
                        record.contentStream = true;
                        record.resourceBase = QString::fromUtf8(resourceText != nullptr ? resourceText : "");
                        candidates_.append(std::move(record));
                        existing = std::prev(candidates_.end());
                    }
                    if (!existing->publicValue.pages.contains(pageIndex)) {
                        existing->publicValue.pages.append(pageIndex);
                    }
                }
                int annotationIndex = 0;
                for (pdf_annot *annot = pdf_first_annot(context, page); annot != nullptr;
                     annot = pdf_next_annot(context, annot), ++annotationIndex) {
                    const enum pdf_annot_type type = pdf_annot_type(context, annot);
                    const QString name = QString::fromUtf8(pdf_annot_name(context, annot));
                    const QString subject = pdf_annot_has_subject(context, annot)
                        ? QString::fromUtf8(pdf_annot_subject(context, annot)) : QString{};
                    const bool ours = name.startsWith(QString::fromLatin1(kNexPdfWatermarkPrefix));
                    const bool standard = type == PDF_ANNOT_WATERMARK;
                    const bool labelled = subject.contains(QStringLiteral("watermark"), Qt::CaseInsensitive)
                        || subject.contains(QStringLiteral("水印"));
                    if (!ours && !standard && !labelled) {
                        continue;
                    }

                    const QString id = ours ? name
                        : QStringLiteral("external:%1:%2").arg(pageIndex).arg(annotationIndex);
                    auto existing = std::find_if(candidates_.begin(), candidates_.end(),
                        [&id](const CandidateRecord &candidate) { return candidate.publicValue.id == id; });
                    if (existing == candidates_.end()) {
                        CandidateRecord record;
                        record.publicValue.id = id;
                        record.publicValue.label = ours
                            ? tr("nexPDF watermark") : tr("Watermark annotation");
                        record.publicValue.confidence = ours ? 1.0 : (standard ? 0.95 : 0.75);
                        record.publicValue.safety = ours ? WatermarkRemovalSafety::Exact
                                                        : WatermarkRemovalSafety::ReviewRequired;
                        record.publicValue.createdByNexPDF = ours;
                        record.annotationName = name;
                        record.annotationIndex = annotationIndex;
                        record.publicValue.bounds = toQRect(pdf_bound_annot(context, annot));
                        candidates_.append(std::move(record));
                        existing = std::prev(candidates_.end());
                    }
                    existing->publicValue.pages.append(pageIndex);
                }

                const fz_rect pageBounds = pdf_bound_page(context, page, FZ_CROP_BOX);
                const qreal pageArea = std::max<qreal>(1.0,
                    static_cast<qreal>(pageBounds.x1 - pageBounds.x0)
                    * static_cast<qreal>(pageBounds.y1 - pageBounds.y0));
                textPage = fz_new_stext_page_from_page(context,
                    reinterpret_cast<fz_page *>(page), nullptr);
                for (fz_stext_block *block = textPage->first_block; block != nullptr; block = block->next) {
                    if (block->type == FZ_STEXT_BLOCK_TEXT) {
                        for (fz_stext_line *line = block->u.t.first_line; line != nullptr; line = line->next) {
                            QString text;
                            int minimumAlpha = 255;
                            for (fz_stext_char *character = line->first_char; character != nullptr;
                                 character = character->next) {
                                const char32_t codepoint = static_cast<char32_t>(character->c);
                                text.append(QString::fromUcs4(&codepoint, 1));
                                minimumAlpha = std::min(minimumAlpha,
                                    static_cast<int>((character->argb >> 24U) & 0xffU));
                            }
                            const QString normalized = text.simplified().toCaseFolded();
                            if (normalized.size() < 3 || normalized.size() > 160) {
                                continue;
                            }
                            const QByteArray hash = QCryptographicHash::hash(normalized.toUtf8(),
                                QCryptographicHash::Sha256).toHex();
                            const QString key = QStringLiteral("text:") + QString::fromLatin1(hash);
                            RepeatedContent &record = repeatedContent[key];
                            record.label = text.simplified();
                            record.pages.insert(pageIndex);
                            record.bounds = toQRect(line->bbox);
                            record.maximumAreaRatio = std::max(record.maximumAreaRatio,
                                record.bounds.width() * record.bounds.height() / pageArea);
                            record.minimumAlpha = std::min(record.minimumAlpha, minimumAlpha);
                            const qreal angle = std::abs(std::atan2(line->dir.y, line->dir.x) * 180.0 / 3.141592653589793);
                            record.tilted = record.tilted || (angle > 8.0 && angle < 172.0);
                        }
                    } else if (block->type == FZ_STEXT_BLOCK_IMAGE && block->u.i.image != nullptr) {
                        std::array<unsigned char, 16> digest{};
                        fz_image_digest(context, block->u.i.image, digest.data());
                        const QByteArray hash(reinterpret_cast<const char *>(digest.data()),
                                              static_cast<qsizetype>(digest.size()));
                        const QString key = QStringLiteral("image:") + QString::fromLatin1(hash.toHex());
                        RepeatedContent &record = repeatedContent[key];
                        record.label = tr("Repeated page-content image");
                        record.pages.insert(pageIndex);
                        record.bounds = toQRect(block->bbox);
                        record.maximumAreaRatio = std::max(record.maximumAreaRatio,
                            record.bounds.width() * record.bounds.height() / pageArea);
                        record.image = true;
                    }
                }
            }
            fz_always(context) {
                fz_drop_stext_page(context, textPage);
                pdf_drop_page(context, page);
            }
            fz_catch(context) {
                scanFailed = true;
                detail = engineMessage(context);
            }
            post([pageIndex, total = pageCount_](DocumentSession *owner) {
                emit owner->progressChanged(QStringLiteral("scan-watermarks"), pageIndex + 1, total);
            });
        }

        if (!scanFailed) {
            const int repeatedThreshold = std::max(2, (pageCount_ + 1) / 2);
            for (auto it = repeatedContent.cbegin(); it != repeatedContent.cend(); ++it) {
                const RepeatedContent &content = it.value();
                const bool repeated = content.pages.size() >= repeatedThreshold;
                const bool visuallySuspicious = content.image
                    ? content.maximumAreaRatio >= 0.20
                    : (content.tilted || content.minimumAlpha < 210)
                        && content.maximumAreaRatio >= 0.015;
                if ((!repeated && pageCount_ > 1) || !visuallySuspicious) {
                    continue;
                }
                CandidateRecord record;
                record.publicValue.id = QStringLiteral("content:") + it.key();
                record.publicValue.label = content.image
                    ? content.label
                    : tr("Repeated page-content text: %1").arg(content.label.left(60));
                record.publicValue.pages = content.pages.values();
                std::sort(record.publicValue.pages.begin(), record.publicValue.pages.end());
                record.publicValue.bounds = content.bounds;
                record.publicValue.confidence = repeated ? 0.72 : 0.52;
                record.publicValue.safety = WatermarkRemovalSafety::Unsupported;
                candidates_.append(std::move(record));
            }
        }

        if (scanFailed) {
            fail(ErrorCode::EngineError, QStringLiteral("scan-watermarks"),
                 tr("Watermark scanning failed."), detail);
            return;
        }
        QVector<WatermarkCandidate> publicCandidates;
        publicCandidates.reserve(candidates_.size());
        for (const CandidateRecord &candidate : std::as_const(candidates_)) {
            publicCandidates.append(candidate.publicValue);
        }
        post([publicCandidates](DocumentSession *owner) {
            emit owner->watermarksScanned(publicCandidates);
        });
    }

    void removeWatermarks(const QStringList &ids)
    {
        if (!requirePdf(QStringLiteral("remove-watermarks"))) {
            return;
        }
        if (ids.isEmpty()) {
            return;
        }
        QVector<CandidateRecord> selected;
        for (const QString &id : ids) {
            const auto found = std::find_if(candidates_.cbegin(), candidates_.cend(),
                [&id](const CandidateRecord &candidate) { return candidate.publicValue.id == id; });
            if (found == candidates_.cend()) {
                fail(ErrorCode::InvalidArgument, QStringLiteral("remove-watermarks"),
                     tr("A selected watermark candidate is no longer valid."), id);
                return;
            }
            if (found->publicValue.safety == WatermarkRemovalSafety::Unsupported) {
                fail(ErrorCode::UnsafeWatermarkRemoval, QStringLiteral("remove-watermarks"),
                     tr("This watermark cannot be isolated safely."), id);
                return;
            }
            selected.append(*found);
        }

        fz_context *context = runtime_->context();
        bool operationStarted = false;
        pdf_page *page = nullptr;
        fz_var(operationStarted);
        fz_var(page);
        fz_try(context) {
            pdf_begin_operation(context, pdf_, "Remove confirmed watermarks");
            operationStarted = true;
            for (const CandidateRecord &candidate : std::as_const(selected)) {
                for (const int pageIndex : candidate.publicValue.pages) {
                    if (candidate.contentStream) {
                        pdf_obj *pageObject = pdf_lookup_page_obj(context, pdf_, pageIndex);
                        pdf_obj *oldContents = pdf_dict_get(context, pageObject, PDF_NAME(Contents));
                        pdf_obj *newContents = nullptr;
                        const QByteArray candidateId = candidate.publicValue.id.toUtf8();
                        if (pdf_is_array(context, oldContents)) {
                            const int count = pdf_array_len(context, oldContents);
                            newContents = pdf_new_array(context, pdf_, count);
                            for (int contentIndex = 0; contentIndex < count; ++contentIndex) {
                                pdf_obj *stream = pdf_array_get(context, oldContents, contentIndex);
                                pdf_obj *idObject = pdf_is_stream(context, stream)
                                    ? pdf_dict_gets(context, stream, "nexPDFWatermarkId") : nullptr;
                                const char *idText = idObject != nullptr
                                    ? pdf_to_text_string(context, idObject) : nullptr;
                                if (idText == nullptr || candidateId != idText) {
                                    pdf_array_push(context, newContents, stream);
                                }
                            }
                            if (pdf_array_len(context, newContents) == 0) {
                                pdf_dict_del(context, pageObject, PDF_NAME(Contents));
                            } else {
                                pdf_dict_put(context, pageObject, PDF_NAME(Contents), newContents);
                            }
                            pdf_drop_obj(context, newContents);
                        } else if (pdf_is_stream(context, oldContents)) {
                            pdf_obj *idObject = pdf_dict_gets(context, oldContents, "nexPDFWatermarkId");
                            const char *idText = idObject != nullptr
                                ? pdf_to_text_string(context, idObject) : nullptr;
                            if (idText != nullptr && candidateId == idText) {
                                pdf_dict_del(context, pageObject, PDF_NAME(Contents));
                            }
                        }

                        if (!candidate.resourceBase.isEmpty()) {
                            pdf_obj *resources = pdf_dict_get(context, pageObject, PDF_NAME(Resources));
                            const QByteArray base = candidate.resourceBase.toLatin1();
                            const std::array<std::pair<pdf_obj *, QByteArray>, 3> resourceEntries{{
                                {PDF_NAME(XObject), base + QByteArrayLiteral("F")},
                                {PDF_NAME(ExtGState), base + QByteArrayLiteral("G")},
                                {PDF_NAME(Properties), base + QByteArrayLiteral("O")}
                            }};
                            for (const auto &[category, name] : resourceEntries) {
                                pdf_obj *dictionary = pdf_dict_get(context, resources, category);
                                if (pdf_is_dict(context, dictionary)) {
                                    pdf_dict_dels(context, dictionary, name.constData());
                                }
                            }
                        }

                        bool hasRemainingOwnedWatermark = false;
                        pdf_obj *remainingContents = pdf_dict_get(
                            context, pageObject, PDF_NAME(Contents));
                        const int remainingCount = pdf_is_array(context, remainingContents)
                            ? pdf_array_len(context, remainingContents)
                            : (pdf_is_stream(context, remainingContents) ? 1 : 0);
                        for (int contentIndex = 0; contentIndex < remainingCount; ++contentIndex) {
                            pdf_obj *stream = pdf_is_array(context, remainingContents)
                                ? pdf_array_get(context, remainingContents, contentIndex)
                                : remainingContents;
                            pdf_obj *idObject = pdf_is_stream(context, stream)
                                ? pdf_dict_gets(context, stream, "nexPDFWatermarkId") : nullptr;
                            const char *idText = idObject != nullptr
                                ? pdf_to_text_string(context, idObject) : nullptr;
                            if (idText != nullptr
                                && QByteArray(idText).startsWith(kNexPdfWatermarkPrefix)) {
                                hasRemainingOwnedWatermark = true;
                                break;
                            }
                        }

                        if (!hasRemainingOwnedWatermark
                            && pdf_dict_gets(context, pageObject,
                                            "nexPDFOriginalPageStateStored") != nullptr) {
                            pdf_obj *originalResources = pdf_dict_gets(
                                context, pageObject, "nexPDFOriginalResources");
                            pdf_obj *originalContents = pdf_dict_gets(
                                context, pageObject, "nexPDFOriginalContents");
                            if (originalResources != nullptr) {
                                pdf_dict_put(context, pageObject,
                                             PDF_NAME(Resources), originalResources);
                            } else {
                                pdf_dict_del(context, pageObject, PDF_NAME(Resources));
                            }
                            if (originalContents != nullptr) {
                                pdf_dict_put(context, pageObject,
                                             PDF_NAME(Contents), originalContents);
                            } else {
                                pdf_dict_del(context, pageObject, PDF_NAME(Contents));
                            }
                            pdf_dict_dels(context, pageObject,
                                          "nexPDFOriginalResources");
                            pdf_dict_dels(context, pageObject,
                                          "nexPDFOriginalContents");
                            pdf_dict_dels(context, pageObject,
                                          "nexPDFOriginalPageStateStored");
                        }
                        continue;
                    }
                    page = pdf_load_page(context, pdf_, pageIndex);
                    int index = 0;
                    for (pdf_annot *annot = pdf_first_annot(context, page); annot != nullptr;
                         annot = pdf_next_annot(context, annot), ++index) {
                        const QString name = QString::fromUtf8(pdf_annot_name(context, annot));
                        const bool matches = !candidate.annotationName.isEmpty()
                            ? name == candidate.annotationName
                            : index == candidate.annotationIndex;
                        if (matches) {
                            pdf_delete_annot(context, page, annot);
                            break;
                        }
                    }
                    pdf_drop_page(context, page);
                    page = nullptr;
                }
                if (candidate.contentStream) {
                    pdf_obj *catalog = pdf_dict_get(context, pdf_trailer(context, pdf_), PDF_NAME(Root));
                    pdf_obj *ocProperties = pdf_dict_get(context, catalog, PDF_NAME(OCProperties));
                    pdf_obj *defaultConfig = pdf_dict_get(context, ocProperties, PDF_NAME(D));
                    const QByteArray candidateId = candidate.publicValue.id.toUtf8();
                    const std::array<pdf_obj *, 3> ocgArrays{
                        pdf_dict_get(context, ocProperties, PDF_NAME(OCGs)),
                        pdf_dict_get(context, defaultConfig, PDF_NAME(Order)),
                        pdf_dict_get(context, defaultConfig, PDF_NAME(ON))
                    };
                    for (pdf_obj *array : ocgArrays) {
                        if (!pdf_is_array(context, array)) {
                            continue;
                        }
                        for (int index = pdf_array_len(context, array) - 1; index >= 0; --index) {
                            pdf_obj *ocg = pdf_array_get(context, array, index);
                            pdf_obj *nameObject = pdf_dict_get(context, ocg, PDF_NAME(Name));
                            const char *nameText = nameObject != nullptr
                                ? pdf_to_text_string(context, nameObject) : nullptr;
                            if (nameText != nullptr && candidateId == nameText) {
                                pdf_array_delete(context, array, index);
                            }
                        }
                    }
                }
            }
            pdf_end_operation(context, pdf_);
            operationStarted = false;
        }
        fz_always(context) { pdf_drop_page(context, page); }
        fz_catch(context) {
            const QString detail = engineMessage(context);
            if (operationStarted) {
                fz_try(context) { pdf_abandon_operation(context, pdf_); }
                fz_catch(context) { }
            }
            fail(ErrorCode::EngineError, QStringLiteral("remove-watermarks"),
                 tr("The confirmed watermarks could not be removed."), detail);
            return;
        }
        candidates_.clear();
        changed();
    }

    void save(const QString &targetPath, const SaveOptions &options)
    {
        if (!requirePdf(QStringLiteral("save"))) {
            return;
        }
        const QFileInfo targetInfo(targetPath);
        if (targetInfo.fileName().isEmpty()) {
            fail(ErrorCode::InvalidArgument, QStringLiteral("save"),
                 tr("Choose a valid destination file."));
            return;
        }
        const QString absoluteTarget = targetInfo.absoluteFilePath();
        if (signedDocument_ && QFileInfo(path_).absoluteFilePath() == absoluteTarget) {
            fail(ErrorCode::SignedDocumentRequiresSaveAs, QStringLiteral("save"),
                 tr("A signed PDF must be saved to a different file because editing invalidates signatures."));
            return;
        }
        if (QFileInfo::exists(absoluteTarget) && !options.overwriteConfirmed) {
            fail(ErrorCode::InvalidArgument, QStringLiteral("save"),
                 tr("Confirm before replacing an existing file."), absoluteTarget);
            return;
        }

        const bool createsEncryption = options.encryption.algorithm == EncryptionAlgorithm::Aes128
            || options.encryption.algorithm == EncryptionAlgorithm::Aes256;
        QByteArray userPassword = options.encryption.userPassword.toUtf8();
        QByteArray ownerPassword = options.encryption.ownerPassword.toUtf8();
        auto clearPasswords = qScopeGuard([&userPassword, &ownerPassword] {
            secureClear(userPassword);
            secureClear(ownerPassword);
        });
        if (createsEncryption && userPassword.isEmpty()) {
            fail(ErrorCode::InvalidArgument, QStringLiteral("save"),
                 tr("A non-empty user password is required for confidential encryption."));
            return;
        }
        if (createsEncryption && ownerPassword.isEmpty()) {
            fail(ErrorCode::InvalidArgument, QStringLiteral("save"),
                 tr("An owner password is required for encrypted output."));
            return;
        }
        if (userPassword.size() >= 128 || ownerPassword.size() >= 128) {
            fail(ErrorCode::InvalidArgument, QStringLiteral("save"),
                 tr("Passwords must be shorter than 128 UTF-8 bytes."));
            return;
        }
        if (!QDir().mkpath(targetInfo.absolutePath())) {
            fail(ErrorCode::IoError, QStringLiteral("save"),
                 tr("The destination directory cannot be created."));
            return;
        }

        const QString temporaryPath = targetInfo.absolutePath()
            + QStringLiteral("/.nexpdf-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces)
            + QStringLiteral(".tmp");
        QFile temporary(temporaryPath);
        if (!temporary.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            fail(ErrorCode::IoError, QStringLiteral("save"),
                 tr("A temporary file cannot be created beside the destination."),
                 temporary.errorString());
            return;
        }
        auto removeTemporary = qScopeGuard([temporaryPath] {
            QFile::remove(temporaryPath);
        });

        fz_context *context = runtime_->context();
        QtOutputState state{&temporary};
        fz_output *output = nullptr;
        pdf_write_options writeOptions{};
        auto clearWritePasswords = qScopeGuard([&writeOptions] {
            secureZero(writeOptions.upwd_utf8, sizeof(writeOptions.upwd_utf8));
            secureZero(writeOptions.opwd_utf8, sizeof(writeOptions.opwd_utf8));
        });
        bool writeFailed = false;
        QString detail;
        fz_var(output);
        fz_try(context) {
            pdf_init_write_options(context, &writeOptions);
            writeOptions.do_garbage = options.garbageCollect ? 1 : 0;
            writeOptions.do_compress = options.compress ? 1 : 0;
            writeOptions.do_compress_images = options.compress ? 1 : 0;
            writeOptions.do_compress_fonts = options.compress ? 1 : 0;
            writeOptions.do_use_objstms = options.useObjectStreams ? 1 : 0;
            writeOptions.do_encrypt = options.encryption.algorithm == EncryptionAlgorithm::Aes256
                ? PDF_ENCRYPT_AES_256
                : options.encryption.algorithm == EncryptionAlgorithm::Aes128
                    ? PDF_ENCRYPT_AES_128
                    : options.encryption.algorithm == EncryptionAlgorithm::None
                        ? PDF_ENCRYPT_NONE : PDF_ENCRYPT_KEEP;
            writeOptions.permissions = permissionBits(options.encryption.permissions);
            std::memcpy(writeOptions.upwd_utf8, userPassword.constData(),
                        static_cast<std::size_t>(userPassword.size()));
            std::memcpy(writeOptions.opwd_utf8, ownerPassword.constData(),
                        static_cast<std::size_t>(ownerPassword.size()));
            output = fz_new_output(context, 64 * 1024, &state, writeQtOutput, closeQtOutput, nullptr);
            output->seek = seekQtOutput;
            output->tell = tellQtOutput;
            pdf_write_document(context, pdf_, output, &writeOptions);
            fz_close_output(context, output);
        }
        fz_always(context) { fz_drop_output(context, output); }
        fz_catch(context) {
            writeFailed = true;
            detail = engineMessage(context);
        }
        temporary.flush();
        temporary.close();
        if (writeFailed) {
            fail(ErrorCode::IoError, QStringLiteral("save"),
                 tr("The PDF could not be written."), detail);
            return;
        }

        if (!validateSavedFile(temporaryPath, options.encryption)) {
            return;
        }

        QString replaceDetail;
        if (!replaceAtomically(temporaryPath, absoluteTarget, &replaceDetail)) {
            fail(ErrorCode::IoError, QStringLiteral("save"),
                 tr("The validated temporary file could not replace the destination."), replaceDetail);
            return;
        }
        removeTemporary.dismiss();

        path_ = absoluteTarget;
        modified_ = false;
        if (options.encryption.algorithm != EncryptionAlgorithm::Keep) {
            encrypted_ = options.encryption.algorithm != EncryptionAlgorithm::None;
            secureClear(password_);
            if (encrypted_) {
                password_ = userPassword;
            }
        }
        post([absoluteTarget](DocumentSession *owner) { emit owner->saved(absoluteTarget); });
        emitState();
    }

    void undo()
    {
        if (!requirePdf(QStringLiteral("undo"))) {
            return;
        }
        fz_context *context = runtime_->context();
        int newPageCount = pageCount_;
        fz_var(newPageCount);
        fz_try(context) {
            if (!pdf_can_undo(context, pdf_)) {
                fz_throw(context, FZ_ERROR_ARGUMENT, "Nothing to undo");
            }
            pdf_undo(context, pdf_);
            newPageCount = fz_count_pages(context, document_);
        }
        fz_catch(context) {
            fail(ErrorCode::EngineError, QStringLiteral("undo"),
                 tr("Undo failed."), engineMessage(context));
            return;
        }
        updatePageCountAfterHistory(newPageCount);
        historyChanged();
    }

    void redo()
    {
        if (!requirePdf(QStringLiteral("redo"))) {
            return;
        }
        fz_context *context = runtime_->context();
        int newPageCount = pageCount_;
        fz_var(newPageCount);
        fz_try(context) {
            if (!pdf_can_redo(context, pdf_)) {
                fz_throw(context, FZ_ERROR_ARGUMENT, "Nothing to redo");
            }
            pdf_redo(context, pdf_);
            newPageCount = fz_count_pages(context, document_);
        }
        fz_catch(context) {
            fail(ErrorCode::EngineError, QStringLiteral("redo"),
                 tr("Redo failed."), engineMessage(context));
            return;
        }
        updatePageCountAfterHistory(newPageCount);
        historyChanged();
    }

private:
    struct CandidateRecord {
        WatermarkCandidate publicValue;
        QString annotationName;
        int annotationIndex = -1;
        bool contentStream = false;
        QString resourceBase;
    };

    QString tr(const char *source) const
    {
        return QCoreApplication::translate("DocumentSession", source);
    }

    template<typename Callback>
    void post(Callback callback)
    {
        const QPointer<DocumentSession> owner = owner_;
        QMetaObject::invokeMethod(owner_, [owner, callback = std::move(callback)]() mutable {
            if (owner) {
                callback(owner.data());
            }
        }, Qt::QueuedConnection);
    }

    void fail(ErrorCode code, const QString &operation, const QString &message,
              const QString &detail = {})
    {
        OperationError error{code, message, detail, operation};
        post([error](DocumentSession *owner) { emit owner->failed(error); });
    }

    bool requireDocument(const QString &operation)
    {
        if (document_ != nullptr) {
            return true;
        }
        fail(ErrorCode::InvalidArgument, operation, tr("Open a PDF first."));
        return false;
    }

    bool requirePdf(const QString &operation)
    {
        return requireDocument(operation) && pdf_ != nullptr;
    }

    void changed()
    {
        renderPool_.clear();
        dropDisplayLists();
        modified_ = true;
        ++revision_;
        emitState();
    }

    void updatePageCountAfterHistory(const int count)
    {
        if (pageCount_ == count) {
            return;
        }
        pageCount_ = count;
        post([count](DocumentSession *owner) { emit owner->pageCountChanged(count); });
    }

    void historyChanged()
    {
        renderPool_.clear();
        dropDisplayLists();
        int position = 1;
        int steps = 0;
        fz_context *context = runtime_->context();
        fz_var(position);
        fz_var(steps);
        fz_try(context) { position = pdf_undoredo_state(context, pdf_, &steps); }
        fz_catch(context) { position = 1; }
        modified_ = position != 0;
        ++revision_;
        emitState();
    }

    void emitState()
    {
        bool canUndo = false;
        bool canRedo = false;
        if (runtime_ && pdf_) {
            fz_context *context = runtime_->context();
            fz_var(canUndo);
            fz_var(canRedo);
            fz_try(context) {
                canUndo = pdf_can_undo(context, pdf_) != 0;
                canRedo = pdf_can_redo(context, pdf_) != 0;
            }
            fz_catch(context) {
                canUndo = false;
                canRedo = false;
            }
        }
        const quint64 revision = revision_;
        const bool modified = modified_;
        post([revision, modified, canUndo, canRedo](DocumentSession *owner) {
            emit owner->stateChanged(revision, modified, canUndo, canRedo);
        });
    }

    static int permissionBits(const DocumentPermissions &permissions)
    {
        int bits = 0;
        bits |= permissions.print ? PDF_PERM_PRINT : 0;
        bits |= permissions.copy ? PDF_PERM_COPY : 0;
        bits |= permissions.annotate ? PDF_PERM_ANNOTATE : 0;
        bits |= permissions.fillForms ? PDF_PERM_FORM : 0;
        bits |= permissions.assemble ? PDF_PERM_ASSEMBLE : 0;
        bits |= permissions.modify ? PDF_PERM_MODIFY : 0;
        bits |= permissions.accessibility ? PDF_PERM_ACCESSIBILITY : 0;
        bits |= permissions.highQualityPrint ? PDF_PERM_PRINT_HQ : 0;
        return bits;
    }

    bool validateSavedFile(const QString &path, const EncryptionOptions &encryption)
    {
        fz_context *context = runtime_->context();
        fz_document *validation = nullptr;
        fz_page *page = nullptr;
        bool valid = false;
        const QByteArray nativePath = QDir::toNativeSeparators(path).toUtf8();
        QByteArray password = encryption.algorithm == EncryptionAlgorithm::Keep
            ? password_ : encryption.userPassword.toUtf8();
        auto clearPassword = qScopeGuard([&password] { secureClear(password); });
        fz_var(validation);
        fz_var(page);
        fz_var(valid);
        fz_try(context) {
            validation = fz_open_document(context, nativePath.constData());
            if (fz_needs_password(context, validation)
                && fz_authenticate_password(context, validation, password.constData()) == 0) {
                fz_throw(context, FZ_ERROR_ARGUMENT, "Saved PDF password validation failed");
            }
            const int count = fz_count_pages(context, validation);
            if (count <= 0) {
                fz_throw(context, FZ_ERROR_FORMAT, "Saved PDF has no pages");
            }
            page = fz_load_page(context, validation, 0);
            fz_drop_page(context, page);
            page = nullptr;
            if (count > 1) {
                page = fz_load_page(context, validation, count - 1);
            }
            valid = true;
        }
        fz_always(context) {
            fz_drop_page(context, page);
            fz_drop_document(context, validation);
        }
        fz_catch(context) {
            fail(ErrorCode::ValidationFailed, QStringLiteral("save"),
                 tr("The temporary PDF failed reopen validation."), engineMessage(context));
            return false;
        }
        return valid;
    }

    void dropDisplayLists()
    {
        if (!runtime_) {
            displayLists_.clear();
            displayListBounds_.clear();
            displayListLru_.clear();
            return;
        }
        fz_context *context = runtime_->context();
        for (fz_display_list *list : std::as_const(displayLists_)) {
            fz_drop_display_list(context, list);
        }
        displayLists_.clear();
        displayListBounds_.clear();
        displayListLru_.clear();
    }

    void touchDisplayList(const int pageIndex)
    {
        displayListLru_.removeAll(pageIndex);
        displayListLru_.append(pageIndex);
        while (displayListLru_.size() > kMaximumCachedDisplayLists) {
            const int evictedPage = displayListLru_.takeFirst();
            const auto evicted = displayLists_.find(evictedPage);
            if (evicted != displayLists_.end()) {
                fz_drop_display_list(runtime_->context(), evicted.value());
                displayLists_.erase(evicted);
            }
            displayListBounds_.remove(evictedPage);
        }
    }

    QPointer<DocumentSession> owner_;
    std::unique_ptr<detail::MuPdfRuntime> runtime_;
    QThreadPool renderPool_;
    QHash<int, fz_display_list *> displayLists_;
    QHash<int, fz_rect> displayListBounds_;
    QList<int> displayListLru_;
    fz_document *document_ = nullptr;
    pdf_document *pdf_ = nullptr;
    QString path_;
    QByteArray password_;
    int pageCount_ = 0;
    quint64 revision_ = 0;
    bool encrypted_ = false;
    bool signedDocument_ = false;
    bool modified_ = false;
    QVector<CandidateRecord> candidates_;
};

DocumentSession::DocumentSession(QObject *parent)
    : QObject(parent), impl_(std::make_unique<Impl>(this)), workerContext_(new QObject)
{
    qRegisterMetaType<OperationError>();
    qRegisterMetaType<OpenOptions>();
    qRegisterMetaType<EditOperation>();
    qRegisterMetaType<RenderRequest>();
    qRegisterMetaType<RenderResult>();
    qRegisterMetaType<WatermarkSpec>();
    qRegisterMetaType<QVector<WatermarkCandidate>>();
    qRegisterMetaType<SaveOptions>();
    qRegisterMetaType<QVector<SearchHit>>();
    qRegisterMetaType<DocumentInfo>();

    documentThread_.setObjectName(QStringLiteral("nexPDF document thread"));
    workerContext_->moveToThread(&documentThread_);
    documentThread_.start();
    QMetaObject::invokeMethod(workerContext_, [this] { impl_->initialize(); },
                              Qt::BlockingQueuedConnection);
}

DocumentSession::~DocumentSession()
{
    if (documentThread_.isRunning()) {
        QMetaObject::invokeMethod(workerContext_, [this] { impl_->closeDocument(false); },
                                  Qt::BlockingQueuedConnection);
        QObject *context = workerContext_;
        QMetaObject::invokeMethod(context, [context] { delete context; },
                                  Qt::BlockingQueuedConnection);
        workerContext_ = nullptr;
        documentThread_.quit();
        documentThread_.wait();
    }
    impl_.reset();
}

void DocumentSession::open(const QString &path, const OpenOptions &options)
{
    QMetaObject::invokeMethod(workerContext_, [this, path, options] { impl_->openDocument(path, options); });
}

void DocumentSession::close()
{
    QMetaObject::invokeMethod(workerContext_, [this] { impl_->closeDocument(); });
}

void DocumentSession::requestRender(const RenderRequest &request)
{
    QMetaObject::invokeMethod(workerContext_, [this, request] { impl_->render(request); });
}

void DocumentSession::search(const QString &text)
{
    QMetaObject::invokeMethod(workerContext_, [this, text] { impl_->search(text); });
}

void DocumentSession::extractText(const int pageIndex, const QRectF &bounds)
{
    QMetaObject::invokeMethod(workerContext_,
        [this, pageIndex, bounds] { impl_->extractText(pageIndex, bounds); });
}

void DocumentSession::applyEdit(const EditOperation &operation)
{
    QMetaObject::invokeMethod(workerContext_, [this, operation] { impl_->edit(operation); });
}

void DocumentSession::addWatermark(const WatermarkSpec &spec)
{
    QMetaObject::invokeMethod(workerContext_, [this, spec] { impl_->addWatermark(spec); });
}

void DocumentSession::scanWatermarks()
{
    QMetaObject::invokeMethod(workerContext_, [this] { impl_->scanWatermarks(); });
}

void DocumentSession::removeWatermarks(const QStringList &candidateIds)
{
    QMetaObject::invokeMethod(workerContext_, [this, candidateIds] { impl_->removeWatermarks(candidateIds); });
}

void DocumentSession::saveAs(const QString &path, const SaveOptions &options)
{
    QMetaObject::invokeMethod(workerContext_, [this, path, options] { impl_->save(path, options); });
}

void DocumentSession::undo()
{
    QMetaObject::invokeMethod(workerContext_, [this] { impl_->undo(); });
}

void DocumentSession::redo()
{
    QMetaObject::invokeMethod(workerContext_, [this] { impl_->redo(); });
}

} // namespace nexpdf
