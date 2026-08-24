#pragma once

#include "nexpdf/document_session.h"

#include <QCache>
#include <QHash>
#include <QSet>
#include <QWidget>

class PdfCanvas final : public QWidget {
    Q_OBJECT

public:
    explicit PdfCanvas(nexpdf::DocumentSession *session, QWidget *parent = nullptr);

    void setDocument(const nexpdf::DocumentInfo &info);
    void clearDocument();
    void setZoom(qreal zoom);
    void setRotation(int rotation);
    void setCacheLimitMiB(int mebibytes);
    [[nodiscard]] int currentPage() const noexcept { return currentPage_; }
    [[nodiscard]] qreal zoom() const noexcept { return zoom_; }
    [[nodiscard]] int rotation() const noexcept { return rotation_; }
    [[nodiscard]] bool hasRenderedContent() const noexcept { return !cache_.isEmpty(); }
    void goToPage(int pageIndex);

signals:
    void currentPageChanged(int pageIndex);
    void regionSelected(int pageIndex, const QRectF &pageRect);
    void pathSelected(int pageIndex, const QVector<QPointF> &pagePoints);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    void acceptRender(const nexpdf::RenderResult &result);

private:
    struct PageLayout {
        QRect rect;
        QSize pixelSize;
        qreal pointsPerPixel = 1.0;
    };

    void rebuildLayout();
    void requestVisiblePages(const QRect &visible);
    QString cacheKey(int pageIndex, const QPoint &tileOrigin) const;
    std::optional<int> pageAt(const QPoint &position) const;

    nexpdf::DocumentSession *session_;
    QVector<PageLayout> pages_;
    QCache<QString, QImage> cache_;
    QSet<QString> pending_;
    QHash<quint64, QString> requestKeys_;
    int pageCount_ = 0;
    int currentPage_ = 0;
    int rotation_ = 0;
    qreal zoom_ = 1.0;
    quint64 revision_ = 0;
    quint64 nextRequestId_ = 1;
    QPoint dragStart_;
    QPoint dragEnd_;
    QRect selectionRect_;
    QVector<QPoint> dragPoints_;
    bool dragging_ = false;
};
