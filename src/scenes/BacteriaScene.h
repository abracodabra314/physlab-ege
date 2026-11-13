#pragma once

#include <QPointF>
#include <QVector>

#include "core/Simulation.h"
#include "scenes/LabScene.h"
#include "scenes/SchemeLabels.h"
#include "scenes/VectorArrows.h"

namespace lab::models {
class BacteriaModel;
}

namespace lab::scenes {

/**
 * Сцена 13. Деление бактерий в чашке Петри.
 *
 * Слева — чашка: поле зрения фиксированной площади, поэтому число клеток в
 * нём ограничено. Клетки рисуются не «россыпью», а по спирали от центра,
 * начиная с посева: occupied клеток всегда образуют круг, радиус которого
 * равен R = R_кадра·√(N/N_мест). Это не украшение, а следствие: клетки лежат
 * слоем в одну клетку, поэтому площадь колонии пропорциональна их числу, и
 * линейка радиуса подписана на рисунке.
 *
 * Когда N достигает N_мест, кадр заполнен, и рост не останавливается:
 * дальше колония ложится вторым и третьим слоем (они показаны темнее), а по
 * стеклу идут трещины — их число растёт вместе с толщиной слоя. Удвоение
 * задаёт период T_d, а не теснота кадра, поэтому число клеток на панели
 * продолжает удваиваться и после переполнения.
 *
 * Одна клетка рисунка — это несколько настоящих клеток колонии (число
 * подписано), иначе 64000 палочек в кадр не поместились бы. Клетки,
 * разделившиеся на текущем интервале учёта, помечены: по ним видно, что рост
 * идёт делением «одна-на-две», а не непрерывным намазыванием.
 *
 * Справа — ход опыта: ступенька завершённых делений лежит рядом с плавной
 * кривой N₀·2^(t/T_d), пунктир на уровне целевого числа N и его пересечение
 * с кривой дают ответ t = T_d·log₂(N/N₀). Под шкалой — размерные линии T_d и
 * T и лента прироста: столбцы ΔN по интервалам учёта.
 *
 * Векторов в модели нет: колонию не толкают и она не летит, у неё меняется
 * число клеток, поэтому при включённых категориях стрелки не появляются.
 *
 * Под схемой — четыре панели: число клеток (теория и ступенька), прирост за
 * интервал с средней скоростью ΔN/Δt, прямая ln N = ln N₀ + kt и не временная
 * зависимость N′(N) = kN — по ней видно, что скорость пропорциональна самой
 * колонии.
 */
class BacteriaScene : public LabScene {
    Q_OBJECT
public:
    /**
     * Одна клетка рисунка: её положение, угол и то, какой слой колонии она
     * показывает. Порядок обхода фиксирован (спираль от центра), поэтому при
     * росте времени клетки только добавляются и никогда не переставляются.
     */
    struct ColonyCell {
        int index = 0;
        QPointF center;
        double angle = 0.0;      // ориентация палочки
        double length = 6.0;     // длина палочки в пикселях
        double width = 2.4;      // её толщина в пикселях
        int layer = 0;           // 0 — первый слой, 1 и 2 — вторые и третьи
        bool dividing = false;   // разделилась на текущем интервале учёта
        double cellsPerCell = 1.0;
    };

    /**
     * Геометрия чертежа в пикселях. Нужна не только отрисовке: по ней
     * проверяют, что шкала времени, уровень цели N и радиус колонии стоят
     * там, где их ищут перетаскиванием ручек.
     */
    struct SchemeGeometry {
        QRectF dish;             // чашка Петри (квадрат под окружность)
        QPointF dishCenter;
        double dishRadius = 0.0;
        double colonyRadius = 0.0;   // радиус колонии в пикселях
        QRectF strip;            // «ход опыта»: оси t и N
        QRectF growthBand;       // лента прироста по интервалам учёта
        double rulerY = 0.0;     // линия размерки T_d
        double startPx = 0.0;    // пиксель начала отсчёта времени
        double endPx = 0.0;      // и его конца (весь опыт с запасом)
        double pixelsPerHour = 0.0;
        double doublingPx = 0.0;    // пиксель конца первого периода удвоения
        double observationEndPx = 0.0;
        double targetPx = 0.0;      // момент, когда клеток станет N
        double targetLevelPy = 0.0; // уровень целевого числа клеток
        double cellsTopPy = 0.0;    // верх шкалы числа клеток
        double currentPx = 0.0;     // текущий момент
        double cellsPerCell = 1.0;  // сколько клеток колонии в одной клетке рисунка
        double capacityCells = 0.0;  // клеток в кадре (слой в одну клетку)
        double layers = 0.0;         // толщина колонии в слоях: N/N_мест
        int occupiedCells = 0;       // клеток рисунка в первом слое
    };

    /** Подписи схемы — общий для лаборатории механизм разведения надписей. */
    using Annotation = labels::Item;

    explicit BacteriaScene(Simulation* model, QObject* parent = nullptr);

    void reconfigure(const QRectF& area) override;

    /** Текущая геометрия чертежа (после reconfigure()). */
    SchemeGeometry schemeGeometry() const;

    /**
     * Клетки рисунка в момент t: их число отвечает целому счёту колонии, а
     * радиус занимаемого ими круга — формуле R = R_кадра·√(N/N_мест).
     */
    QVector<ColonyCell> cellsFor(double t) const;

    /**
     * Векторов момента нет: в модели деления нет ни скоростей, ни сил.
     * Метод возвращает пустой список и при включённых категориях — так
     * проверка видит, что стрелки здесь не придуманы.
     */
    QVector<vectors::Arrow> vectorsFor(double t) const;

    /** Подписи схемы с их расположением (после reconfigure()). */
    QVector<Annotation> annotations() const;

    /** Полоса схемы: чашка, шкала опыта и лента прироста. */
    QRectF schemeBand() const;

    /** Пиксельная позиция момента t на шкале опыта (обратная к геометрии). */
    double sceneXForTime(double t) const;
    /** И обратный перевод: пиксель шкалы -> момент времени. */
    double timeForSceneX(double sceneX) const;
    /** Пиксельная позиция числа клеток N на вертикальной шкале хода опыта. */
    double sceneYForCells(double cells) const;
    /** И обратный перевод. */
    double cellsForSceneY(double sceneY) const;

    /** Сколько клеток колонии показывает одна клетка рисунка. */
    double cellsPerDrawnCell() const;
    /** Число мест для клеток рисунка в первом слое чашки (= N_мест/n). */
    int drawnSlots() const;
    /**
     * Радиус колонии в пикселях при данном переполнении кадра:
     * R = R_кадра·√(N/N_мест), поэтому при N = N_мест он доходит до края.
     */
    double colonyRadiusPx(double overflow = 1.0) const;
    /** Число занятых мест рисунка при N клетках колонии. */
    int occupiedSlots(double cells) const;

protected:
    void paintScheme(QPainter* painter) override;
    void positionHandles() override;
    double mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const override;
    double handleScenePosition(const HandleInfo& info) const override;

private:
    /** Верх вертикальной шкалы числа клеток (с учётом отметки цели). */
    double cellsAxisTop() const;
    /** Позиция i-го места спирали в круге заданного радиуса. */
    QPointF slotPosition(int slot, int layer, double fill) const;

    void drawDish(QPainter* painter, const QVector<ColonyCell>& cells) const;
    void drawColonyBoundary(QPainter* painter) const;
    void drawCracks(QPainter* painter) const;
    void drawStrip(QPainter* painter) const;
    void drawGrowthBand(QPainter* painter) const;
    void drawPlots(QPainter* painter) const;

    /** Столбцы прироста: ΔN по интервалам учёта. */
    QVector<QPointF> growthReadoutSeries() const;

    // Геометрия, пересчитываемая в reconfigure().
    QRectF m_band;
    QRectF m_dish;
    QRectF m_strip;
    QRectF m_growthBand;
    double m_rulerY = 0.0;
    /** Границы шкалы времени: 0 и длительность опыта с запасом. */
    double m_startPx = 0.0;
    double m_endPx = 0.0;
    double m_pixelsPerHour = 1.0;
    /** Вертикальная шкала хода опыта: 0 клеток снизу, m_cellsTop сверху. */
    double m_cellsTop = 1.0;
    double m_stripBottomY = 0.0;
    double m_stripTopY = 0.0;
    double m_pixelsPerCell = 1.0;
    double m_cellsPerCell = 1.0;
    double m_capacityCells = 1.0;
    /** Габарит клетки рисунка в пикселях: он постоянен при любой постановке. */
    double m_cellLength = 6.0;
    double m_cellWidth = 2.4;
};

}  // namespace lab::scenes
