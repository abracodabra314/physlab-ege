#include "models/TankModel.h"

#include <algorithm>
#include <cmath>

#include "utils/MathText.h"

namespace lab::models {
namespace {

constexpr double kEps = 1e-9;
constexpr double kLitersPerCubeMeter = 1000.0;

}  // namespace

TankModel::TankModel(QObject* parent) : Simulation(parent) {
    seedDefaults();
}

QString TankModel::title() const {
    return QStringLiteral("Резервуар с водой");
}

QString TankModel::subtitle() const {
    return QStringLiteral("Краны наполнения и слива, уровень и объём воды");
}

QString TankModel::statement() const {
    return QStringLiteral(
        "В резервуар проведены две трубы наполнения и две трубы слива. "
        "Определите, через сколько минут резервуар наполнится доверху "
        "(или опустеет), и как меняется уровень воды со временем.");
}

QString TankModel::problemText() const {
    using utils::formatNumber;
    const Flows f = flows();
    QString text = QStringLiteral("Резервуар имеет основание %1 м × %1 м и высоту %2 м; начальная высота "
                                  "воды — %3 м.")
                       .arg(formatNumber(width(), 1))
                       .arg(formatNumber(height(), 1))
                       .arg(formatNumber(levelAt(0.0), 2));
    text += QStringLiteral(" Краны наполнения дают %1 и %2 л/мин, трубы слива уносят %3 и %4 л/мин.")
                .arg(formatNumber(f.qIn1, 0))
                .arg(formatNumber(f.qIn2, 0))
                .arg(formatNumber(f.qOut1, 0))
                .arg(formatNumber(f.qOut2, 0));
    return text;
}

QVector<ParamSpec> TankModel::paramSpecs() const {
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
    const auto toggle = [](QString id, QString label, QString group) {
        ParamSpec s;
        s.id = std::move(id);
        s.label = std::move(label);
        s.unit = QStringLiteral("кран открыт");
        s.kind = ParamKind::Toggle;
        s.group = std::move(group);
        s.defaultValue = 0.0;
        return s;
    };

    const QString tankGroup = QStringLiteral("Резервуар");
    const QString fillGroup = QStringLiteral("Трубы наполнения");
    const QString drainGroup = QStringLiteral("Трубы слива");

    QVector<ParamSpec> specs;
    specs.push_back(slider(QStringLiteral("width"), QStringLiteral("Сторона основания"),
                           QStringLiteral("м"), 0.5, 5.0, 0.1, 1.0, false, 2.0, tankGroup));
    specs.push_back(slider(QStringLiteral("height"), QStringLiteral("Высота резервуара"),
                           QStringLiteral("м"), 0.5, 5.0, 0.1, 1.0, false, 2.5, tankGroup));
    specs.push_back(slider(QStringLiteral("level"), QStringLiteral("Начальный уровень воды"),
                           QStringLiteral("м"), 0.0, 5.0, 0.05, 2.0, true, 0.5, tankGroup,
                           QStringLiteral("Перетащите воду мышью, чтобы задать уровень.")));

    specs.push_back(slider(QStringLiteral("qin1"), QStringLiteral("Кран 1"),
                           QStringLiteral("л/мин"), 0.0, 300.0, 1.0, 0.0, true, 60.0, fillGroup));
    // Кран 1 открыт по умолчанию: иначе начальный сценарий («все закрыты»)
    // не показывает никакого изменения уровня.
    auto in1on = toggle(QStringLiteral("in1on"), QStringLiteral("Кран 1 включён"), fillGroup);
    in1on.defaultValue = 1.0;
    specs.push_back(in1on);
    specs.push_back(slider(QStringLiteral("qin2"), QStringLiteral("Кран 2"),
                           QStringLiteral("л/мин"), 0.0, 300.0, 1.0, 0.0, true, 0.0, fillGroup));
    specs.push_back(toggle(QStringLiteral("in2on"), QStringLiteral("Кран 2 включён"), fillGroup));

    specs.push_back(slider(QStringLiteral("qout1"), QStringLiteral("Слив 1"),
                           QStringLiteral("л/мин"), 0.0, 300.0, 1.0, 0.0, true, 0.0, drainGroup));
    specs.push_back(toggle(QStringLiteral("out1on"), QStringLiteral("Слив 1 открыт"), drainGroup));
    specs.push_back(slider(QStringLiteral("qout2"), QStringLiteral("Слив 2"),
                           QStringLiteral("л/мин"), 0.0, 300.0, 1.0, 0.0, true, 0.0, drainGroup));
    specs.push_back(toggle(QStringLiteral("out2on"), QStringLiteral("Слив 2 открыт"), drainGroup));
    return specs;
}

double TankModel::width() const {
    return param(QStringLiteral("width"));
}

double TankModel::height() const {
    return param(QStringLiteral("height"));
}

double TankModel::initialVolume() const {
    // Уровень задан в метрах: пересчитываем в объём через площадь основания.
    const double level = std::clamp(param(QStringLiteral("level")), 0.0, height());
    return level * baseArea() * kLitersPerCubeMeter;
}

double TankModel::baseArea() const {
    return width() * width();
}

double TankModel::capacity() const {
    return baseArea() * height() * kLitersPerCubeMeter;
}

TankModel::Flows TankModel::flows() const {
    Flows f;
    if (paramFlag(QStringLiteral("in1on")))
        f.qIn1 = param(QStringLiteral("qin1"));
    if (paramFlag(QStringLiteral("in2on")))
        f.qIn2 = param(QStringLiteral("qin2"));
    if (paramFlag(QStringLiteral("out1on")))
        f.qOut1 = param(QStringLiteral("qout1"));
    if (paramFlag(QStringLiteral("out2on")))
        f.qOut2 = param(QStringLiteral("qout2"));
    return f;
}

double TankModel::inflowRate() const {
    const Flows f = flows();
    return f.qIn1 + f.qIn2;
}

bool TankModel::tapOpen(const QString& tapId) const {
    return paramFlag(QStringLiteral("%1on").arg(tapId));
}

double TankModel::tapRate(const QString& tapId) const {
    // Идентификаторы параметров: «qin1», «qout2» — к ним добавляется q.
    return tapOpen(tapId) ? param(QStringLiteral("q%1").arg(tapId)) : 0.0;
}

double TankModel::outflowRate() const {
    const Flows f = flows();
    return f.qOut1 + f.qOut2;
}

double TankModel::netRate() const {
    return inflowRate() - outflowRate();
}

double TankModel::volumeAt(double t) const {
    const double volume = initialVolume() + netRate() * std::max(0.0, t);
    return std::clamp(volume, 0.0, capacity());
}

double TankModel::levelAt(double t) const {
    const double area = baseArea();
    if (area < kEps)
        return 0.0;
    return volumeAt(t) / area / kLitersPerCubeMeter;
}

double TankModel::timeToFill() const {
    const double rate = netRate();
    if (rate <= kEps)
        return std::numeric_limits<double>::infinity();
    return (capacity() - initialVolume()) / rate;
}

double TankModel::timeToEmpty() const {
    const double rate = -netRate();
    if (rate <= kEps)
        return std::numeric_limits<double>::infinity();
    return initialVolume() / rate;
}

double TankModel::eventTime() const {
    if (!validate().ok)
        return std::numeric_limits<double>::quiet_NaN();
    const double fill = timeToFill();
    const double empty = timeToEmpty();
    double result = std::numeric_limits<double>::infinity();
    if (std::isfinite(fill) && fill > -kEps)
        result = std::min(result, std::max(0.0, fill));
    if (std::isfinite(empty) && empty > -kEps)
        result = std::min(result, std::max(0.0, empty));
    if (!std::isfinite(result))
        return std::numeric_limits<double>::quiet_NaN();
    return result;
}

SimState TankModel::stateAt(double t) const {
    SimState state;
    state.time = t;

    const Flows f = flows();
    const double volume = volumeAt(t);
    const double level = levelAt(t);
    state.primary = level;
    state.secondary = volume;

    const auto makeTap = [](QString id, QString label, double rate, bool active, int variant) {
        EntityVisual tap;
        tap.id = std::move(id);
        tap.label = std::move(label);
        tap.position = rate;
        tap.value1 = rate;
        tap.active = active && rate > kEps;
        tap.variant = variant;
        return tap;
    };
    state.entities = {
        makeTap(QStringLiteral("in1"), QStringLiteral("Кран 1"), f.qIn1, paramFlag(QStringLiteral("in1on")), 0),
        makeTap(QStringLiteral("in2"), QStringLiteral("Кран 2"), f.qIn2, paramFlag(QStringLiteral("in2on")), 4),
        makeTap(QStringLiteral("out1"), QStringLiteral("Слив 1"), f.qOut1, paramFlag(QStringLiteral("out1on")), 1),
        makeTap(QStringLiteral("out2"), QStringLiteral("Слив 2"), f.qOut2, paramFlag(QStringLiteral("out2on")), 5),
    };

    if (volume >= capacity() - kEps && netRate() > kEps) {
        state.resolved = true;
        state.eventLabel = QStringLiteral("Резервуар заполнен доверху");
    } else if (volume <= kEps && netRate() < -kEps) {
        state.resolved = true;
        state.eventLabel = QStringLiteral("Резервуар полностью опустошён");
    }
    return state;
}

double TankModel::timePerSecond() const {
    const double event = eventTime();
    if (std::isfinite(event) && event > 0.5)
        return std::clamp(event / 15.0, 0.05, 60.0);
    const double rate = std::max(1.0, std::abs(netRate()));
    return std::clamp(capacity() / rate / 15.0, 0.05, 60.0);
}

double TankModel::duration() const {
    const double event = eventTime();
    double horizon = 5.0;
    if (std::isfinite(event))
        horizon = std::max(horizon, event * 1.15 + 1.0);
    else
        horizon = std::max(horizon, capacity() / std::max(1.0, std::abs(netRate())) * 0.5);
    return std::min(horizon, 10000.0);
}

double TankModel::stepSize() const {
    return duration() / 40.0;
}

ValidationResult TankModel::validate() const {
    ValidationResult result;
    if (width() < kEps || height() < kEps) {
        result.ok = false;
        result.message = QStringLiteral("Размеры резервуара должны быть больше нуля.");
        result.hint = QStringLiteral("Увеличьте сторону основания или высоту резервуара.");
        return result;
    }
    if (inflowRate() < kEps && outflowRate() < kEps) {
        result.ok = false;
        result.message = QStringLiteral("Все краны закрыты — уровень воды не будет меняться.");
        result.hint = QStringLiteral("Откройте хотя бы один кран наполнения или слива.");
        return result;
    }
    if (netRate() < 0.0 && initialVolume() < kEps) {
        result.ok = false;
        result.message = QStringLiteral("Слив открыт, но в резервуаре нет воды.");
        result.hint = QStringLiteral("Откройте кран наполнения или налейте воду, перетащив её уровень.");
        return result;
    }
    if (netRate() > 0.0 && initialVolume() >= capacity() - kEps) {
        result.ok = false;
        result.message = QStringLiteral("Резервуар уже полон, а наполнение продолжается.");
        result.hint = QStringLiteral("Приоткройте слив или уменьшите уровень.");
        return result;
    }
    return result;
}

// ------------------------------------------------------------------
// Запись решения
// ------------------------------------------------------------------
Derivation TankModel::derivation() const {
    using utils::formatNumber;
    Derivation d;
    const ValidationResult check = validate();
    d.valid = check.ok;
    d.problemText = problemText();

    const Flows f = flows();
    const double s = baseArea();
    const double vMax = capacity();
    const double v0 = initialVolume();
    const double q = netRate();
    const bool filling = q > kEps;

    d.unknown = filling ? QStringLiteral("Время наполнения резервуара доверху")
                        : QStringLiteral("Время опустошения резервуара до нуля");

    if (!check.ok) {
        d.steps.push_back({QStringLiteral("Внимание"), QStringLiteral("\\text{%1}").arg(check.message)});
        d.answerTemplate = check.hint;
        return d;
    }

    d.steps.push_back({QStringLiteral("1. Исходные данные"),
                       QStringLiteral("a = %1 \\text{ м},\\; H = %2 \\text{ м},\\; h_{0} = %3 \\text{ м},\\; "
                                      "q_{\\text{н1}} = %4,\\; q_{\\text{н2}} = %5,\\; q_{\\text{с1}} = %6,\\; "
                                      "q_{\\text{с2}} = %7 \\text{ л/мин}")
                           .arg(formatNumber(width(), 1))
                           .arg(formatNumber(height(), 1))
                           .arg(formatNumber(levelAt(0.0), 2))
                           .arg(formatNumber(f.qIn1, 0))
                           .arg(formatNumber(f.qIn2, 0))
                           .arg(formatNumber(f.qOut1, 0))
                           .arg(formatNumber(f.qOut2, 0))});

    d.steps.push_back({QStringLiteral("2. Объёмные величины"),
                       QStringLiteral("S = a^{2} = %1 \\text{ м}^{2},\\qquad "
                                      "V_{\\text{max}} = S \\cdot H = %2 \\text{ м}^{3} = %3 \\text{ л}")
                           .arg(formatNumber(s, 2))
                           .arg(formatNumber(s * height(), 2))
                           .arg(formatNumber(vMax, 0))});
    d.steps.push_back({QStringLiteral("Начальный объём"),
                       QStringLiteral("V_{0} = S \\cdot h_{0} = %1 \\text{ л}")
                           .arg(formatNumber(v0, 0))});

    QString rateText = QStringLiteral("q_{\\text{н1}} + q_{\\text{н2}} - q_{\\text{с1}} - q_{\\text{с2}} = %1 + %2 - %3 - %4 = %5 \\text{ л/мин}")
                           .arg(formatNumber(f.qIn1, 0))
                           .arg(formatNumber(f.qIn2, 0))
                           .arg(formatNumber(f.qOut1, 0))
                           .arg(formatNumber(f.qOut2, 0))
                           .arg(formatNumber(q, 1));
    d.steps.push_back({QStringLiteral("3. Итоговая производительность"),
                       QStringLiteral("Q = %1").arg(rateText)});

    d.steps.push_back({QStringLiteral("4. Модель объёма"),
                       QStringLiteral("V(t) = V_{0} + Q \\cdot t,\\qquad h(t) = \\frac{V(t)}{S \\cdot 1000}")});

    const double event = eventTime();
    if (filling) {
        d.steps.push_back({QStringLiteral("5. Условие наполнения"),
                           QStringLiteral("V(t) = V_{\\text{max}}:\\quad V_{0} + Qt = V_{\\text{max}}")});
        d.steps.push_back({QStringLiteral("6. Подстановка"),
                           QStringLiteral("t = \\frac{V_{\\text{max}} - V_{0}}{Q} = "
                                          "\\frac{%1 - %2}{%3}")
                               .arg(formatNumber(vMax, 0))
                               .arg(formatNumber(v0, 0))
                               .arg(formatNumber(q, 1))});
    } else {
        d.steps.push_back({QStringLiteral("5. Условие опустошения"),
                           QStringLiteral("V(t) = 0:\\quad V_{0} + Qt = 0,\\; Q < 0")});
        d.steps.push_back({QStringLiteral("6. Подстановка"),
                           QStringLiteral("t = \\frac{V_{0}}{|Q|} = \\frac{%1}{%2}")
                               .arg(formatNumber(v0, 0))
                               .arg(formatNumber(std::abs(q), 1))});
    }

    if (!std::isfinite(event)) {
        d.steps.push_back({QStringLiteral("Анализ"),
                           QStringLiteral("Q = 0 — приток равен оттоку, уровень не меняется")});
        d.answerTemplate = QStringLiteral("Уровень не изменится: приток равен оттоку");
        return d;
    }

    d.steps.push_back({QStringLiteral("7. Ответ"),
                       QStringLiteral("t = %1 мин \\approx %2")
                           .arg(formatNumber(event, 2), utils::formatDuration(event, timeUnit()))});
    d.answerTemplate = QStringLiteral("%1 — через %2")
                           .arg(filling ? QStringLiteral("Резервуар наполнится")
                                        : QStringLiteral("Резервуар опустеет"),
                                utils::formatDuration(event, timeUnit()));
    return d;
}

QVector<Readout> TankModel::readouts() const {
    using utils::formatNumber;
    QVector<Readout> list;
    const SimState state = currentState();
    const double t = time();

    list.push_back({QStringLiteral("Время эксперимента"), QStringLiteral("%1 мин").arg(formatNumber(t, 1)), false});
    list.push_back({QStringLiteral("Уровень воды"), QStringLiteral("%1 м").arg(formatNumber(state.primary, 3)), true});
    list.push_back({QStringLiteral("Объём воды"), QStringLiteral("%1 л").arg(formatNumber(state.secondary, 1)), true});
    list.push_back({QStringLiteral("Вместимость резервуара"), QStringLiteral("%1 л").arg(formatNumber(capacity(), 0)), false});
    list.push_back({QStringLiteral("Приток"), QStringLiteral("+%1 л/мин").arg(formatNumber(inflowRate(), 1)), false});
    list.push_back({QStringLiteral("Отток"), QStringLiteral("−%1 л/мин").arg(formatNumber(outflowRate(), 1)), false});
    list.push_back({QStringLiteral("Итоговая производительность"), QStringLiteral("%1 л/мин").arg(formatNumber(netRate(), 1)), false});

    const double fill = timeToFill();
    if (std::isfinite(fill))
        list.push_back({QStringLiteral("Осталось до наполнения"),
                        QStringLiteral("%1 мин").arg(formatNumber(std::max(0.0, fill - t), 1)), true});
    const double empty = timeToEmpty();
    if (std::isfinite(empty))
        list.push_back({QStringLiteral("Осталось до опустошения"),
                        QStringLiteral("%1 мин").arg(formatNumber(std::max(0.0, empty - t), 1)), true});
    if (!state.eventLabel.isEmpty())
        list.push_back({QStringLiteral("Событие"), state.eventLabel, true});
    return list;
}

QVector<HandleInfo> TankModel::handles() const {
    QVector<HandleInfo> list;
    HandleInfo water;
    water.id = QStringLiteral("level");
    water.label = QStringLiteral("Уровень воды");
    water.value = param(QStringLiteral("level"));
    water.minValue = 0.0;
    water.maxValue = height();
    water.step = 0.01;
    water.unit = QStringLiteral("м");
    water.readbackLabel = QStringLiteral("Объём воды");
    water.readbackValue = QStringLiteral("%1 л").arg(utils::formatNumber(initialVolume(), 0));
    list.push_back(water);

    HandleInfo lid;
    lid.id = QStringLiteral("height");
    lid.label = QStringLiteral("Высота резервуара");
    lid.value = height();
    lid.minValue = 0.5;
    lid.maxValue = 5.0;
    lid.step = 0.05;
    lid.unit = QStringLiteral("м");
    lid.readbackLabel = QStringLiteral("Вместимость");
    lid.readbackValue = QStringLiteral("%1 л").arg(utils::formatNumber(capacity(), 0));
    list.push_back(lid);
    return list;
}

}  // namespace lab::models
