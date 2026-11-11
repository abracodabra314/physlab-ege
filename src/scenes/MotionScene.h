#pragma once

#include "scenes/LabScene.h"

namespace lab::scenes {

/**
 * Сцена модели «Два движущихся автомобиля»: дорога с разметкой и обочинами,
 * координатная ось (начало слева, «+» вправо), два автомобиля в боковой
 * проекции, размерная линия расстояния, векторы скоростей, точка встречи
 * и график x(t).
 */
class MotionScene : public LabScene {
    Q_OBJECT
public:
    explicit MotionScene(Simulation* model, QObject* parent = nullptr);

    void reconfigure(const QRectF& area) override;
    void positionHandles() override;

protected:
    void paintScheme(QPainter* painter) override;

private:
    /** Схематичный автомобиль: вид сбоку, направление задаётся знаком dir. */
    void drawCar(QPainter* painter, const QPointF& center, double dir, const QColor& color,
                 const QString& label, bool moving, bool waiting);

    double roadY() const;          // экранная координата оси дороги
    double roadHalfWidth() const;  // половина толщины полотна
    double laneOffset() const;     // смещение полос (машины не наезжают друг на друга)
    double carScale() const;       // масштаб объектов относительно размера сцены
    double axisPlotTop() const;    // верх границы графика (под осью координат)
};

}  // namespace lab::scenes
