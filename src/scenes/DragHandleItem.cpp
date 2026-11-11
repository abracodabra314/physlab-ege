#include "scenes/DragHandleItem.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include <algorithm>
#include <cmath>

namespace lab {
namespace {
constexpr double kRadius = 9.0;
}

DragHandleItem::DragHandleItem(const HandleInfo& info, QGraphicsItem* parent)
    : QGraphicsObject(parent), m_info(info), m_value(info.value) {
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setAcceptHoverEvents(true);
    setZValue(100);
    setCursor(Qt::OpenHandCursor);
}

void DragHandleItem::setInfo(const HandleInfo& info) {
    m_info = info;
    setValue(info.value);
    update();
}

void DragHandleItem::setValue(double value) {
    m_value = clampAndQuantize(value);
    update();
}

double DragHandleItem::clampAndQuantize(double value) const {
    double result = std::clamp(value, m_info.minValue, m_info.maxValue);
    if (m_info.step > 0.0)
        result = std::round(result / m_info.step) * m_info.step;
    return std::clamp(result, m_info.minValue, m_info.maxValue);
}

void DragHandleItem::setAnchor(const QPointF& scenePos) {
    m_anchor = scenePos;
    prepareGeometryChange();
    setPos(scenePos);
}

QRectF DragHandleItem::boundingRect() const {
    return QRectF(-kRadius - 4, -kRadius - 4, (kRadius + 4) * 2, (kRadius + 4) * 2);
}

void DragHandleItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* /*option*/,
                           QWidget* /*widget*/) {
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRectF ring(-kRadius, -kRadius, kRadius * 2, kRadius * 2);

    if (m_dragging || m_hover) {
        painter->setPen(QPen(QColor(33, 118, 198), 3.0));
        painter->setBrush(QColor(33, 118, 198, 60));
    } else {
        painter->setPen(QPen(QColor(55, 71, 89), 2.0));
        painter->setBrush(QColor(255, 255, 255, 210));
    }
    painter->drawEllipse(ring);

    // Внутренняя точка — «рукоятка».
    painter->setBrush(m_dragging || m_hover ? QColor(33, 118, 198) : QColor(55, 71, 89));
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(QPointF(0, 0), 3.2, 3.2);

    // Стрелки, показывающие допустимое направление перемещения.
    painter->setPen(QPen(QColor(55, 71, 89), 1.4));
    const double a = kRadius + 3.0;
    auto arrow = [&](const QPointF& tip, const QPointF& dir) {
        painter->drawLine(tip - dir, tip);
        painter->drawLine(tip, tip + QPointF(-dir.y(), dir.x()) * 0.9 - dir * 0.35);
        painter->drawLine(tip, tip + QPointF(dir.y(), -dir.x()) * 0.9 - dir * 0.35);
    };
    if (m_axis != Axis::Vertical) {
        arrow(QPointF(a, 0), QPointF(3.0, 0));
        arrow(QPointF(-a, 0), QPointF(-3.0, 0));
    }
    if (m_axis != Axis::Horizontal) {
        arrow(QPointF(0, a), QPointF(0, 3.0));
        arrow(QPointF(0, -a), QPointF(0, -3.0));
    }
}

void DragHandleItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    m_dragging = true;
    setSelected(true);
    // Смещение центра ручки относительно точки захвата — чтобы ручка
    // не «прыгала» под курсор при начале перетаскивания.
    m_dragOffset = m_anchor - mapToScene(event->pos());
    emit dragStarted(m_info.id);
    update();
    event->accept();
}

void DragHandleItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (!m_dragging) {
        event->ignore();
        return;
    }
    QPointF target = mapToScene(event->pos()) + m_dragOffset;
    if (m_axis == Axis::Horizontal)
        target.setY(m_anchor.y());
    else if (m_axis == Axis::Vertical)
        target.setX(m_anchor.x());

    if (m_mapper) {
        const double newValue = clampAndQuantize(m_mapper(target));
        if (!qFuzzyCompare(1.0 + newValue, 1.0 + m_value)) {
            m_value = newValue;
            emit valueDragged(m_info.id, m_value);
        }
    }
    setAnchor(target);
    update();
    event->accept();
}

void DragHandleItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (!m_dragging) {
        event->ignore();
        return;
    }
    m_dragging = false;
    setSelected(false);
    emit dragFinished(m_info.id);
    update();
    event->accept();
}

void DragHandleItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
    m_hover = true;
    setCursor(m_axis == Axis::Vertical ? Qt::SizeVerCursor : Qt::SizeHorCursor);
    update();
    event->accept();
}

void DragHandleItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    m_hover = false;
    unsetCursor();
    update();
    event->accept();
}

}  // namespace lab
