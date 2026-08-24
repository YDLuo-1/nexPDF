#include "pdf_canvas.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollArea>
#include <QScrollBar>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {
constexpr int kPageGap = 24;
constexpr int kTileEdge = 512;
constexpr int kMuPdfStoreMiB = 64;
constexpr int kDefaultTotalCacheMiB = 256;
constexpr QSize kDefaultPagePoints(595, 842);
}

PdfCanvas::PdfCanvas(nexpdf::DocumentSession *session, QWidget *parent)
    : QWidget(parent), session_(session),
      cache_((kDefaultTotalCacheMiB - kMuPdfStoreMiB) * 1024)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    connect(session_, &nexpdf::DocumentSession::renderReady,
            this, &PdfCanvas::acceptRender);
}

void PdfCanvas::setDocument(const nexpdf::DocumentInfo &info)
{
    pageCount_ = info.pageCount;
    revision_ = info.revision;
    currentPage_ = 0;
    pages_.clear();
    cache_.clear();
    pending_.clear();
    requestKeys_.clear();
    selectionRect_ = {};
    rebuildLayout();
}

void PdfCanvas::clearDocument()
{
    pageCount_ = 0;
    pages_.clear();
    cache_.clear();
    pending_.clear();
    requestKeys_.clear();
    selectionRect_ = {};
    resize(1, 1);
    update();
}

void PdfCanvas::setZoom(const qreal zoom)
{
    const qreal bounded = std::clamp(zoom, 0.1, 6.0);
    if (qFuzzyCompare(zoom_, bounded)) {
        return;
    }
    zoom_ = bounded;
    for (PageLayout &page : pages_) page.pixelSize = {};
    cache_.clear();
    pending_.clear();
    requestKeys_.clear();
    rebuildLayout();
}

void PdfCanvas::setRotation(const int rotation)
{
    const int value = ((rotation % 360) + 360) % 360;
    if (rotation_ == value) {
        return;
    }
    rotation_ = value;
    for (PageLayout &page : pages_) page.pixelSize = {};
    cache_.clear();
    pending_.clear();
    requestKeys_.clear();
    rebuildLayout();
}

void PdfCanvas::setCacheLimitMiB(const int mebibytes)
{
    const int totalBudget = std::clamp(mebibytes, kMuPdfStoreMiB, 1024);
    cache_.setMaxCost((totalBudget - kMuPdfStoreMiB) * 1024);
}

void PdfCanvas::goToPage(const int pageIndex)
{
    if (pageIndex < 0 || pageIndex >= pages_.size()) {
        return;
    }
    if (auto *area = qobject_cast<QScrollArea *>(parentWidget()->parentWidget())) {
        area->verticalScrollBar()->setValue(pages_[pageIndex].rect.top());
    }
}

void PdfCanvas::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), QColor(42, 44, 48));
    if (pageCount_ == 0) {
        painter.setPen(Qt::lightGray);
        painter.drawText(rect(), Qt::AlignCenter, tr("Open a PDF to begin"));
        return;
    }

    const QRect visible = visibleRegion().boundingRect().adjusted(0, -height(), 0, height());
    for (int index = 0; index < pages_.size(); ++index) {
        const QRect pageRect = pages_[index].rect;
        if (!pageRect.intersects(visible)) {
            continue;
        }
        painter.fillRect(pageRect, Qt::white);
        bool drewTile = false;
        const QRect localVisible = event->rect().intersected(pageRect).translated(-pageRect.topLeft());
        const int firstX = std::max(0, localVisible.left() / kTileEdge * kTileEdge);
        const int firstY = std::max(0, localVisible.top() / kTileEdge * kTileEdge);
        for (int y = firstY; y <= localVisible.bottom(); y += kTileEdge) {
            for (int x = firstX; x <= localVisible.right(); x += kTileEdge) {
                if (const QImage *image = cache_.object(cacheKey(index, QPoint(x, y)))) {
                    painter.drawImage(pageRect.topLeft() + QPoint(x, y), *image);
                    drewTile = true;
                }
            }
        }
        if (!drewTile) {
            painter.setPen(Qt::gray);
            painter.drawText(pageRect, Qt::AlignCenter, tr("Page") + QStringLiteral(" %1").arg(index + 1));
        }
    }

    if (dragging_ || !selectionRect_.isEmpty()) {
        painter.setPen(QPen(QColor(40, 130, 255), 2, Qt::DashLine));
        painter.setBrush(QColor(40, 130, 255, 35));
        painter.drawRect(dragging_ ? QRect(dragStart_, dragEnd_).normalized() : selectionRect_);
    }
    requestVisiblePages(visibleRegion().boundingRect());
}

void PdfCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && pageAt(event->position().toPoint()).has_value()) {
        dragging_ = true;
        selectionRect_ = {};
        dragStart_ = dragEnd_ = event->position().toPoint();
        dragPoints_ = {dragStart_};
        update();
    }
}

void PdfCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (dragging_) {
        dragEnd_ = event->position().toPoint();
        if (dragPoints_.isEmpty() || (dragPoints_.last() - dragEnd_).manhattanLength() >= 2) {
            dragPoints_.append(dragEnd_);
        }
        update();
    }
}

void PdfCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (!dragging_ || event->button() != Qt::LeftButton) {
        return;
    }
    dragEnd_ = event->position().toPoint();
    dragging_ = false;
    const auto page = pageAt(dragStart_);
    if (page && pageAt(dragEnd_) == page) {
        const PageLayout &layout = pages_[*page];
        QRect selection = QRect(dragStart_, dragEnd_).normalized().intersected(layout.rect);
        selectionRect_ = selection;
        selection.translate(-layout.rect.topLeft());
        const qreal factor = layout.pointsPerPixel;
        if (selection.width() >= 2 && selection.height() >= 2) {
            emit regionSelected(*page, QRectF(selection.x() * factor, selection.y() * factor,
                                              selection.width() * factor, selection.height() * factor));
        }
        QVector<QPointF> pagePoints;
        pagePoints.reserve(dragPoints_.size());
        for (const QPoint &point : std::as_const(dragPoints_)) {
            const QPoint local = point - layout.rect.topLeft();
            pagePoints.append(QPointF(local.x() * factor, local.y() * factor));
        }
        emit pathSelected(*page, pagePoints);
    }
    dragPoints_.clear();
    update();
}

void PdfCanvas::acceptRender(const nexpdf::RenderResult &result)
{
    const auto request = requestKeys_.find(result.requestId);
    if (request == requestKeys_.end()) {
        return;
    }
    const QString key = request.value();
    requestKeys_.erase(request);
    pending_.remove(key);
    if (result.revision != revision_ || result.pageIndex < 0 || result.pageIndex >= pages_.size()) {
        return;
    }
    if (result.image.isNull()) {
        return;
    }
    auto *image = new QImage(result.image);
    cache_.insert(key, image, std::max(1, static_cast<int>(image->sizeInBytes() / 1024)));

    const QSize oldSize = pages_[result.pageIndex].pixelSize;
    const QSize newSize = result.pagePixelSize;
    if (newSize.isValid() && oldSize != newSize) {
        pages_[result.pageIndex].pixelSize = newSize;
        rebuildLayout();
    } else {
        update(pages_[result.pageIndex].rect);
    }
}

void PdfCanvas::rebuildLayout()
{
    pages_.resize(pageCount_);
    int y = kPageGap;
    int widest = 0;
    QSize defaultSize = kDefaultPagePoints * zoom_;
    if (rotation_ == 90 || rotation_ == 270) {
        defaultSize.transpose();
    }
    for (int i = 0; i < pageCount_; ++i) {
        const QSize size = pages_[i].pixelSize.isValid() ? pages_[i].pixelSize : defaultSize;
        pages_[i].rect = QRect(kPageGap, y, size.width(), size.height());
        pages_[i].pointsPerPixel = 1.0 / zoom_;
        y += size.height() + kPageGap;
        widest = std::max(widest, size.width());
    }
    resize(widest + 2 * kPageGap, std::max(1, y));
    update();
}

void PdfCanvas::requestVisiblePages(const QRect &visible)
{
    const int centerY = visible.center().y();
    int nearestPage = currentPage_;
    int nearestDistance = std::numeric_limits<int>::max();
    for (int index = 0; index < pages_.size(); ++index) {
        const int distance = std::abs(pages_[index].rect.center().y() - centerY);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearestPage = index;
        }
        if (!pages_[index].rect.intersects(visible.adjusted(0, -visible.height(), 0, visible.height()))) {
            continue;
        }
        const QRect pageVisible = pages_[index].rect
            .intersected(visible.adjusted(0, -visible.height(), 0, visible.height()))
            .translated(-pages_[index].rect.topLeft());
        const int firstX = std::max(0, pageVisible.left() / kTileEdge * kTileEdge);
        const int firstY = std::max(0, pageVisible.top() / kTileEdge * kTileEdge);
        for (int y = firstY; y <= pageVisible.bottom(); y += kTileEdge) {
            for (int x = firstX; x <= pageVisible.right(); x += kTileEdge) {
                const QPoint origin(x, y);
                const QString key = cacheKey(index, origin);
                if (cache_.contains(key) || pending_.contains(key)) {
                    continue;
                }
                pending_.insert(key);
                nexpdf::RenderRequest request;
                request.requestId = nextRequestId_++;
                requestKeys_.insert(request.requestId, key);
                request.revision = revision_;
                request.pageIndex = index;
                request.scale = zoom_;
                request.rotation = rotation_;
                request.tilePixels = QRect(origin, QSize(kTileEdge, kTileEdge));
                request.priority = pages_[index].rect.intersects(visible) ? 10 : 0;
                session_->requestRender(request);
            }
        }
    }
    if (nearestPage != currentPage_) {
        currentPage_ = nearestPage;
        emit currentPageChanged(currentPage_);
    }
}

QString PdfCanvas::cacheKey(const int pageIndex, const QPoint &tileOrigin) const
{
    return QStringLiteral("%1:%2:%3:%4:%5:%6")
        .arg(revision_).arg(pageIndex).arg(qRound(zoom_ * 1000)).arg(rotation_)
        .arg(tileOrigin.x()).arg(tileOrigin.y());
}

std::optional<int> PdfCanvas::pageAt(const QPoint &position) const
{
    for (int index = 0; index < pages_.size(); ++index) {
        if (pages_[index].rect.contains(position)) {
            return index;
        }
    }
    return std::nullopt;
}
