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
    return QStringLiteral("Производительности, моменты включения, A = Σ pᵢ·(t − tᵢ)");
}

QString WorkModel::statement() const {
    return QStringLiteral(
        "Нужно выполнить работу, приняв её за единицу. Каждый механизм "
        "работает со своей производительностью и включается в своё время. "
        "Чер сколько времени работа будет выполнена?");
}

QString WorkModel::problemText() const {
    using utils::formatNumber;
    QString text = QStringLiteral("Вся работа принята за 1. ");
    bool any = false;
    for (int i = 0; i < kMachineCount; ++i) {
        if (!enabled(i))
            continue;
        any = true;
        const double rate = rateOf(i);
        text += QStringLiteral("Механизм %1 выполняет %2 % всей работы в минуту (в одиночку — за %3 мин), "
                               "включается через %4 мин; ")
                    .arg(i + 1)
                    .arg(formatNumber(rate * 100.0, 1))
                    .arg(formatNumber(1.0 / rate, 1))
                    .arg(formatNumber(startTimeOf(i), 2));
    }
    if (!any)
        text += QStringLiteral("Все механизмы выключены.");
    return text;
}

QVector<ParamSpec> WorkModel::paramSpecs() const {
    const auto slider = [](QString id, QString label, QString unit, double mn, double mx, double step,
                           double decimals, double def, QString group, QString help = {}, bool allowZero = false) {
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

    const double defaults[kMachineCount] = {1.0 / 12.0, 1.0 / 8.0, 1.0 / 24.0};
    QVector<ParamSpec> specs;
    for (int i = 0; i < kMachineCount; ++i) {
        const QString idx = QString::number(i + 1);
        const QString group = QStringLiteral("Механизм %1").arg(idx);
        specs.push_back(slider(QStringLiteral("p%1").arg(idx),
                               QStringLiteral("Производительность механизма %1").arg(idx),
                               QStringLiteral("часть/мин"), 1.0 / 120.0, 1.0, 0.01, 3.0, defaults[i], group,
                               QStringLiteral("В одиночку механизм выполнит всю работу за %1 мин.")
                                   .arg(utils::formatNumber(1.0 / defaults[i], 1))));
        specs.push_back(slider(QStringLiteral("start%1").arg(idx),
                               QStringLiteral("Время включения механизма %1").arg(idx),
                               QStringLiteral("мин"), 0.0, 60.0, 0.5, 2.0, 0.0, group,
                               QStringLiteral("0 — механизм работает с самого начала."), /*allowZero=*/true));
        specs.push_back(toggle(QStringLiteral("on%1").arg(idx), QStringLiteral("Механизм %1 участвует").arg(idx),
                               group, i < 2 ? 1.0 : 0.0));
    }
    return specs;
}

double WorkModel::rateOf(int index) const {
    if (index < 0 || index >= kMachineCount)
        return 0.0;
    const double raw = param(QStringLiteral("p%1").arg(index + 1));
    return enabled(index) ? std::max(0.0, raw) : 0.0;
}

double WorkModel::startTimeOf(int index) const {
    if (index < 0 || index >= kMachineCount)
        return 0.0;
    return std::max(0.0, param(QStringLiteral("start%1").arg(index + 1)));
}

bool WorkModel::enabled(int index) const {
    if (index < 0 || index >= kMachineCount)
        return false;
    return paramFlag(QStringLiteral("on%1").arg(index + 1));
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

double WorkModel::progressAt(double t) const {
    const double timeValue = std::max(0.0, t);
    double sum = 0.0;
    for (int i = 0; i < kMachineCount; ++i)
        sum += rateOf(i) * std::max(0.0, timeValue - startTimeOf(i));
    return std::clamp(sum, 0.0, 1.0);
}

double WorkModel::contributionOf(int index, double t) const {
    if (!enabled(index))
        return 0.0;
    const double running = std::max(0.0, std::max(0.0, t) - startTimeOf(index));
    return std::clamp(rateOf(index) * running, 0.0, 1.0);
}

bool WorkModel::isWorking(int index, double t) const {
    return enabled(index) && t > startTimeOf(index) + kEps && progressAt(t) < 1.0 - kEps &&
           contributionOf(index, t) < 1.0 - kEps;
}

double WorkModel::completionTime() const {
    // Кусочно-линейная функция A(t): идём по возрастанию моментов включения;
    // на каждом интервале суммарная производительность постоянна.
    QVector<int> order;
    for (int i = 0; i < kMachineCount; ++i) {
        if (enabled(i))
            order.push_back(i);
    }
    if (order.isEmpty())
        return std::numeric_limits<double>::infinity();
    std::sort(order.begin(), order.end(),
              [this](int a, int b) { return startTimeOf(a) < startTimeOf(b); });

    double work = 0.0;
    double rate = 0.0;
    double prev = startTimeOf(order.first());
    qsizetype next = 0;
    while (next < order.size()) {
        const double onTime = startTimeOf(order[next]);
        if (onTime > prev + kEps) {
            if (rate > kEps) {
                const double need = (1.0 - work) / rate;
                if (prev + need <= onTime + kEps)
                    return prev + need;
                work += rate * (onTime - prev);
            }
            prev = onTime;
        }
        while (next < order.size() && startTimeOf(order[next]) <= prev + kEps) {
            rate += rateOf(order[next]);
            ++next;
        }
    }
    if (rate > kEps)
        return prev + (1.0 - work) / rate;
    return std::numeric_limits<double>::infinity();
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
    state.secondary = completionTime();

    double contributionSum = 0.0;
    for (int i = 0; i < kMachineCount; ++i) {
        EntityVisual machine;
        machine.id = QStringLiteral("machine%1").arg(i + 1);
        machine.label = QStringLiteral("Механизм %1").arg(i + 1);
        machine.value1 = rateOf(i);
        machine.value2 = startTimeOf(i);
        // position — личный вклад механизма в работу (0..1).
        machine.position = contributionOf(i, t);
        machine.active = isWorking(i, t);
        machine.visible = enabled(i);
        machine.variant = i;
        contributionSum += machine.position;
        state.entities.push_back(machine);
    }
    // Вклады механизмов в сумме дают выполненную работу.
    state.primary = std::clamp(std::min(contributionSum, 1.0), 0.0, 1.0);

    const double finish = completionTime();
    if (std::isfinite(finish) && t >= finish - 1e-9) {
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
    for (int i = 0; i < kMachineCount; ++i) {
        if (enabled(i) && rateOf(i) < kEps) {
            result.ok = false;
            result.message = QStringLiteral("Производительность механизма %1 должна быть больше нуля.").arg(i + 1);
            result.hint = QStringLiteral("Увеличьте производительность или выключите механизм.");
            return result;
        }
    }
    if (!std::isfinite(completionTime())) {
        result.ok = false;
        result.message = QStringLiteral("При выбранных временах включения работа никогда не завершится.");
        result.hint = QStringLiteral("Уменьшите времена включения или включите дополнительные механизмы.");
        return result;
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
    d.unknown = QStringLiteral("Время t выполнения всей работы A = 1");

    if (!check.ok) {
        d.steps.push_back({QStringLiteral("Внимание"), QStringLiteral("\\text{%1}").arg(check.message)});
        d.answerTemplate = check.hint;
        return d;
    }

    QString given = QStringLiteral("A = 1,\\quad ");
    for (int i = 0; i < kMachineCount; ++i) {
        if (!enabled(i))
            continue;
        const QString idx = QString::number(i + 1);
        given += QStringLiteral("p_{%1} = %2 \\text{ мин}^{-1},\\; t_{0,%1} = %3 \\text{ мин};\\quad")
                     .arg(idx)
                     .arg(formatNumber(rateOf(i), 3))
                     .arg(formatNumber(startTimeOf(i), 2));
    }
    d.steps.push_back({QStringLiteral("1. Исходные данные"), given});

    d.steps.push_back({QStringLiteral("2. Модель"),
                       QStringLiteral("A(t) = \\sum_{i} p_{i} \\cdot \\max(0,\\, t - t_{0,i}),\\qquad A = 1")});

    // Этапы: от момента включения к моменту включения; на каждом интервале
    // суммарная производительность постоянна.
    QVector<int> order;
    for (int i = 0; i < kMachineCount; ++i) {
        if (enabled(i))
            order.push_back(i);
    }
    std::sort(order.begin(), order.end(),
              [this](int a, int b) { return startTimeOf(a) < startTimeOf(b); });

    struct Stage {
        double startTime = 0.0;   // начало этапа
        double rate = 0.0;        // суммарная производительность на этапе
        double workStart = 0.0;   // выполненная работа к началу этапа
    };
    QVector<Stage> stages;
    {
        double work = 0.0;
        double rate = 0.0;
        double prevTime = startTimeOf(order.first());
        qsizetype next = 0;
        while (next < order.size()) {
            const double onTime = startTimeOf(order[next]);
            if (onTime > prevTime + kEps) {
                if (rate > kEps)
                    work += rate * (onTime - prevTime);
                prevTime = onTime;
            }
            while (next < order.size() && startTimeOf(order[next]) <= prevTime + kEps) {
                rate += rateOf(order[next]);
                ++next;
            }
            stages.push_back({prevTime, rate, work});
        }
    }

    QString stagesText;
    for (int s = 0; s + 1 < stages.size(); ++s) {
        const Stage& stage = stages[s];
        const double gained = stage.rate * (stages[s + 1].startTime - stage.startTime);
        stagesText += QStringLiteral("на [%1; %2] мин: ΔA = %3·(%2 − %1) = %4; ")
                          .arg(formatNumber(stage.startTime, 2))
                          .arg(formatNumber(stages[s + 1].startTime, 2))
                          .arg(formatNumber(stage.rate, 3))
                          .arg(formatNumber(gained, 3));
    }
    if (!stagesText.isEmpty())
        d.steps.push_back({QStringLiteral("3. Этапы включения"), QStringLiteral("\\text{%1}").arg(stagesText)});

    d.steps.push_back({QStringLiteral("4. Суммарная производительность после всех включений"),
                       QStringLiteral("\\sum_{i \\in \\text{вкл}} p_{i} = %1 \\text{ мин}^{-1}")
                           .arg(formatNumber(totalRate(), 3))});

    const double finish = completionTime();
    // Этап, на котором работа завершается.
    Stage finalStage = stages.last();
    for (const Stage& stage : stages) {
        const double endOfWork = stage.workStart + stage.rate * (finish - stage.startTime);
        if (endOfWork >= 1.0 - kEps) {
            finalStage = stage;
            break;
        }
    }

    d.steps.push_back({QStringLiteral("5. Уравнение финального этапа"),
                       QStringLiteral("При t ≥ %1 мин суммарная производительность %2: "
                                      "A(t) = %3 + %2·(t − %1)")
                           .arg(formatNumber(finalStage.startTime, 2), formatNumber(finalStage.rate, 3),
                                formatNumber(finalStage.workStart, 4))});
    d.steps.push_back({QStringLiteral("6. Решение"),
                       QStringLiteral("1 = %1 + %2·(t − %3) \\;\\Rightarrow\\; "
                                      "t = %3 + \\frac{1 - %1}{%2} = %4 \\text{ мин}")
                           .arg(formatNumber(finalStage.workStart, 4), formatNumber(finalStage.rate, 3),
                                formatNumber(finalStage.startTime, 2), formatNumber(finish, 2))});

    d.steps.push_back({QStringLiteral("7. Проверка"),
                       QStringLiteral("A(%1) = %2 ⇒ работа выполнена полностью")
                           .arg(formatNumber(finish, 2), formatNumber(progressAt(finish), 3))});

    d.steps.push_back({QStringLiteral("8. Ответ"),
                       QStringLiteral("t = %1 мин \\approx %2")
                           .arg(formatNumber(finish, 2), utils::formatDuration(finish, timeUnit()))});

    d.answerTemplate = QStringLiteral("Работа будет выполнена за %1")
                           .arg(utils::formatDuration(finish, timeUnit()));
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
        list.push_back({machine.label + QStringLiteral(": включение"),
                        QStringLiteral("через %1 мин").arg(formatNumber(machine.value2, 2)), false});
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
    return list;
}

}  // namespace lab::models
