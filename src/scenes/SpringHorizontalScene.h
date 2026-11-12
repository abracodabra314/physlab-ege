#pragma once

#include "core/Simulation.h"
#include "scenes/LabScene.h"
#include "scenes/VectorArrows.h"

namespace lab::models {
class SpringHorizontalModel;
}

namespace lab::scenes {

/**
 * Сцена 7A. Груз на горизонтальной пружине, закреплённой на стене.
 *
 * Отрисовка берёт только данные модели: координату груза — из stateAt(t),
 * длины и направления стрелок — из сил, посчитанных в уравнении движения,
 * графики — из узлов численного интегрирования. Физических вычислений
 * в методах рисования нет.
 *
 * Верхняя часть — схема (стена со штриховкой, пружина, груз на поверхности,
 * линейка смещений относительно положения равновесия), нижняя — три
 * синхронизированных графика x(t), v(t), a(t).
 */
class SpringHorizontalScene : public LabScene {
    Q_OBJECT
public:
    explicit SpringHorizontalScene(Simulation* model, QObject* parent = nullptr);

    void reconfigure(const QRectF& area) override;

    /**
     * Векторы, которые сцена рисует в момент t: направления, точки приложения
     * и длины берутся из расчёта модели. Список отдан наружу, чтобы геометрическую
     * корректность стрелок можно было проверить без растрирования; отрисовка
     * использует ровно эти данные.
     */
    QVector<vectors::Arrow> vectorsFor(double t) const;

protected:
    void paintScheme(QPainter* painter) override;
    void positionHandles() override;
    double mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const override;
    double handleScenePosition(const HandleInfo& info) const override;

private:
    /** Пиксельная позиция смещения x (в метрах) относительно равновесия. */
    double sceneXOf(double meters) const;
    /** Смещение (м) по пиксельной позиции сцены. */
    double metersOfSceneX(qreal sceneX) const;
    /** Прямоугольник груза при смещении x. */
    QRectF blockRectOf(double meters) const;

    /** Рисует список векторов, полученный из vectorsFor(). */
    void drawForces(QPainter* painter, const QVector<vectors::Arrow>& arrows) const;
    void drawPlots(QPainter* painter) const;

    // Геометрия, пересчитываемая в reconfigure().
    QRectF m_schemeRect;
    QRectF m_wallRect;
    double m_groundY = 0.0;
    double m_eqSceneX = 0.0;
    double m_pxPerMeter = 200.0;
    double m_forceScale = 6.0;
    double m_velocityScale = 30.0;
};

}  // namespace lab::scenes
