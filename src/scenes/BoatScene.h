#pragma once

#include "scenes/LabScene.h"

namespace lab::scenes {

/**
 * Сцена модели «Теплоходы»: река с берегами и направлением течения,
 * две пристани, два теплохода в боковой проекции, векторы скоростей
 * (собственная, течение, относительно берега), размерные линии и
 * подсветка причалов в момент прибытия.
 */
class BoatScene : public LabScene {
    Q_OBJECT
public:
    explicit BoatScene(Simulation* model, QObject* parent = nullptr);

    void reconfigure(const QRectF& area) override;
    void positionHandles() override;

protected:
    void paintScheme(QPainter* painter) override;

private:
    double waterY() const;      // экранная координата кромки воды
    double riverDepth() const;  // глубина русла в пикселях
    double laneOffset() const;  // смещение судовых дорог (теплоходы не пересекаются)
    double boatScale() const;   // масштаб объектов относительно размера сцены
    QRectF riverRect() const;   // прямоугольник русла
};

}  // namespace lab::scenes
