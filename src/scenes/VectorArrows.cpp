#include "scenes/VectorArrows.h"

#include <QPainter>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

#include "scenes/LabScene.h"
#include "scenes/Style.h"

namespace lab::vectors {

QColor colorOf(Kind kind) {
    switch (kind) {
        case Kind::Velocity:     return style::accent();          // синий
        case Kind::Acceleration: return style::accentWarm();      // оранжевый
        case Kind::Gravity:      return style::danger();          // красный
        case Kind::Normal:       return style::success();         // зелёный
        case Kind::Friction:     return QColor(142, 84, 176);     // фиолетовый
        case Kind::Tension:      return QColor(20, 130, 140);     // бирюзовый
        case Kind::Elastic:      return QColor(20, 130, 140);     // бирюзовый (тяга/упругость)
        case Kind::Flow:         return style::waterDeep();       // сине-стальной
        case Kind::Current:      return QColor(214, 122, 34);     // оранжевый (ток)
    }
    return style::ink();
}

double autoScale(double magMax, double maxPx, double minPx) {
    if (magMax <= 0.0)
        return 1.0;
    // Самый крупный вектор рисуется длиной ~maxPx, но не «крошечным».
    return std::clamp(maxPx / magMax, minPx / magMax, 1e6);
}

double visualLength(double magnitude, double pxPerUnit, double minPx, double maxPx) {
    const double value = std::abs(magnitude);
    if (value <= 1e-12)
        return 0.0;
    return std::clamp(value * pxPerUnit, minPx, maxPx);
}

void drawArrow(QPainter* painter, const Arrow& arrow) {
    QPointF dir = arrow.to - arrow.from;
    const double length = std::hypot(dir.x(), dir.y());
    if (length < 1e-6)
        return;
    dir /= length;

    const QColor color = arrow.color.isValid() ? arrow.color : colorOf(arrow.kind);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const double headLen = std::min(11.0, length * 0.45);
    const QPointF base = arrow.to - dir * headLen;
    const QPointF normal(-dir.y(), dir.x());
    const double halfWidth = std::min(5.5, arrow.width * 2.1);
    QPolygonF head;
    head << arrow.to << base + normal * halfWidth << base - normal * halfWidth;

    if (arrow.halo) {
        // Светлая обводка: стрелка остаётся видимой на тёмной заливке тела.
        painter->setPen(QPen(QColor(255, 255, 255, 235), arrow.width + 3.2,
                             arrow.dashed ? Qt::DashLine : Qt::SolidLine, Qt::FlatCap));
        painter->drawLine(arrow.from, base);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(255, 255, 255, 235));
        painter->drawPolygon(head);
    }

    QPen pen(color, arrow.width, arrow.dashed ? Qt::DashLine : Qt::SolidLine, Qt::FlatCap);
    painter->setPen(pen);
    // Хвост до наконечника (сам наконечник дорисовывается отдельно).
    painter->drawLine(arrow.from, base);

    // Наконечник — закрашенный треугольник.
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawPolygon(head);

    painter->restore();

    // Обозначение величины: только символ, без чисел.
    if (arrow.symbol.isEmpty())
        return;
    const QPointF labelPos = arrow.labelAtTail ? arrow.from + arrow.labelOffset
                                               : arrow.to + arrow.labelOffset;
    LabScene::drawTag(painter, labelPos, arrow.symbol, QColor(255, 255, 255, 225), color.darker(125));
}

}  // namespace lab::vectors
