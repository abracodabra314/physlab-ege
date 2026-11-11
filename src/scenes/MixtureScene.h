#pragma once

#include "scenes/LabScene.h"

namespace lab::scenes {

/**
 * Сцена модели «Смешивание растворов»: две ёмкости с растворами разной
 * концентрации, насосные линии, приёмный смеситель, шкала концентрации.
 */
class MixtureScene : public LabScene {
    Q_OBJECT
public:
    explicit MixtureScene(Simulation* model, QObject* parent = nullptr);

    void reconfigure(const QRectF& area) override;
    void positionHandles() override;

protected:
    void paintScheme(QPainter* painter) override;
    double mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const override;
    double handleScenePosition(const HandleInfo& info) const override;

private:
    /** Цвет жидкости по концентрации (от почти белого к насыщенному). */
    static QColor liquidColor(double concentrationPercent);
    QRectF vesselRect(int index) const;   // 0, 1 — растворы, 2 — смеситель
    /** Высота зоны, которую занимает максимальный диапазон объёмов. */
    double zoneHeight() const;
    /** Литров на один пиксель: единый масштаб для всех трёх ёмкостей. */
    double litersPerPixel() const;
};

}  // namespace lab::scenes
