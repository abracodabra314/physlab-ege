#pragma once

#include "scenes/LabScene.h"

namespace lab::scenes {

/**
 * Сцена модели «Резервуар»: вертикальный бак с водой, краны наполнения
 * слева, трубы слива справа, размерные линии уровня и высоты, график V(t).
 */
class TankScene : public LabScene {
    Q_OBJECT
public:
    explicit TankScene(Simulation* model, QObject* parent = nullptr);

    void reconfigure(const QRectF& area) override;
    void positionHandles() override;

protected:
    void paintScheme(QPainter* painter) override;
    double mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const override;
    double handleScenePosition(const HandleInfo& info) const override;

private:
    /** Пикселей на 1 метр: фиксированный масштаб, не зависящий от параметров. */
    double scalePx() const;
    /** Прямоугольник бака на сцены (зависит от текущих размеров бака). */
    QRectF tankRect() const;
    /** Зона, в которую вписывается максимальный бак (5 м × 5 м). */
    QRectF zoneRect() const;
};

}  // namespace lab::scenes
