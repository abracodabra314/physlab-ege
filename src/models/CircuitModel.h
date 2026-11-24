#pragma once

#include <functional>
#include <initializer_list>

#include <QString>
#include <QStringList>
#include <QVector>

#include "circuit/CircuitNet.h"
#include "circuit/MnaSolver.h"
#include "core/Simulation.h"
#include "core/Trajectory.h"

namespace lab::models {

/**
 * Модель 15. Конструктор электрической цепи.
 *
 * Пользователь собирает схему на сетке: ставит элементы, поворачивает их и
 * соединяет клеммы кликами. Модель не знает заранее, какая цепь получилась:
 * она строит уравнения метода узловых потенциалов по найденным узлам
 * (lab::circuit::MnaSolver). Поэтому расчёт один и для двух резисторов, и
 * для мостовой схемы: формулы последовательного и параллельного включения из
 * решения системы следуют, а не подставляются в код.
 *
 * Узлы расчёта — те же точки, что на чертеже: концы проводов, его изломы и
 * клеммы элементов. Пересечение двух проводов, идущих каждый по своей
 * прямой, узлом не считается, поэтому соединение видно геометрически, а ток
 * идёт только по тем линиям, которые действительно соединены.
 *
 * Переходный процесс (заряд конденсатора, рост тока в катушке) считается
 * интегрированием: в каждый момент решается та же система, из неё берут
 * токи ветвей и производные
 *   dU_C/dt = −I_C/C,   dI_L/dt = U_L/L,
 * которыми шагает состояние. Экспоненты и постоянная времени в код не
 * подставлены: τ находят по рассчитанным точкам, а момент окончания
 * процесса виден по тому, что состояние перестает сдвигаться.
 *
 * Ключ работает по-настоящему: до t₀ он в одном состоянии, после — в другом,
 * и структуру уравнений перестраивают на t₀. Напряжение конденсатора и ток
 * катушки в этот момент не меняются — они непрерывны, и это ограничение
 * соблюдает интегрирование, а не отдельная проверка.
 */
class CircuitModel : public Simulation {
    Q_OBJECT
public:
    /** Ячейка готовой схемы: элемент в точке сетки со своим значением. */
    struct PresetCell {
        circuit::PartType type = circuit::PartType::Resistor;
        int column = 0;
        int row = 0;
        bool horizontal = true;
        double value = 0.0;
    };

    /** Провод готовой схемы: ломаная по точкам сетки. */
    struct PresetWire {
        QVector<circuit::GridPoint> points;
    };

    /**
     * Готовая схема справочника: элементы, провода и параметры опыта.
     * Названия пресетов берут отсюда, поэтому список можно продолжать.
     */
    struct PresetSpec {
        QString name;
        QVector<PresetCell> cells;
        QVector<PresetWire> wires;
        double window = 4.0;
        double delay = 0.0;
        double capacitorVoltage = 0.0;
        double inductorCurrent = 0.0;
    };

    /** Инструмент сцены: ставить элемент, рисовать провод или стирать. */
    enum class Tool {
        Part,     // ставить элемент выбранного типа
        Wire,     // рисовать провод
        Delete,   // удалить элемент или провод
    };

    explicit CircuitModel(QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("circuit"); }
    QString title() const override;
    QString subtitle() const override;
    QString statement() const override;
    QString problemText() const override;

    /** Переходный процесс измеряют в секундах: τ = RC у RC-цепи — сотые доли. */
    QString timeUnit() const override { return QStringLiteral("s"); }
    double timePerSecond() const override;
    double duration() const override;
    double stepSize() const override;

    QVector<ParamSpec> paramSpecs() const override;
    /**
     * Проверяются и параметры, и топология: схема без источника или с
     * неподключённой клеммой остаётся на экране с объяснением, а не падает.
     */
    ValidationResult validate() const override;

    SimState stateAt(double t) const override;
    double eventTime() const override;

    Derivation derivation() const override;
    QVector<Readout> readouts() const override;

    QVector<HandleInfo> handles() const override;
    void setHandleValue(const QString& id, double value) override;

    // ---------------- готовые схемы ----------------
    /** Число пунктов списка схем, включая «моя схема». */
    static int presetCount();
    static QString presetName(int index);
    /** Пункт «моя схема»: пользователь собирает цепь сам. */
    static int customPresetIndex();
    /** Ячейки готовой схемы (пусто у пункта «моя схема»). */
    static QVector<PresetCell> presetCells(int index);
    /** Строит схему из справочника; false, если пункта нет. */
    bool applyPreset(int index);

    // ---------------- палитра ----------------
    static int toolCount();
    static QString toolName(int index);
    Tool tool() const;
    /** Тип элемента выбранного пункта палитры. */
    circuit::PartType toolPartType() const;
    /** Значение, которое получит новый элемент выбранного типа. */
    double toolValue() const;
    /** Подсказка: что делает текущий инструмент и что нажать дальше. */
    QString toolHint() const;

    // ---------------- сетка и топология ----------------
    /** Точек сетки по горизонтали и вертикали (узлы 0..columns). */
    int gridColumns() const;
    int gridRows() const;
    const circuit::CircuitNet& net() const { return m_net; }

    /** Ставит элемент выбранного типа началом в anchor; -1 — место занято. */
    int placeAt(const circuit::GridPoint& anchor);
    /** Провод между точками по линиям сетки (с одним изгибом). */
    int wireBetween(const circuit::GridPoint& from, const circuit::GridPoint& to);
    /**
     * Соединение за клик: если точки уже соединены, снимает соединение
     * (удаляет провод), иначе прокладывает провод между ними.
     */
    bool connectTerminals(const circuit::GridPoint& first, const circuit::GridPoint& second);
    /** Поворот элемента в точке на 90°; false — поворачивать нечего. */
    bool rotateAt(const circuit::GridPoint& point);
    /** Удаляет элемент или провод в точке; false — в точке ничего нет. */
    bool removeAt(const circuit::GridPoint& point);
    /** Очищает схему и переводит постановку на «свою схему». */
    void clearCircuit();

    /** Первая точка незавершённого соединения. */
    bool hasPendingTerminal() const { return m_pendingTerminal >= 0; }
    circuit::GridPoint pendingTerminal() const;
    /**
     * Клик по клемме или проводу: первый клик запоминает точку, второй
     * соединяет её с новой. Возвращает true, когда соединение состоялось
     * или было снято.
     */
    bool pickTerminal(const circuit::GridPoint& point);
    void cancelPendingTerminal() { m_pendingTerminal = -1; }

    /** Элемент, выбранный для правки значения; -1 — выбора нет. */
    int selectedPart() const { return m_selected; }
    void setSelectedPart(int partId);

    /** Имена элементов схемы: «R1 (резистор)». */
    QStringList partNames() const;
    /** Ключи, замкнутые в момент t. */
    QVector<int> closedSwitchesAt(double t) const;
    bool hasSwitch() const;
    /** В схеме есть конденсатор или катушка: нужен переходный процесс. */
    bool hasTransient() const;

    // ---------------- расчёт ----------------
    /**
     * Решение цепи в момент t: потенциалы узлов, токи и напряжения ветвей,
     * токи участков проводов. При непригодной схеме ok = false и заполнены
     * message с hint.
     */
    circuit::Solution solutionAt(double t) const;
    circuit::Solution currentSolution() const { return solutionAt(time()); }

    double currentOf(int partId, double t) const;
    double voltageOf(int partId, double t) const;
    double chargeOf(int partId, double t) const;
    /** Напряжение на зажимах источника: ЭДС минус падение на внутреннем сопротивлении. */
    double terminalVoltageAt(double t) const;
    /** Ток, который отдаёт источник, А. */
    double sourceCurrentAt(double t) const;
    /**
     * Сопротивление, которое «видит» источник: R = U/I по рассчитанным
     * значению. Для мостовой схемы его не даёт ни складывание, ни
     * параллельное соединение — только решение системы.
     */
    double equivalentResistanceAt(double t) const;
    double deliveredPowerAt(double t) const;
    double sourcePowerAt(double t) const;
    double storedEnergyAt(double t) const;
    /** ЭДС первого источника в схеме (0, если источника нет). */
    double sourceEmf() const;
    /**
     * Постоянная времени ветви, найденная по рассчитанному ряду как время
     * изменения на 1 − 1/e от конечного приращения.
     */
    double timeConstantOf(int partId) const;
    /** Момент, когда переходный процесс практически завершился (NaN — его нет). */
    double transientEndTime() const;
    /** Узлы обеих клемм элемента (для подписей потенциала на схеме). */
    QVector<circuit::CircuitNet::TerminalRef> branchTerminals(int partId) const;

    // ---------------- подписи и ряды ----------------
    QString partValueText(int partId) const;
    /** Силу тока пишут милли- и микроамперами: голые степени читаются хуже. */
    static QString currentText(double amperes);
    static QString voltageText(double volts);
    static QString resistanceText(double ohms);

    QVector<QPointF> sourceCurrentSeries() const;
    QVector<QPointF> branchCurrentSeries(int partId) const;
    QVector<QPointF> branchVoltageSeries(int partId) const;
    QVector<QPointF> terminalVoltageSeries() const;
    QVector<QPointF> chargeSeries(int partId) const;
    QVector<QPointF> energySeries() const;
    QVector<QPointF> sourcePowerSeries() const;
    QVector<QPointF> dissipatedPowerSeries() const;
    /**
     * Напряжение на зажимах против тока: вольт-амперная характеристика,
     * построенная по рассчитанным парам (I, U). Наклон −r получается сам.
     */
    QVector<QPointF> terminalVersusCurrent() const;
    QVector<QPointF> equivalentResistanceSeries() const;

signals:
    /** Топология или выбор элемента изменились: панели перестраиваются. */
    void circuitChanged();

private:
    // ---------------- параметры ----------------
    double emf() const;
    double internalResistance() const;
    double switchDelay() const;
    bool switchCloses() const;
    double initialCapacitorVoltage() const;
    double initialInductorCurrent() const;
    double observationTime() const;
    double sliderValue(circuit::PartType type) const;

    /** Фаза расчёта: промежуток времени, состояние ключа и траектория. */
    struct Phase {
        double from = 0.0;
        double to = 0.0;
        bool afterSwitch = false;
        circuit::MnaSystem system;
        Trajectory track;
        QVector<double> initial;
    };

    /** Счётчик перестроек топологии: по нему решают, что систему надо пересобрать. */
    void touchTopology();
    /** Пересобирает узлы, уравнения и траектории переходного процесса. */
    void ensureSolved() const;
    /** Ключи, замкнутые до переключения (false) или после (true). */
    QVector<int> closedSwitchesAfter(bool afterSwitch) const;
    /** Решение системы данной фазы при заданном состоянии U_C и I_L. */
    circuit::Solution solvePhase(int phaseIndex, double t, const QVector<double>& state) const;
    /** Начальное состояние переходного процесса по параметрам. */
    QVector<double> initialState() const;
    QVector<int> capacitorIds() const;
    QVector<int> inductorIds() const;
    /** Идентификаторы элементов, чьи величины входят в состояние. */
    QVector<int> dynamicIds() const;
    /** Индекс фазы, к которой относится момент t. */
    int phaseIndexAt(double t) const;
    /** Состояние (U_C, I_L) в момент t по траектории фазы. */
    QVector<double> stateVectorAt(double t) const;
    /** Ряд, построенный по решениям цепи на всей шкале времени. */
    QVector<QPointF> sampled(const std::function<double(const circuit::Solution&, double)>& value) const;
    /** Первый элемент, способный поддерживать ток: источник, конденсатор, катушка. */
    const circuit::Part* activePart() const;

    static const PresetSpec* presetSpec(int index);

    mutable circuit::CircuitNet m_net;
    mutable QVector<Phase> m_phases;
    mutable QVector<int> m_dynamicIds;
    mutable QVector<double> m_initialState;
    mutable double m_transientEnd = std::numeric_limits<double>::quiet_NaN();

    int m_selected = -1;
    int m_pendingTerminal = -1;
    /** Пункт схем, который уже применён к сетке. */
    int m_appliedPreset = -1;

    /** Подпись всех входов, от которых зависит расчёт. */
    struct Signature {
        int topology = 0;
        int preset = -1;
        double emf = 0.0;
        double internal = 0.0;
        double delay = 0.0;
        double capacitorVoltage = 0.0;
        double inductorCurrent = 0.0;
        double window = 0.0;
        int switchMode = -1;
        int parts = 0;
        double partValues = 0.0;

        bool operator==(const Signature& other) const;
    };
    Signature signature() const;
    mutable Signature m_solved;
};

}  // namespace lab::models
