#include "models/WorkModel.h"

#include <algorithm>
#include <cmath>

#include "utils/MathText.h"

namespace lab::models {
namespace {

constexpr double kEps = 1e-9;
constexpr int kMachineCount = 3;

}  // namespace

WorkModel::WorkModel(QObject* parent) : Simulation(parent) {
    seedDefaults();
}

QString WorkModel::title() const {
    return QStringLiteral("Совместная работа механизмов");
}

QString WorkModel::subtitle() const {
    return QStringLiteral("Производительности, раздельная и совместная работа");
}

QString WorkModel::statement() const {
    return QStringLiteral(
        "Нужно выполнить работу, приняв её за единицу. Первый механизм "
        "выполняет её один за известное время, второй — за своё. Через "
        "сколько времени работа будет выполнена, если механизмы "
        "работают вместе?");
}

QString WorkModel::problemText() const {
    using utils::formatNumber;
    QString text = QStringLiteral("Вся работа принята за 1. ");
    for (int i = 0; i < kMachineCount; ++i) {
        if (!enabled(i))
            continue;
        const double rate = rateOf(i);
        text += QStringLiteral("Механизм %1 выполняет %2 % всей работы в минуту (в одиночку — за %3 мин); ")
                    .arg(i + 1)
                    .arg(formatNumber(rate * 100.0, 1))
                    .arg(formatNumber(1.0 / rate, 1));
    }
    if (mode() == Mode::Sequential) {
        text += QStringLiteral("Сначала %1 мин работает только механизм %2, затем включаются все.")
                    .arg(formatNumber(soloTime(), 2))
                    .arg(soloIndex() + 1);
    } else {
        text += QStringLiteral("Механизмы работают вместе с самого начала.");
    }
    return text;
}

QVector<ParamSpec> WorkModel::paramSpecs() const {
    const auto slider = [](QString id, QString label, QString unit, double mn, double mx, double step,
                           double decimals, double def, QString group, QString help = {}) {
        ParamSpec s;
        s.id = std::move(id);
        s.label = std::move(label);
        s.unit = std::move(unit);
        s.kind = ParamKind::Slider;
        s.minValue = mn;
        s.maxValue = mx;
        s.stepValue = step;
        s.decimals = decimals;
        s.allowZero = false;
        s.defaultValue = def;
        s.group = std::move(group);
        s.helpText = std::move(help);
        return s;
    };
    const auto toggle = [](QString id, QString label, QString group, double def) {
        ParamSpec s;
        s.id = std::move(id);
        s.label = std::move(label);
        s.unit = QStringLiteral("механизм включён");
        s.kind = ParamKind::Toggle;
        s.group = std::move(group);
        s.defaultValue = def;
        return s;
    };

    const QString scenario = QStringLiteral("Сценарий");
    QVector<ParamSpec> specs;

    ParamSpec modeSpec;
    modeSpec.id = QStringLiteral("mode");
    modeSpec.label = QStringLiteral("Как работают механизмы");
    modeSpec.kind = ParamKind::Choice;
    modeSpec.choices = {QStringLiteral("Все работают вместе"),
                        QStringLiteral("Сначала один, потом все вместе")};
    modeSpec.group = scenario;
    modeSpec.defaultValue = 0.0;
    specs.push_back(modeSpec);

    ParamSpec solo;
    solo.id = QStringLiteral("solo");
    solo.label = QStringLiteral("Какой механизм работает один");
    solo.kind = ParamKind::Choice;
    solo.choices = {QStringLiteral("Механизм 1"), QStringLiteral("Механизм 2"), QStringLiteral("Механизм 3")};
    solo.group = scenario;
    solo.defaultValue = 0.0;
    specs.push_back(solo);

    auto soloWork = slider(QStringLiteral("soloWork"), QStringLiteral("Часть работы, выполненная в одиночку"),
                           QStringLiteral("доля"), 0.05, 0.95, 0.05, 2.0, 0.3, scenario,
                           QStringLiteral("Например, 0,3 — первый механизм один сделал 30 % всей работы."));
    specs.push_back(soloWork);

    const double defaults[kMachineCount] = {1.0 / 12.0, 1.0 / 8.0, 1.0 / 24.0};
    for (int i = 0; i < kMachineCount; ++i) {
        const QString idx = QString::number(i + 1);
        auto rate = slider(QStringLiteral("p%1").arg(idx),
                           QStringLiteral("Производительность механизма %1").arg(idx),
                           QStringLiteral("часть/мин"), 1.0 / 120.0, 1.0, 0.01, 3.0, defaults[i],
                           QStringLiteral("Механизм %1").arg(idx),
                           QStringLiteral("В одиночку механизм выполнит всю работу за %1 мин.")
                               .arg(utils::formatNumber(1.0 / defaults[i], 1)));
        rate.group = QStringLiteral("Механизм %1").arg(idx);
        specs.push_back(rate);
        specs.push_back(toggle(QStringLiteral("on%1").arg(idx), QStringLiteral("Механизм %1 участвует").arg(idx),
                               QStringLiteral("Механизм %1").arg(idx), i < 2 ? 1.0 : 0.0));
    }
    return specs;
}

double WorkModel::rateOf(int index) const {
    const double raw = param(QStringLiteral("p%1").arg(index + 1));
    return enabled(index) ? std::max(0.0, raw) : 0.0;
}

bool WorkModel::enabled(int index) const {
    return paramFlag(QStringLiteral("on%1").arg(index + 1));
}

WorkModel::Mode WorkModel::mode() const {
    return paramChoiceIndex(QStringLiteral("mode")) == 0 ? Mode::Together : Mode::Sequential;
}

int WorkModel::soloIndex() const {
    return qBound(0, paramChoiceIndex(QStringLiteral("solo")), kMachineCount - 1);
}

QVector<double> WorkModel::activeRates() const {
    QVector<double> rates;
    for (int i = 0; i < kMachineCount; ++i) {
        if (enabled(i))
            rates.push_back(rateOf(i));
    }
    return rates;
}

double WorkModel::totalRate() const {
    double sum = 0.0;
    for (int i = 0; i < kMachineCount; ++i)
        sum += rateOf(i);
    return sum;
}

double WorkModel::soloTime() const {
    if (mode() != Mode::Sequential)
        return 0.0;
    const double soloRate = rateOf(soloIndex());
    if (soloRate < kEps)
        return std::numeric_limits<double>::infinity();
    return param(QStringLiteral("soloWork")) / soloRate;
}

double WorkModel::progressAt(double t) const {
    const double timeValue = std::max(0.0, t);
    if (mode() == Mode::Sequential && enabled(soloIndex())) {
        const double ts = soloTime();
        const double soloWork = param(QStringLiteral("soloWork"));
        if (timeValue <= ts)
            return std::min(1.0, rateOf(soloIndex()) * timeValue);
        return std::min(1.0, soloWork + totalRate() * (timeValue - ts));
    }
    return std::min(1.0, totalRate() * timeValue);
}

double WorkModel::completionTime() const {
    const double total = totalRate();
    if (total < kEps)
        return std::numeric_limits<double>::infinity();
    if (mode() == Mode::Sequential && enabled(soloIndex())) {
        const double soloWork = param(QStringLiteral("soloWork"));
        const double ts = soloTime();
        if (!std::isfinite(ts))
            return std::numeric_limits<double>::infinity();
        if (soloWork >= 1.0 - kEps)
            return ts;
        return ts + (1.0 - soloWork) / total;
    }
    return 1.0 / total;
}

double WorkModel::eventTime() const {
    if (!validate().ok)
        return std::numeric_limits<double>::quiet_NaN();
    return completionTime();
}

SimState WorkModel::stateAt(double t) const {
    SimState state;
    state.time = t;
    state.primary = progressAt(t);

    const double sequentialSwitch = mode() == Mode::Sequential ? soloTime() : 0.0;

    for (int i = 0; i < kMachineCount; ++i) {
        EntityVisual machine;
        machine.id = QStringLiteral("machine%1").arg(i + 1);
        machine.label = QStringLiteral("Механизм %1").arg(i + 1);
        machine.value1 = rateOf(i);
        if (mode() == Mode::Sequential) {
            // В соло-фазе работает только выбранный механизм.
            machine.active = enabled(i) && (i == soloIndex() || t >= sequentialSwitch);
        } else {
            machine.active = enabled(i);
        }
        const bool inUse = machine.active && t < completionTime() + kEps && state.primary < 1.0 - kEps;
        machine.visible = enabled(i);
        // position — личный вклад механизма в работу (0..1).
        if (!enabled(i)) {
            machine.position = 0.0;
        } else if (mode() == Mode::Sequential) {
            const double soloRate = rateOf(i);
            if (i == soloIndex()) {
                const double ts = soloTime();
                const double soloWork = param(QStringLiteral("soloWork"));
                machine.position = t <= ts ? soloRate * t : soloWork + soloRate * std::max(0.0, t - ts);
            } else {
                machine.position = t >= sequentialSwitch ? rateOf(i) * std::max(0.0, t - sequentialSwitch) : 0.0;
            }
        } else {
            machine.position = std::min(1.0, rateOf(i) * t);
        }
        machine.value2 = inUse ? 1.0 : 0.0;  // признак «крутится прямо сейчас» (для анимации шестерни)
        machine.variant = i;
        state.entities.push_back(machine);
    }

    if (t >= completionTime() - 1e-9 && std::isfinite(completionTime())) {
        state.resolved = true;
        state.eventLabel = QStringLiteral("Вся работа выполнена");
    }
    return state;
}

double WorkModel::timePerSecond() const {
    const double total = completionTime();
    if (std::isfinite(total) && total > 0.5)
        return std::clamp(total / 12.0, 0.05, 120.0);
    return 1.0;
}

double WorkModel::duration() const {
    const double total = completionTime();
    if (std::isfinite(total))
        return std::max(1.0, total * 1.2 + 0.5);
    return 30.0;
}

double WorkModel::stepSize() const {
    return duration() / 40.0;
}

ValidationResult WorkModel::validate() const {
    ValidationResult result;
    if (totalRate() < kEps) {
        result.ok = false;
        result.message = QStringLiteral("Все механизмы выключены — работа не выполняется.");
        result.hint = QStringLiteral("Включите хотя бы один механизм.");
        return result;
    }
    if (mode() == Mode::Sequential && !enabled(soloIndex())) {
        result.ok = false;
        result.message = QStringLiteral("Выбранный механизм %1 выключен, но по сценарию он должен работать первым.")
                             .arg(soloIndex() + 1);
        result.hint = QStringLiteral("Включите этот механизм или выберите другого.");
        return result;
    }
    for (int i = 0; i < kMachineCount; ++i) {
        if (enabled(i) && rateOf(i) < kEps) {
            result.ok = false;
            result.message = QStringLiteral("Производительность механизма %1 должна быть больше нуля.").arg(i + 1);
            result.hint = QStringLiteral("Увеличьте производительность или выключите механизм.");
            return result;
        }
    }
    return result;
}

// ------------------------------------------------------------------
// Запись решения
// ------------------------------------------------------------------
Derivation WorkModel::derivation() const {
    using utils::formatNumber;
    Derivation d;
    const ValidationResult check = validate();
    d.valid = check.ok;
    d.problemText = problemText();

    const bool sequential = mode() == Mode::Sequential;
    d.unknown = QStringLiteral("Время t выполнения всей работы A = 1");

    if (!check.ok) {
        d.steps.push_back({QStringLiteral("Внимание"), QStringLiteral("\\text{%1}").arg(check.message)});
        d.answerTemplate = check.hint;
        return d;
    }

    QString given = QStringLiteral("A = 1,\\quad ");
    QString ratesSum;
    for (int i = 0; i < kMachineCount; ++i) {
        if (!enabled(i))
            continue;
        const QString idx = QString::number(i + 1);
        given += QStringLiteral("p_{%1} = %2 \\text{ мин}^{-1};\\quad").arg(idx).arg(formatNumber(rateOf(i), 3));
        if (ratesSum.isEmpty())
            ratesSum = QStringLiteral("p_{%1}").arg(idx);
        else
            ratesSum += QStringLiteral(" + p_{%1}").arg(idx);
    }
    d.steps.push_back({QStringLiteral("1. Исходные данные"), given});

    d.steps.push_back({QStringLiteral("2. Модель"),
                       QStringLiteral("A = (p_{1} + p_{2} + \\ldots) \\cdot t = "
                                      "\\sum_{i} p_{i} \\cdot t,\\qquad A = 1")});

    double total = totalRate();
    d.steps.push_back({QStringLiteral("3. Суммарная производительность"),
                       QStringLiteral("\\sum_{i \\in \\text{вкл}} p_{i} = %1 \\text{ мин}^{-1}")
                           .arg(formatNumber(total, 3))});

    if (!sequential) {
        d.steps.push_back({QStringLiteral("4. Уравнение"),
                           QStringLiteral("1 = %1 \\cdot t \\;\\Rightarrow\\; t = \\frac{1}{%1}")
                               .arg(formatNumber(total, 3))});
    } else {
        const double soloWork = param(QStringLiteral("soloWork"));
        const double ts = soloTime();
        d.steps.push_back({QStringLiteral("4. Раздельный этап"),
                           QStringLiteral("Механизм %1 один выполняет часть %2 — это занимает "
                                          "t_{1} = \\frac{A_{1}}{p_{%1}} = \\frac{%2}{%3} = %4 мин")
                               .arg(soloIndex() + 1)
                               .arg(formatNumber(soloWork, 2))
                               .arg(formatNumber(rateOf(soloIndex()), 3))
                               .arg(formatNumber(ts, 2))});
        d.steps.push_back({QStringLiteral("Совместный этап"),
                           QStringLiteral("Оставшуюся часть 1 - %1 выполняют все вместе: "
                                          "t_{2} = \\frac{1 - %1}{\\sum p_{i}} = %2 мин")
                               .arg(formatNumber(soloWork, 2))
                               .arg(formatNumber((1.0 - soloWork) / total, 2))});
        d.steps.push_back({QStringLiteral("5. Итог"),
                           QStringLiteral("t = t_{1} + t_{2} = %1 + %2 = %3 мин")
                               .arg(formatNumber(ts, 2), formatNumber((1.0 - soloWork) / total, 2),
                                    formatNumber(completionTime(), 2))});
    }

    if (!std::isfinite(total) || total < kEps) {
        d.answerTemplate = QStringLiteral("Работа не выполняется");
        return d;
    }

    if (!sequential) {
        d.steps.push_back({QStringLiteral("5. Ответ"),
                           QStringLiteral("t = %1 мин \\approx %2")
                               .arg(formatNumber(completionTime(), 2),
                                    utils::formatDuration(completionTime(), timeUnit()))});
    }

    d.answerTemplate = QStringLiteral("Работа будет выполнена за %1")
                           .arg(utils::formatDuration(completionTime(), timeUnit()));
    return d;
}

QVector<Readout> WorkModel::readouts() const {
    using utils::formatNumber;
    QVector<Readout> list;
    const SimState state = currentState();
    const double t = time();

    list.push_back({QStringLiteral("Время эксперимента"), QStringLiteral("%1 мин").arg(formatNumber(t, 2)), false});

    for (const EntityVisual& machine : state.entities) {
        if (!machine.visible)
            continue;
        list.push_back({machine.label + QStringLiteral(": вклад"),
                        QStringLiteral("%1 %").arg(formatNumber(machine.position * 100.0, 1)), false});
        list.push_back({machine.label + QStringLiteral(": производительность"),
                        QStringLiteral("%1 %/мин").arg(formatNumber(machine.value1 * 100.0, 2)), false});
    }

    list.push_back({QStringLiteral("Суммарная производительность"),
                    QStringLiteral("%1 %/мин").arg(formatNumber(totalRate() * 100.0, 2)), false});
    list.push_back({QStringLiteral("Выполнено работы"),
                    QStringLiteral("%1 %").arg(formatNumber(state.primary * 100.0, 1)), true});

    const double total = completionTime();
    list.push_back({QStringLiteral("Время выполнения всей работы"),
                    std::isfinite(total) ? QStringLiteral("%1 мин").arg(formatNumber(total, 2))
                                         : QStringLiteral("∞"),
                    true});
    if (!state.eventLabel.isEmpty())
        list.push_back({QStringLiteral("Событие"), state.eventLabel, true});
    return list;
}

QVector<HandleInfo> WorkModel::handles() const {
    QVector<HandleInfo> list;
    for (int i = 0; i < kMachineCount; ++i) {
        HandleInfo h;
        h.id = QStringLiteral("p%1").arg(i + 1);
        h.label = QStringLiteral("Производительность %1").arg(i + 1);
        h.value = rateOf(i);
        h.minValue = 1.0 / 120.0;
        h.maxValue = 1.0;
        h.step = 0.01;
        h.unit = QStringLiteral("часть/мин");
        h.axis = QStringLiteral("y");
        h.readbackLabel = QStringLiteral("Время в одиночку");
        h.readbackValue = QStringLiteral("%1 мин").arg(utils::formatNumber(1.0 / std::max(1e-6, rateOf(i)), 1));
        list.push_back(h);
    }
    if (mode() == Mode::Sequential) {
        HandleInfo part;
        part.id = QStringLiteral("soloWork");
        part.label = QStringLiteral("Часть одиночной работы");
        part.value = param(QStringLiteral("soloWork"));
        part.minValue = 0.05;
        part.maxValue = 0.95;
        part.step = 0.01;
        part.unit = QStringLiteral("доля");
        part.axis = QStringLiteral("x");
        part.readbackLabel = QStringLiteral("Время одиночного этапа");
        part.readbackValue = QStringLiteral("%1 мин").arg(utils::formatNumber(soloTime(), 2));
        list.push_back(part);
    }
    return list;
}

}  // namespace lab::models
