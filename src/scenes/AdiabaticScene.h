#pragma once

#include "core/Simulation.h"
#include "scenes/LabScene.h"
#include "scenes/SchemeLabels.h"
#include "scenes/VectorArrows.h"

namespace lab::models {
class AdiabaticModel;
}

namespace lab::scenes {

/**
 * Сцена 11. Адиабатное сжатие газа в цилиндре с поршнем.
 *
 * Схема построена так, чтобы было видно причинно-следственную связь задачи:
 * поршень опускается — объём уменьшается — из уравнения адиабаты растут
 * давление и температура — сила давления на поршень становится больше.
 * Положение поршня берётся из модели (h = V/S), поэтому цилиндр показан в
 * настоящем отношении высот, а не «на глаз».
 *
 * Молекулы внутри сосуда — не украшение: их направление задаётся моделью,
 * а скорость каждой равна среднеквадратичной v = √(3RT/M) с тем же
 * множителем, что и у стрелок. Именно поэтому при сжатии штрихи у молекул
 * удлиняются: газ разогревается, и средняя скорость растёт.
 *
 * Из векторов в модели есть только силы, действующие на поршень: сила
 * давления газа F = pS, сила внешнего давления p₀S и внешняя сила,
 * удерживающая поршень при медленном процессе. Скорость поршня —
 * кинематическая величина процесса, она показывается отдельной категорией.
 *
 * Под схемой — четыре панели: две не временные зависимости p(V) (адиабата
 * рядом с изотермой той же температуры) и T(V), и два графика от времени —
 * p(t), T(t) и работа A(t) с изменением внутренней энергии ΔU(t).
 */
class AdiabaticScene : public LabScene {
    Q_OBJECT
public:
    /**
     * Геометрия чертежа в пикселях. Нужна не только отрисовке: по ней
     * проверяют, что высота столба газа соответствует объёму из модели
     * (V = S·h) и что поршень не уходит за стенки сосуда.
     */
    struct SchemeGeometry {
        QRectF vessel;          // сосуд (внутренняя полость)
        QPointF piston;         // нижняя кромка поршня
        double gasHeightPx = 0.0;
        double strokeTopPx = 0.0;    // положение поршня при наибольшем объёме
        double strokeBottomPx = 0.0;  // и при наименьшем
        double pixelsPerMeter = 0.0;
    };

    /**
     * Одна молекула на схеме: точка и направление её скорости. Скорость
     * задана безразмерным единичным вектором и общей для всех молекул
     * величиной v_px — по ней видно, что все молекулы ускоряются одинаково.
     */
    struct MoleculeItem {
        int index = 0;
        QPointF position;       // центр молекулы, пиксели
        QPointF direction;      // единичный вектор направления движения
        double speedMeters = 0.0;   // та же скорость в м/с (из формулы модели)
        bool insideGas = false;     // молекула внутри объёма газа
    };

    /** Подписи схемы — общий для лаборатории механизм разведения надписей. */
    using Annotation = labels::Item;

    explicit AdiabaticScene(Simulation* model, QObject* parent = nullptr);

    void reconfigure(const QRectF& area) override;

    /** Текущая геометрия чертежа (после reconfigure()). */
    SchemeGeometry schemeGeometry() const;

    /** Молекулы в момент t: их скорость равна √(3RT(t)/M). */
    QVector<MoleculeItem> moleculesFor(double t) const;

    /**
     * Векторы момента t. Силы берутся из давления в данный момент:
     * F = pS, p₀S и (p − p₀)S. Ускорения нет — процесс квазистатический,
     * поршень движется равномерно и равнодействующая сил на него равна нулю.
     */
    QVector<vectors::Arrow> vectorsFor(double t) const;

    /** Подписи схемы с их расположением (после reconfigure()). */
    QVector<Annotation> annotations() const;

    /** Полоса схемы: сосуд, поршень и шкалы. */
    QRectF schemeBand() const;

protected:
    void paintScheme(QPainter* painter) override;
    void positionHandles() override;
    double mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const override;
    double handleScenePosition(const HandleInfo& info) const override;

private:
    /** Высоты, соответствующие объёмам постановки, и преобразования масштаба. */
    double heightForVolume(double volume) const;
    double volumeForHeight(double height) const;
    /** Пиксели, соответствующие высоте столба газа height. */
    double pixelsForHeight(double height) const;
    /** Обратное преобразование: пиксели от дна -> высота столба газа. */
    double heightForPixels(double liftPx) const;
    /** Левый край сосуда: по нему позиционируются ручки и шкалы. */
    double vesselLeftX() const;

    void drawVessel(QPainter* painter) const;
    void drawGas(QPainter* painter) const;
    void drawMolecules(QPainter* painter, const QVector<MoleculeItem>& molecules) const;
    void drawPiston(QPainter* painter) const;
    void drawStrokeScale(QPainter* painter) const;
    void drawPressureGauge(QPainter* painter) const;
    void drawVectors(QPainter* painter, const QVector<vectors::Arrow>& arrows) const;
    void drawPlots(QPainter* painter) const;

    // Геометрия, пересчитываемая в reconfigure().
    QRectF m_band;                 // полоса схемы
    QRectF m_vessel;               // сосуд: стенки и полость
    /** Пикселей на метр высоты столба газа (одинаково по обеим осям). */
    double m_pixelsPerMeter = 1.0;
    /** Высоты столба газа при V₁ и V₂. */
    double m_startHeight = 0.0;
    double m_endHeight = 0.0;
    /** Наибольшая из показываемых высот — по ней подбирается масштаб. */
    double m_maxHeight = 1.0;
    /** Позиция левого края сосуда и ширина полости. */
    double m_borePx = 150.0;
    /** Пиксель дна сосуда. */
    double m_bottomY = 0.0;
    /** Опорный масштаб стрелок сил (пикселей на ньютон). */
    double m_forceScale = 0.01;
    /** Опорный масштаб стрелки скорости поршня. */
    double m_speedScale = 60.0;
};

}  // namespace lab::scenes
