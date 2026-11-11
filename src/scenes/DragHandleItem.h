#pragma once

#include <QGraphicsObject>

#include <functional>

#include "core/Simulation.h"

namespace lab {

/**
 * Перетаскиваемая точка сцены, привязанная к параметру модели.
 *
 * Ручка хранит идентификатор параметра и диапазон допустимых значений
 * в ЕДИНИЦАХ МОДЕЛИ. Функции пересчёта «пиксели <-> единицы модели»
 * предоставляет сцена. При перетаскивании ручка вычисляет новое значение
 * модели и уведомляет сцену; сцена применяет его через
 * Simulation::setHandleValue, после чего обновляется вся математика —
 * перемещение объекта меняет состояние системы, а не только картинку.
 */
class DragHandleItem : public QGraphicsObject {
    Q_OBJECT
public:
    /** Перевод позиции в пикселях в значение модели и обратно. */
    using ValueMapper = std::function<double(const QPointF& scenePos)>;

    DragHandleItem(const HandleInfo& info, QGraphicsItem* parent = nullptr);

    QString id() const { return m_info.id; }
    const HandleInfo& info() const { return m_info; }
    void setInfo(const HandleInfo& info);

    /** Значение модели, которому соответствует текущая позиция. */
    double value() const { return m_value; }
    /** Программная установка значения (синхронизация с моделью). */
    void setValue(double value);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    /** Позиционирование: центр ручки в координатах сцены. */
    void setAnchor(const QPointF& scenePos);
    QPointF anchor() const { return m_anchor; }

    void setValueMapper(ValueMapper mapper) { m_mapper = std::move(mapper); }

    /** Ограничение движения по одной из осей. */
    enum class Axis { Horizontal, Vertical, Both };
    void setAxis(Axis axis) { m_axis = axis; }

signals:
    /** Значение изменилось в результате перетаскивания пользователем. */
    void valueDragged(const QString& id, double value);
    /** Начало/конец перетаскивания (для паузы анимации). */
    void dragStarted(const QString& id);
    void dragFinished(const QString& id);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    double clampAndQuantize(double value) const;

    HandleInfo m_info;
    double m_value = 0.0;
    QPointF m_anchor;
    Axis m_axis = Axis::Horizontal;
    bool m_dragging = false;
    bool m_hover = false;
    QPointF m_dragOffset;
    ValueMapper m_mapper;
};

}  // namespace lab
