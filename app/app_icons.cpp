#include "app_icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QResource>

void initializeNexPdfResources()
{
    Q_INIT_RESOURCE(nexpdf_assets);
}

namespace {

constexpr QColor inkColor{42, 51, 70};
constexpr QColor accentColor{232, 72, 86};

void configurePen(QPainter &painter, const QColor &color = inkColor, const qreal width = 1.8)
{
    painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
}

void drawPage(QPainter &painter, const QRectF &bounds)
{
    const qreal fold = bounds.width() * 0.28;
    QPainterPath page;
    page.moveTo(bounds.left(), bounds.top());
    page.lineTo(bounds.right() - fold, bounds.top());
    page.lineTo(bounds.right(), bounds.top() + fold);
    page.lineTo(bounds.right(), bounds.bottom());
    page.lineTo(bounds.left(), bounds.bottom());
    page.closeSubpath();
    painter.drawPath(page);
    painter.drawLine(QPointF(bounds.right() - fold, bounds.top()),
                     QPointF(bounds.right() - fold, bounds.top() + fold));
    painter.drawLine(QPointF(bounds.right() - fold, bounds.top() + fold),
                     QPointF(bounds.right(), bounds.top() + fold));
}

void drawArrow(QPainter &painter, const QPointF &from, const QPointF &to)
{
    painter.drawLine(from, to);
    const qreal direction = to.x() >= from.x() ? 1.0 : -1.0;
    painter.drawLine(to, QPointF(to.x() - 3.0 * direction, to.y() - 2.5));
    painter.drawLine(to, QPointF(to.x() - 3.0 * direction, to.y() + 2.5));
}

void drawVerticalArrow(QPainter &painter, const qreal x, const qreal fromY, const qreal toY)
{
    painter.drawLine(QPointF(x, fromY), QPointF(x, toY));
    const qreal direction = toY >= fromY ? 1.0 : -1.0;
    painter.drawLine(QPointF(x, toY), QPointF(x - 2.5, toY - 3.0 * direction));
    painter.drawLine(QPointF(x, toY), QPointF(x + 2.5, toY - 3.0 * direction));
}

void drawMagnifier(QPainter &painter, const QPointF &center, const qreal radius)
{
    painter.drawEllipse(center, radius, radius);
    const qreal diagonal = radius * 0.7;
    painter.drawLine(center + QPointF(diagonal, diagonal),
                     center + QPointF(diagonal + 4.0, diagonal + 4.0));
}

QPixmap renderIcon(const nexpdf::icons::Kind kind, const int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(size / 24.0, size / 24.0);
    configurePen(painter);

    switch (kind) {
    case nexpdf::icons::Kind::Open: {
        QPainterPath folder;
        folder.moveTo(2.5, 7.0);
        folder.lineTo(8.5, 7.0);
        folder.lineTo(10.5, 9.0);
        folder.lineTo(21.0, 9.0);
        folder.lineTo(19.2, 19.0);
        folder.lineTo(3.0, 19.0);
        folder.closeSubpath();
        painter.drawPath(folder);
        painter.drawLine(QPointF(3.0, 7.0), QPointF(3.0, 18.5));
        break;
    }
    case nexpdf::icons::Kind::SaveAs:
        painter.drawRoundedRect(QRectF(4.0, 3.0, 16.0, 18.0), 1.5, 1.5);
        painter.drawRect(QRectF(7.0, 3.0, 9.0, 6.0));
        painter.drawRoundedRect(QRectF(7.0, 14.0, 10.0, 7.0), 1.0, 1.0);
        break;
    case nexpdf::icons::Kind::Undo:
    case nexpdf::icons::Kind::Redo: {
        const bool redo = kind == nexpdf::icons::Kind::Redo;
        QPainterPath curve;
        curve.moveTo(redo ? 18.0 : 6.0, 8.0);
        curve.cubicTo(redo ? 14.0 : 10.0, 4.0, redo ? 5.0 : 19.0, 7.0,
                      redo ? 5.0 : 19.0, 14.0);
        painter.drawPath(curve);
        const qreal x = redo ? 18.0 : 6.0;
        painter.drawLine(QPointF(x, 8.0), QPointF(x + (redo ? -4.0 : 4.0), 7.0));
        painter.drawLine(QPointF(x, 8.0), QPointF(x + (redo ? -1.0 : 1.0), 12.0));
        break;
    }
    case nexpdf::icons::Kind::PreviousPage:
    case nexpdf::icons::Kind::NextPage: {
        const bool next = kind == nexpdf::icons::Kind::NextPage;
        const qreal barX = next ? 19.0 : 5.0;
        painter.drawLine(QPointF(barX, 5.0), QPointF(barX, 19.0));
        const QPolygonF chevron = next
            ? QPolygonF{QPointF(8.0, 5.0), QPointF(15.0, 12.0), QPointF(8.0, 19.0)}
            : QPolygonF{QPointF(16.0, 5.0), QPointF(9.0, 12.0), QPointF(16.0, 19.0)};
        painter.drawPolyline(chevron);
        break;
    }
    case nexpdf::icons::Kind::ZoomOut:
    case nexpdf::icons::Kind::ZoomIn:
        drawMagnifier(painter, QPointF(10.0, 10.0), 6.0);
        painter.drawLine(QPointF(7.0, 10.0), QPointF(13.0, 10.0));
        if (kind == nexpdf::icons::Kind::ZoomIn) {
            painter.drawLine(QPointF(10.0, 7.0), QPointF(10.0, 13.0));
        }
        break;
    case nexpdf::icons::Kind::ActualSize:
        painter.drawLine(QPointF(4.0, 9.0), QPointF(4.0, 4.0));
        painter.drawLine(QPointF(4.0, 4.0), QPointF(9.0, 4.0));
        painter.drawLine(QPointF(15.0, 4.0), QPointF(20.0, 4.0));
        painter.drawLine(QPointF(20.0, 4.0), QPointF(20.0, 9.0));
        painter.drawLine(QPointF(4.0, 15.0), QPointF(4.0, 20.0));
        painter.drawLine(QPointF(4.0, 20.0), QPointF(9.0, 20.0));
        painter.drawLine(QPointF(15.0, 20.0), QPointF(20.0, 20.0));
        painter.drawLine(QPointF(20.0, 20.0), QPointF(20.0, 15.0));
        break;
    case nexpdf::icons::Kind::Encrypt:
    case nexpdf::icons::Kind::Decrypt: {
        const bool unlocked = kind == nexpdf::icons::Kind::Decrypt;
        painter.drawRoundedRect(QRectF(5.0, 10.0, 14.0, 11.0), 2.0, 2.0);
        QPainterPath shackle;
        shackle.moveTo(unlocked ? 16.0 : 8.0, 10.0);
        shackle.lineTo(unlocked ? 16.0 : 8.0, 7.0);
        shackle.cubicTo(unlocked ? 16.0 : 8.0, 2.5, unlocked ? 8.0 : 16.0, 2.5,
                        unlocked ? 8.0 : 16.0, 7.0);
        painter.drawPath(shackle);
        painter.setPen(QPen(accentColor, 2.2, Qt::SolidLine, Qt::RoundCap));
        painter.drawPoint(QPointF(12.0, 15.5));
        break;
    }
    case nexpdf::icons::Kind::InsertPage:
    case nexpdf::icons::Kind::DeletePage:
    case nexpdf::icons::Kind::PageUp:
    case nexpdf::icons::Kind::PageDown:
        drawPage(painter, QRectF(3.5, 2.5, 13.0, 19.0));
        configurePen(painter, accentColor, 1.9);
        if (kind == nexpdf::icons::Kind::InsertPage) {
            painter.drawLine(QPointF(14.0, 15.5), QPointF(21.0, 15.5));
            painter.drawLine(QPointF(17.5, 12.0), QPointF(17.5, 19.0));
        } else if (kind == nexpdf::icons::Kind::DeletePage) {
            painter.drawLine(QPointF(14.0, 13.0), QPointF(20.0, 19.0));
            painter.drawLine(QPointF(20.0, 13.0), QPointF(14.0, 19.0));
        } else {
            drawVerticalArrow(painter, 19.0, kind == nexpdf::icons::Kind::PageUp ? 19.0 : 11.0,
                              kind == nexpdf::icons::Kind::PageUp ? 11.0 : 19.0);
        }
        break;
    case nexpdf::icons::Kind::ImportPages:
        drawPage(painter, QRectF(2.5, 5.0, 11.0, 16.0));
        painter.drawLine(QPointF(7.0, 2.5), QPointF(18.0, 2.5));
        painter.drawLine(QPointF(18.0, 2.5), QPointF(21.0, 5.5));
        configurePen(painter, accentColor, 1.9);
        drawArrow(painter, QPointF(12.0, 13.0), QPointF(21.0, 13.0));
        break;
    case nexpdf::icons::Kind::RotateLeft:
    case nexpdf::icons::Kind::RotateRight: {
        const bool right = kind == nexpdf::icons::Kind::RotateRight;
        QPainterPath arc;
        arc.moveTo(right ? 18.0 : 6.0, 8.0);
        arc.cubicTo(right ? 15.0 : 9.0, 3.0, right ? 6.0 : 18.0, 4.0,
                    right ? 5.0 : 19.0, 12.0);
        arc.cubicTo(right ? 4.0 : 20.0, 18.0, right ? 10.0 : 14.0, 21.0,
                    right ? 14.0 : 10.0, 19.0);
        painter.drawPath(arc);
        const qreal x = right ? 18.0 : 6.0;
        painter.drawLine(QPointF(x, 8.0), QPointF(x + (right ? -4.0 : 4.0), 7.0));
        painter.drawLine(QPointF(x, 8.0), QPointF(x + (right ? -1.0 : 1.0), 12.0));
        break;
    }
    case nexpdf::icons::Kind::AddText:
        drawPage(painter, QRectF(3.0, 2.5, 14.0, 19.0));
        configurePen(painter, accentColor, 2.0);
        painter.drawLine(QPointF(7.0, 9.0), QPointF(14.0, 9.0));
        painter.drawLine(QPointF(10.5, 9.0), QPointF(10.5, 17.0));
        painter.drawLine(QPointF(17.0, 17.5), QPointF(22.0, 17.5));
        painter.drawLine(QPointF(19.5, 15.0), QPointF(19.5, 20.0));
        break;
    case nexpdf::icons::Kind::AddImage:
    case nexpdf::icons::Kind::ImageWatermark: {
        painter.drawRoundedRect(QRectF(3.0, 4.0, 18.0, 16.0), 1.5, 1.5);
        painter.drawEllipse(QPointF(16.5, 8.0), 1.5, 1.5);
        QPainterPath mountains;
        mountains.moveTo(5.0, 17.5);
        mountains.lineTo(9.0, 12.5);
        mountains.lineTo(12.0, 15.5);
        mountains.lineTo(15.0, 11.5);
        mountains.lineTo(20.0, 17.5);
        painter.drawPath(mountains);
        if (kind == nexpdf::icons::Kind::ImageWatermark) {
            configurePen(painter, accentColor, 2.0);
            painter.drawLine(QPointF(4.5, 19.5), QPointF(19.5, 4.5));
        }
        break;
    }
    case nexpdf::icons::Kind::Highlight:
        painter.drawLine(QPointF(4.0, 7.0), QPointF(20.0, 7.0));
        painter.drawLine(QPointF(4.0, 12.0), QPointF(20.0, 12.0));
        painter.drawLine(QPointF(4.0, 17.0), QPointF(20.0, 17.0));
        painter.fillRect(QRectF(3.0, 9.8, 18.0, 4.4), QColor(255, 214, 75, 150));
        break;
    case nexpdf::icons::Kind::Underline:
    case nexpdf::icons::Kind::StrikeOut:
        painter.drawLine(QPointF(7.0, 5.0), QPointF(7.0, 14.0));
        painter.drawLine(QPointF(17.0, 5.0), QPointF(17.0, 14.0));
        painter.drawLine(QPointF(7.0, 5.0), QPointF(17.0, 5.0));
        configurePen(painter, accentColor, 2.0);
        painter.drawLine(kind == nexpdf::icons::Kind::Underline
                             ? QPointF(5.0, 18.5) : QPointF(4.0, 10.0),
                         kind == nexpdf::icons::Kind::Underline
                             ? QPointF(19.0, 18.5) : QPointF(20.0, 10.0));
        break;
    case nexpdf::icons::Kind::Rectangle:
        painter.drawRoundedRect(QRectF(3.5, 5.0, 17.0, 14.0), 1.0, 1.0);
        break;
    case nexpdf::icons::Kind::Ellipse:
        painter.drawEllipse(QRectF(3.5, 5.0, 17.0, 14.0));
        break;
    case nexpdf::icons::Kind::Ink: {
        QPainterPath stroke;
        stroke.moveTo(3.0, 18.0);
        stroke.cubicTo(7.0, 5.0, 10.0, 20.0, 14.0, 9.0);
        stroke.cubicTo(16.0, 4.0, 18.0, 9.0, 21.0, 5.0);
        painter.drawPath(stroke);
        configurePen(painter, accentColor, 2.1);
        painter.drawLine(QPointF(3.0, 21.0), QPointF(13.0, 21.0));
        break;
    }
    case nexpdf::icons::Kind::Move:
        painter.drawLine(QPointF(12.0, 3.0), QPointF(12.0, 21.0));
        painter.drawLine(QPointF(3.0, 12.0), QPointF(21.0, 12.0));
        painter.drawPolyline(QPolygonF{QPointF(9.5, 6.0), QPointF(12.0, 3.0), QPointF(14.5, 6.0)});
        painter.drawPolyline(QPolygonF{QPointF(9.5, 18.0), QPointF(12.0, 21.0), QPointF(14.5, 18.0)});
        painter.drawPolyline(QPolygonF{QPointF(6.0, 9.5), QPointF(3.0, 12.0), QPointF(6.0, 14.5)});
        painter.drawPolyline(QPolygonF{QPointF(18.0, 9.5), QPointF(21.0, 12.0), QPointF(18.0, 14.5)});
        break;
    case nexpdf::icons::Kind::Resize:
        painter.drawRoundedRect(QRectF(5.0, 5.0, 14.0, 14.0), 1.0, 1.0);
        configurePen(painter, accentColor, 1.9);
        drawArrow(painter, QPointF(10.0, 10.0), QPointF(4.0, 4.0));
        drawArrow(painter, QPointF(14.0, 14.0), QPointF(20.0, 20.0));
        break;
    case nexpdf::icons::Kind::DeleteObject:
        painter.drawRoundedRect(QRectF(4.0, 4.0, 16.0, 16.0), 1.5, 1.5);
        configurePen(painter, accentColor, 2.1);
        painter.drawLine(QPointF(8.0, 8.0), QPointF(16.0, 16.0));
        painter.drawLine(QPointF(16.0, 8.0), QPointF(8.0, 16.0));
        break;
    case nexpdf::icons::Kind::RedactionPreview:
        painter.drawLine(QPointF(4.0, 6.0), QPointF(20.0, 6.0));
        painter.drawLine(QPointF(4.0, 18.0), QPointF(20.0, 18.0));
        painter.setPen(QPen(accentColor, 1.8, Qt::DashLine, Qt::RoundCap));
        painter.drawRect(QRectF(3.0, 9.0, 18.0, 6.0));
        break;
    case nexpdf::icons::Kind::ApplyRedactions:
        painter.drawLine(QPointF(4.0, 6.0), QPointF(20.0, 6.0));
        painter.drawLine(QPointF(4.0, 18.0), QPointF(20.0, 18.0));
        painter.fillRect(QRectF(3.0, 9.0, 18.0, 6.0), inkColor);
        break;
    case nexpdf::icons::Kind::Search:
        drawMagnifier(painter, QPointF(10.0, 10.0), 6.5);
        break;
    case nexpdf::icons::Kind::TextWatermark:
        drawPage(painter, QRectF(3.0, 2.5, 18.0, 19.0));
        configurePen(painter, accentColor, 2.0);
        painter.drawLine(QPointF(5.0, 18.5), QPointF(19.0, 5.5));
        configurePen(painter, inkColor, 1.8);
        painter.drawLine(QPointF(7.0, 8.0), QPointF(15.0, 8.0));
        painter.drawLine(QPointF(11.0, 8.0), QPointF(11.0, 15.0));
        break;
    case nexpdf::icons::Kind::ScanWatermark:
        drawPage(painter, QRectF(3.0, 2.5, 13.0, 18.0));
        configurePen(painter, accentColor, 1.9);
        drawMagnifier(painter, QPointF(16.0, 15.5), 4.0);
        break;
    case nexpdf::icons::Kind::RemoveWatermark:
        drawPage(painter, QRectF(3.0, 2.5, 18.0, 19.0));
        configurePen(painter, accentColor, 2.1);
        painter.drawLine(QPointF(5.0, 19.0), QPointF(19.0, 5.0));
        painter.drawLine(QPointF(5.0, 5.0), QPointF(19.0, 19.0));
        break;
    }

    return pixmap;
}

} // namespace

namespace nexpdf::icons {

QIcon actionIcon(const Kind kind)
{
    QIcon icon;
    icon.addPixmap(renderIcon(kind, 24));
    icon.addPixmap(renderIcon(kind, 48));
    return icon;
}

QIcon applicationIcon()
{
    static const bool initialized = [] {
        initializeNexPdfResources();
        return true;
    }();
    Q_UNUSED(initialized);
    return QIcon(QStringLiteral(":/nexpdf/nexpdf-256.png"));
}

} // namespace nexpdf::icons
