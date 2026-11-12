#pragma once

#include "core/Simulation.h"
#include "scenes/LabScene.h"
#include "scenes/VectorArrows.h"

namespace lab::models {
class SpringVerticalModel;
}

namespace lab::scenes {

/**
 * Сцена 7B. Груз на вертикальной пружине, закреплённой сверху.
 *
 * Все координаты берутся из модели: длина пружины — из растяжения Δl(t),
 * положение груза — из смещения x(t), стрелки — из сил mg и k·Δl.
 * Справа — вертикальная шкала положения, под схемой — графики x(t), v(t), a(t).
 */
class SpringVerticalScene : public LabScene {
    Q_OBJECT
public:
    explicit SpringVerticalScene(Simulation* model, QObject* parent = nullptr);

    void reconfigure(const QRectF& area) override;

    /** Векторы, рисуемые в момент t: только mg и Fупр — трения в модели нет. */
    QVector<vectors::Arrow> vectorsFor(double t) const;

protected:
    void paintScheme(QPainter* painter) override;
    void positionHandles() override;
    double mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const override;
    double handleScenePosition(const HandleInfo& info) const override;

private:
    /** Пиксельная позиция смещения x (в метрах) относительно равновесия. */
    double sceneYOfElongation(double elongation) const;
    double elongationOfSceneY(qreal sceneY) const;
    /** Сторона груза в пикселях. */
    double blockSize() const;
    QRectF blockRectOfElongation(double elongation) const;

    void drawForces(QPainter* painter, const QVector<vectors::Arrow>& arrows) const;
    void drawPlots(QPainter* painter) const;

    QRectF m_schemeRect;
    QRectF m_supportRect;
    double m_anchorY = 0.0;        // точка крепления пружины
    double m_anchorX = 0.0;
    double m_pxPerMeter = 300.0;
    double m_forceScale = 40.0;
    double m_velocityScale = 30.0;
    double m_elongationMin = 0.0;
    double m_elongationMax = 0.4;
};

}  // namespace lab::scenes
