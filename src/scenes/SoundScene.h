#pragma once

#include "core/Simulation.h"
#include "scenes/LabScene.h"
#include "scenes/SchemeLabels.h"
#include "scenes/VectorArrows.h"

namespace lab::models {
class SoundModel;
}

namespace lab::scenes {

/**
 * Сцена 10. Гудок судна: распространение звука и приём сигналов.
 *
 * Схема строится вокруг главного факта задачи: каждый звуковой фронт
 * расходится из ТОЙ точки, где сигнал был подан. Центр окружности поэтому
 * неподвижен (он задан положением судна в момент излучения xₖ = v₁tₖ), а
 * растёт только радиус rₖ = c(t − tₖ). Если переставить наблюдателя или
 * изменить его скорость, окружности останутся на своих местах: двигается
 * только тот, кто слушает.
 *
 * Из-за этого приём сигнала — геометрическое условие «правая граница
 * окружности совпала с наблюдателем», а не отдельная формула: отметки
 * приёма рисуются там, где фронт пересёк судно наблюдателя.
 *
 * Под схемой — три графика от времени (расстояние между судами, радиусы
 * фронтов, пути судов) и не временный график T′(v), построенный по рядам
 * модели: он показывает, при каких направлениях движения приёмы следуют
 * чаще, а при каких — реже.
 */
class SoundScene : public LabScene {
    Q_OBJECT
public:
    /**
     * Один звуковой фронт в пикселях сцены. Нужен не только отрисовке:
     * по нему проверяют, что центр окружности привязан к точке излучения
     * и не смещается при изменении скорости наблюдателя.
     */
    struct FrontItem {
        int index = 0;              // 0 — первый сигнал
        bool emitted = false;       // фронт уже излучён
        bool received = false;      // фронт уже принят наблюдателем
        QPointF center;             // центр окружности (точка излучения), пиксели
        double centerMeters = 0.0;  // то же положение в метрах модели (xₖ = v₁tₖ)
        double radiusMeters = 0.0;  // радиус фронта, м
        double radiusPx = 0.0;      // тот же радиус в пикселях
        double arrivalTime = 0.0;   // момент приёма, с (NaN — не будет принят)
        /**
         * Точка, где фронт догонит наблюдателя: она показана и до приёма
         * (это предсказание формулы), и после (состоявшийся факт).
         * hasReceptionMark — false, если приём вообще не наступит.
         */
        bool hasReceptionMark = false;
        QPointF receptionMark;
    };

    /** Подписи схемы — общий для лаборатории механизм разведения надписей. */
    using Annotation = labels::Item;

    explicit SoundScene(Simulation* model, QObject* parent = nullptr);

    void reconfigure(const QRectF& area) override;

    /** Пикселей на один метр оси X: по нему проверяются радиусы фронтов. */
    double pixelsPerMeter() const;

    /** Положение судна с гудком на линии воды в момент t. */
    QPointF sourcePointAt(double t) const;
    /** Положение судна наблюдателя на линии воды в момент t. */
    QPointF observerPointAt(double t) const;

    /** Фронты звуковой волны в момент t (по одному на каждый сигнал). */
    QVector<FrontItem> frontsFor(double t) const;

    /**
     * Векторы момента t. Сил и ускорений в модели нет: суда движутся
     * равномерно, а звук распространяется по заданному закону, поэтому из
     * векторов — только скорости обоих судов.
     */
    QVector<vectors::Arrow> vectorsFor(double t) const;

    /** Подписи схемы с их расположением (после reconfigure()). */
    QVector<Annotation> annotations() const;

    /** Полоса схемы: река, фронты, подписи — всё рисуется внутри неё. */
    QRectF schemeBand() const;

protected:
    void paintScheme(QPainter* painter) override;
    void positionHandles() override;
    double mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const override;
    double handleScenePosition(const HandleInfo& info) const override;

private:
    /** Горизонтальная линейка температуры воздуха и её преобразования. */
    QRectF temperatureBarRect() const;
    double temperatureBarX(double celsius) const;
    double temperatureForBarX(double sceneX) const;

    /** Линия воды и положение судов по линиям движения. */
    double waterY() const;
    double laneOffset() const;
    double boatScale() const;
    QRectF riverRect() const;

    void drawRiver(QPainter* painter) const;
    void drawFronts(QPainter* painter, const QVector<FrontItem>& fronts) const;
    void drawReceptionMarks(QPainter* painter, const QVector<FrontItem>& fronts) const;
    void drawShips(QPainter* painter) const;
    void drawGapRuler(QPainter* painter) const;
    void drawTemperatureBar(QPainter* painter) const;
    void drawVectors(QPainter* painter, const QVector<vectors::Arrow>& arrows) const;
    void drawPlots(QPainter* painter) const;

    // Геометрия, пересчитываемая в reconfigure().
    QRectF m_band;                  // полоса схемы
    double m_waterY = 0.0;          // линия воды — она же ось X
    double m_laneOffset = 16.0;     // смещение линий движения судов
    double m_boatScale = 1.0;       // масштаб изображений судов
    /** Пикселей на метр оси X; одинаков по вертикали, чтобы фронт был окружностью. */
    double m_pixelsPerMeter = 0.05;
    /** Пиксель, соответствующий центру «интересной» части оси модели. */
    double m_axisCenterX = 0.0;
    /** Центр модельной оси X: пути обоих судов расположены симметрично. */
    double m_modelCenterX = 0.0;
};

}  // namespace lab::scenes
