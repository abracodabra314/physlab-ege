#pragma once

#include <QString>
#include <QVector>

#include "circuit/CircuitNet.h"

namespace lab::circuit {

/**
 * Расчёт цепи методом узловых потенциалов.
 *
 * Это не набор формул для отдельных случаев, а один алгоритм для любой
 * топологии: по первому закону Кирхгофа для каждого узла записывают сумму
 * токов ветвей, а ветвь с заданным напряжением добавляет неизвестный ток и
 * уравнение связи потенциалов. Ветки входят в матрицу одинаково:
 *  - резистор, ключ, амперметр, вольтметр — проводимость G = 1/R;
 *  - источник ЭДС и конденсатор — неизвестный ток ветви и уравнение
 *    «разность потенциалов клемм равна ЭДС (напряжению) минус r·I»;
 *  - катушка с известным током — источник тока в ветви.
 * Поэтому токи и напряжения считают сразу для всей цепи: свёртывать
 * сопротивления по схемам «последовательно/параллельно» не нужно, частные
 * формулы являются следствием решения системы, а не её частью.
 *
 * Переходный процесс (заряд конденсатора, рост тока в катушке) получают
 * интегрированием: производные dU_C/dt = I_C/C и dI_L/dt = U_L/L берут из
 * решения той же системы в каждый момент времени. Экспонента и постоянная
 * времени τ = RC в код не подставляются: их видно по рассчитанным точкам.
 */
namespace parts {
/** Сопротивление замкнутого ключа и амперметра, Ом. */
constexpr double kClosedResistance = 0.05;
/** Сопротивление разомкнутого ключа, Ом: ток через него пренебрежимо мал. */
constexpr double kOpenResistance = 5.0e7;
/** Сопротивление идеального вольтметра, Ом. */
constexpr double kVoltmeterResistance = 5.0e7;
/** Относительный порог ведущего элемента при решении системы. */
constexpr double kPivotTolerance = 1.0e-11;
}  // namespace parts

/**
 * Заданные величины ветвей: ЭДС источников с внутренними сопротивлениями,
 * напряжения на конденсаторах и токи в катушках. Все три вида входят в
 * уравнения как источники, поэтому любое их сочетание решается одинаково.
 *
 * Направления one для всех ветвей: ток I через ветвь идёт от первой клеммы
 * (start) ко второй (finish), напряжение U = φ(start) − φ(finish). Для
 * источника I < 0 означает, что ток вытекает из клеммы «+» в цепь.
 */
class Drive {
public:
    Drive() = default;
    explicit Drive(const CircuitNet& net) : m_net(&net), m_rows(size_t(net.parts().size())) {
        for (int i = 0; i < net.parts().size(); ++i) {
            const Part& part = net.parts().at(size_t(i));
            m_rows[size_t(i)].emf = part.type == PartType::Source ? part.value : 0.0;
        }
    }

    /** Сопротивление внутри источника, Ом: 0 — идеальный источник. */
    void setInternalResistance(double resistance);
    double internalResistance() const { return m_internal; }

    bool setEmf(int partId, double value);
    bool setCapacitorVoltage(int partId, double value);
    bool setInductorCurrent(int partId, double value);

    double emfOf(int partId) const;
    double capacitorVoltageOf(int partId) const;
    double inductorCurrentOf(int partId) const;
    /** Сопротивление ветви по её типу: приборы и ключ задаются здесь. */
    double branchResistanceOf(int partId) const;

private:
    struct Row {
        double emf = 0.0;
        double capacitorVoltage = 0.0;
        double inductorCurrent = 0.0;
    };

    const CircuitNet* m_net = nullptr;
    QVector<Row> m_rows;
    double m_internal = 0.0;

    int indexOf(int partId) const;
};

/**
 * Скомпилированная топология: узлы, уравнения и ветви. Её нужно перестроить
 * только когда изменились соединения, типы элементов или их сопротивления.
 */
class MnaSystem {
public:
    MnaSystem() = default;

    /**
     * Строит систему по топологии; ground — узел с нулевым потенциалом.
     * closedSwitches — id замкнутых ключей: их сопротивление (и структура
     * уравнений) зависит от момента времени, поэтому при переключении систему
     * пересобирают.
     */
    bool compile(const CircuitNet& net, int ground, const QVector<int>& closedSwitches = {});

    bool empty() const { return m_branches.isEmpty(); }
    /** Число узлов топологии, включая заземлённый. */
    int nodeCount() const { return m_nodeCount; }
    /** Число неизвестных: потенциалы узлов плюс токи ветвей с заданным напряжением. */
    int unknownCount() const { return m_equations; }
    /** Строка уравнения узла; -1 — узел заземлён. */
    int nodeEquation(int node) const;
    /** Узел, потенциал которого считают нулём. */
    int ground() const { return m_ground; }

    /** Ветвь элемента в матрице. */
    struct Branch {
        int partId = 0;
        PartType type = PartType::Resistor;
        int fromNode = -1;      // узел первой клеммы (-1 — земля)
        int toNode = -1;        // узел второй клеммы (-1 — земля)
        double conductance = 0.0;
        int sourceIndex = -1;   // номер неизвестного тока ветви (ветвь с напряжением)
    };
    const QVector<Branch>& branches() const { return m_branches; }
    int branchIndexOfPart(int partId) const;
    /** Сумма проводимостей, подходящих к узлу: по ней ищут «висячие» узлы. */
    double nodeConductance(int node) const;

private:
    int m_nodeCount = 0;
    int m_equations = 0;
    int m_ground = -1;
    QVector<int> m_nodeRow;
    QVector<double> m_nodeDiagonal;
    QVector<Branch> m_branches;
};

/** Результат расчёта цепи в один момент времени. */
struct Solution {
    /** Одна ветвь элемента: что дал расчёт. */
    struct Value {
        int partId = 0;
        PartType type = PartType::Resistor;
        bool connected = false;    // обе клеммы попали в узлы
        bool conducting = false;   // через ветвь идёт ток
        double current = 0.0;      // А, от первой клеммы ко второй
        double voltage = 0.0;      // В, φ(первая клемма) − φ(вторая)
        double power = 0.0;        // Вт, поглощаемая мощность
        double charge = 0.0;       // Кл (конденсатор)
        double flux = 0.0;         // Вб (катушка)
        double derivative = 0.0;   // dU_C/dt (В/с) или dI_L/dt (А/с)
    };

    /**
     * Ток участка чертежа: по участкам двигают носители заряда. Направление
     * положительно вдоль path.
     */
    struct Segment {
        int partId = 0;           // 0 — участок провода
        int wireId = 0;           // 0 — участок ветви элемента
        QVector<GridPoint> path;
        double current = 0.0;
        bool determined = true;   // false — ток не следует однозначно из топологии
    };

    bool ok = false;
    QString message;   // почему расчёт не удался (пусто — всё в порядке)
    QString hint;      // что исправить в схеме

    /** Потенциалы узлов по их номерам (В); заземлённый узел — 0. */
    QVector<double> nodePotentials;
    QVector<Value> values;
    QVector<Segment> segments;
    /** Энергия в конденсаторах и катушках, Дж. */
    double storedEnergy = 0.0;

    const Value* value(int partId) const;
    /** Ток, который отдаёт источник ЭДС (в цепь), А. */
    double sourceCurrent() const;
    /** Мощность, которую отдаёт источник, Вт. */
    double sourcePower() const;
    /** Суммарная мощность на резисторах, ключах и приборах, Вт. */
    double dissipatedPower() const;
};

/**
 * Решение системы: сборка матрицы проводимостей, метод Гаусса с выбором
 * ведущего элемента и разбор ветвей и участков чертежа.
 */
class MnaSolver {
public:
    /** Решает цепь при заданных ЭДС, напряжениях конденсаторов и токах катушек. */
    static Solution solve(const MnaSystem& system, const CircuitNet& net, const Drive& drive);

    /**
     * Разносит токи элементов по участкам проводов из первого закона Кирхгофа
     * для точек чертежа. Участки, ток на которых не определяется топологией
     * (петля из одних проводов), помечают determined = false.
     */
    static void distributeCurrents(const CircuitNet& net, Solution& solution);

    /**
     * Точки чертежа, где ток разветвляется: клеммы, концы и изломы проводов.
     * Точка, где провод идёт прямо, узлом не считается: ток там тот же.
     */
    static QVector<GridPoint> junctionPoints(const CircuitNet& net);
};

}  // namespace lab::circuit
