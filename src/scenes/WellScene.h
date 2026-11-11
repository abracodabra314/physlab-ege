#pragma once

#include "scenes/LabScene.h"

namespace lab::scenes {

/**
 * Сцена модели «Колодец»: шахта с грунтом, падающий камень, уровень воды,
 * расходящиеся звуковые волны, размерная линия глубины, шкала времени T.
 * Глубина модели растёт вниз — перевод координат для ручек переопределён.
 */
class WellScene : public LabScene {
    Q_OBJECT
public:
    explicit WellScene(Simulation* model, QObject* parent = nullptr);

    void reconfigure(const QRectF& area) override;
    void positionHandles() override;

protected:
    void paintScheme(QPainter* painter) override;
    double mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const override;
    double handleScenePosition(const HandleInfo& info) const override;

private:
    QRectF shaftRect() const;
    double depthToY(double depthValue) const;
    double yToDepth(qreal y) const;
};

}  // namespace lab::scenes
