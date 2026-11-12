#pragma once

#include "core/Simulation.h"
#include "scenes/LabScene.h"
#include "scenes/VectorArrows.h"

namespace lab::models {
class ProjectileModel;
}

namespace lab::scenes {

/**
 * Сцена 8. Полёт снаряда по параболе.
 *
 * Схема строится по данным модели: трасса — по формуле y(x), положение
 * снаряда — по x(t) и y(t), стрелки — по проекциям скорости и силе тяжести.
 * На схеме показываются проекции движущейся точки на обе оси координат
 * (это отдельная задача ТЗ): от снаряда к осям идут пунктирные линии
 * с маркерами, а по осям бегут точки x(t) и y(t).
 *
 * Под схемой — четыре графика: x(t), y(t), vₓ(t), vᵧ(t).
 */
class ProjectileScene : public LabScene {
    Q_OBJECT
public:
    explicit ProjectileScene(Simulation* model, QObject* parent = nullptr);

    void reconfigure(const QRectF& area) override;

    /** Векторы момента t: v, его проекции, a и mg — только то, что есть в модели. */
    QVector<vectors::Arrow> vectorsFor(double t) const;

protected:
    void paintScheme(QPainter* painter) override;
    void positionHandles() override;
    double mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const override;
    double handleScenePosition(const HandleInfo& info) const override;

private:
    /** Позиция точки сцены по координатам модели (x вправо, y вверх). */
    QPointF scenePointOf(double x, double y) const;
    /** Точка вылета и точка падения в координатах сцены. */
    QPointF muzzleScenePos() const;

    void drawProjections(QPainter* painter, const QPointF& shell) const;
    void drawTrajectory(QPainter* painter) const;
    void drawAngleArc(QPainter* painter) const;
    void drawForces(QPainter* painter, const QVector<vectors::Arrow>& arrows) const;
    void drawPlots(QPainter* painter) const;

    /** Радиус луча направления выстрела (для ручки угла и дуги угла). */
    double aimRadius() const;
    /** «Красивый» шаг шкалы: 1·10ⁿ, 2·10ⁿ или 5·10ⁿ. */
    static double niceStep(double rough);

    // Геометрия, пересчитываемая в reconfigure().
    QRectF m_schemeRect;
    /** Зона «мира»: внутри схемы область, куда спроектированы координаты. */
    QRectF m_worldRect;
    double m_worldXMin = 0.0;
    double m_worldXMax = 100.0;
    double m_worldYMin = 0.0;
    double m_worldYMax = 40.0;
    double m_groundY = 0.0;
    double m_forceScale = 4.0;
    double m_velocityScale = 4.0;
};

}  // namespace lab::scenes
