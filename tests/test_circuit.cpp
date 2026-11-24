#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "TestSupport.h"
#include "circuit/CircuitNet.h"
#include "circuit/MnaSolver.h"
#include "models/CircuitModel.h"

using lab::circuit::CircuitNet;
using lab::circuit::Drive;
using lab::circuit::GridPoint;
using lab::circuit::MnaSolver;
using lab::circuit::MnaSystem;
using lab::circuit::Part;
using lab::circuit::PartType;
using lab::circuit::Solution;
using lab::models::CircuitModel;
using lab::test::setSetup;

namespace {

/** Индексы готовых схем справочника. */
enum Preset {
    kPresetSingle = 0,     // источник и резистор
    kPresetSeries = 1,     // два резистора последовательно
    kPresetParallel = 2,   // три резистора параллельно
    kPresetBridge = 3,     // мост из пяти резисторов
    kPresetCharge = 4,     // заряд конденсатора
    kPresetDischarge = 5,  // разряд конденсатора
    kPresetCoil = 6,       // рост тока в катушке
};

/** Схема справочника с заданными параметрами опыта. */
void configure(CircuitModel& model, int preset, const QString& assignments = QString()) {
    ASSERT_TRUE(model.applyPreset(preset));
    if (!assignments.isEmpty())
        setSetup(model, assignments);
}

/** Элементы схемы данного типа. */
QVector<int> idsOfType(const CircuitModel& model, PartType type) {
    QVector<int> found;
    for (const Part& part : model.net().parts()) {
        if (part.type == type)
            found.push_back(part.id);
    }
    return found;
}

/** id единственного элемента данного типа; -1 — таких нет или их несколько. */
int onlyPart(const CircuitModel& model, PartType type) {
    const auto found = idsOfType(model, type);
    return found.size() == 1 ? found.first() : -1;
}

/**
 * Наибольшая невязка первого закона Кирхгофа по всем узлам: сумма токов,
 * втекающих в узел, должна обнуляться. Считается по рассчитанным токам ветвей
 * и узлам топологии, поэтому проверяет расчёт, а не формулу.
 */
double kirchhoffResidual(const CircuitModel& model, double t) {
    QVector<double> balance(size_t(std::max(1, model.net().nodeCount())), 0.0);
    for (const Part& part : model.net().parts()) {
        const auto nodes = model.net().branchNodes(part.id);
        const double current = model.currentOf(part.id, t);
        if (!std::isfinite(current))
            continue;
        if (nodes.first().node >= 0)
            balance[size_t(nodes.first().node)] -= current;   // ток вытекает из первой клеммы
        if (nodes.last().node >= 0)
            balance[size_t(nodes.last().node)] += current;    // и втекает во вторую
    }
    double worst = 0.0;
    for (double value : balance)
        worst = std::max(worst, std::abs(value));
    return worst;
}

/** Приращение запасённой энергии в единицу времени по соседним моментам. */
double storedPowerGain(const CircuitModel& model, double t, double h = 1e-4) {
    return (model.storedEnergyAt(t + h) - model.storedEnergyAt(t - h)) / (2.0 * h);
}

/** Точка сетки по номеру строки и столбца. */
GridPoint cell(int column, int row) {
    GridPoint point;
    point.column = column;
    point.row = row;
    return point;
}

}  // namespace

// ------------------------------------------------------------------
// Узлы: что соединено на чертеже
// ------------------------------------------------------------------

TEST(CircuitNet, WireEndsAndBendsBecomeOneNode) {
    CircuitNet net;
    const int resistor = net.addPart(PartType::Resistor, cell(4, 4), true, 100.0);
    ASSERT_GT(resistor, 0);
    // Провод с изломом: конец, изгиб и второй конец — один узел.
    ASSERT_GT(net.addWire({cell(2, 8), cell(2, 6), cell(4, 6)}), 0);
    ASSERT_GT(net.addWire({cell(4, 6), cell(6, 6)}), 0);
    EXPECT_GE(net.rebuildNodes(), 1);

    EXPECT_TRUE(net.connected(cell(2, 8), cell(6, 6)));
    EXPECT_EQ(net.nodeIndex(cell(2, 8)), net.nodeIndex(cell(4, 6)));
    EXPECT_EQ(net.nodeIndex(cell(2, 8)), net.nodeIndex(cell(6, 6)));
}

TEST(CircuitNet, StraightCrossingIsNotAConnection) {
    CircuitNet net;
    // Два провода пересекаются крестом, но каждый идёт по своей прямой.
    ASSERT_GT(net.addWire({cell(2, 5), cell(10, 5)}), 0);
    ASSERT_GT(net.addWire({cell(6, 1), cell(6, 9)}), 0);
    ASSERT_EQ(net.rebuildNodes(), 2);

    // Точка пересечения общая, а узлов два: электрического контакта нет.
    EXPECT_FALSE(net.connected(cell(2, 5), cell(6, 1)));
    EXPECT_EQ(net.nodeIndex(cell(2, 5)), net.nodeIndex(cell(10, 5)));
    EXPECT_NE(net.nodeIndex(cell(6, 5)), net.nodeIndex(cell(6, 1)));
}

TEST(CircuitNet, TeeJunctionIsAConnection) {
    CircuitNet net;
    ASSERT_GT(net.addWire({cell(2, 5), cell(10, 5)}), 0);
    // Конец второго провода упирается в середину первого — получается Т-стык.
    ASSERT_GT(net.addWire({cell(6, 5), cell(6, 9)}), 0);
    ASSERT_EQ(net.rebuildNodes(), 1);
    EXPECT_TRUE(net.connected(cell(2, 5), cell(6, 9)));
}

TEST(CircuitNet, TerminalsOnTheSameWireShareANode) {
    CircuitNet net;
    const int source = net.addPart(PartType::Source, cell(2, 4), true, 6.0);
    const int resistor = net.addPart(PartType::Resistor, cell(10, 4), true, 100.0);
    ASSERT_GT(source, 0);
    ASSERT_GT(resistor, 0);
    // Клеммы источника замкнуты на клеммы резистора двумя проводниками.
    ASSERT_GT(net.addWire({cell(4, 4), cell(10, 4)}), 0);
    ASSERT_GT(net.addWire({cell(2, 4), cell(2, 8), cell(12, 8), cell(12, 4)}), 0);
    ASSERT_GE(net.rebuildNodes(), 2);

    const auto nodes = net.branchNodes(source);
    ASSERT_EQ(nodes.size(), 2);
    const auto other = net.branchNodes(resistor);
    ASSERT_EQ(other.size(), 2);
    // Клеммы одной ветви всегда на разных узлах, а соответствия их — общие.
    EXPECT_NE(nodes.first().node, nodes.last().node);
    EXPECT_TRUE(nodes.first().node == other.first().node || nodes.first().node == other.last().node);
    EXPECT_TRUE(nodes.last().node == other.first().node || nodes.last().node == other.last().node);
}

TEST(CircuitNet, GroundIsOneOfTheSourceTerminals) {
    CircuitNet net;
    const int source = net.addPart(PartType::Source, cell(2, 4), true, 6.0);
    ASSERT_GT(source, 0);
    ASSERT_GT(net.addPart(PartType::Resistor, cell(8, 4), true, 100.0), 0);
    ASSERT_GT(net.addWire({cell(4, 4), cell(6, 4)}), 0);
    ASSERT_GT(net.addWire({cell(2, 4), cell(2, 7), cell(10, 7), cell(10, 4)}), 0);
    ASSERT_GE(net.rebuildNodes(), 2);

    const int ground = net.groundNode();
    const auto nodes = net.branchNodes(source);
    EXPECT_TRUE(ground == nodes.first().node || ground == nodes.last().node);
}

TEST(CircuitNet, FreeTerminalsAreTheUnconnectedPins) {
    CircuitNet net;
    ASSERT_GT(net.addPart(PartType::Resistor, cell(4, 4), true, 100.0), 0);
    net.rebuildNodes();
    // Обе клеммы ещё ни к чему не подключены.
    EXPECT_EQ(net.freeTerminals().size(), 2);
    ASSERT_GT(net.addWire({cell(4, 4), cell(4, 7)}), 0);
    net.rebuildNodes();
    EXPECT_EQ(net.freeTerminals().size(), 1);
}

// ------------------------------------------------------------------
// Метод узловых потенциалов как единственный расчёт
// ------------------------------------------------------------------

TEST(CircuitSolver, OhmsLawFollowsFromTheSystem) {
    CircuitNet net;
    const int source = net.addPart(PartType::Source, cell(2, 4), true, 6.0);
    const int resistor = net.addPart(PartType::Resistor, cell(8, 4), true, 200.0);
    ASSERT_GT(source, 0);
    ASSERT_GT(resistor, 0);
    ASSERT_GT(net.addWire({cell(4, 4), cell(8, 4)}), 0);
    ASSERT_GT(net.addWire({cell(2, 4), cell(2, 8), cell(10, 8), cell(10, 4)}), 0);
    ASSERT_GE(net.rebuildNodes(), 2);

    MnaSystem system;
    ASSERT_TRUE(system.compile(net, net.groundNode()));
    Drive drive(net);
    const Solution solution = MnaSolver::solve(system, net, drive);
    ASSERT_TRUE(solution.ok) << solution.message.toStdString();

    const Solution::Value* value = solution.value(resistor);
    ASSERT_NE(value, nullptr);
    EXPECT_TRUE(value->conducting);
    // U = φ₁ − φ₂ и I = U/R — то, что дал расчёт, а не подставленная формула.
    EXPECT_NEAR(value->voltage / value->current, 200.0, 1e-6);
    EXPECT_NEAR(std::abs(value->current), 6.0 / 200.0, 1e-9);
    EXPECT_NEAR(std::abs(value->power), std::abs(value->voltage * value->current), 1e-12);

    // Заземлённый узел имеет нулевой потенциал: от него отсчитывают все φ.
    bool grounded = false;
    for (int node = 0; node < system.nodeCount(); ++node) {
        if (system.nodeEquation(node) < 0) {
            EXPECT_NEAR(solution.nodePotentials.at(size_t(node)), 0.0, 1e-12);
            grounded = true;
        }
    }
    EXPECT_TRUE(grounded);
}

TEST(CircuitSolver, SolutionRespectsKirchhoffAtEveryNode) {
    CircuitModel model;
    configure(model, kPresetBridge);
    ASSERT_TRUE(model.validate().ok);
    // Ток источника не меньше нуля: схема отдаёт энергию, а не берёт.
    EXPECT_GT(model.sourceCurrentAt(1.0), 0.0);
    EXPECT_LT(kirchhoffResidual(model, 1.0), 1e-9);
    EXPECT_LT(kirchhoffResidual(model, 0.0), 1e-9);
}

TEST(CircuitSolver, SourceTerminalVoltageIsEmfMinusInternalDrop) {
    CircuitModel model;
    configure(model, kPresetSingle);
    model.setParam(QStringLiteral("internal"), 2.0);
    ASSERT_TRUE(model.validate().ok);

    const double current = model.sourceCurrentAt(1.0);
    const double emf = model.sourceEmf();
    // U = E − I·r — связь, из которой следует и «идеальный источник».
    EXPECT_NEAR(model.terminalVoltageAt(1.0), emf - current * 2.0, 1e-9);
    EXPECT_LT(model.terminalVoltageAt(1.0), emf);
}

TEST(CircuitSolver, EmfSliderDrivesEveryPresetCircuit) {
    for (int preset : {kPresetSingle, kPresetSeries, kPresetParallel, kPresetBridge}) {
        CircuitModel model;
        configure(model, preset);
        ASSERT_TRUE(model.validate().ok);
        const double before = model.sourceCurrentAt(1.0);
        const double emf = model.sourceEmf();
        ASSERT_GT(emf, 0.0);

        // Удвоение ЭДС удваивает все токи: цепь из сопротивлений линейна.
        model.setParam(QStringLiteral("emf"), std::min(24.0, 2.0 * emf));
        const double scale = std::min(2.0 * emf, 24.0) / emf;
        EXPECT_NEAR(model.sourceCurrentAt(1.0) / before, scale, 1e-6) << "пресет " << preset;
        // Сопротивление цепи от ЭДС не зависит.
        EXPECT_NEAR(model.equivalentResistanceAt(1.0), model.equivalentResistanceAt(0.2), 1e-9);
    }
}

// ------------------------------------------------------------------
// Готовые схемы: токи и сопротивления
// ------------------------------------------------------------------

TEST(CircuitPresets, EveryPresetIsValidCircuit) {
    for (int preset = 0; preset < CircuitModel::customPresetIndex(); ++preset) {
        CircuitModel model;
        configure(model, preset);
        const auto check = model.validate();
        EXPECT_TRUE(check.ok) << "пресет " << preset << ": " << check.message.toStdString();
        EXPECT_GT(model.net().nodeCount(), 1);
        // В каждой готовой схеме есть источник или заряженный конденсатор.
        EXPECT_FALSE(model.net().parts().isEmpty());
        EXPECT_LT(kirchhoffResidual(model, 0.5 * model.duration()), 1e-6);
    }
    // Последний пункт — «моя схема»: она пустая и расчёта не требует.
    EXPECT_EQ(CircuitModel::presetCount() - 1, CircuitModel::customPresetIndex());
    EXPECT_TRUE(CircuitModel::presetCells(CircuitModel::customPresetIndex()).isEmpty());
}

TEST(CircuitPresets, SingleResistorGivesFullCircuitCurrent) {
    CircuitModel model;
    configure(model, kPresetSingle);
    model.setParam(QStringLiteral("emf"), 6.0);
    model.setParam(QStringLiteral("internal"), 0.5);
    ASSERT_TRUE(model.validate().ok);

    const int resistor = onlyPart(model, PartType::Resistor);
    ASSERT_GT(resistor, 0);
    // I = E/(R + r) при R = 100 Ом из справочника.
    EXPECT_NEAR(model.currentOf(resistor, 1.0), 6.0 / 100.5, 1e-9);
    EXPECT_NEAR(model.terminalVoltageAt(1.0), 6.0 - model.sourceCurrentAt(1.0) * 0.5, 1e-9);
    EXPECT_NEAR(model.equivalentResistanceAt(1.0), 100.0, 1e-6);
}

TEST(CircuitPresets, SeriesResistorsAddUpAndCarryOneCurrent) {
    CircuitModel model;
    configure(model, kPresetSeries);
    ASSERT_TRUE(model.validate().ok);

    const auto resistors = idsOfType(model, PartType::Resistor);
    ASSERT_EQ(resistors.size(), 2);
    double total = 0.0;
    for (int id : resistors)
        total += model.net().findPart(id)->value;
    EXPECT_NEAR(model.equivalentResistanceAt(1.0), total, 1e-6);

    const double current = model.sourceCurrentAt(1.0);
    EXPECT_NEAR(current, model.sourceEmf() / (total + model.param(QStringLiteral("internal"))), 1e-9);
    double sum = 0.0;
    for (int id : resistors) {
        // Один ток на обеих ветвях — признак последовательного соединения.
        EXPECT_NEAR(model.currentOf(id, 1.0), current, 1e-9);
        EXPECT_GT(model.voltageOf(id, 1.0), 0.0);
        sum += model.voltageOf(id, 1.0);
    }
    // Напряжения ветвей в сумме дают напряжение на зажимах.
    EXPECT_NEAR(sum, model.terminalVoltageAt(1.0), 1e-9);
}

TEST(CircuitPresets, ParallelResistorsShareVoltageAndSplitCurrent) {
    CircuitModel model;
    configure(model, kPresetParallel);
    ASSERT_TRUE(model.validate().ok);

    const auto resistors = idsOfType(model, PartType::Resistor);
    ASSERT_EQ(resistors.size(), 3);
    // 1/R = 1/R₁ + 1/R₂ + 1/R₃ — следует из решения системы, а не из формулы.
    double inverse = 0.0;
    double sum = 0.0;
    for (int id : resistors) {
        const double resistance = model.net().findPart(id)->value;
        inverse += 1.0 / resistance;
        // Общее напряжение: у каждой ветви своё отношение U/I = R.
        EXPECT_NEAR(model.voltageOf(id, 1.0) / model.currentOf(id, 1.0), resistance, 1e-6);
        sum += model.currentOf(id, 1.0);
    }
    EXPECT_NEAR(model.equivalentResistanceAt(1.0), 1.0 / inverse, 1e-6);
    EXPECT_NEAR(sum, model.sourceCurrentAt(1.0), 1e-9);
    // Больше всего тока берёт самое малое сопротивление.
    EXPECT_GT(model.currentOf(resistors.first(), 1.0), model.currentOf(resistors.last(), 1.0));
}

TEST(CircuitPresets, BridgeIsNotReducibleToSeriesAndParallel) {
    CircuitModel model;
    configure(model, kPresetBridge);
    ASSERT_TRUE(model.validate().ok);

    const auto resistors = idsOfType(model, PartType::Resistor);
    ASSERT_EQ(resistors.size(), 5);
    const double measured = model.equivalentResistanceAt(1.0);

    // Ни складывание, ни параллельное соединение плеч этого числа не дают.
    const double series = model.net().findPart(resistors.first())->value +
                          model.net().findPart(resistors.last())->value;
    EXPECT_GT(std::abs(measured - series), 1.0);
    double naiveArms = 0.0;
    for (int i = 0; i + 1 < resistors.size(); i += 2)
        naiveArms += 1.0 / (model.net().findPart(resistors.at(size_t(i)))->value +
                           model.net().findPart(resistors.at(size_t(i + 1)))->value);
    EXPECT_GT(std::abs(measured - 1.0 / naiveArms), 1.0);

    // Мост не сбалансирован: через «перемычку» идёт заметный ток.
    double throughBridge = 0.0;
    for (int id : resistors)
        throughBridge = std::max(throughBridge, std::abs(model.currentOf(id, 1.0)));
    EXPECT_GT(throughBridge, 1e-4);
    // Но токи по-прежнему подчиняются Кирхгофу, а мощности сходятся.
    EXPECT_LT(kirchhoffResidual(model, 1.0), 1e-9);
    EXPECT_NEAR(model.sourcePowerAt(1.0), model.deliveredPowerAt(1.0), 1e-9);
}

TEST(CircuitPresets, PowerBalanceClosesOnComputedValues) {
    for (int preset : {kPresetSingle, kPresetSeries, kPresetParallel, kPresetBridge, kPresetCharge,
                       kPresetDischarge, kPresetCoil}) {
        CircuitModel model;
        configure(model, preset);
        ASSERT_TRUE(model.validate().ok) << "пресет " << preset;
        for (double part : {0.1, 0.4, 0.8}) {
            const double t = model.duration() * part;
            // Что отдал источник, то согрелось в цепи и отложилось в поле.
            const double balance = model.sourcePowerAt(t) - model.deliveredPowerAt(t) -
                                   storedPowerGain(model, t);
            EXPECT_NEAR(balance, 0.0, 1e-2 + 1e-2 * std::abs(model.sourcePowerAt(t)))
                << "пресет " << preset << ", t = " << t;
        }
    }
}

// ------------------------------------------------------------------
// Вольт-амперная характеристика источника
// ------------------------------------------------------------------

TEST(CircuitPresets, TerminalVersusCurrentSlopeIsInternalResistance) {
    CircuitModel model;
    configure(model, kPresetCoil);
    model.setParam(QStringLiteral("internal"), 3.0);
    ASSERT_TRUE(model.validate().ok);

    const QVector<QPointF> curve = model.terminalVersusCurrent();
    ASSERT_GE(curve.size(), 3);
    // U = E − I·r: все точки ряда лежат на прямой с наклоном −r.
    const double emf = model.sourceEmf();
    for (const QPointF& point : curve)
        EXPECT_NEAR(point.y() + 3.0 * point.x(), emf, 1e-6);
    const QPointF& first = curve.first();
    const QPointF& last = curve.last();
    ASSERT_GT(std::abs(last.x() - first.x()), 1e-9);
    const double slope = (last.y() - first.y()) / (last.x() - first.x());
    EXPECT_NEAR(slope, -3.0, 1e-6);
}

// ------------------------------------------------------------------
// Переходные процессы
// ------------------------------------------------------------------

TEST(CircuitTransient, CapacitorChargeStartsAtMaximumCurrent) {
    CircuitModel model;
    configure(model, kPresetCharge);
    ASSERT_TRUE(model.validate().ok);

    const int capacitor = onlyPart(model, PartType::Capacitor);
    ASSERT_GT(capacitor, 0);
    const int ballast = onlyPart(model, PartType::Resistor);
    ASSERT_GT(ballast, 0);

    // В первый момент разряженный конденсатор — перемычка: весь ток в нём.
    const double start = model.sourceEmf() /
                        (model.net().findPart(ballast)->value + model.param(QStringLiteral("internal")));
    EXPECT_NEAR(model.currentOf(capacitor, 0.0), start, 1e-9);
    EXPECT_NEAR(model.voltageOf(capacitor, 0.0), 0.0, 1e-9);

    // Ток убывает, напряжение растёт: процесс идёт в одну сторону.
    double previousCurrent = model.currentOf(capacitor, 0.0);
    double previousVoltage = 0.0;
    for (double t = 0.05; t <= model.duration(); t += 0.05) {
        const double current = model.currentOf(capacitor, t);
        const double voltage = model.voltageOf(capacitor, t);
        EXPECT_LE(current, previousCurrent + 1e-12) << "t = " << t;
        EXPECT_GE(voltage, previousVoltage - 1e-12) << "t = " << t;
        EXPECT_GE(voltage, -1e-12);
        previousCurrent = current;
        previousVoltage = voltage;
    }
    // К концу конденсатор заряжен почти до ЭДС, а ток стал ничтожно мал.
    EXPECT_NEAR(model.voltageOf(capacitor, model.duration()), model.sourceEmf(), 0.02);
    EXPECT_LT(model.currentOf(capacitor, model.duration()), 1e-3 * start);
}

TEST(CircuitTransient, ChargeTimeConstantComesFromComputedSeries) {
    CircuitModel model;
    configure(model, kPresetCharge);
    const int capacitor = onlyPart(model, PartType::Capacitor);
    ASSERT_GT(capacitor, 0);

    const double tau = model.timeConstantOf(capacitor);
    ASSERT_TRUE(std::isfinite(tau));
    EXPECT_GT(tau, 0.0);
    // τ — время, за которое величина прошла долю 1 − 1/e конечного приращения.
    const double start = model.voltageOf(capacitor, 0.0);
    const double finish = model.voltageOf(capacitor, model.duration());
    const double wanted = start + (finish - start) * (1.0 - 1.0 / std::exp(1.0));
    EXPECT_NEAR(model.voltageOf(capacitor, tau), wanted, 0.02 * std::abs(finish - start));
    // Через пять постоянных процесс практически закончен, и момент конца
    // ряда модель находит по тому, что состояние перестало двигаться.
    EXPECT_NEAR(model.voltageOf(capacitor, 5.0 * tau), finish, 0.02 * std::abs(finish - start));
    EXPECT_TRUE(std::isfinite(model.transientEndTime()));
    EXPECT_GT(model.transientEndTime(), 0.0);
    EXPECT_LE(model.transientEndTime(), model.duration());
}

TEST(CircuitTransient, DischargeDecaysWithoutSource) {
    CircuitModel model;
    configure(model, kPresetDischarge);
    ASSERT_TRUE(model.validate().ok);
    // Источник в этой схеме не нужен: его роль играет заряженный конденсатор.
    EXPECT_TRUE(idsOfType(model, PartType::Source).isEmpty());
    EXPECT_DOUBLE_EQ(model.sourceEmf(), 0.0);

    const int capacitor = onlyPart(model, PartType::Capacitor);
    const int load = onlyPart(model, PartType::Resistor);
    ASSERT_GT(capacitor, 0);
    ASSERT_GT(load, 0);

    const double initial = model.param(QStringLiteral("U0"));
    EXPECT_NEAR(model.voltageOf(capacitor, 0.0), initial, 1e-9);
    // Начальный ток задаёт только резистор: I₀ = U₀/R.
    EXPECT_NEAR(std::abs(model.currentOf(load, 0.0)), initial / model.net().findPart(load)->value, 1e-9);

    double previous = model.voltageOf(capacitor, 0.0);
    for (double t = 0.05; t <= model.duration(); t += 0.05) {
        const double voltage = model.voltageOf(capacitor, t);
        EXPECT_LE(voltage, previous + 1e-12) << "t = " << t;
        EXPECT_GE(voltage, 0.0);
        previous = voltage;
    }
    EXPECT_LT(model.voltageOf(capacitor, model.duration()), 0.01 * initial);
    // Запас поля за время разряда ушёл в резистор целиком.
    EXPECT_LT(model.storedEnergyAt(model.duration()), 1e-4 * model.storedEnergyAt(0.0));
}

TEST(CircuitTransient, CoilCurrentGrowsToSteadyValue) {
    CircuitModel model;
    configure(model, kPresetCoil);
    ASSERT_TRUE(model.validate().ok);

    const int coil = onlyPart(model, PartType::Inductor);
    const int ballast = onlyPart(model, PartType::Resistor);
    ASSERT_GT(coil, 0);
    ASSERT_GT(ballast, 0);

    // Ток катушки не начинается мгновенно: в нуле он равен начальному.
    EXPECT_NEAR(model.currentOf(coil, 0.0), model.param(QStringLiteral("I0")), 1e-12);
    double previous = model.currentOf(coil, 0.0);
    for (double t = 0.01; t <= model.duration(); t += 0.01) {
        const double current = model.currentOf(coil, t);
        EXPECT_GE(current, previous - 1e-12) << "t = " << t;
        previous = current;
    }
    // Установившийся ток определяет всё активное сопротивление цепи: амперметр
    // — тоже ветвь с сопротивлением, и он входит в сумму.
    const int meter = onlyPart(model, PartType::Ammeter);
    double ohmic = model.net().findPart(ballast)->value + model.param(QStringLiteral("internal"));
    if (meter > 0)
        ohmic += model.net().findPart(meter)->value;
    const double steady = model.currentOf(coil, model.duration());
    const double expected = model.sourceEmf() / ohmic;
    EXPECT_NEAR(steady, expected, 1e-3 * expected);
    // В установившемся режиме напряжение на идеальной катушке обнуляется.
    EXPECT_LT(std::abs(model.voltageOf(coil, model.duration())), 1e-2 * model.sourceEmf());

    const double tau = model.timeConstantOf(coil);
    ASSERT_TRUE(std::isfinite(tau));
    // τ = L/R — отношение индуктивности к сопротивлению цепи, по рассчитанному ряду.
    const double inductance = model.net().findPart(coil)->value;
    const double reference = inductance / ohmic;
    EXPECT_NEAR(tau, reference, 0.05 * reference);
}

TEST(CircuitTransient, EnergyStoredInFieldMatchesComputedValues) {
    CircuitModel charge;
    configure(charge, kPresetCharge);
    const int capacitor = onlyPart(charge, PartType::Capacitor);
    ASSERT_GT(capacitor, 0);
    {
        // Запас поля считают из рассчитанных напряжения и ёмкости.
        const double voltage = charge.voltageOf(capacitor, 1.0);
        const double capacitance = charge.net().findPart(capacitor)->value * 1e-6;   // значение в мкФ
        EXPECT_NEAR(charge.storedEnergyAt(1.0), 0.5 * capacitance * voltage * voltage, 1e-9);
    }
    EXPECT_GE(charge.storedEnergyAt(charge.duration()), charge.storedEnergyAt(0.0));

    CircuitModel coil;
    configure(coil, kPresetCoil);
    const int inductor = onlyPart(coil, PartType::Inductor);
    ASSERT_GT(inductor, 0);
    const double current = coil.currentOf(inductor, 0.5);
    const double inductance = coil.net().findPart(inductor)->value;
    EXPECT_NEAR(coil.storedEnergyAt(0.5), 0.5 * inductance * current * current, 1e-9);
}

TEST(CircuitTransient, SeriesForTransientAndStaticCircuits) {
    CircuitModel staticCircuit;
    configure(staticCircuit, kPresetSeries);
    const int resistor = idsOfType(staticCircuit, PartType::Resistor).first();
    const QVector<QPointF> flat = staticCircuit.branchCurrentSeries(resistor);
    ASSERT_GE(flat.size(), 2);
    // Цепь без реактивностей: ряд — горизонтальная линия на всём наблюдении.
    for (const QPointF& point : flat)
        EXPECT_NEAR(point.y(), flat.first().y(), 1e-12);
    EXPECT_NEAR(flat.first().x(), 0.0, 1e-12);
    EXPECT_NEAR(flat.last().x(), staticCircuit.duration(), 1e-9);

    CircuitModel charging;
    configure(charging, kPresetCharge);
    const int capacitor = onlyPart(charging, PartType::Capacitor);
    const QVector<QPointF> curve = charging.branchCurrentSeries(capacitor);
    ASSERT_GE(curve.size(), 10);
    // А в переходно цепи тот же метод даёт ряд, который явно меняется.
    EXPECT_GT(curve.first().y(), 10.0 * curve.last().y());
    EXPECT_GT(curve.last().x(), 0.5 * charging.duration());
}

// ------------------------------------------------------------------
// Ключ: две фазы расчёта
// ------------------------------------------------------------------

namespace {

/** Замкнутая цепь из источника, ключа и резистора, собранная инструментами. */
int buildSwitchedCircuit(CircuitModel& model) {
    EXPECT_FALSE(model.applyPreset(CircuitModel::customPresetIndex()));
    setSetup(model, QStringLiteral("tool=0"));       // источник ЭДС
    EXPECT_GT(model.placeAt(cell(2, 2)), 0);
    setSetup(model, QStringLiteral("tool=4"));       // ключ
    const int key = model.placeAt(cell(7, 2));
    EXPECT_GT(key, 0);
    setSetup(model, QStringLiteral("tool=1"));       // резистор
    EXPECT_GT(model.placeAt(cell(14, 2)), 0);
    // Соединение в петлю: два коротких участка и возврат по нижней линии.
    EXPECT_GT(model.wireBetween(cell(4, 2), cell(7, 2)), 0);
    EXPECT_GT(model.wireBetween(cell(9, 2), cell(14, 2)), 0);
    EXPECT_GT(model.wireBetween(cell(16, 2), cell(16, 6)), 0);
    EXPECT_GT(model.wireBetween(cell(16, 6), cell(2, 6)), 0);
    EXPECT_GT(model.wireBetween(cell(2, 6), cell(2, 2)), 0);
    return key;
}

}  // namespace

TEST(CircuitSwitch, CircuitIsSplitAtSwitchingMoment) {
    CircuitModel model;
    const int key = buildSwitchedCircuit(model);
    ASSERT_GT(key, 0);
    EXPECT_TRUE(model.hasSwitch());
    EXPECT_TRUE(model.hasTransient() == false);

    model.setParam(QStringLiteral("delay"), 1.0);
    EXPECT_FALSE(model.closedSwitchesAt(0.5).contains(key));
    EXPECT_TRUE(model.closedSwitchesAt(1.5).contains(key));

    // До t₀ ключ разомкнут: ток через него практически не идёт.
    EXPECT_LT(std::abs(model.sourceCurrentAt(0.2)), 1e-5);
    EXPECT_LT(std::abs(model.sourceCurrentAt(0.9)), 1e-5);
    // После t₀ цепь замкнута, и ток задают только сопротивления.
    const double steady = model.sourceCurrentAt(2.5);
    EXPECT_GT(steady, 1e-3);
    EXPECT_NEAR(steady, model.sourceEmf() / (100.0 + model.param(QStringLiteral("internal"))), 1e-3);
    // Момент переключения — событие задачи.
    EXPECT_NEAR(model.eventTime(), 1.0, 1e-9);
}

TEST(CircuitSwitch, OpeningStopsCurrentAfterDelay) {
    CircuitModel model;
    const int key = buildSwitchedCircuit(model);
    ASSERT_GT(key, 0);
    model.setParam(QStringLiteral("delay"), 1.0);
    model.setParam(QStringLiteral("mode"), 1.0);   // ключ размыкается при t₀

    EXPECT_TRUE(model.closedSwitchesAt(0.2).contains(key));
    EXPECT_FALSE(model.closedSwitchesAt(2.0).contains(key));
    EXPECT_GT(std::abs(model.sourceCurrentAt(0.2)), 1e-3);
    EXPECT_LT(std::abs(model.sourceCurrentAt(2.5)), 1e-5);
}

TEST(CircuitSwitch, CapacitorVoltageIsContinuousAcrossSwitching) {
    CircuitModel model;
    // Источник, ключ, резистор и конденсатор в одной петле: до t₀ цепь замкнута,
    // после размыкания — оборвана.
    ASSERT_FALSE(model.applyPreset(CircuitModel::customPresetIndex()));
    setSetup(model, QStringLiteral("tool=0"));
    ASSERT_GT(model.placeAt(cell(2, 2)), 0);
    setSetup(model, QStringLiteral("tool=4"));
    ASSERT_GT(model.placeAt(cell(7, 2)), 0);
    setSetup(model, QStringLiteral("tool=1; R=100"));
    ASSERT_GT(model.placeAt(cell(12, 2)), 0);          // балластный резистор
    setSetup(model, QStringLiteral("tool=2; C=200; U0=3; delay=1; mode=1"));
    const int capacitor = model.placeAt(cell(17, 2));
    ASSERT_GT(capacitor, 0);
    ASSERT_GT(model.wireBetween(cell(4, 2), cell(7, 2)), 0);
    ASSERT_GT(model.wireBetween(cell(9, 2), cell(12, 2)), 0);
    ASSERT_GT(model.wireBetween(cell(14, 2), cell(17, 2)), 0);
    ASSERT_GT(model.wireBetween(cell(19, 2), cell(20, 2)), 0);
    ASSERT_GT(model.wireBetween(cell(20, 2), cell(20, 6)), 0);
    ASSERT_GT(model.wireBetween(cell(20, 6), cell(2, 6)), 0);
    ASSERT_GT(model.wireBetween(cell(2, 6), cell(2, 2)), 0);
    ASSERT_TRUE(model.validate().ok);
    EXPECT_TRUE(model.hasTransient());
    EXPECT_NEAR(model.voltageOf(capacitor, 0.0), 3.0, 1e-9);

    const double before = model.voltageOf(capacitor, 1.0 - 1e-6);
    const double after = model.voltageOf(capacitor, 1.0 + 1e-6);
    ASSERT_TRUE(std::isfinite(before) && std::isfinite(after));
    // Напряжение конденсатора не скачет: скачок запретил бы физике, а не проверке.
    EXPECT_NEAR(before, after, 1e-6 * std::max(1.0, std::abs(after)));
    EXPECT_TRUE(std::isfinite(model.currentOf(capacitor, 1.0)));
    // А вот ток ветви меняет поведение: после размыкания ключа цепь оборвана.
    EXPECT_NEAR(model.currentOf(capacitor, 1.5), 0.0, 1e-6);
}

// ------------------------------------------------------------------
// Неверные схемы: сообщение, а не падение
// ------------------------------------------------------------------

TEST(CircuitInvalid, EmptyGridReportsWhatToFix) {
    CircuitModel model;
    model.applyPreset(CircuitModel::customPresetIndex());   // пустая сетка
    const auto empty = model.validate();
    EXPECT_FALSE(empty.ok);
    EXPECT_FALSE(empty.message.isEmpty());
    EXPECT_FALSE(empty.hint.isEmpty());
    // Расчёт на пустой схеме не роняет модель и не выдаёт чисел.
    const Solution solution = model.solutionAt(0.5);
    EXPECT_FALSE(solution.ok);
    EXPECT_TRUE(std::isnan(model.sourceCurrentAt(0.5)));
    EXPECT_TRUE(std::isnan(model.equivalentResistanceAt(0.5)));
    EXPECT_TRUE(model.readouts().size() >= 3);
    // Сцена с пустой схемой тоже рисуется без падения.
    EXPECT_TRUE(std::isnan(model.terminalVoltageAt(0.5)));
}

TEST(CircuitInvalid, ResistorWithoutSourceIsNotACircuit) {
    CircuitModel model;
    model.applyPreset(CircuitModel::customPresetIndex());
    setSetup(model, QStringLiteral("tool=1"));   // резистор
    ASSERT_GT(model.placeAt(cell(4, 4)), 0);
    // Резистор стоит, но источник в схему не поставлен: расчёт честно об этом говорит.
    const auto check = model.validate();
    EXPECT_FALSE(check.ok);
    EXPECT_FALSE(check.message.isEmpty());
    EXPECT_FALSE(check.hint.isEmpty());
}

TEST(CircuitInvalid, DanglingTerminalExplainsItself) {
    CircuitModel model;
    model.applyPreset(CircuitModel::customPresetIndex());
    setSetup(model, QStringLiteral("tool=0"));   // источник
    ASSERT_GT(model.placeAt(cell(2, 2)), 0);
    setSetup(model, QStringLiteral("tool=1"));   // и резистор, но их не соединять
    ASSERT_GT(model.placeAt(cell(10, 8)), 0);

    const auto check = model.validate();
    EXPECT_FALSE(check.ok);
    EXPECT_FALSE(check.message.isEmpty());
}

TEST(CircuitInvalid, ExtremeParametersStillCompute) {
    CircuitModel zero;
    configure(zero, kPresetSingle);
    zero.setParam(QStringLiteral("emf"), 0.5);   // нижняя граница слайдера
    if (zero.validate().ok) {
        EXPECT_TRUE(std::isfinite(zero.sourceCurrentAt(1.0)));
        EXPECT_GT(zero.sourceCurrentAt(1.0), 0.0);
    }

    CircuitModel big;
    configure(big, kPresetSingle);
    big.setParam(QStringLiteral("emf"), 24.0);
    big.setParam(QStringLiteral("internal"), 5.0);
    ASSERT_TRUE(big.validate().ok);
    EXPECT_TRUE(std::isfinite(big.sourceCurrentAt(1.0)));
    EXPECT_TRUE(std::isfinite(big.equivalentResistanceAt(1.0)));
    EXPECT_NEAR(big.terminalVoltageAt(1.0), 24.0 - big.sourceCurrentAt(1.0) * 5.0, 1e-9);
}

// ------------------------------------------------------------------
// Инструменты правки схемы
// ------------------------------------------------------------------

TEST(CircuitEditing, PlaceRotateAndRemove) {
    CircuitModel model;
    model.applyPreset(CircuitModel::customPresetIndex());
    setSetup(model, QStringLiteral("tool=1; R=150"));   // резистор

    const int id = model.placeAt(cell(3, 3));
    ASSERT_GT(id, 0);
    const Part* part = model.net().findPart(id);
    ASSERT_NE(part, nullptr);
    EXPECT_EQ(part->type, PartType::Resistor);
    EXPECT_NEAR(part->value, 150.0, 1e-9);
    EXPECT_TRUE(part->horizontal);
    EXPECT_EQ(model.net().partAt(cell(3, 3)), id);
    EXPECT_EQ(model.selectedPart(), id);

    // Поворот меняет ориентацию, не трогая значение.
    ASSERT_TRUE(model.rotateAt(cell(4, 3)));
    EXPECT_FALSE(model.net().findPart(id)->horizontal);
    EXPECT_NEAR(model.net().findPart(id)->value, 150.0, 1e-9);

    ASSERT_TRUE(model.removeAt(cell(3, 3)));
    EXPECT_TRUE(model.net().parts().isEmpty());
    EXPECT_EQ(model.net().partAt(cell(3, 3)), -1);
}

TEST(CircuitEditing, PlacingRespectsThePaletteValue) {
    CircuitModel model;
    model.applyPreset(CircuitModel::customPresetIndex());
    setSetup(model, QStringLiteral("tool=2; C=470"));   // конденсатор
    const int capacitor = model.placeAt(cell(5, 5));
    ASSERT_GT(capacitor, 0);
    EXPECT_EQ(model.net().findPart(capacitor)->type, PartType::Capacitor);
    EXPECT_NEAR(model.net().findPart(capacitor)->value, 470.0, 1e-9);

    setSetup(model, QStringLiteral("tool=3; L=1.5"));   // катушка
    const int coil = model.placeAt(cell(5, 9));
    ASSERT_GT(coil, 0);
    EXPECT_EQ(model.net().findPart(coil)->type, PartType::Inductor);

    // Занятое место не плодит двойников: второй элемент либо встаёт иначе,
    // либо не ставится вовсе.
    const int again = model.placeAt(cell(5, 5));
    if (again > 0)
        EXPECT_FALSE(model.net().findPart(again)->horizontal);
    // За сеткой места нет.
    EXPECT_LT(model.placeAt(cell(model.gridColumns() + 4, 2)), 0);
}

TEST(CircuitEditing, TerminalClicksConnectAndDisconnect) {
    CircuitModel model;
    model.applyPreset(CircuitModel::customPresetIndex());
    setSetup(model, QStringLiteral("tool=1"));
    ASSERT_GT(model.placeAt(cell(2, 2)), 0);
    ASSERT_GT(model.placeAt(cell(8, 2)), 0);

    model.pickTerminal(cell(4, 2));
    ASSERT_TRUE(model.hasPendingTerminal());
    EXPECT_EQ(model.pendingTerminal(), cell(4, 2));
    model.pickTerminal(cell(8, 2));
    EXPECT_FALSE(model.hasPendingTerminal());

    // Соединение состоялось: клеммы теперь один узел (узлы считает расчёт).
    model.validate();
    EXPECT_TRUE(model.net().connected(cell(4, 2), cell(8, 2)));

    // Тот же клик по соединённой паре соединение снимает.
    model.pickTerminal(cell(4, 2));
    model.pickTerminal(cell(8, 2));
    model.validate();
    EXPECT_FALSE(model.net().connected(cell(4, 2), cell(8, 2)));

    // Незамкнутый клик отменяется, чтобы подсказка не висела на чертеже.
    model.pickTerminal(cell(2, 2));
    EXPECT_TRUE(model.hasPendingTerminal());
    model.cancelPendingTerminal();
    EXPECT_FALSE(model.hasPendingTerminal());
}

TEST(CircuitEditing, SelectionDrivesHandlesAndReadouts) {
    CircuitModel model;
    configure(model, kPresetSeries);
    const auto resistors = idsOfType(model, PartType::Resistor);
    ASSERT_FALSE(resistors.isEmpty());
    model.setSelectedPart(resistors.first());
    EXPECT_EQ(model.selectedPart(), resistors.first());

    EXPECT_EQ(model.partNames().size(), model.net().parts().size());
    EXPECT_FALSE(model.partNames().isEmpty());

    // Ручка значения появляется только после выбора элемента.
    bool valueHandle = false;
    for (const lab::HandleInfo& handle : model.handles())
        valueHandle = valueHandle || handle.id == QLatin1String("value");
    EXPECT_TRUE(valueHandle);

    // Ручка меняет и элемент, и расчёт.
    const double before = model.currentOf(resistors.first(), 1.0);
    model.setSelectedPart(resistors.first());
    model.setHandleValue(QStringLiteral("value"), 400.0);
    EXPECT_NEAR(model.net().findPart(resistors.first())->value, 400.0, 1e-9);
    EXPECT_LT(model.currentOf(resistors.first(), 1.0), before);
}

TEST(CircuitEditing, ClearingCircuitEmptiesTheGrid) {
    CircuitModel model;
    configure(model, kPresetBridge);
    ASSERT_FALSE(model.net().parts().isEmpty());
    model.clearCircuit();
    EXPECT_TRUE(model.net().parts().isEmpty());
    EXPECT_TRUE(model.net().wires().isEmpty());
    EXPECT_FALSE(model.validate().ok);
}

TEST(CircuitEditing, PaletteCoversEveryPartType) {
    EXPECT_GE(CircuitModel::toolCount(), 9);
    for (int index = 0; index < CircuitModel::toolCount(); ++index) {
        EXPECT_FALSE(CircuitModel::toolName(index).isEmpty());
        EXPECT_FALSE(CircuitModel::presetName(index).isEmpty());
    }
}

// ------------------------------------------------------------------
// Вывод для учебной панели
// ------------------------------------------------------------------

TEST(CircuitOutput, ReadoutsDescribeTheComputedCircuit) {
    CircuitModel model;
    configure(model, kPresetSeries);
    ASSERT_TRUE(model.validate().ok);

    const auto readouts = model.readouts();
    EXPECT_GE(readouts.size(), 8);
    bool hasCurrent = false;
    bool hasVoltage = false;
    for (const lab::Readout& readout : readouts) {
        EXPECT_FALSE(readout.label.isEmpty());
        EXPECT_FALSE(readout.value.isEmpty());
        hasCurrent = hasCurrent || readout.value.contains(QStringLiteral("А"));
        hasVoltage = hasVoltage || readout.value.contains(QStringLiteral("В"));
    }
    EXPECT_TRUE(hasCurrent);
    EXPECT_TRUE(hasVoltage);
}

TEST(CircuitOutput, DerivationGoesFromKirchhoffToAnswer) {
    CircuitModel model;
    configure(model, kPresetParallel);
    const lab::Derivation derivation = model.derivation();
    ASSERT_TRUE(derivation.valid);
    EXPECT_FALSE(derivation.problemText.isEmpty());
    EXPECT_FALSE(derivation.unknown.isEmpty());
    ASSERT_GE(derivation.steps.size(), 4);

    bool kirchhoff = false;
    bool answer = false;
    for (const lab::DerivationStep& step : derivation.steps) {
        EXPECT_FALSE(step.kind.isEmpty());
        EXPECT_FALSE(step.content.isEmpty());
        kirchhoff = kirchhoff || step.kind.contains(QStringLiteral("Кирхгоф"));
        answer = answer || step.kind == QStringLiteral("Ответ");
    }
    EXPECT_TRUE(kirchhoff);
    EXPECT_TRUE(answer);
    EXPECT_FALSE(derivation.answerTemplate.isEmpty());
}

TEST(CircuitOutput, InvalidCircuitStillExplainsItself) {
    CircuitModel model;
    model.applyPreset(CircuitModel::customPresetIndex());
    const lab::Derivation derivation = model.derivation();
    EXPECT_FALSE(derivation.valid);
    ASSERT_GE(derivation.steps.size(), 1);
    EXPECT_FALSE(derivation.steps.first().content.isEmpty());
}

TEST(CircuitOutput, FormatHelpersKeepSmallAndLargeValuesReadable) {
    EXPECT_EQ(CircuitModel::currentText(0.0), QStringLiteral("0 А"));
    EXPECT_TRUE(CircuitModel::currentText(0.0125).contains(QStringLiteral("мА")));
    EXPECT_TRUE(CircuitModel::currentText(0.000125).contains(QStringLiteral("мкА")));
    EXPECT_TRUE(CircuitModel::currentText(1.5e-6).contains(QStringLiteral("мкА")));
    EXPECT_EQ(CircuitModel::voltageText(0.0), QStringLiteral("0 В"));
    EXPECT_TRUE(CircuitModel::voltageText(0.0012).contains(QStringLiteral("мВ")));
    EXPECT_TRUE(CircuitModel::resistanceText(4.7e6).contains(QStringLiteral("МОм")));
    EXPECT_TRUE(CircuitModel::resistanceText(4700.0).contains(QStringLiteral("кОм")));
    EXPECT_EQ(CircuitModel::resistanceText(-1.0), QStringLiteral("—"));
    EXPECT_EQ(CircuitModel::currentText(std::numeric_limits<double>::quiet_NaN()), QStringLiteral("—"));
}

TEST(CircuitOutput, ModelTextsAreFilledIn) {
    CircuitModel model;
    configure(model, kPresetSingle);
    EXPECT_FALSE(model.title().isEmpty());
    EXPECT_FALSE(model.subtitle().isEmpty());
    EXPECT_FALSE(model.statement().isEmpty());
    EXPECT_FALSE(model.problemText().isEmpty());
    EXPECT_EQ(model.id(), QStringLiteral("circuit"));
    EXPECT_GT(model.duration(), 0.0);
    EXPECT_GT(model.stepSize(), 0.0);
    EXPECT_LT(model.stepSize(), model.duration());
    EXPECT_GT(model.timePerSecond(), 0.0);

    const auto specs = model.paramSpecs();
    ASSERT_GE(specs.size(), 8);
    bool presetChoice = false;
    bool toolChoice = false;
    for (const lab::ParamSpec& spec : specs) {
        EXPECT_FALSE(spec.label.isEmpty());
        presetChoice = presetChoice || spec.id == QLatin1String("preset");
        toolChoice = toolChoice || spec.id == QLatin1String("tool");
    }
    EXPECT_TRUE(presetChoice);
    EXPECT_TRUE(toolChoice);
}

TEST(CircuitOutput, StateAtDescribesTheSameInstantAsReadouts) {
    CircuitModel model;
    configure(model, kPresetParallel);
    for (double t : {0.0, 0.3, 1.7, model.duration()}) {
        const lab::SimState state = model.stateAt(t);
        EXPECT_NEAR(state.time, t, 1e-9);
        EXPECT_TRUE(std::isfinite(state.primary));
        for (const lab::EntityVisual& entity : state.entities)
            EXPECT_TRUE(std::isfinite(entity.position));
    }
}
