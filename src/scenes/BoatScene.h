#pragma once

#include "scenes/LabScene.h"

namespace lab::scenes {

/**
 * Сцена модели «Теплоходы»: река с направлением течения, две пристани,
 * два теплохода, размерная линия расстояния L, отметки времени прибытия.
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
    double waterY() const;
};

}  // namespace lab::scenes
