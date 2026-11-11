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
    QRectF shaftZone() const;    // фиксированная зона шахты (постоянный масштаб)
    double pxPerMeter() const;   // пикселей на метр глубины
    double depthToY(double depthValue) const;
    double yToDepth(qreal y) const;
    /** Наблюдатель у края колодца (фиксирован во время эксперимента). */
    static void drawObserver(QPainter* painter, const QPointF& groundPos);
};

}  // namespace lab::scenes
