#include "models/CircuitModel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "utils/MathText.h"

namespace lab::models {
namespace {

using circuit::CircuitNet;
using circuit::Drive;
using circuit::GridPoint;
using circuit::MnaSolver;
using circuit::MnaSystem;
using circuit::Part;
using circuit::PartType;
using circuit::Solution;

/** Точек в рядах, которые уходят на графики. */
constexpr int kPlotPoints = 241;

/**
 * Относительное изменение состояния за шаг, при котором переходный процесс
 * считают завершённым. Состояние меняется по экспоненте, и её производная
 * пропорциональна самому состоянию: когда сдвиг стал в сто тысяч раз меньше
 * размаха процесса, прошло больше пяти постоянных времени, и дальше величина
 * не меняется заметным образом.
 */
constexpr double kSettleRatio = 1.0e-5;

/** Вспомогательный слайдер. */
ParamSpec slider(const QString& id, const QString& label, const QString& unit, double minValue, double maxValue,
                 double step, int decimals, bool allowZero, double defaultValue, const QString& group,
                 const QString& help) {
    ParamSpec spec;
    spec.id = id;
    spec.label = label;
    spec.unit = unit;
    spec.kind = ParamKind::Slider;
    spec.minValue = minValue;
    spec.maxValue = maxValue;
    spec.stepValue = step;
    spec.decimals = double(decimals);
    spec.allowZero = allowZero;
    spec.defaultValue = defaultValue;
    spec.group = group;
    spec.helpText = help;
    return spec;
}

/** Вспомогательный список. */
ParamSpec choice(const QString& id, const QString& label, const QStringList& items, const QString& group,
                 const QString& help, int defaultIndex) {
    ParamSpec spec;
    spec.id = id;
    spec.label = label;
    spec.kind = ParamKind::Choice;
    spec.choices = items;
    spec.minValue = 0.0;
    spec.maxValue = double(items.size() - 1);
    spec.stepValue = 1.0;
    spec.decimals = 0.0;
    spec.defaultValue = double(defaultIndex);
    spec.group = group;
    spec.helpText = help;
    return spec;
}

/** Границы значения элемента по типу: для ручки, палитры и проверки. */
QPair<double, double> partValueRange(PartType type) {
    switch (type) {
    case PartType::Source:    return {0.5, 24.0};
    case PartType::Resistor:  return {1.0, 200000.0};
    case PartType::Capacitor: return {10.0, 100000.0};
    case PartType::Inductor:  return {0.05, 50.0};
    case PartType::Switch:    return {0.0, 5.0};
    case PartType::Ammeter:   return {circuit::parts::kClosedResistance, circuit::parts::kClosedResistance};
    case PartType::Voltmeter: return {circuit::parts::kVoltmeterResistance, circuit::parts::kVoltmeterResistance};
    }
    return {0.0, 1.0};
}

/**
 * Готовые схемы справочника. Их собирают теми же кликами по сетке, что и свою
 * схему: список только экономит время, а расчёт для любой из них один и тот же.
 *
 * Элемент занимает два шага сетки: первая клемма в своей клетке, вторая — через
 * две клетки по направлению элемента. Поэтому провода в пресетах задают по
 * клеммам, а не «на глаз»: `first` и `second` ниже возвращают ровно те клетки,
 * которые модель считает концами элемента. Контур в каждой схеме замкнут по
 * построению: последний провод всегда возвращается к первой клемме источника.
 */
constexpr int kGridColumns = 23;
constexpr int kGridRows = 12;
constexpr int kReturnRow = kGridRows - 2;  // нижняя линия возвращающего провода

using PresetSpec = CircuitModel::PresetSpec;
using PresetCell = CircuitModel::PresetCell;
using PresetWire = CircuitModel::PresetWire;

PresetCell addPart(PresetSpec& s, PartType type, int column, int row, bool horizontal,
                   double value = 0.0) {
    PresetCell cell;
    cell.type = type;
    cell.column = column;
    cell.row = row;
    cell.horizontal = horizontal;
    cell.value = value;
    s.cells.push_back(cell);
    return cell;
}

// Клеммы элемента: первая в его клетке, вторая — через два шага сетки.
QPoint firstTerminal(const PresetCell& cell) { return QPoint(cell.column, cell.row); }

QPoint secondTerminal(const PresetCell& cell) {
    return cell.horizontal ? QPoint(cell.column + 2, cell.row)
                           : QPoint(cell.column, cell.row + 2);
}

void addRun(PresetSpec& s, const QVector<QPoint>& points) {
    PresetWire wire;
    for (const QPoint& point : points) {
        circuit::GridPoint cell;
        cell.column = point.x();
        cell.row = point.y();
        wire.points.push_back(cell);
    }
    s.wires.push_back(wire);
}

// Возврат к первой клемме источника по нижней линии сетки.
void returnToSource(PresetSpec& s, QPoint from, QPoint sourceFirst) {
    addRun(s, {from, QPoint(from.x(), kReturnRow), QPoint(sourceFirst.x(), kReturnRow),
                sourceFirst});
}

// 1. Полная цепь: источник с внутренним сопротивлением и резистор.
//    I = E/(R + r), напряжение на зажимах чуть меньше ЭДС.
PresetSpec simpleCircuit() {
    PresetSpec s;
    s.name = QStringLiteral("полная цепь: источник и резистор");
    const PresetCell src = addPart(s, PartType::Source, 2, 2, true, 12.0);
    const PresetCell load = addPart(s, PartType::Resistor, 19, 6, false, 100.0);
    addRun(s, {secondTerminal(src), QPoint(19, 2), firstTerminal(load)});
    returnToSource(s, secondTerminal(load), firstTerminal(src));
    return s;
}

// 2. Последовательная цепь: узлов между элементами нет, ток через все один.
PresetSpec seriesCircuit() {
    PresetSpec s;
    s.name = QStringLiteral("два резистора последовательно");
    const PresetCell src = addPart(s, PartType::Source, 2, 2, true, 12.0);
    const PresetCell lamp = addPart(s, PartType::Resistor, 7, 2, true, 40.0);
    const PresetCell load = addPart(s, PartType::Resistor, 12, 2, true, 100.0);
    addRun(s, {secondTerminal(src), firstTerminal(lamp)});
    addRun(s, {secondTerminal(lamp), firstTerminal(load)});
    returnToSource(s, secondTerminal(load), firstTerminal(src));
    return s;
}

// 3. Параллельное соединение: верхний узел разветвляет ток, нижний собирает.
PresetSpec parallelCircuit() {
    PresetSpec s;
    s.name = QStringLiteral("три резистора параллельно");
    const PresetCell src = addPart(s, PartType::Source, 2, 2, true, 12.0);
    const PresetCell r1 = addPart(s, PartType::Resistor, 8, 5, false, 100.0);
    const PresetCell r2 = addPart(s, PartType::Resistor, 13, 5, false, 200.0);
    const PresetCell r3 = addPart(s, PartType::Resistor, 18, 5, false, 300.0);
    addRun(s, {secondTerminal(src), QPoint(18, 2)});  // верхняя шина
    for (const PresetCell* branch : {&r1, &r2, &r3}) {
        addRun(s, {QPoint(branch->column, 2), firstTerminal(*branch)});
        addRun(s, {secondTerminal(*branch), QPoint(branch->column, kReturnRow)});
    }
    addRun(s, {QPoint(18, kReturnRow), QPoint(2, kReturnRow), firstTerminal(src)});
    return s;
}

// 4. Мост: здесь нечем сворачивать сопротивления — только узловые уравнения.
//    Верхняя и нижняя шины — прямые горизонтальные провода; ноги ставят
//    вертикальными элементами так, чтобы их клеммы ложились точно на шины.
PresetSpec bridgeCircuit() {
    PresetSpec s;
    s.name = QStringLiteral("мост из пяти резисторов");
    const PresetCell src = addPart(s, PartType::Source, 2, 6, true, 12.0);
    const PresetCell upperLeft = addPart(s, PartType::Resistor, 8, 1, false, 100.0);
    const PresetCell lowerLeft = addPart(s, PartType::Resistor, 8, 8, false, 200.0);
    const PresetCell upperRight = addPart(s, PartType::Resistor, 16, 1, false, 100.0);
    const PresetCell lowerRight = addPart(s, PartType::Resistor, 16, 8, false, 300.0);
    const PresetCell bridge = addPart(s, PartType::Resistor, 11, 6, true, 50.0);

    addRun(s, {QPoint(4, 0), QPoint(17, 0)});            // верхняя шина
    addRun(s, {secondTerminal(src), QPoint(4, 0)});
    addRun(s, {firstTerminal(upperLeft), QPoint(8, 0)});
    addRun(s, {firstTerminal(upperRight), QPoint(16, 0)});
    addRun(s, {firstTerminal(src), QPoint(2, 9), QPoint(17, 9)});  // нижняя шина
    addRun(s, {secondTerminal(lowerLeft), QPoint(8, 9)});
    addRun(s, {secondTerminal(lowerRight), QPoint(16, 9)});
    // Средняя точка левой ноги: вывод верхнего резистора, вывод нижнего и
    // левая клемма мостового резистора — одна точка.
    addRun(s, {secondTerminal(upperLeft), QPoint(8, 6), firstTerminal(bridge)});
    addRun(s, {firstTerminal(lowerLeft), QPoint(8, 6)});
    // То же справа.
    addRun(s, {secondTerminal(upperRight), QPoint(16, 6), secondTerminal(bridge)});
    addRun(s, {firstTerminal(lowerRight), QPoint(16, 6)});
    return s;
}

// 5. Заряд конденсатора через резистор: τ = RC, ток убывает, напряжение растёт.
PresetSpec chargingCircuit() {
    PresetSpec s;
    s.name = QStringLiteral("заряд конденсатора (RC)");
    const PresetCell src = addPart(s, PartType::Source, 2, 2, true, 6.0);
    const PresetCell ballast = addPart(s, PartType::Resistor, 7, 2, true, 10000.0);
    const PresetCell cap = addPart(s, PartType::Capacitor, 16, 6, false, 200.0);
    const PresetCell meter = addPart(s, PartType::Voltmeter, 20, 6, false,
                                      circuit::parts::kVoltmeterResistance);
    addRun(s, {secondTerminal(src), firstTerminal(ballast)});
    addRun(s, {secondTerminal(ballast), QPoint(16, 2), firstTerminal(cap)});
    returnToSource(s, secondTerminal(cap), firstTerminal(src));
    addRun(s, {firstTerminal(meter), QPoint(18, 6), firstTerminal(cap)});
    addRun(s, {secondTerminal(meter), QPoint(18, 8), secondTerminal(cap)});
    s.window = 16.0;   // восемь постоянных времени: процесс виден целиком
    return s;
}

// 6. Разряд конденсатора на резистор: источника ЭДС нет, ток даёт заряд.
PresetSpec dischargingCircuit() {
    PresetSpec s;
    s.name = QStringLiteral("разряд конденсатора на резистор");
    const PresetCell cap = addPart(s, PartType::Capacitor, 6, 5, false, 500.0);
    const PresetCell load = addPart(s, PartType::Resistor, 14, 5, false, 2000.0);
    addRun(s, {firstTerminal(cap), firstTerminal(load)});
    addRun(s, {secondTerminal(cap), secondTerminal(load)});
    s.capacitorVoltage = 6.0;   // конденсатор заряжен до старта
    s.window = 8.0;
    return s;
}

// 7. Рост тока в катушке: ток устанавливается за τ = L/(R + r).
PresetSpec inductorCircuit() {
    PresetSpec s;
    s.name = QStringLiteral("рост тока в катушке (RL)");
    const PresetCell src = addPart(s, PartType::Source, 2, 2, true, 5.0);
    const PresetCell ballast = addPart(s, PartType::Resistor, 7, 2, true, 20.0);
    const PresetCell coil = addPart(s, PartType::Inductor, 12, 2, true, 2.0);
    const PresetCell meter = addPart(s, PartType::Ammeter, 17, 6, false,
                                      circuit::parts::kClosedResistance);
    addRun(s, {secondTerminal(src), firstTerminal(ballast)});
    addRun(s, {secondTerminal(ballast), firstTerminal(coil)});
    addRun(s, {secondTerminal(coil), QPoint(17, 2), firstTerminal(meter)});
    returnToSource(s, secondTerminal(meter), firstTerminal(src));
    s.window = 1.0;
    return s;
}

const QVector<PresetSpec>& presetTable() {
    static const QVector<PresetSpec> table = {simpleCircuit(),
                                             seriesCircuit(),
                                             parallelCircuit(),
                                             bridgeCircuit(),
                                             chargingCircuit(),
                                             dischargingCircuit(),
                                             inductorCircuit()};
    return table;
}


}  // namespace

// ------------------------------------------------------------------
// Построение модели
// ------------------------------------------------------------------

CircuitModel::CircuitModel(QObject* parent) : Simulation(parent) {
    seedDefaults();
    m_net.setGridSize(kGridColumns, kGridRows);
    m_appliedPreset = -1;
    applyPreset(0);
    m_appliedPreset = 0;
}

QString CircuitModel::title() const { return QStringLiteral("Конструктор электрической цепи"); }

QString CircuitModel::subtitle() const {
    return QStringLiteral("Сборка схемы на сетке и расчёт по методу узловых потенциалов");
}

QString CircuitModel::statement() const {
    return QStringLiteral(
        "Соберите цепь из источника, резисторов и приборов. Найдите ток в каждой ветви, напряжение "
        "на зажимах источника и мощность, которую он отдаёт. Если в цепи есть конденсатор или "
        "катушка, проследите, как меняется ток после переключения ключа.");
}

QString CircuitModel::problemText() const {
    const QStringList names = partNames();
    QString text = QStringLiteral("Схема «%1»").arg(presetName(int(paramChoiceIndex(QStringLiteral("preset")))));
    if (!names.isEmpty())
        text += QStringLiteral(": %1").arg(names.join(QStringLiteral(", ")));
    text += QStringLiteral(". Внутреннее сопротивление источника r = %1 Ом.")
                 .arg(utils::formatNumber(internalResistance(), 2));
    const Solution solution = currentSolution();
    const double end = transientEndTime();
    if (hasTransient() && std::isfinite(end)) {
        text += QStringLiteral(" Через какое время после переключения ключа ток перестанет меняться и каков "
                               "его установившееся значение?");
    } else {
        text += QStringLiteral(" Определите токи ветвей, напряжение на зажимах источника и мощность, "
                               "которую он отдаёт.");
    }
    Q_UNUSED(solution);
    return text;
}

QStringList CircuitModel::partNames() const {
    QStringList names;
    for (const Part& part : m_net.parts()) {
        names.push_back(QStringLiteral("%1 — %2 %3").arg(part.name, circuit::partWord(part.type),
                                                          partValueText(part.id)));
    }
    return names;
}

// ------------------------------------------------------------------
// Готовые схемы
// ------------------------------------------------------------------

const CircuitModel::PresetSpec* CircuitModel::presetSpec(int index) {
    const QVector<PresetSpec>& table = presetTable();
    if (index < 0 || index >= table.size())
        return nullptr;
    return &table.at(index);
}

int CircuitModel::presetCount() { return presetTable().size() + 1; }

QString CircuitModel::presetName(int index) {
    const PresetSpec* spec = presetSpec(index);
    return spec ? spec->name : QStringLiteral("моя схема — собирать самому");
}

int CircuitModel::customPresetIndex() { return presetTable().size(); }

QVector<CircuitModel::PresetCell> CircuitModel::presetCells(int index) {
    const PresetSpec* spec = presetSpec(index);
    if (!spec)
        return {};
    return QVector<PresetCell>(spec->cells.begin(), spec->cells.end());
}

bool CircuitModel::applyPreset(int index) {
    cancelPendingTerminal();
    m_selected = -1;
    m_net.clear();
    const PresetSpec* spec = presetSpec(index);
    if (!spec) {
        // Пункт «моя схема»: сетка пустая, расчёт начнётся, когда появятся ветви.
        touchTopology();
        return false;
    }
    for (const PresetCell& cell : presetCells(index))
        m_net.addPart(cell.type, GridPoint{cell.column, cell.row}, cell.horizontal, cell.value);
    // ЭДС источника задаёт слайдер, поэтому пресет переносит в него своё
    // напряжение: ползунок остаётся рабочим и на готовой схеме.
    for (const PresetCell& cell : spec->cells) {
        if (cell.type == PartType::Source) {
            setParamRaw(QStringLiteral("emf"), std::clamp(cell.value, 0.5, 24.0));
            break;
        }
    }
    for (const PresetWire& wire : spec->wires)
        m_net.addWire(wire.points);
    m_net.rebuildNodes();

    setParamRaw(QStringLiteral("T"), spec->window);
    setParamRaw(QStringLiteral("delay"), spec->delay);
    // Значение ключа — момент его переключения: его держат и параметр, и элемент.
    for (const Part& part : m_net.parts()) {
        if (part.type == PartType::Switch)
            m_net.setPartValue(part.id, std::max(0.05, std::clamp(spec->delay, 0.05, 5.0)));
    }
    setParamRaw(QStringLiteral("U0"), spec->capacitorVoltage);
    setParamRaw(QStringLiteral("I0"), spec->inductorCurrent);
    touchTopology();
    emit circuitChanged();
    return true;
}

// ------------------------------------------------------------------
// Параметры
// ------------------------------------------------------------------

QVector<ParamSpec> CircuitModel::paramSpecs() const {
    QVector<ParamSpec> specs;
    const QString scheme = QStringLiteral("Схема");
    const QString values = QStringLiteral("Значения новых элементов");
    const QString process = QStringLiteral("Переходный процесс");

    QStringList presets;
    for (int index = 0; index < presetCount(); ++index)
        presets.push_back(presetName(index));
    specs.push_back(choice(QStringLiteral("preset"), QStringLiteral("Схема"), presets, scheme,
                           QStringLiteral("Готовые схемы стоят на той же сетке, что и собранная вручную; "
                                          "пункт «моя схема» оставляет то, что вы поставили сами"),
                           0));

    QStringList tools;
    for (int index = 0; index < toolCount(); ++index)
        tools.push_back(toolName(index));
    specs.push_back(choice(QStringLiteral("tool"), QStringLiteral("Палитра"), tools, scheme,
                           QStringLiteral("Выберите элемент или инструмент и кликните по сетке"), 1));

    specs.push_back(slider(QStringLiteral("emf"), QStringLiteral("ЭДС источника E"), QStringLiteral("В"), 0.5, 24.0,
                           0.1, 2, false, 6.0, values,
                           QStringLiteral("ЭДС задаёт напряжение на узлах: ток полной цепи I = E/(R + r)")));
    specs.push_back(slider(QStringLiteral("internal"), QStringLiteral("Внутреннее сопротивление r"),
                           QStringLiteral("Ом"), 0.0, 5.0, 0.05, 2, true, 0.5, values,
                           QStringLiteral("Сопротивление самого источника: напряжение на зажимах U = E − I·r")));
    specs.push_back(slider(QStringLiteral("R"), QStringLiteral("Сопротивление нового резистора"),
                           QStringLiteral("Ом"), 1.0, 2000.0, 1.0, 0, false, 100.0, values,
                           QStringLiteral("Значение ставят на сетку; у стоящего элемента его меняют ручкой")));
    specs.push_back(slider(QStringLiteral("C"), QStringLiteral("Ёмкость нового конденсатора"), QStringLiteral("мкФ"),
                           10.0, 100000.0, 10.0, 0, false, 1000.0, values,
                           QStringLiteral("q = C·U, а ток заряда I = C·dU/dt — из-за него ток меняется со временем")));
    specs.push_back(slider(QStringLiteral("L"), QStringLiteral("Индуктивность новой катушки"), QStringLiteral("Гн"),
                           0.05, 50.0, 0.05, 2, false, 2.0, values,
                           QStringLiteral("U = L·dI/dt: ток в катушке не может измениться скачком")));

    QStringList modes;
    modes.push_back(QStringLiteral("ключ замыкается при t₀"));
    modes.push_back(QStringLiteral("ключ размыкается при t₀"));
    specs.push_back(choice(QStringLiteral("mode"), QStringLiteral("Ключ"), modes, process,
                           QStringLiteral("До t₀ ключ в одном состоянии, после — в другом"), 0));
    specs.push_back(slider(QStringLiteral("delay"), QStringLiteral("Момент переключения t₀"), QStringLiteral("с"), 0.0,
                           5.0, 0.05, 2, true, 0.5, process,
                           QStringLiteral("Время, с которого ключ меняет состояние: до него цепь в другом режиме")));
    specs.push_back(slider(QStringLiteral("U0"), QStringLiteral("Начальное напряжение конденсатора U₀"),
                           QStringLiteral("В"), 0.0, 24.0, 0.1, 2, true, 0.0, process,
                           QStringLiteral("С каким зарядом конденсатор входит в опыт: по нему считают разряд")));
    specs.push_back(slider(QStringLiteral("I0"), QStringLiteral("Начальный ток катушки I₀"), QStringLiteral("А"), 0.0,
                           2.0, 0.05, 2, true, 0.0, process,
                           QStringLiteral("Ток, который уже течёт в катушке: он непрерывен и не меняется скачком")));
    specs.push_back(slider(QStringLiteral("T"), QStringLiteral("Длительность наблюдения T"), QStringLiteral("с"), 1.0,
                           60.0, 1.0, 0, false, 4.0, process,
                           QStringLiteral("Сколько времени идёт опыт: при переходном процессе его хватает на "
                                          "несколько постоянных времени")));
    return specs;
}

double CircuitModel::emf() const { return param(QStringLiteral("emf")); }
double CircuitModel::internalResistance() const { return param(QStringLiteral("internal")); }
double CircuitModel::switchDelay() const { return param(QStringLiteral("delay")); }
bool CircuitModel::switchCloses() const { return paramChoiceIndex(QStringLiteral("mode")) == 0; }
double CircuitModel::initialCapacitorVoltage() const { return param(QStringLiteral("U0")); }
double CircuitModel::initialInductorCurrent() const { return param(QStringLiteral("I0")); }
double CircuitModel::observationTime() const { return param(QStringLiteral("T")); }

double CircuitModel::sliderValue(PartType type) const {
    switch (type) {
    case PartType::Source:    return emf();
    case PartType::Resistor:  return param(QStringLiteral("R"));
    case PartType::Capacitor: return param(QStringLiteral("C"));
    case PartType::Inductor:  return param(QStringLiteral("L"));
    case PartType::Switch:    return std::max(0.05, switchDelay());
    case PartType::Ammeter:   return circuit::parts::kClosedResistance;
    case PartType::Voltmeter: return circuit::parts::kVoltmeterResistance;
    }
    return 1.0;
}

// ------------------------------------------------------------------
// Палитра
// ------------------------------------------------------------------

int CircuitModel::toolCount() { return 9; }

QString CircuitModel::toolName(int index) {
    switch (index) {
    case 0: return QStringLiteral("источник ЭДС");
    case 1: return QStringLiteral("резистор");
    case 2: return QStringLiteral("конденсатор");
    case 3: return QStringLiteral("катушка");
    case 4: return QStringLiteral("ключ");
    case 5: return QStringLiteral("амперметр");
    case 6: return QStringLiteral("вольтметр");
    case 7: return QStringLiteral("провод");
    default: return QStringLiteral("стереть элемент или провод");
    }
}

CircuitModel::Tool CircuitModel::tool() const {
    const int index = paramChoiceIndex(QStringLiteral("tool"));
    if (index == 7)
        return Tool::Wire;
    if (index == 8)
        return Tool::Delete;
    return Tool::Part;
}

PartType CircuitModel::toolPartType() const {
    const int index = paramChoiceIndex(QStringLiteral("tool"));
    if (index < 0 || index > 6)
        return PartType::Resistor;
    return PartType(index);
}

double CircuitModel::toolValue() const {
    const PartType type = toolPartType();
    switch (type) {
    case PartType::Ammeter:
    case PartType::Voltmeter:
        /*
         * Сопротивления приборов заданы моделью прибора, а не пользователем:
         * амперметр должен быть близок к нулю, вольтметр — велик, иначе они
         * перестанут быть приборами и начнут влиять на цепь.
         */
        return circuit::parts::kClosedResistance;
    case PartType::Switch:
        return std::max(0.05, switchDelay());
    default:
        return sliderValue(type);
    }
}

QString CircuitModel::toolHint() const {
    switch (tool()) {
    case Tool::Wire:
        return QStringLiteral("Кликните первую точку, затем вторую: провод проложат по линиям сетки");
    case Tool::Delete:
        return QStringLiteral("Кликните по элементу или проводу, чтобы убрать его из схемы");
    case Tool::Part:
    default:
        return QStringLiteral("Кликните по сетке: элемент встанет по горизонтали, а если место занято — "
                              "повернётся вертикально; правый клик по элементу поворачивает его");
    }
}

// ------------------------------------------------------------------
// Сетка и топология
// ------------------------------------------------------------------

int CircuitModel::gridColumns() const { return m_net.gridColumns(); }
int CircuitModel::gridRows() const { return m_net.gridRows(); }

int CircuitModel::placeAt(const GridPoint& anchor) {
    const PartType type = toolPartType();
    const double value = toolValue();
    /*
     * Ориентацию подбирают по месту: если по горизонтали элемент не встаёт
     * (вышел за край или символ занят), его ставят вертикально. Так один
     * клик даёт элемент в разумной ориентации, а правый клик поворачивает.
     */
    int id = m_net.addPart(type, anchor, true, value);
    if (id < 0)
        id = m_net.addPart(type, anchor, false, value);
    if (id < 0)
        return -1;
    m_selected = id;
    touchTopology();
    emit circuitChanged();
    return id;
}

int CircuitModel::wireBetween(const GridPoint& from, const GridPoint& to) {
    if (from == to)
        return -1;
    // Ломаная по линиям сетки: с одним изгибом, как ведут провод на чертеже.
    QVector<GridPoint> path;
    if (from.column == to.column || from.row == to.row)
        path = {from, to};
    else
        path = {from, GridPoint{to.column, from.row}, to};
    const int id = m_net.addWire(path);
    if (id < 0)
        return -1;
    m_net.rebuildNodes();
    touchTopology();
    emit circuitChanged();
    return id;
}

bool CircuitModel::connectTerminals(const GridPoint& first, const GridPoint& second) {
    if (first == second)
        return false;
    m_net.rebuildNodes();
    if (m_net.connected(first, second)) {
        /*
         * Точки уже соединены: повторный клик по той же паре снимает
         * соединение, удаляя провод, который к ней подходит. Так соединение
         * обратимо, и схему можно править, не стирая всё.
         */
        const int wire = m_net.wireNear(first);
        if (wire <= 0 || !m_net.removeWire(wire))
            return false;
        m_net.rebuildNodes();
        touchTopology();
        emit circuitChanged();
        return true;
    }
    return wireBetween(first, second) > 0;
}

bool CircuitModel::pickTerminal(const GridPoint& point) {
    if (m_pendingTerminal < 0) {
        m_pendingTerminal = int(circuit::pointKey(point));
        emit circuitChanged();
        return false;
    }
    const GridPoint first{int(uint16_t(quint32(m_pendingTerminal) >> 16)),
                          int(uint16_t(quint32(m_pendingTerminal) & 0xffffu))};
    m_pendingTerminal = -1;
    emit circuitChanged();
    if (first == point)
        return false;
    return connectTerminals(first, point);
}

bool CircuitModel::rotateAt(const GridPoint& point) {
    const int id = m_net.partAt(point);
    if (id <= 0)
        return false;
    if (!m_net.rotatePart(id))
        return false;
    m_selected = id;
    touchTopology();
    emit circuitChanged();
    return true;
}

bool CircuitModel::removeAt(const GridPoint& point) {
    const int id = m_net.partAt(point);
    if (id > 0) {
        const bool removed = m_net.removePart(id);
        if (m_selected == id)
            m_selected = -1;
        if (removed) {
            touchTopology();
            emit circuitChanged();
        }
        return removed;
    }
    const int wire = m_net.wireNear(point);
    if (wire <= 0)
        return false;
    const bool removed = m_net.removeWire(wire);
    if (removed) {
        touchTopology();
        emit circuitChanged();
    }
    return removed;
}

void CircuitModel::clearCircuit() {
    m_net.clear();
    m_selected = -1;
    cancelPendingTerminal();
    setParamRaw(QStringLiteral("preset"), double(customPresetIndex()));
    m_appliedPreset = customPresetIndex();
    touchTopology();
    emit circuitChanged();
}

void CircuitModel::setSelectedPart(int partId) {
    if (m_selected == partId)
        return;
    m_selected = partId;
    emit parametersChanged();
    emit circuitChanged();
}

GridPoint CircuitModel::pendingTerminal() const {
    if (m_pendingTerminal < 0)
        return GridPoint{-1, -1};
    return GridPoint{int(uint16_t(quint32(m_pendingTerminal) >> 16)),
                     int(uint16_t(quint32(m_pendingTerminal) & 0xffffu))};
}

bool CircuitModel::hasSwitch() const {
    for (const Part& part : m_net.parts()) {
        if (part.type == PartType::Switch)
            return true;
    }
    return false;
}

bool CircuitModel::hasTransient() const {
    for (const Part& part : m_net.parts()) {
        if (part.type == PartType::Capacitor || part.type == PartType::Inductor)
            return true;
    }
    return false;
}

QVector<int> CircuitModel::closedSwitchesAt(double t) const {
    QVector<int> closed;
    const double delay = switchDelay();
    for (const Part& part : m_net.parts()) {
        if (part.type != PartType::Switch)
            continue;
        const bool on = switchCloses() ? (t >= delay - 1e-12) : (t < delay - 1e-12);
        if (on)
            closed.push_back(part.id);
    }
    return closed;
}

QVector<int> CircuitModel::closedSwitchesAfter(bool afterSwitch) const {
    QVector<int> closed;
    for (const Part& part : m_net.parts()) {
        if (part.type != PartType::Switch)
            continue;
        // До t₀ ключ в одном состоянии, после — в противоположном.
        const bool on = switchCloses() ? afterSwitch : !afterSwitch;
        if (on)
            closed.push_back(part.id);
    }
    return closed;
}

const Part* CircuitModel::activePart() const {
    /*
     * Ток в цепи поддерживают не только источники ЭДС: заряженный конденсатор
     * и катушка с током тоже являются ветвями с заданным напряжением или
     * током, поэтому цепь без источника работает — она просто разряжается.
     */
    for (const Part& part : m_net.parts()) {
        if (part.type == PartType::Source)
            return &part;
    }
    for (const Part& part : m_net.parts()) {
        if (part.type == PartType::Capacitor && std::abs(initialCapacitorVoltage()) > 1e-12)
            return &part;
    }
    for (const Part& part : m_net.parts()) {
        if (part.type == PartType::Inductor && std::abs(initialInductorCurrent()) > 1e-12)
            return &part;
    }
    return nullptr;
}

double CircuitModel::sourceEmf() const {
    for (const Part& part : m_net.parts()) {
        if (part.type == PartType::Source)
            return emf();
    }
    return 0.0;
}

// ------------------------------------------------------------------
// Расчёт: фазы и переходный процесс
// ------------------------------------------------------------------

QVector<int> CircuitModel::capacitorIds() const {
    QVector<int> ids;
    for (const Part& part : m_net.parts()) {
        if (part.type == PartType::Capacitor)
            ids.push_back(part.id);
    }
    return ids;
}

QVector<int> CircuitModel::inductorIds() const {
    QVector<int> ids;
    for (const Part& part : m_net.parts()) {
        if (part.type == PartType::Inductor)
            ids.push_back(part.id);
    }
    return ids;
}

QVector<int> CircuitModel::dynamicIds() const {
    QVector<int> ids = capacitorIds();
    const QVector<int> inductors = inductorIds();
    ids.append(inductors);
    return ids;
}

QVector<double> CircuitModel::initialState() const {
    QVector<double> state;
    for (const Part& part : m_net.parts()) {
        if (part.type == PartType::Capacitor)
            state.push_back(initialCapacitorVoltage());
    }
    for (const Part& part : m_net.parts()) {
        if (part.type == PartType::Inductor)
            state.push_back(initialInductorCurrent());
    }
    return state;
}

void CircuitModel::touchTopology() {
    m_solved.topology += 1;
    invalidateSolution();
    emit parametersChanged();
    emit derivationChanged();
}

CircuitModel::Signature CircuitModel::signature() const {
    Signature current;
    current.preset = paramChoiceIndex(QStringLiteral("preset"));
    current.emf = emf();
    current.internal = internalResistance();
    current.delay = switchDelay();
    current.capacitorVoltage = initialCapacitorVoltage();
    current.inductorCurrent = initialInductorCurrent();
    current.window = observationTime();
    current.switchMode = paramChoiceIndex(QStringLiteral("mode"));
    current.topology = m_solved.topology;
    current.parts = m_net.parts().size();
    // Значения элементов влияют на матрицу: их сводят в одну контрольную сумму.
    double sum = 0.0;
    for (const Part& part : m_net.parts())
        sum += double(part.id) * part.value;
    current.partValues = sum;
    return current;
}

bool CircuitModel::Signature::operator==(const Signature& other) const {
    return std::abs(topology - other.topology) < 0.5 && preset == other.preset && emf == other.emf &&
           internal == other.internal && delay == other.delay && capacitorVoltage == other.capacitorVoltage &&
           inductorCurrent == other.inductorCurrent && window == other.window && switchMode == other.switchMode &&
           parts == other.parts && partValues == other.partValues;
}

void CircuitModel::ensureSolved() const {
    auto* self = const_cast<CircuitModel*>(this);
    const int preset = paramChoiceIndex(QStringLiteral("preset"));
    if (m_appliedPreset != preset) {
        if (m_appliedPreset < 0 && preset == customPresetIndex()) {
            // Первый запуск на пункте «моя схема»: сетку не трогают.
            self->m_appliedPreset = preset;
        } else {
            self->applyPreset(preset);
            self->m_appliedPreset = preset;
        }
    }

    const Signature current = signature();
    if (current == m_solved)
        return;

    m_net.rebuildNodes();
    m_dynamicIds = dynamicIds();
    m_initialState = initialState();
    m_transientEnd = std::numeric_limits<double>::quiet_NaN();

    const int ground = m_net.groundNode();
    const double total = std::max(1e-6, observationTime());
    const double delay = std::clamp(switchDelay(), 0.0, total);
    const bool split = hasSwitch() && delay > 1e-9 && delay < total - 1e-9;
    QVector<QPair<double, bool>> spans;
    if (split)
        spans = {{delay, false}, {total - delay, true}};
    else
        spans = {{total, !closedSwitchesAt(total * 0.5).isEmpty()}};

    m_phases.clear();
    double cursor = 0.0;
    for (const auto& span : spans) {
        Phase phase;
        phase.from = cursor;
        phase.to = cursor + span.first;
        phase.afterSwitch = span.second;
        cursor = phase.to;
        if (ground >= 0)
            phase.system.compile(m_net, ground, closedSwitchesAfter(phase.afterSwitch));
        m_phases.push_back(phase);
    }

    /*
     * Состояние (напряжения конденсаторов и токи катушек) меняют фазу
     * интегрирования. Ключ меняет структуру уравнений, поэтому фазы до t₀ и
     * после него считают отдельными системами, а состояние в момент
     * переключения передают как есть: напряжение конденсатора и ток катушки
     * непрерывны — их скачок запретил бы физике, а не отдельной проверке.
     */
    const bool dynamic = !m_dynamicIds.isEmpty();
    for (int index = 0; index < m_phases.size(); ++index) {
        Phase& phase = m_phases[size_t(index)];
        if (phase.system.empty()) {
            phase.track = Trajectory();
            continue;
        }
        phase.initial = index == 0 ? m_initialState : m_phases.at(size_t(index - 1)).track.stateAt(phase.to - phase.from);
        if (index == 0 && phase.initial.isEmpty())
            phase.initial = QVector<double>{0.0};
        if (index > 0 && phase.initial.isEmpty())
            phase.initial = m_initialState.isEmpty() ? QVector<double>{0.0} : m_initialState;

        Drive drive(m_net);
        drive.setInternalResistance(internalResistance());
        const double span = std::max(1e-6, phase.to - phase.from);
        if (!dynamic) {
            // Цепь только из сопротивлений: состояние не нужно, ряд строят по решениям системы.
            phase.track.simulate(span, span / 4.0,
                                 [](double, const QVector<double>& y, QVector<double>& dydt) {
                                     dydt = QVector<double>(y.size(), 0.0);
                                 },
                                 QVector<double>{0.0}, 3);
            continue;
        }

        const Trajectory::Rhs rhs = [this, index](double, const QVector<double>& y, QVector<double>& dydt) {
            const Solution step = solvePhase(index, 0.0, y);
            dydt.resize(y.size());
            for (int i = 0; i < y.size(); ++i) {
                const int id = m_dynamicIds.at(size_t(i));
                const Solution::Value* value = step.value(id);
                dydt[size_t(i)] = value && std::isfinite(value->derivative) ? value->derivative : 0.0;
            }
        };

        /*
         * Предел интегрирования задают сами узлы: их число постоянно, поэтому
         * шаг мелкий и экспонента разрешается точно. Момент, когда процесс
         * завершился, находят потом по узлам — там, где состояние перестаёт
         * сдвигаться, а не по формуле τ.
         */
        phase.track.simulate(span, span / 1200.0, rhs, phase.initial, 1600);
    }

    // Момент завершения процесса: конец последнего узла, где состояние ещё менялось.
    m_transientEnd = std::numeric_limits<double>::quiet_NaN();
    for (const Phase& phase : m_phases) {
        if (phase.track.empty() || m_dynamicIds.isEmpty())
            continue;
        const int last = phase.track.sampleCount() - 1;
        for (int i = last; i >= 1; --i) {
            double change = 0.0;
            double scale = 0.0;
            for (int c = 0; c < m_dynamicIds.size(); ++c) {
                const double now = phase.track.valueAt(i, c);
                const double before = phase.track.valueAt(i - 1, c);
                change += std::abs(now - before);
                scale = std::max(scale, std::abs(now));
            }
            if (scale <= 0.0 || change > kSettleRatio * scale) {
                m_transientEnd = phase.from + phase.track.timeAt(i);
                break;
            }
        }
    }

    m_solved = current;
}

Solution CircuitModel::solvePhase(int phaseIndex, double t, const QVector<double>& state) const {
    Q_UNUSED(t);
    if (phaseIndex < 0 || phaseIndex >= m_phases.size())
        return Solution();
    const Phase& phase = m_phases.at(size_t(phaseIndex));
    if (phase.system.empty())
        return Solution();

    Drive drive(m_net);
    drive.setInternalResistance(internalResistance());
    /*
     * ЭДС всех источников берёт из слайдера: значение, записанное в элемент при
     * постановке, служит только начальным. Так один ползунок правит цепь целиком,
     * а не только те источники, что поставили вручную.
     */
    for (const Part& part : m_net.parts()) {
        if (part.type == PartType::Source)
            drive.setEmf(part.id, emf());
    }
    for (int i = 0; i < m_dynamicIds.size(); ++i) {
        const int id = m_dynamicIds.at(size_t(i));
        const double value = i < state.size() ? state.at(size_t(i)) : 0.0;
        const Part* part = m_net.findPart(id);
        if (!part)
            continue;
        if (part->type == PartType::Capacitor)
            drive.setCapacitorVoltage(id, value);
        else
            drive.setInductorCurrent(id, value);
    }
    return MnaSolver::solve(phase.system, m_net, drive);
}

int CircuitModel::phaseIndexAt(double t) const {
    ensureSolved();
    for (int i = m_phases.size() - 1; i >= 0; --i) {
        if (t >= m_phases.at(size_t(i)).from - 1e-12)
            return i;
    }
    return 0;
}

QVector<double> CircuitModel::stateVectorAt(double t) const {
    ensureSolved();
    if (m_dynamicIds.isEmpty())
        return {};
    const int index = phaseIndexAt(t);
    if (index < 0 || index >= m_phases.size())
        return m_initialState;
    const Phase& phase = m_phases.at(size_t(index));
    if (phase.track.empty())
        return phase.initial.isEmpty() ? m_initialState : phase.initial;
    const double local = std::clamp(t - phase.from, 0.0, phase.track.timeAt(phase.track.sampleCount() - 1));
    QVector<double> state = phase.track.stateAt(local);
    return state.isEmpty() ? phase.initial : state;
}

Solution CircuitModel::solutionAt(double t) const {
    ensureSolved();
    const int index = phaseIndexAt(t);
    return solvePhase(index, t, stateVectorAt(t));
}

QVector<QPointF> CircuitModel::sampled(const std::function<double(const Solution&, double)>& value) const {
    QVector<QPointF> points;
    ensureSolved();
    if (!m_phases.isEmpty()) {
        const double total = std::max(1e-6, observationTime());
        points.reserve(kPlotPoints);
        for (int i = 0; i < kPlotPoints; ++i) {
            const double t = total * double(i) / double(kPlotPoints - 1);
            const Solution solution = solutionAt(t);
            if (!solution.ok)
                break;
            points.push_back(QPointF(t, value(solution, t)));
        }
    }
    return points;
}

QVector<QPointF> CircuitModel::sourceCurrentSeries() const {
    return sampled([](const Solution& solution, double) { return solution.sourceCurrent(); });
}

QVector<QPointF> CircuitModel::branchCurrentSeries(int partId) const {
    return sampled([partId](const Solution& solution, double) {
        const Solution::Value* value = solution.value(partId);
        return value ? value->current : 0.0;
    });
}

QVector<QPointF> CircuitModel::branchVoltageSeries(int partId) const {
    return sampled([partId](const Solution& solution, double) {
        const Solution::Value* value = solution.value(partId);
        return value ? value->voltage : 0.0;
    });
}

QVector<QPointF> CircuitModel::terminalVoltageSeries() const {
    return sampled([this](const Solution& solution, double t) {
        Q_UNUSED(solution);
        return terminalVoltageAt(t);
    });
}

QVector<QPointF> CircuitModel::chargeSeries(int partId) const {
    return sampled([partId](const Solution& solution, double) {
        const Solution::Value* value = solution.value(partId);
        return value ? value->charge : 0.0;
    });
}

QVector<QPointF> CircuitModel::energySeries() const {
    return sampled([](const Solution& solution, double) { return solution.storedEnergy; });
}

QVector<QPointF> CircuitModel::sourcePowerSeries() const {
    return sampled([](const Solution& solution, double) { return solution.sourcePower(); });
}

QVector<QPointF> CircuitModel::dissipatedPowerSeries() const {
    return sampled([](const Solution& solution, double) { return solution.dissipatedPower(); });
}

QVector<QPointF> CircuitModel::terminalVersusCurrent() const {
    QVector<QPointF> points;
    const QVector<QPointF> current = sourceCurrentSeries();
    points.reserve(current.size());
    // Пары (I, U): по оси X стоит ток, а не момент времени, иначе получился бы
    // обычный график U(t), а не характеристика источника.
    for (const QPointF& point : current)
        points.push_back(QPointF(point.y(), terminalVoltageAt(point.x())));
    return points;
}

QVector<QPointF> CircuitModel::equivalentResistanceSeries() const {
    QVector<QPointF> points;
    const double total = std::max(1e-6, observationTime());
    for (int i = 0; i < kPlotPoints; ++i) {
        const double t = total * double(i) / double(kPlotPoints - 1);
        const double resistance = equivalentResistanceAt(t);
        if (std::isfinite(resistance))
            points.push_back(QPointF(t, resistance));
    }
    return points;
}

double CircuitModel::currentOf(int partId, double t) const {
    const Solution solution = solutionAt(t);
    if (!solution.ok)
        return std::numeric_limits<double>::quiet_NaN();
    const Solution::Value* value = solution.value(partId);
    return value ? value->current : 0.0;
}

double CircuitModel::voltageOf(int partId, double t) const {
    const Solution solution = solutionAt(t);
    if (!solution.ok)
        return std::numeric_limits<double>::quiet_NaN();
    const Solution::Value* value = solution.value(partId);
    return value ? value->voltage : 0.0;
}

double CircuitModel::chargeOf(int partId, double t) const {
    const Solution solution = solutionAt(t);
    const Solution::Value* value = solution.value(partId);
    return value ? value->charge : 0.0;
}

double CircuitModel::sourceCurrentAt(double t) const {
    const Solution solution = solutionAt(t);
    return solution.ok ? solution.sourceCurrent() : std::numeric_limits<double>::quiet_NaN();
}

double CircuitModel::terminalVoltageAt(double t) const {
    const Solution solution = solutionAt(t);
    if (!solution.ok)
        return std::numeric_limits<double>::quiet_NaN();
    /*
     * Первая клемма источника — «минус», поэтому напряжением на зажимах будет
     * разность потенциалов клемм с обратным знаком: φ(+) − φ(−).
     */
    for (const Part& part : m_net.parts()) {
        if (part.type != PartType::Source)
            continue;
        const Solution::Value* value = solution.value(part.id);
        if (!value)
            continue;
        return -value->voltage;
    }
    for (const Part& part : m_net.parts()) {
        if (part.type != PartType::Capacitor)
            continue;
        const Solution::Value* value = solution.value(part.id);
        if (value)
            return value->voltage;
    }
    return 0.0;
}

double CircuitModel::equivalentResistanceAt(double t) const {
    /*
     * Сопротивление берут из рассчитанных тока и напряжения: R = U/I. Для
     * мостовой схемы такого значения не дают ни складывание, ни параллельное
     * соединение — его даёт только решение системы узлов.
     */
    const double current = sourceCurrentAt(t);
    if (std::abs(current) < 1e-12)
        return std::numeric_limits<double>::quiet_NaN();
    const double voltage = terminalVoltageAt(t);
    if (!std::isfinite(voltage))
        return voltage;
    return voltage / current;
}

double CircuitModel::deliveredPowerAt(double t) const { return solutionAt(t).dissipatedPower(); }
double CircuitModel::sourcePowerAt(double t) const { return solutionAt(t).sourcePower(); }
double CircuitModel::storedEnergyAt(double t) const { return solutionAt(t).storedEnergy; }

double CircuitModel::transientEndTime() const {
    ensureSolved();
    if (m_dynamicIds.isEmpty())
        return std::numeric_limits<double>::quiet_NaN();
    return m_transientEnd;
}

QVector<CircuitNet::TerminalRef> CircuitModel::branchTerminals(int partId) const {
    ensureSolved();
    return m_net.branchNodes(partId);
}

double CircuitModel::timeConstantOf(int partId) const {
    /*
     * Постоянную времени не вычисляют по формуле RC или L/R: её берут из
     * рассчитанного ряда как время, за которое величина меняется на долю
     * 1 − 1/e от конечного приращения. Так значение остаётся результатом
     * того же расчёта цепи, что и токи.
     *
     * Берут узлы траектории, а не ряд для графика: узлов в разы больше, и
     * момент 1 − 1/e попадает на них с точностью шага интегрирования, а не
     * отрисовки.
     */
    const Part* part = m_net.findPart(partId);
    if (!part)
        return std::numeric_limits<double>::quiet_NaN();
    ensureSolved();

    // Постоянная времени у процесса одна: ряду статической ветви предпочитаем
    // узлы динамической, где процесс разрешён во всей полноте.
    int component = m_dynamicIds.indexOf(partId);
    if (component < 0 && !m_dynamicIds.isEmpty())
        component = 0;
    if (component >= 0) {
        for (const Phase& phase : m_phases) {
            if (phase.track.empty())
                continue;
            const int last = phase.track.sampleCount() - 1;
            if (last < 4)
                continue;
            const double start = phase.track.valueAt(0, component);
            const double finish = phase.track.valueAt(last, component);
            const double span = finish - start;
            if (std::abs(span) < 1e-12)
                continue;
            const double wanted = start + span * (1.0 - 1.0 / std::exp(1.0));
            for (int i = 1; i <= last; ++i) {
                const double before = phase.track.valueAt(i - 1, component);
                const double after = phase.track.valueAt(i, component);
                if ((before - wanted) * (after - wanted) > 0.0)
                    continue;
                const double width = std::abs(after - before);
                const double weight = width > 1e-18 ? (wanted - before) / width : 0.0;
                const double local =
                    phase.track.timeAt(i - 1) + weight * (phase.track.timeAt(i) - phase.track.timeAt(i - 1));
                return phase.from + local;
            }
        }
        return std::numeric_limits<double>::quiet_NaN();
    }

    const QVector<QPointF> series = branchVoltageSeries(partId);
    if (series.size() < 8)
        return std::numeric_limits<double>::quiet_NaN();
    const double start = series.first().y();
    const double finish = series.last().y();
    const double span = finish - start;
    if (std::abs(span) < 1e-12)
        return std::numeric_limits<double>::quiet_NaN();
    const double wanted = start + span * (1.0 - 1.0 / std::exp(1.0));
    for (int i = 1; i < series.size(); ++i) {
        const double before = series.at(size_t(i - 1)).y();
        const double after = series.at(size_t(i)).y();
        if ((before - wanted) * (after - wanted) > 0.0)
            continue;
        const double width = std::abs(after - before);
        const double weight = width > 1e-18 ? (wanted - before) / width : 0.0;
        const double t0 = series.at(size_t(i - 1)).x();
        const double t1 = series.at(size_t(i)).x();
        // Ряд идёт от нуля: время отсчитывают от старта переходного процесса.
        const double delay = hasSwitch() ? std::clamp(switchDelay(), 0.0, observationTime()) : 0.0;
        const double found = t0 + weight * (t1 - t0);
        return found > delay ? found - delay : std::numeric_limits<double>::quiet_NaN();
    }
    return std::numeric_limits<double>::quiet_NaN();
}

// ------------------------------------------------------------------
// Форматирование величин
// ------------------------------------------------------------------

namespace {

/** Число с порядком величины: 2,5·10⁻⁴ вместо нуля после округления. */
QString scientific(double value, const QString& unit) {
    const double magnitude = std::abs(value);
    const int exponent = int(std::floor(std::log10(magnitude)));
    const double mantissa = value / std::pow(10.0, double(exponent));
    return QStringLiteral("%1·10^%2 %3").arg(utils::formatNumber(mantissa, 2)).arg(exponent).arg(unit);
}

}  // namespace

QString CircuitModel::currentText(double amperes) {
    if (!std::isfinite(amperes))
        return QStringLiteral("—");
    const double magnitude = std::abs(amperes);
    if (magnitude < 1e-14)
        return QStringLiteral("0 А");
    if (magnitude >= 1.0)
        return utils::formatNumber(amperes, 3) + QStringLiteral(" А");
    if (magnitude >= 1e-3)
        return utils::formatNumber(amperes * 1e3, 2) + QStringLiteral(" мА");
    if (magnitude >= 1e-6)
        return utils::formatNumber(amperes * 1e6, 2) + QStringLiteral(" мкА");
    if (magnitude >= 1e-9)
        return utils::formatNumber(amperes * 1e9, 2) + QStringLiteral(" нА");
    return scientific(amperes, QStringLiteral("А"));
}

QString CircuitModel::voltageText(double volts) {
    if (!std::isfinite(volts))
        return QStringLiteral("—");
    const double magnitude = std::abs(volts);
    if (magnitude < 1e-14)
        return QStringLiteral("0 В");
    if (magnitude >= 1.0)
        return utils::formatNumber(volts, 3) + QStringLiteral(" В");
    if (magnitude >= 1e-3)
        return utils::formatNumber(volts * 1e3, 2) + QStringLiteral(" мВ");
    if (magnitude >= 1e-6)
        return utils::formatNumber(volts * 1e6, 2) + QStringLiteral(" мкВ");
    return scientific(volts, QStringLiteral("В"));
}

QString CircuitModel::resistanceText(double ohms) {
    if (!std::isfinite(ohms) || ohms <= 0.0)
        return QStringLiteral("—");
    if (ohms >= 1e6)
        return utils::formatNumber(ohms / 1e6, 2) + QStringLiteral(" МОм");
    if (ohms >= 1e3)
        return utils::formatNumber(ohms / 1e3, 2) + QStringLiteral(" кОм");
    return utils::formatNumber(ohms, ohms < 10.0 ? 2 : 0) + QStringLiteral(" Ом");
}

QString CircuitModel::partValueText(int partId) const {
    const Part* part = m_net.findPart(partId);
    if (!part)
        return QString();
    switch (part->type) {
    case PartType::Source:
        return utils::formatNumber(emf(), 2) + QStringLiteral(" В");
    case PartType::Capacitor:
        return utils::formatNumber(part->value, 0) + QStringLiteral(" мкФ");
    case PartType::Inductor:
        return utils::formatNumber(part->value, 2) + QStringLiteral(" Гн");
    case PartType::Switch:
        return utils::formatNumber(part->value, 2) + QStringLiteral(" с");
    case PartType::Ammeter:
        return resistanceText(circuit::parts::kClosedResistance);
    case PartType::Voltmeter:
        return resistanceText(circuit::parts::kVoltmeterResistance);
    case PartType::Resistor:
        return resistanceText(part->value);
    }
    return QString();
}

// ------------------------------------------------------------------
// Состояние, проверка, шкала времени
// ------------------------------------------------------------------

SimState CircuitModel::stateAt(double t) const {
    SimState state;
    state.time = t;
    if (!validate().ok)
        return state;

    const Solution solution = solutionAt(t);
    if (!solution.ok)
        return state;

    for (const Part& part : m_net.parts()) {
        const Solution::Value* value = solution.value(part.id);
        EntityVisual entity;
        entity.id = part.name;
        entity.label = QStringLiteral("%1 %2").arg(circuit::partWord(part.type), part.name);
        entity.position = value ? value->voltage : 0.0;
        entity.value1 = value ? value->current : 0.0;
        entity.value2 = value ? value->power : 0.0;
        entity.active = value && value->conducting;
        entity.variant = int(part.type);
        state.entities.push_back(entity);
    }

    const double end = transientEndTime();
    state.primary = solution.sourceCurrent();
    state.secondary = terminalVoltageAt(t);
    const double delay = std::clamp(switchDelay(), 0.0, observationTime());
    if (hasSwitch() && t >= delay - 1e-9)
        state.eventLabel = switchCloses() ? QStringLiteral("Ключ замкнут") : QStringLiteral("Ключ разомкнут");
    else if (hasTransient() && std::isfinite(end) && t >= end)
        state.eventLabel = QStringLiteral("Переходный процесс завершился");
    state.resolved = hasTransient() ? (std::isfinite(end) && t >= end) : true;
    return state;
}

ValidationResult CircuitModel::validate() const {
    ValidationResult check;
    if (observationTime() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Длительность наблюдения должна быть больше нуля");
        check.hint = QStringLiteral("Задайте T не меньше 1 с");
        return check;
    }
    if (internalResistance() < 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Внутреннее сопротивление не бывает отрицательным");
        check.hint = QStringLiteral("Задайте r не меньше 0");
        return check;
    }
    for (const Part& part : m_net.parts()) {
        const bool bad = (part.type == PartType::Resistor || part.type == PartType::Capacitor ||
                          part.type == PartType::Inductor) &&
                         part.value <= 0.0;
        if (!bad)
            continue;
        const QString quantity = part.type == PartType::Resistor
                                     ? QStringLiteral("Сопротивление")
                                     : part.type == PartType::Capacitor ? QStringLiteral("Ёмкость")
                                                                        : QStringLiteral("Индуктивность");
        check.ok = false;
        check.message = QStringLiteral("%1 %2 должно быть больше нуля").arg(quantity, part.name);
        check.hint = QStringLiteral("Увеличьте значение элемента или уберите его из схемы");
        return check;
    }

    // Элементы с запасённой энергией заменяют источник: проверка это учитывает.
    QSet<int> energized;
    for (const Part& part : m_net.parts()) {
        if ((part.type == PartType::Capacitor && std::abs(initialCapacitorVoltage()) > 1e-12) ||
            (part.type == PartType::Inductor && std::abs(initialInductorCurrent()) > 1e-12))
            energized.insert(part.id);
    }

    const CircuitNet::Check topology = m_net.checkTopology(energized);
    if (!topology.ok) {
        check.ok = false;
        check.message = topology.message;
        check.hint = topology.hint;
        return check;
    }
    check.message = topology.message;
    check.hint = topology.hint;
    return check;
}

double CircuitModel::timePerSecond() const {
    /*
     * Переходный процесс должен читаться глазами: за восемь секунд реального
     * времени проходит всё наблюдение, поэтому шкалу делят на восемь.
     */
    return std::max(1e-6, observationTime()) / 8.0;
}

double CircuitModel::duration() const { return std::max(1e-6, observationTime()); }

double CircuitModel::stepSize() const { return duration() / 400.0; }

double CircuitModel::eventTime() const {
    ensureSolved();
    if (hasSwitch()) {
        const double delay = switchDelay();
        if (delay > 0.0 && delay < observationTime())
            return delay;
    }
    return transientEndTime();
}

// ------------------------------------------------------------------
// Ручки
// ------------------------------------------------------------------

QVector<HandleInfo> CircuitModel::handles() const {
    QVector<HandleInfo> list;
    const double total = std::max(1e-6, observationTime());

    HandleInfo source;
    source.id = QStringLiteral("emf");
    source.label = QStringLiteral("ЭДС источника E");
    source.unit = QStringLiteral("В");
    source.minValue = 0.5;
    source.maxValue = 24.0;
    source.step = 0.1;
    source.axis = QStringLiteral("y");
    source.value = std::clamp(emf(), 0.5, 24.0);
    source.readbackLabel = QStringLiteral("ЭДС источника");
    source.readbackValue = utils::formatNumber(emf(), 2);
    list.push_back(source);

    const Part* selected = m_selected > 0 ? m_net.findPart(m_selected) : nullptr;
    if (selected && selected->type != PartType::Ammeter && selected->type != PartType::Voltmeter) {
        const QPair<double, double> range = partValueRange(selected->type);
        HandleInfo value;
        value.id = QStringLiteral("value");
        value.label = QStringLiteral("Значение %1").arg(selected->name);
        value.unit = circuit::valueUnit(selected->type);
        value.minValue = range.first;
        value.maxValue = range.second;
        value.step = std::max((range.second - range.first) / 100.0, 1e-6);
        value.axis = QStringLiteral("y");
        value.value = std::clamp(selected->value, range.first, range.second);
        value.readbackLabel = selected->name;
        value.readbackValue = partValueText(selected->id);
        list.push_back(value);
    }

    if (hasSwitch()) {
        HandleInfo delay;
        delay.id = QStringLiteral("delay");
        delay.label = QStringLiteral("Момент переключения t₀");
        delay.unit = QStringLiteral("с");
        delay.minValue = 0.0;
        delay.maxValue = total;
        delay.step = total / 200.0;
        delay.axis = QStringLiteral("x");
        delay.value = std::clamp(switchDelay(), 0.0, total);
        delay.readbackLabel = QStringLiteral("Ключ переключится при t =");
        delay.readbackValue = utils::formatNumber(switchDelay(), 2);
        list.push_back(delay);
    }

    HandleInfo window;
    window.id = QStringLiteral("window");
    window.label = QStringLiteral("Конец наблюдения T");
    window.unit = QStringLiteral("с");
    window.minValue = 1.0;
    window.maxValue = 60.0;
    window.step = 1.0;
    window.axis = QStringLiteral("x");
    window.value = std::clamp(total, 1.0, 60.0);
    window.readbackLabel = QStringLiteral("Длительность опыта");
    window.readbackValue = utils::formatNumber(total, 0);
    list.push_back(window);

    return list;
}

void CircuitModel::setHandleValue(const QString& id, double value) {
    if (id == QLatin1String("emf")) {
        setParam(QStringLiteral("emf"), value);
        // ЭДС задают и как значение источника: схему пересчитывают целиком.
        bool changed = false;
        for (const Part& part : m_net.parts()) {
            if (part.type == PartType::Source && m_net.setPartValue(part.id, value))
                changed = true;
        }
        if (changed)
            touchTopology();
        return;
    }
    if (id == QLatin1String("delay")) {
        setParam(QStringLiteral("delay"), value);
        bool changed = false;
        for (const Part& part : m_net.parts()) {
            if (part.type == PartType::Switch && m_net.setPartValue(part.id, std::max(0.05, value)))
                changed = true;
        }
        if (changed)
            touchTopology();
        return;
    }
    if (id == QLatin1String("window")) {
        setParam(QStringLiteral("T"), value);
        touchTopology();
        return;
    }
    if (id == QLatin1String("value") && m_selected > 0) {
        const Part* part = m_net.findPart(m_selected);
        if (!part)
            return;
        const QPair<double, double> range = partValueRange(part->type);
        const double clamped = std::clamp(value, range.first, range.second);
        if (part->type == PartType::Switch) {
            setParam(QStringLiteral("delay"), clamped);
            if (m_net.setPartValue(m_selected, std::max(0.05, clamped)))
                touchTopology();
            return;
        }
        if (m_net.setPartValue(m_selected, clamped)) {
            const QString paramId = part->type == PartType::Resistor   ? QStringLiteral("R")
                                    : part->type == PartType::Capacitor ? QStringLiteral("C")
                                    : part->type == PartType::Inductor  ? QStringLiteral("L")
                                                                        : QStringLiteral("emf");
            setParam(paramId, clamped);
            touchTopology();
        }
        return;
    }
    Simulation::setHandleValue(id, value);
}

// ------------------------------------------------------------------
// Показания и запись решения
// ------------------------------------------------------------------

QVector<Readout> CircuitModel::readouts() const {
    QVector<Readout> list;
    const double t = time();
    const Solution solution = solutionAt(t);
    list.push_back({QStringLiteral("Схема"), presetName(int(paramChoiceIndex(QStringLiteral("preset")))), false});
    list.push_back({QStringLiteral("Элементов в схеме"), QString::number(m_net.parts().size()), false});
    list.push_back({QStringLiteral("Узлов расчёта"), QString::number(m_net.nodeCount()), false});
    if (!solution.ok) {
        list.push_back({QStringLiteral("Расчёт"), solution.message, true});
        return list;
    }
    list.push_back({QStringLiteral("Ток источника"), currentText(solution.sourceCurrent()), true});
    list.push_back({QStringLiteral("Напряжение на зажимах"), voltageText(terminalVoltageAt(t)), true});
    list.push_back({QStringLiteral("Сопротивление всей цепи"), resistanceText(equivalentResistanceAt(t)), true});
    list.push_back({QStringLiteral("Мощность источника"),
                    utils::formatNumber(sourcePowerAt(t), 3) + QStringLiteral(" Вт"), false});
    list.push_back({QStringLiteral("Мощность в цепи"),
                    utils::formatNumber(deliveredPowerAt(t), 3) + QStringLiteral(" Вт"), false});
    const double drive = sourceEmf();
    if (drive > 0.0) {
        list.push_back({QStringLiteral("Доля ЭДС на цепи"),
                        utils::formatNumber(100.0 * terminalVoltageAt(t) / drive, 1) + QStringLiteral(" %"), false});
    }
    for (const Part& part : m_net.parts()) {
        const Solution::Value* value = solution.value(part.id);
        if (!value)
            continue;
        list.push_back({QStringLiteral("%1: ток").arg(part.name), currentText(value->current), false});
        list.push_back({QStringLiteral("%1: напряжение").arg(part.name), voltageText(value->voltage), false});
        if (part.type == PartType::Capacitor) {
            list.push_back({QStringLiteral("%1: заряд").arg(part.name),
                            utils::formatNumber(value->charge * 1e6, 2) + QStringLiteral(" мкКл"), false});
        }
        if (part.type == PartType::Inductor) {
            list.push_back({QStringLiteral("%1: энергия поля").arg(part.name),
                            utils::formatNumber(0.5 * part.value * value->current * value->current, 4) +
                                QStringLiteral(" Дж"), false});
        }
    }
    if (hasTransient()) {
        const double end = transientEndTime();
        list.push_back({QStringLiteral("Энергия поля"),
                        utils::formatNumber(storedEnergyAt(t), 4) + QStringLiteral(" Дж"), false});
        list.push_back({QStringLiteral("Переходный процесс"),
                        std::isfinite(end) ? QStringLiteral("завершён при t = %1 с").arg(utils::formatNumber(end, 2))
                                           : QStringLiteral("идёт всё наблюдение"), false});
        for (const int id : m_dynamicIds) {
            const double tau = timeConstantOf(id);
            if (std::isfinite(tau))
                list.push_back({QStringLiteral("Постоянная времени %1").arg(m_net.findPart(id)->name),
                                utils::formatNumber(tau, 3) + QStringLiteral(" с"), false});
        }
    }
    return list;
}

Derivation CircuitModel::derivation() const {
    Derivation d;
    const ValidationResult check = validate();
    d.valid = check.ok;
    d.problemText = problemText();
    if (!check.ok) {
        d.steps.push_back({QStringLiteral("Внимание"), QStringLiteral("\\text{%1}").arg(check.message)});
        d.steps.push_back({QStringLiteral("Что сделать"), QStringLiteral("\\text{%1}").arg(check.hint)});
        return d;
    }

    const double t = time();
    const Solution solution = solutionAt(t);
    d.unknown = QStringLiteral("токи ветвей, напряжение на зажимах источника и мощность");

    QStringList given;
    for (const Part& part : m_net.parts())
        given.push_back(QStringLiteral("%1 = %2").arg(part.name, partValueText(part.id)));
    d.steps.push_back({QStringLiteral("Дано"),
                       QStringLiteral("\\text{Схема собрана из }%1\\text{; }r = %2\\ \\text{Ом}")
                           .arg(given.join(QStringLiteral(", ")), utils::formatNumber(internalResistance(), 2))});

    /*
     * Расчёт не подгоняют под частный случай: для каждого узла записывают
     * баланс токов (первый закон Кирхгофа), проводящую ветвь выражают через
     * разность потенциалов узлов, а ветвь с заданным напряжением даёт
     * неизвестный ток и условие связи. Из этой системы частные формулы
     * последовательного и параллельного соединений следуют сами.
     */
    d.steps.push_back({QStringLiteral("Первый закон Кирхгофа для каждого узла"),
                       QStringLiteral("\\sum_{k}{ I_{k} = 0 }")});
    d.steps.push_back({QStringLiteral("Ветвь с проводимостью"),
                       QStringLiteral("I_{k} = G_{k}(\\varphi_{a} - \\varphi_{b}),\\quad G_{k} = \\frac{1}{R_{k}}")});
    d.steps.push_back({QStringLiteral("Ветвь источника (связь узлов и неизвестный ток)"),
                       QStringLiteral("\\varphi_{-} - \\varphi_{+} - r I = -E,\\quad U = E - I r")});

    QStringList conductances;
    for (const Part& part : m_net.parts()) {
        if (part.type != PartType::Resistor)
            continue;
        conductances.push_back(QStringLiteral("G(%1) = \\frac{1}{%2} = %3")
                                   .arg(part.name, resistanceText(part.value),
                                        utils::formatNumber(1.0 / part.value, 5)));
    }
    if (!conductances.isEmpty())
        d.steps.push_back({QStringLiteral("Проводимости ветвей"), conductances.join(QStringLiteral("\\quad "))});

    QStringList potentials;
    for (int node = 0; node < solution.nodePotentials.size(); ++node) {
        potentials.push_back(QStringLiteral("\\varphi_{%1} = %2")
                                 .arg(QString::number(node + 1),
                                      utils::formatNumber(solution.nodePotentials.at(size_t(node)), 3)));
    }
    if (!potentials.isEmpty())
        d.steps.push_back({QStringLiteral("Решение системы узловых уравнений, В"), potentials.join(QStringLiteral("\\quad "))});

    QStringList branches;
    for (const Part& part : m_net.parts()) {
        const Solution::Value* value = solution.value(part.id);
        if (!value)
            continue;
        branches.push_back(QStringLiteral("I(%1) = %2,\\ U(%1) = %3")
                               .arg(part.name, currentText(value->current), voltageText(value->voltage)));
    }
    if (!branches.isEmpty())
        d.steps.push_back({QStringLiteral("Токи и напряжения ветвей"), branches.join(QStringLiteral("\\quad "))});

    const double resistance = equivalentResistanceAt(t);
    if (std::isfinite(resistance)) {
        d.steps.push_back({QStringLiteral("Сопротивление, которое видит источник"),
                           QStringLiteral("R = \\frac{U}{I} = \\frac{%1}{%2} = %3")
                               .arg(voltageText(terminalVoltageAt(t)), currentText(solution.sourceCurrent()),
                                    resistanceText(resistance))});
    }
    d.steps.push_back({QStringLiteral("Мощность"),
                       QStringLiteral("P_{ист} = U I = %1,\\quad P_{цепи} = \\sum_{k}{ I_{k}^{2} R_{k} } = %2")
                           .arg(utils::formatNumber(sourcePowerAt(t), 3),
                                utils::formatNumber(deliveredPowerAt(t), 3))});

    if (hasTransient()) {
        /*
         * Скорость процесса берут из решения системы в каждый момент:
         * производные не подставляют, их даёт тот же расчёт токов.
         */
        d.steps.push_back({QStringLiteral("Переходный процесс: производные из решения цепи"),
                           QStringLiteral("\\frac{dU_{C}}{dt} = -\\frac{I_{C}}{C},\\qquad "
                                          "\\frac{dI_{L}}{dt} = \\frac{U_{L}}{L}")});
        QStringList rates;
        for (const int id : m_dynamicIds) {
            const Part* part = m_net.findPart(id);
            const Solution::Value* value = solution.value(id);
            if (!part || !value)
                continue;
            rates.push_back(part->type == PartType::Capacitor
                                ? QStringLiteral("\\frac{dU(%1)}{dt} = %2").arg(part->name,
                                                                                utils::formatNumber(value->derivative, 3))
                                : QStringLiteral("\\frac{dI(%1)}{dt} = %2").arg(part->name,
                                                                                utils::formatNumber(value->derivative, 3)));
        }
        if (!rates.isEmpty())
            d.steps.push_back({QStringLiteral("Скорость изменения в момент t"), rates.join(QStringLiteral("\\quad "))});

        QStringList constants;
        for (const int id : m_dynamicIds) {
            const Part* part = m_net.findPart(id);
            const double tau = timeConstantOf(id);
            if (part && std::isfinite(tau) && tau > 0.0)
                constants.push_back(QStringLiteral("\\tau(%1) = %2").arg(part->name, utils::formatNumber(tau, 3)));
        }
        if (!constants.isEmpty())
            d.steps.push_back({QStringLiteral("Постоянная времени по рассчитанным точкам"),
                               constants.join(QStringLiteral("\\quad "))});
    }

    const double end = transientEndTime();
    d.answerTemplate = QStringLiteral("Ток %1; напряжение на зажимах %2; мощность %3 Вт%4")
                           .arg(currentText(solution.sourceCurrent()), voltageText(terminalVoltageAt(t)),
                                utils::formatNumber(sourcePowerAt(t), 3),
                                hasTransient() && std::isfinite(end)
                                    ? QStringLiteral("; переходный процесс завершён при t = %1 с")
                                          .arg(utils::formatNumber(end, 2))
                                    : QString());
    d.steps.push_back({QStringLiteral("Ответ"), d.answerTemplate});
    return d;
}

}  // namespace lab::models
