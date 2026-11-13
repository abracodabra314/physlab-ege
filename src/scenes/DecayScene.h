#pragma once

#include <QPointF>
#include <QVector>

#include "core/Simulation.h"
#include "scenes/LabScene.h"
#include "scenes/SchemeLabels.h"
#include "scenes/VectorArrows.h"

namespace lab::models {
class DecayModel;
}

namespace lab::scenes {

/**
 * Сцена 12. Радиоактивный распад: опыт с большим числом ядер.
 *
 * Схема состоит из двух частей, и обе берут числа из модели.
 *
 * Слева — образец: каждая клетка это несколько ядер (число ядер в клетке
 * подбирается, чтобы клетки оставались различимы). Тёмная клетка — ядро ещё
 * цело, пустая — распалось. Клетки гаснут по одной, потому что счётчик
 * модели перебирает ядра и целое число их убывает: именно это и есть
 * «дискретный» ход опыта, которого нет у гладкой формулы.
 *
 * Справа — ход опыта во времени: ступенька счёта лежит рядом с кривой
 * N₀·2^(−t/T½), а вокруг кривой — коридор ±√N: ожидаемый разброс одного
 * опыта. Отметки T½, 2T½, 3T½ подписаны долями (1/2), (1/4), (1/8), поэтому
 * видно, что период полураспада — это не «почти всё распалось», а ровно
 * половина. Горизонтальная линия на уровне d·N₀ и её пересечение с кривой
 * показывают искомый момент t = ln(1/d)/λ. Под шкалой — лента счётчика:
 * число распадов на интервалах наблюдения, и размерная линия T½.
 *
 * Векторов в модели нет: распад не движение и не сила, поэтому ни одна
 * категория стрелок здесь не появляется (даже при включённых переключателях).
 * Ядра не летают, их количество меняется.
 *
 * Четыре панели под схемой построены по рядам модели: распавшиеся ядра
 * (теория и счёт), активность A = λN (теория и показания счётчика),
 * прямая ln N = ln N₀ − λt и расхождение счёта с теорией в процентах
 * вместе с коридором √N/N₀.
 */
class DecayScene : public LabScene {
    Q_OBJECT
public:
    /**
     * Одна клетка образца: её положение и то, цело ли в ней ядро.
     * Порядок проверки клеток фиксирован, поэтому с ростом времени клетки
     * только гаснут и никогда не загораются снова.
     */
    struct NucleusCell {
        int index = 0;
        QRectF rect;
        bool alive = false;
        bool justDecayed = false;   // гаснет на текущем шаге наблюдения
        double nucleiPerCell = 1.0;
    };

    /**
     * Геометрия чертежа в пикселях. Нужна не только отрисовке: по ней
     * проверяют, что шкала времени и уровень доли d стоят там, где их
     * ищут перетаскиванием ручки.
     */
    struct SchemeGeometry {
        QRectF sample;         // рамка образца с клетками
        QRectF strip;          // «ход опыта»: оси t и N
        QRectF counterBand;    // лента счётчика распадов
        double rulerY = 0.0;   // линия размерки T½
        double startPx = 0.0;  // пиксель начала отсчёта времени
        double endPx = 0.0;    // и его конца (весь опыт с запасом)
        double pixelsPerHour = 0.0;
        double halfLifePx = 0.0;
        double observationEndPx = 0.0;
        double targetPx = 0.0;      // момент, когда останется доля d
        double targetLevelPy = 0.0;  // уровень N₀·d на шкале ядер
        double nucleiTopPy = 0.0;    // уровень N₀
        double currentPx = 0.0;      // текущий момент
        int cellsPerNucleus = 1;     // сколько ядер в одной клетке
    };

    /** Подписи схемы — общий для лаборатории механизм разведения надписей. */
    using Annotation = labels::Item;

    explicit DecayScene(Simulation* model, QObject* parent = nullptr);

    void reconfigure(const QRectF& area) override;

    /** Текущая геометрия чертежа (после reconfigure()). */
    SchemeGeometry schemeGeometry() const;

    /**
     * Клетки образца в момент t: их число отвечает дискретному счёту модели,
     * а не гладкой кривой, поэтому целых клеток ровно столько, сколько
     * ядер показал счётчик.
     */
    QVector<NucleusCell> nucleiGridFor(double t) const;

    /**
     * Векторов момента нет: в модели распада нет ни скоростей, ни сил.
     * Метод возвращает пустой список и при включённых категориях — так
     * проверка видит, что стрелки здесь не придуманы.
     */
    QVector<vectors::Arrow> vectorsFor(double t) const;

    /** Подписи схемы с их расположением (после reconfigure()). */
    QVector<Annotation> annotations() const;

    /** Полоса схемы: образец, шкала времени и лента счётчика. */
    QRectF schemeBand() const;

    /** Пиксельная позиция момента t на шкале опыта (обратная к геометрии). */
    double sceneXForTime(double t) const;
    /** И обратный перевод: пиксель шкалы -> момент времени. */
    double timeForSceneX(double sceneX) const;
    /** Пиксельная позиция числа ядер N на вертикальной шкале хода опыта. */
    double sceneYForNuclei(double nuclei) const;
    /** И обратный перевод. */
    double nucleiForSceneY(double sceneY) const;

protected:
    void paintScheme(QPainter* painter) override;
    void positionHandles() override;
    double mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const override;
    double handleScenePosition(const HandleInfo& info) const override;

private:
    /** Сколько ядер показывает одна клетка при текущем N₀ и размерах рамки. */
    int nucleiPerCell() const;
    /** Столбцы и строки решётки образца для текущего числа клеток. */
    void gridLayout(int cells, int& columns, int& rows) const;

    void drawSample(QPainter* painter, const QVector<NucleusCell>& cells) const;
    void drawStrip(QPainter* painter) const;
    void drawCounterBand(QPainter* painter) const;
    void drawPlots(QPainter* painter) const;

    /** Ряд показаний счётчика: распады на интервалах отображения. */
    QVector<QPointF> counterReadoutSeries() const;

    // Геометрия, пересчитываемая в reconfigure().
    QRectF m_band;
    QRectF m_sample;
    QRectF m_strip;
    QRectF m_counterBand;
    double m_rulerY = 0.0;
    /** Границы шкалы времени: 0 и длительность опыта с запасом. */
    double m_startPx = 0.0;
    double m_endPx = 0.0;
    double m_pixelsPerHour = 1.0;
    /** Вертикальная шкала хода опыта: 0 ядер снизу, m_nucleiTop сверху. */
    double m_nucleiTop = 1.0;
    double m_stripBottomY = 0.0;
    double m_stripTopY = 0.0;
    double m_pixelsPerNucleus = 1.0;
    /** Порядок обхода клеток: он фиксирован, поэтому счёт только убывает. */
    QVector<int> m_permutation;
    int m_cellCount = 0;
    int m_cellsPerNucleus = 1;
};

}  // namespace lab::scenes
