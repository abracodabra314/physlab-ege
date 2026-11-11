#include "models/MixtureModel.h"

#include <algorithm>
#include <cmath>

#include "utils/MathText.h"

namespace lab::models {
namespace {

constexpr double kEps = 1e-9;

}  // namespace

MixtureModel::MixtureModel(QObject* parent) : Simulation(parent) {
    seedDefaults();
}

QString MixtureModel::title() const {
    return QStringLiteral("Смешивание растворов");
}

QString MixtureModel::subtitle() const {
    return QStringLiteral("Объёмы, концентрации, итоговая концентрация смеси");
}

QString MixtureModel::statement() const {
    return QStringLiteral(
        "Первый раствор объёмом V₁ с концентрацией C₁ полностью вливают во "
        "второй раствор объёмом V₂ с концентрацией C₂. Какова концентрация "
        "получившейся смеси?");
}

QString MixtureModel::problemText() const {
    using utils::formatNumber;
    QString text;
    if (tapOpen(0))
        text += QStringLiteral("Имеется %1 л раствора с концентрацией %2 %%. ")
                    .arg(formatNumber(volumeOf(0), 1))
                    .arg(formatNumber(concentrationOf(0) * 100.0, 1));
    if (tapOpen(1))
        text += QStringLiteral("Имеется %1 л раствора с концентрацией %2 %%. ")
                    .arg(formatNumber(volumeOf(1), 1))
                    .arg(formatNumber(concentrationOf(1) * 100.0, 1));
    if (tapOpen(0) && tapOpen(1))
        text += pumpsSimultaneously()
                    ? QStringLiteral("Оба крана открываются одновременно, оба раствора перекачиваются в "
                                     "пустой смеситель.")
                    : QStringLiteral("Сначала в пустой смеситель полностью перекачивают первый раствор, "
                                     "затем — второй.");
    else
        text += QStringLiteral("Кран открыт только у этой линии, раствор перекачивается в пустой смеситель.");
    return text;
}

QVector<ParamSpec> MixtureModel::paramSpecs() const {
    const auto slider = [](QString id, QString label, QString unit, double mn, double mx, double step,
                           double decimals, bool allowZero, double def, QString group, QString help = {}) {
        ParamSpec s;
        s.id = std::move(id);
        s.label = std::move(label);
        s.unit = std::move(unit);
        s.kind = ParamKind::Slider;
        s.minValue = mn;
        s.maxValue = mx;
        s.stepValue = step;
        s.decimals = decimals;
        s.allowZero = allowZero;
        s.defaultValue = def;
        s.group = std::move(group);
        s.helpText = std::move(help);
        return s;
    };

    const QString first = QStringLiteral("Раствор 1");
    const QString second = QStringLiteral("Раствор 2");

    const auto toggle = [](QString id, QString label, QString group, double def) {
        ParamSpec s;
        s.id = std::move(id);
        s.label = std::move(label);
        s.unit = QStringLiteral("кран открыт");
        s.kind = ParamKind::Toggle;
        s.group = std::move(group);
        s.defaultValue = def;
        return s;
    };

    QVector<ParamSpec> specs;
    auto v1 = slider(QStringLiteral("v1"), QStringLiteral("Объём раствора"),
                     QStringLiteral("л"), 1.0, 200.0, 1.0, 1.0, false, 30.0, first,
                     QStringLiteral("Перетащите ёмкость на сцене — объём изменится."));
    specs.push_back(v1);
    specs.push_back(slider(QStringLiteral("c1"), QStringLiteral("Концентрация"),
                           QStringLiteral("%"), 0.0, 100.0, 0.5, 1.0, true, 12.0, first));
    specs.push_back(toggle(QStringLiteral("on1"), QStringLiteral("Кран линии 1 открыт"), first, 1.0));

    auto v2 = slider(QStringLiteral("v2"), QStringLiteral("Объём раствора"),
                     QStringLiteral("л"), 1.0, 200.0, 1.0, 1.0, false, 70.0, second,
                     QStringLiteral("Перетащите ёмкость на сцене — объём изменится."));
    specs.push_back(v2);
    specs.push_back(slider(QStringLiteral("c2"), QStringLiteral("Концентрация"),
                           QStringLiteral("%"), 0.0, 100.0, 0.5, 1.0, true, 42.0, second));
    specs.push_back(toggle(QStringLiteral("on2"), QStringLiteral("Кран линии 2 открыт"), second, 1.0));

    const QString schedule = QStringLiteral("Порядок перекачки");
    ParamSpec mode;
    mode.id = QStringLiteral("mode");
    mode.label = QStringLiteral("Как открывать краны");
    mode.kind = ParamKind::Choice;
    mode.choices = {QStringLiteral("По очереди: сначала раствор 1, затем раствор 2"),
                    QStringLiteral("Одновременно: оба крана открыты")};
    mode.group = schedule;
    mode.defaultValue = 0.0;
    specs.push_back(mode);
    return specs;
}

bool MixtureModel::tapOpen(int number) const {
    return paramFlag(number == 0 ? QStringLiteral("on1") : QStringLiteral("on2"));
}

bool MixtureModel::pumpsSimultaneously() const {
    return paramChoiceIndex(QStringLiteral("mode")) == 1;
}

double MixtureModel::volumeOf(int number) const {
    return std::max(0.0, param(number == 0 ? QStringLiteral("v1") : QStringLiteral("v2")));
}

double MixtureModel::concentrationOf(int number) const {
    // Пользователь задаёт проценты; внутри — доли.
    return param(number == 0 ? QStringLiteral("c1") : QStringLiteral("c2")) / 100.0;
}

double MixtureModel::soluteOf(int number) const {
    return volumeOf(number) * concentrationOf(number);
}

double MixtureModel::pumpDuration(int number) const {
    return volumeOf(number) / kPumpRate;
}

double MixtureModel::lineStartTime(int number) const {
    // Закрытый кран: линия не работает вовсе.
    if (!tapOpen(number))
        return std::numeric_limits<double>::infinity();
    if (pumpsSimultaneously())
        return 0.0;
    // По очереди: первая линия стартует сразу, вторая — когда первая опустеет.
    if (number == 0)
        return 0.0;
    return tapOpen(0) ? pumpDuration(0) : 0.0;
}

double MixtureModel::secondPhaseStart() const {
    return lineStartTime(1);
}

double MixtureModel::pumpSwitchTime() const {
    return lineStartTime(1);
}

double MixtureModel::transferredAt(int number, double t) const {
    const double start = lineStartTime(number);
    if (!std::isfinite(start) || t <= start)
        return 0.0;
    return std::min(volumeOf(number), kPumpRate * (t - start));
}

double MixtureModel::currentConcentrationAt(double t) const {
    const double totalVolume = transferredAt(0, t) + transferredAt(1, t);
    if (totalVolume < kEps)
        return 0.0;
    const double solute = transferredAt(0, t) * concentrationOf(0) + transferredAt(1, t) * concentrationOf(1);
    return solute / totalVolume;
}

double MixtureModel::totalVolume() const {
    // В смеситель попадают только растворы линий с открытыми кранами.
    double sum = 0.0;
    for (int i = 0; i < 2; ++i) {
        if (tapOpen(i))
            sum += volumeOf(i);
    }
    return sum;
}

double MixtureModel::totalSolute() const {
    double sum = 0.0;
    for (int i = 0; i < 2; ++i) {
        if (tapOpen(i))
            sum += soluteOf(i);
    }
    return sum;
}

double MixtureModel::finalConcentration() const {
    const double volume = totalVolume();
    if (volume < kEps)
        return 0.0;
    return totalSolute() / volume;
}

double MixtureModel::eventTime() const {
    if (!validate().ok)
        return std::numeric_limits<double>::quiet_NaN();
    // Момент, когда последняя открытая линия закончит перекачку.
    double finish = 0.0;
    for (int i = 0; i < 2; ++i) {
        const double start = lineStartTime(i);
        if (!std::isfinite(start))
            continue;
        finish = std::max(finish, start + pumpDuration(i));
    }
    return finish;
}

SimState MixtureModel::stateAt(double t) const {
    SimState state;
    state.time = t;

    const double firstPoured = transferredAt(0, t);
    const double secondPoured = transferredAt(1, t);
    const double mixedVolume = firstPoured + secondPoured;
    const double mixedConcentration = currentConcentrationAt(t);

    state.primary = mixedConcentration;                 // доля
    state.secondary = mixedVolume;

    const double firstLevel = volumeOf(0) > kEps ? (volumeOf(0) - firstPoured) / volumeOf(0) : 0.0;
    const double secondLevel = volumeOf(1) > kEps ? (volumeOf(1) - secondPoured) / volumeOf(1) : 0.0;

    EntityVisual vessel1;
    vessel1.id = QStringLiteral("vessel1");
    vessel1.label = QStringLiteral("Раствор 1");
    vessel1.position = firstLevel;              // 1 = полный, 0 = пустой
    vessel1.value1 = concentrationOf(0) * 100.0;
    vessel1.value2 = volumeOf(0);
    // Линия активна, только если кран открыт и перекачка ещё идёт.
    vessel1.active = tapOpen(0) && firstPoured > kEps && firstLevel > kEps;
    vessel1.variant = 0;

    EntityVisual vessel2;
    vessel2.id = QStringLiteral("vessel2");
    vessel2.label = QStringLiteral("Раствор 2");
    vessel2.position = secondLevel;
    vessel2.value1 = concentrationOf(1) * 100.0;
    vessel2.value2 = volumeOf(1);
    const bool secondRunning = tapOpen(1) && t > lineStartTime(1) + kEps && secondPoured < volumeOf(1) - kEps;
    vessel2.active = secondRunning;
    vessel2.variant = 1;

    EntityVisual mixer;
    mixer.id = QStringLiteral("mixer");
    mixer.label = QStringLiteral("Смеситель");
    const double capacity = totalVolume();
    mixer.position = capacity > kEps ? mixedVolume / capacity : 0.0;
    mixer.value1 = mixedConcentration * 100.0;
    mixer.value2 = mixedVolume;
    mixer.active = mixedVolume > kEps;
    mixer.variant = 2;

    state.entities = {vessel1, vessel2, mixer};

    if (t >= eventTime() - 1e-9) {
        state.resolved = true;
        if (tapOpen(0) && tapOpen(1))
            state.eventLabel = QStringLiteral("Оба раствора перелиты, смесь готова");
        else
            state.eventLabel = QStringLiteral("Раствор открытой линии перелит, смесь готова");
    }
    return state;
}

double MixtureModel::timePerSecond() const {
    const double total = eventTime();
    if (std::isfinite(total) && total > 0.2)
        return std::clamp(total / 12.0, 0.2, 40.0);
    return 2.0;
}

double MixtureModel::duration() const {
    const double total = eventTime();
    if (std::isfinite(total))
        return total * 1.2 + 0.5;
    return 10.0;
}

double MixtureModel::stepSize() const {
    return duration() / 40.0;
}

ValidationResult MixtureModel::validate() const {
    ValidationResult result;
    if (!tapOpen(0) && !tapOpen(1)) {
        result.ok = false;
        result.message = QStringLiteral("Оба крана закрыты — переливание не выполняется.");
        result.hint = QStringLiteral("Откройте кран хотя бы одной линии.");
        return result;
    }
    if (totalVolume() < kEps) {
        result.ok = false;
        result.message = QStringLiteral("Оба раствора пусты — смешивать нечего.");
        result.hint = QStringLiteral("Увеличьте объём хотя бы одной ёмкости.");
        return result;
    }
    return result;
}

// ------------------------------------------------------------------
// Запись решения
// ------------------------------------------------------------------
Derivation MixtureModel::derivation() const {
    using utils::formatNumber;
    Derivation d;
    const ValidationResult check = validate();
    d.valid = check.ok;
    d.problemText = problemText();
    d.unknown = QStringLiteral("Концентрация C получившейся смеси");

    if (!check.ok) {
        d.steps.push_back({QStringLiteral("Внимание"), QStringLiteral("\\text{%1}").arg(check.message)});
        d.answerTemplate = check.hint;
        return d;
    }

    // В расчёт входят только растворы линий с открытыми кранами.
    const double v1 = tapOpen(0) ? volumeOf(0) : 0.0;
    const double v2 = tapOpen(1) ? volumeOf(1) : 0.0;
    const double c1 = concentrationOf(0);
    const double c2 = concentrationOf(1);
    const double m1 = tapOpen(0) ? soluteOf(0) : 0.0;
    const double m2 = tapOpen(1) ? soluteOf(1) : 0.0;

    d.steps.push_back({QStringLiteral("1. Исходные данные"),
                       QStringLiteral("V_{1} = %1 \\text{ л},\\; C_{1} = %2 \\text{ \\%};\\quad "
                                      "V_{2} = %3 \\text{ л},\\; C_{2} = %4 \\text{ \\%}")
                           .arg(formatNumber(v1, 1), formatNumber(c1 * 100.0, 1),
                                formatNumber(v2, 1), formatNumber(c2 * 100.0, 1))});

    if (tapOpen(0) && tapOpen(1)) {
        d.steps.push_back({QStringLiteral("Порядок перекачки"),
                           pumpsSimultaneously()
                               ? QStringLiteral("\\text{Оба крана открыты одновременно: } "
                                                "V(t) = 2q t \\\\text{ при } t \\le T_{\\min},\\; q = 60 "
                                                "\\text{ л/мин}")
                               : QStringLiteral("\\text{Сначала линия 1 (0…%1 мин), затем линия 2 "
                                                "(%1…%2 мин)}")
                                     .arg(formatNumber(pumpDuration(0), 2), formatNumber(eventTime(), 2))});
    }

    d.steps.push_back({QStringLiteral("2. Масса вещества в каждом растворе"),
                       QStringLiteral("m = V \\cdot C")});
    d.steps.push_back({QStringLiteral("Подстановка"),
                       QStringLiteral("m_{1} = V_{1} C_{1} = %1 \\cdot %2 = %3 \\text{ л},\\qquad "
                                      "m_{2} = V_{2} C_{2} = %4 \\cdot %5 = %6 \\text{ л}")
                           .arg(formatNumber(v1, 1), formatNumber(c1, 4), formatNumber(m1, 3),
                                formatNumber(v2, 1), formatNumber(c2, 4), formatNumber(m2, 3))});

    d.steps.push_back({QStringLiteral("3. Закон сохранения смеси"),
                       QStringLiteral("V = V_{1} + V_{2},\\qquad m = m_{1} + m_{2}")});
    d.steps.push_back({QStringLiteral("Подстановка"),
                       QStringLiteral("V = %1 + %2 = %3 \\text{ л},\\qquad m = %4 + %5 = %6 \\text{ л}")
                           .arg(formatNumber(v1, 1), formatNumber(v2, 1), formatNumber(v1 + v2, 1),
                                formatNumber(m1, 3), formatNumber(m2, 3), formatNumber(m1 + m2, 3))});

    d.steps.push_back({QStringLiteral("4. Формула концентрации смеси"),
                       QStringLiteral("C = \\frac{V_{1} C_{1} + V_{2} C_{2}}{V_{1} + V_{2}}")});
    d.steps.push_back({QStringLiteral("5. Подстановка"),
                       QStringLiteral("C = \\frac{%1 \\cdot %2 + %3 \\cdot %4}{%1 + %3} = "
                                      "\\frac{%5}{%6}")
                           .arg(formatNumber(v1, 1), formatNumber(c1, 4), formatNumber(v2, 1),
                                formatNumber(c2, 4), formatNumber(m1 + m2, 3), formatNumber(v1 + v2, 1))});

    const double cMix = finalConcentration();
    d.steps.push_back({QStringLiteral("6. Ответ"),
                       QStringLiteral("C = %1 \\approx %2 \\text{ \\%}")
                           .arg(formatNumber(cMix, 5), formatNumber(cMix * 100.0, 2))});

    d.answerTemplate = QStringLiteral("Концентрация смеси — %1 %").arg(formatNumber(cMix * 100.0, 2));
    return d;
}

QVector<Readout> MixtureModel::readouts() const {
    using utils::formatNumber;
    QVector<Readout> list;
    const SimState state = currentState();
    const double t = time();

    list.push_back({QStringLiteral("Время эксперимента"), QStringLiteral("%1 мин").arg(formatNumber(t, 2)), false});

    for (int i = 0; i < 2; ++i) {
        const QString name = QStringLiteral("Раствор %1").arg(i + 1);
        const double remaining = volumeOf(i) - transferredAt(i, t);
        list.push_back({name + QStringLiteral(": кран"),
                        tapOpen(i) ? QStringLiteral("открыт") : QStringLiteral("закрыт"), false});
        list.push_back({name + QStringLiteral(": осталось в ёмкости"),
                        QStringLiteral("%1 л").arg(formatNumber(remaining, 1)), false});
        list.push_back({name + QStringLiteral(": вещество (m = V·C)"),
                        QStringLiteral("%1 л").arg(formatNumber(soluteOf(i), 3)), false});
    }

    list.push_back({QStringLiteral("Объём смеси"), QStringLiteral("%1 л").arg(formatNumber(state.secondary, 1)), false});
    list.push_back({QStringLiteral("Вещества в смеси"), QStringLiteral("%1 л").arg(formatNumber(
                        transferredAt(0, t) * concentrationOf(0) + transferredAt(1, t) * concentrationOf(1), 3)),
                    false});
    list.push_back({QStringLiteral("Концентрация смеси сейчас"),
                    QStringLiteral("%1 %").arg(formatNumber(state.primary * 100.0, 2)), true});
    list.push_back({QStringLiteral("Концентрация после переливания"),
                    QStringLiteral("%1 %").arg(formatNumber(finalConcentration() * 100.0, 2)), true});

    if (!state.eventLabel.isEmpty())
        list.push_back({QStringLiteral("Событие"), state.eventLabel, true});
    return list;
}

QVector<HandleInfo> MixtureModel::handles() const {
    QVector<HandleInfo> list;
    for (int i = 0; i < 2; ++i) {
        HandleInfo h;
        h.id = QStringLiteral("v%1").arg(i + 1);
        h.label = QStringLiteral("Объём ёмкости %1").arg(i + 1);
        h.value = volumeOf(i);
        h.minValue = 1.0;
        h.maxValue = 200.0;
        h.step = 0.5;
        h.unit = QStringLiteral("л");
        h.axis = QStringLiteral("y");
        h.readbackLabel = QStringLiteral("Вещество в растворе");
        h.readbackValue = QStringLiteral("%1 л").arg(utils::formatNumber(soluteOf(i), 2));
        list.push_back(h);
    }
    return list;
}

}  // namespace lab::models
