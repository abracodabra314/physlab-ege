#pragma once

#include "scenes/LabScene.h"

namespace lab::scenes {

/**
 * Сцена модели «Два движущихся автомобиля»: дорога с разметкой,
 * координатная шкала, два автомобиля, размерная линия расстояния,
 * векторы скоростей и график s(t).
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
    static void drawCar(QPainter* painter, const QPointF& center, double dir, const QColor& color,
                        const QString& label, bool moving, bool waiting);

    double roadY() const;   // экранная координата оси дороги
};

}  // namespace lab::scenes
