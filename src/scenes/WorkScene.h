#pragma once

#include "scenes/LabScene.h"

namespace lab::scenes {

/**
 * Сцена модели «Совместная работа механизмов»: три станка с шестернями,
 * шкала выполненной работы, диаграмма вкладов каждого механизма.
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
    double machineX(int index) const;
};

}  // namespace lab::scenes
