#include "pdf_canvas.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {
constexpr int kPageGap = 24;
constexpr int kTileEdge = 512;
constexpr int kMaximumTileEdge = 1024;  // DocumentSession rejects rendered tile edges above this.
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
    // An edit rebuilds the document (revision bump) but keeps the page layout;
    // preserve the scroll position so edits do not teleport the view to the top.
    auto *area = qobject_cast<QScrollArea *>(parentWidget() ? parentWidget()->parentWidget() : nullptr);
    const int savedScroll = area ? area->verticalScrollBar()->value() : 0;
    const bool sameLayout = info.pageCount == pageCount_ && pageCount_ > 0;

    pageCount_ = info.pageCount;
    revision_ = info.revision;
    currentPage_ = 0;
    pages_.clear();
    cache_.clear();
    pending_.clear();
    requestKeys_.clear();
    requestDensities_.clear();
    outstandingRequests_.clear();
    retriedRequests_.clear();
    searchHits_.clear();
    activeHit_ = -1;
    selectionRect_ = {};
    rebuildLayout();
    if (sameLayout && area) {
        area->verticalScrollBar()->setValue(
            std::clamp(savedScroll, 0, area->verticalScrollBar()->maximum()));
    }
}

void PdfCanvas::clearDocument()
{
    pageCount_ = 0;
    pages_.clear();
    cache_.clear();
    pending_.clear();
    requestKeys_.clear();
    requestDensities_.clear();
    outstandingRequests_.clear();
    retriedRequests_.clear();
    searchHits_.clear();
    activeHit_ = -1;
    selectionRect_ = {};
    resize(1, 1);
    update();
}

void PdfCanvas::setSearchHits(const QVector<nexpdf::SearchHit> &hits)
{
    searchHits_ = hits;
    activeHit_ = hits.isEmpty() ? -1 : 0;
    update();
}

void PdfCanvas::setActiveHit(const int index)
{
    if (searchHits_.isEmpty()) {
        return;
    }
    const int bounded = std::clamp(index, 0, static_cast<int>(searchHits_.size()) - 1);
    if (activeHit_ == bounded) {
        return;
    }
    activeHit_ = bounded;
    update();
}

void PdfCanvas::revealActiveHit()
{
    if (activeHit_ < 0 || activeHit_ >= searchHits_.size()) {
        return;
    }
    const nexpdf::SearchHit &hit = searchHits_[activeHit_];
    if (hit.pageIndex < 0 || hit.pageIndex >= pages_.size()) {
        return;
    }
    auto *area = qobject_cast<QScrollArea *>(parentWidget()->parentWidget());
    if (area == nullptr) {
        return;
    }
    const PageLayout &layout = pages_[hit.pageIndex];
    if (hit.quads.isEmpty()) {
        area->verticalScrollBar()->setValue(layout.rect.top());
        return;
    }
    QRectF bounds = hit.quads.first();
    for (const QRectF &quad : hit.quads) {
        bounds = bounds.united(quad);
    }
    const int y = layout.rect.top() + qRound(bounds.top() * zoom_);
    const int target = y - area->viewport()->height() / 3;
    area->verticalScrollBar()->setValue(std::clamp(target, 0, area->verticalScrollBar()->maximum()));
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
    requestDensities_.clear();
    outstandingRequests_.clear();
    retriedRequests_.clear();
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
    requestDensities_.clear();
    outstandingRequests_.clear();
    retriedRequests_.clear();
    rebuildLayout();
}

void PdfCanvas::setCacheLimitMiB(const int mebibytes)
{
    const int totalBudget = std::clamp(mebibytes, kMuPdfStoreMiB, 1024);
    cache_.setMaxCost((totalBudget - kMuPdfStoreMiB) * 1024);
}

void PdfCanvas::setEyeCare(const bool enabled)
{
    if (eyeCare_ == enabled) {
        return;
    }
    eyeCare_ = enabled;
    update();
}

void PdfCanvas::restorePosition(const int pageIndex, const qreal pageFraction)
{
    if (pageIndex < 0 || pageIndex >= pages_.size()) {
        return;
    }
    auto *area = qobject_cast<QScrollArea *>(parentWidget()->parentWidget());
    if (area == nullptr) {
        return;
    }
    const QRect &rect = pages_[pageIndex].rect;
    const int value = rect.top() + qRound(std::clamp(pageFraction, 0.0, 1.0) * rect.height());
    area->verticalScrollBar()->setValue(std::clamp(value, 0, area->verticalScrollBar()->maximum()));
}

qreal PdfCanvas::currentPageFraction() const
{
    if (currentPage_ < 0 || currentPage_ >= pages_.size()) {
        return 0.0;
    }
    auto *area = qobject_cast<QScrollArea *>(parentWidget() ? parentWidget()->parentWidget() : nullptr);
    if (area == nullptr) {
        return 0.0;
    }
    const PageLayout &layout = pages_[currentPage_];
    if (layout.rect.height() <= 0) {
        return 0.0;
    }
    const qreal fraction = (area->verticalScrollBar()->value() - layout.rect.top())
        / static_cast<qreal>(layout.rect.height());
    return std::clamp(fraction, 0.0, 1.0);
}

void PdfCanvas::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const int steps = event->angleDelta().y() / 120;
        if (steps != 0) {
            emit zoomRequested(std::pow(1.2, steps));
        }
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

qreal PdfCanvas::fitWidthZoom() const
{
    auto *area = qobject_cast<QScrollArea *>(parentWidget() ? parentWidget()->parentWidget() : nullptr);
    if (area == nullptr || pages_.isEmpty()) {
        return 1.0;
    }
    const PageLayout &layout = pages_[std::clamp(currentPage_, 0, static_cast<int>(pages_.size()) - 1)];
    const qreal pointsWidth = layout.rect.width() * layout.pointsPerPixel;
    if (pointsWidth <= 0.0) {
        return 1.0;
    }
    const int available = area->viewport()->width() - 2 * kPageGap - 8;
    return std::clamp(available / pointsWidth, 0.1, 6.0);
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
    const int edge = tileEdge();
    for (int index = 0; index < pages_.size(); ++index) {
        const QRect pageRect = pages_[index].rect;
        if (!pageRect.intersects(visible)) {
            continue;
        }
        painter.fillRect(pageRect, Qt::white);
        bool drewTile = false;
        const QRect localVisible = event->rect().intersected(pageRect).translated(-pageRect.topLeft());
        const int firstX = std::max(0, localVisible.left() / edge * edge);
        const int firstY = std::max(0, localVisible.top() / edge * edge);
        for (int y = firstY; y <= localVisible.bottom(); y += edge) {
            for (int x = firstX; x <= localVisible.right(); x += edge) {
                if (const QImage *image = cache_.object(cacheKey(index, QPoint(x, y)))) {
                    painter.drawImage(pageRect.topLeft() + QPoint(x, y), *image);
                    drewTile = true;
                }
            }
        }
        if (eyeCare_) {
            // Multiply keeps strokes and images readable while tinting the page;
            // white maps exactly to the tint color.
            painter.save();
            painter.setCompositionMode(QPainter::CompositionMode_Multiply);
            painter.fillRect(pageRect, QColor(0xC8, 0xE6, 0xC9));
            painter.restore();
        }
        for (int hitIndex = 0; hitIndex < searchHits_.size(); ++hitIndex) {
            const nexpdf::SearchHit &hit = searchHits_[hitIndex];
            if (hit.pageIndex != index) {
                continue;
            }
            const bool active = hitIndex == activeHit_;
            painter.setBrush(active ? QColor(255, 150, 0, 170) : QColor(255, 214, 0, 95));
            painter.setPen(active ? QPen(QColor(200, 100, 0)) : Qt::NoPen);
            for (const QRectF &quad : hit.quads) {
                painter.drawRect(QRectF(pageRect.left() + quad.left() * zoom_,
                                        pageRect.top() + quad.top() * zoom_,
                                        quad.width() * zoom_, quad.height() * zoom_));
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
    qreal density = 1.0;
    if (const auto recorded = requestDensities_.constFind(result.requestId); recorded != requestDensities_.constEnd()) {
        density = recorded.value();
    }
    if (result.revision != revision_ || result.pageIndex < 0 || result.pageIndex >= pages_.size()) {
        requestKeys_.erase(request);
        requestDensities_.remove(result.requestId);
        outstandingRequests_.remove(result.requestId);
        retriedRequests_.remove(result.requestId);
        pending_.remove(key);
        return;
    }
    if (result.image.isNull()) {
        // Retry a transiently failed tile exactly once before giving up so a
        // hiccup does not leave a permanently blank region.
        if (!retriedRequests_.contains(result.requestId) && outstandingRequests_.contains(result.requestId)) {
            retriedRequests_.insert(result.requestId);
            session_->requestRender(outstandingRequests_.value(result.requestId));
            return;
        }
        requestKeys_.erase(request);
        requestDensities_.remove(result.requestId);
        outstandingRequests_.remove(result.requestId);
        retriedRequests_.remove(result.requestId);
        pending_.remove(key);
        return;
    }
    requestKeys_.erase(request);
    requestDensities_.remove(result.requestId);
    outstandingRequests_.remove(result.requestId);
    retriedRequests_.remove(result.requestId);
    pending_.remove(key);
    auto *image = new QImage(result.image);
    image->setDevicePixelRatio(density);
    cache_.insert(key, image, std::max(1, static_cast<int>(image->sizeInBytes() / 1024)));

    const QSize oldSize = pages_[result.pageIndex].pixelSize;
    const QSize newSize(std::max(1, qRound(result.pagePixelSize.width() / density)),
                        std::max(1, qRound(result.pagePixelSize.height() / density)));
    if (oldSize != newSize) {
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
        const int edge = tileEdge();
        const qreal density = renderDensity();
        const int renderedEdge = qRound(edge * density);
        const int firstX = std::max(0, pageVisible.left() / edge * edge);
        const int firstY = std::max(0, pageVisible.top() / edge * edge);
        for (int y = firstY; y <= pageVisible.bottom(); y += edge) {
            for (int x = firstX; x <= pageVisible.right(); x += edge) {
                const QPoint origin(x, y);
                const QString key = cacheKey(index, origin);
                if (cache_.contains(key) || pending_.contains(key)) {
                    continue;
                }
                pending_.insert(key);
                nexpdf::RenderRequest request;
                request.requestId = nextRequestId_++;
                requestKeys_.insert(request.requestId, key);
                requestDensities_.insert(request.requestId, density);
                outstandingRequests_.insert(request.requestId, request);
                request.revision = revision_;
                request.pageIndex = index;
                request.scale = zoom_ * density;
                request.rotation = rotation_;
                request.tilePixels = QRect(qRound(origin.x() * density), qRound(origin.y() * density),
                                           renderedEdge, renderedEdge);
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
    return QStringLiteral("%1:%2:%3:%4:%5:%6:%7")
        .arg(revision_).arg(pageIndex).arg(qRound(zoom_ * 1000)).arg(qRound(renderDensity() * 100))
        .arg(rotation_).arg(tileOrigin.x()).arg(tileOrigin.y());
}

qreal PdfCanvas::renderDensity() const
{
    return std::max(1.0, devicePixelRatioF());
}

int PdfCanvas::tileEdge() const
{
    const qreal density = renderDensity();
    if (qRound(kTileEdge * density) <= kMaximumTileEdge) {
        return kTileEdge;
    }
    return std::max(64, static_cast<int>(kMaximumTileEdge / density));
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
