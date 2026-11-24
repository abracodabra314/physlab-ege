#pragma once

#include <QPointF>
#include <QRectF>
#include <QVector>

#include "circuit/CircuitNet.h"
#include "circuit/MnaSolver.h"
#include "core/Simulation.h"
#include "scenes/LabScene.h"
#include "scenes/SchemeLabels.h"
#include "scenes/VectorArrows.h"

namespace lab::models {
class CircuitModel;
}

namespace lab::scenes {

/**
 * Сцена 15. Конструктор электрической цепи.
 *
 * Чертёж — лист клетчатой бумаги: элементы стоят на узлах сетки, провода
 * идут по линиям сетки и поворачивают на 90°. Точка сетки здесь значит
 * больше, чем координата: именно по ней модель решает, какие клеммы соединены
 * в один узел, а узлы дают правую часть уравнений метода узловых потенциалов.
 * Поэтому сцена ничего не «пересчитывает по схеме»: она показывает то, что
 * посчитала модель, и только тем, что посчитала.
 *
 * Из модели на чертеж попадают:
 *  - токи и напряжения ветвей (подписи элементов и показания приборов);
 *  - потенциалы узлов (их подписывают у точек, где сходятся проводники);
 *  - токи участков проводов — по ним движутся носители заряда, и только там,
 *    где расчёт дал однозначный ответ: участок с неопределённым током остаётся
 *    без движущихся точек, иначе направление зависело бы от порядка обхода;
 *  - условное направление тока (оранжевые стрелки с символом I) — вместе с
 *    переключателем «силы», потому что ток в цепи — та же условная стрелка,
 *    что и сила на других чертежах.
 *
 * Мышью на листе делают схему: клик ставит элемент выбранного типа, клик по
 * уже стоящему элементу сначала его выбирает (появляется ручка значения),
 * повторный клик поворачивает на 90°. Инструмент «провод» соединяет клеммы за
 * два клика, а правая кнопка убирает элемент или провод — как и сказано в
 * подсказке модели про неверную схему.
 *
 * Четыре панели под листом берут ряды модели: ток источника и выбранной
 * ветви, напряжение на зажимах и напряжение ветви, запасённая энергия с
 * мощностями и вольт-амперная характеристика источника (U от I): наклон её
 * даёт −r без всякой формулы.
 */
class CircuitScene : public LabScene {
    Q_OBJECT
public:
    /** Геометрия листа в пикселях: нужен и отрисовке, и кликам. */
    struct Sheet {
        QRectF board;              // поле чертежа с сеткой
        QRectF readouts;           // правая колонка показаний
        QRectF panelsBand;         // полоса четырёх панелей
        double cell = 24.0;        // шаг сетки, px
        QPointF origin;            // центр клетки (0, 0)
        int columns = 1;           // клеток по горизонтали (узлы 0..columns)
        int rows = 1;              // клеток по вертикали
    };

    explicit CircuitScene(Simulation* model, QObject* parent = nullptr);

    void reconfigure(const QRectF& area) override;

    /** Лист чертежа после последнего reconfigure(). */
    Sheet sheet() const { return m_sheet; }
    /** Центр клетки сетки. */
    QPointF cellCenter(const circuit::GridPoint& point) const;
    /** Клетка сетки под точкой сцены. */
    circuit::GridPoint cellAt(const QPointF& pos) const;
    /** Полилайн по точкам сетки. */
    QVector<QPointF> polyline(const QVector<circuit::GridPoint>& path) const;
    /** Середина символа элемента: точка, где ставят его обозначение. */
    QPointF symbolCenter(const circuit::Part& part) const;
    /** Клетка сетки по номерам столбца и строки (для проверки геометрии). */
    static circuit::GridPoint netCell(int column, int row);
    /** Точка на листе, куда ставят ручку данной величины (открыто для тестов). */
    QPointF handleAnchorFor(const HandleInfo& info) const;

    /**
     * Условные стрелки тока: по одной на каждую проводящую ветвь. Их показывают
     * вместе с силами, потому что ток в цепи — та же условная стрелка, что и
     * сила на механическом чертеже.
     */
    QVector<vectors::Arrow> vectorsFor(double t) const;

protected:
    void paintScheme(QPainter* painter) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

    /** Ручки: x — по оси времени, y — у символа элемента, который они правят. */
    void positionHandles() override;
    double mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const override;

private:
    /** Правая колонка: что стоит на листе и что даёт расчёт. */
    QVector<QPair<QString, QString>> readoutLines(double t) const;

    void drawGrid(QPainter* painter) const;
    void drawWires(QPainter* painter, const circuit::Solution& solution) const;
    void drawParts(QPainter* painter, const circuit::Solution& solution, double t) const;
    void drawCarriers(QPainter* painter, const circuit::Solution& solution, double t) const;
    void drawTerminals(QPainter* painter) const;
    void drawReadouts(QPainter* painter) const;
    void drawPlots(QPainter* painter) const;
    void drawVectors(QPainter* painter, const QVector<vectors::Arrow>& arrows) const;

    /**
     * Символ элемента: от первой клеммы ко второй, по направлению элемента.
     * closed говорит, замкнут ли ключ в этот момент: рычаг ключа — часть
     * символа, а состояние берёт из модели.
     */
    void drawPartSymbol(QPainter* painter, const circuit::Part& part, const QColor& ink,
                        bool closed) const;
    /** Элемент, чья область содержит точку сетки (клеммы тоже относятся). */
    int partAt(const circuit::GridPoint& point) const;
    /** Клемма элемента в точке сетки или -1, если клеммы нет. */
    int terminalAt(const circuit::GridPoint& point) const;

    models::CircuitModel* circuitModel() const;
    /** Точка на листе, куда ставят ручку данной величины: над её элементом. */
    QPointF valueHandleAnchor(const HandleInfo& info) const;

    Sheet m_sheet;
    int m_clickedPart = -1;   // элемент последнего клика: второй клик крутит его
    /** Состояние ключа в рисуемый момент: нужно символу, потому и mutable. */
    mutable bool m_leverClosed = false;
};

}  // namespace lab::scenes
