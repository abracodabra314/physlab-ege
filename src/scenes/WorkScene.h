#pragma once

#include "scenes/LabScene.h"

namespace lab::scenes {

/**
 * Сцена модели «Совместная работа механизмов»: производственная площадка
 * со станками (шестерни, вход/выход, индикация состояния), общая лента
 * конвейера, шкала готовности работы и продукция, пропорциональная
 * выполненной доле.
 */
class WorkScene : public LabScene {
    Q_OBJECT
public:
    explicit WorkScene(Simulation* model, QObject* parent = nullptr);

    void reconfigure(const QRectF& area) override;
    void positionHandles() override;

protected:
    void paintScheme(QPainter* painter) override;
    double mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const override;
    double handleScenePosition(const HandleInfo& info) const override;

private:
    QRectF machineZone() const;
    QRectF conveyorRect() const;   // общая лента конвейера под станками
    double machineX(int index) const;
    /** Корпус станка: станина, стойка, шестерни, вход/выход. */
    static void drawMachineBody(QPainter* painter, double x, double baseY, const QColor& color, bool running,
                                double spin);
};

}  // namespace lab::scenes
