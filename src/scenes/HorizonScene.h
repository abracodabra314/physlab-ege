#pragma once

#include "core/Simulation.h"
#include "scenes/LabScene.h"
#include "scenes/SchemeLabels.h"
#include "scenes/VectorArrows.h"

namespace lab::models {
class HorizonModel;
}

namespace lab::scenes {

/**
 * Сцена 9. Дальность видимого горизонта.
 *
 * В основе схемы — реальная геометрия задачи, а не условная картинка:
 * окружность планеты с центром O, наблюдатель в точке A на высоте h,
 * точка касания B и линия взгляда AB. Прямоугольный треугольник OAB
 * показан целиком: радиус OB, отрезок OA, касательная AB, прямой угол
 * в точке B и центральный угол α у центра. Дуга поверхности от точки C
 * (ближайшая к наблюдателю точка) до точки горизонта выделена отдельно:
 * это путь s = R·α вдоль поверхности.
 *
 * Центральный угол на чертеже увеличен: в действительности он меньше
 * градуса, и без увеличения треугольник OAB слился бы в линию. Угол
 * увеличивается во столько раз во всех точках подъёма, поэтому форма
 * зависимости сохраняется; о величине увеличения сказано на схеме, а
 * все числа берутся из формул модели, а не с линейки рисунка.
 *
 * Под схемой — два графика от времени (h(t), d(t) и s(t)) и два не
 * временных графика d(h) и s(h), построенных по рядам модели.
 */
class HorizonScene : public LabScene {
    Q_OBJECT
public:
    /**
     * Геометрия чертежа в пикселях сцены. Нужна не только отрисовке:
     * по ней можно проверить, что треугольник OAB построен верно
     * (точка горизонта на поверхности, наблюдатель на высоте, прямой
     * угол в точке касания).
     */
    struct SchemeGeometry {
        QPointF center;            // O — центр планеты
        double planetRadiusPx = 0.0;
        QPointF subsatellite;      // точка поверхности под наблюдателем
        QPointF observer;          // A — наблюдатель
        QPointF horizon;           // B — точка горизонта
        double drawnAlpha = 0.0;   // центральный угол на чертеже, рад
    };

    /**
     * Подписи чертежа: текст, его центр и габарит «пилюли». Все надписи
     * собираются в одном месте и разводятся без наложений, поэтому их
     * взаимное расположение можно проверить (подписи не должны набегать
     * друг на друга).
     */
    using Annotation = labels::Item;

    explicit HorizonScene(Simulation* model, QObject* parent = nullptr);

    void reconfigure(const QRectF& area) override;

    /** Текущая геометрия чертежа (после reconfigure()). */
    SchemeGeometry schemeGeometry() const;

    /** Подписи чертежа с их расположением (после reconfigure()). */
    QVector<Annotation> annotations() const;

    /**
     * Векторы момента t. Сил в модели нет (подъём наблюдателя задан
     * кинематически, его сила в модель не входит), ускорения тоже нет:
     * подъём равномерный. Поэтому из векторов — только скорость подъёма.
     */
    QVector<vectors::Arrow> vectorsFor(double t) const;

protected:
    void paintScheme(QPainter* painter) override;
    void positionHandles() override;
    double mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const override;
    double handleScenePosition(const HandleInfo& info) const override;

private:
    /** Точка сцены по направлению из центра планеты: угол θ от вертикали вверх. */
    QPointF planetPoint(double angleRadians, double radiusPx) const;
    /** Положение наблюдателя A, точки горизонта B и точки поверхности под наблюдателем. */
    QPointF observerScenePos() const;
    QPointF horizonScenePos() const;
    QPointF subsatelliteScenePos() const;

    /** Угол α (рад), отложенный на чертеже с увеличением angleExaggeration(). */
    double drawnCentralAngle() const;
    /** Пикселей над поверхностью для высоты height (с учётом увеличения угла). */
    double liftForHeight(double height) const;
    /** Обратное преобразование: пиксели над поверхностью -> высота модели. */
    double heightForLift(double liftPx) const;
    /** Множитель увеличения чертежного угла относительно настоящего. */
    double angleExaggeration() const;

    /** Прямоугольник логарифмической линейки радиусов и её преобразования. */
    QRectF radiusBarRect() const;
    double radiusBarX(double radiusKilometers) const;
    double radiusForBarX(double sceneX) const;

    void drawPlanet(QPainter* painter) const;
    void drawArcPath(QPainter* painter) const;
    void drawSightLines(QPainter* painter) const;
    void drawRightAngleMark(QPainter* painter, const QPointF& at) const;
    void drawHeightScale(QPainter* painter) const;
    void drawRadiusScale(QPainter* painter) const;
    void drawObserver(QPainter* painter) const;
    void drawVectors(QPainter* painter, const QVector<vectors::Arrow>& arrows) const;
    void drawPlots(QPainter* painter) const;

    // Геометрия, пересчитываемая в reconfigure().
    QRectF m_schemeRect;
    /** Зона схемы, в которую вписаны планета и шкалы. */
    QRectF m_worldRect;
    QPointF m_center;                 // центр планеты O в пикселях
    double m_planetRadiusPx = 120.0;  // радиус планеты в пикселях
    /** Отметка вертикальной шкалы высот (левее планеты). */
    double m_rulerX = 0.0;
    double m_velocityScale = 4.0;     // пикселей на м/с скорости подъёма
};

}  // namespace lab::scenes
