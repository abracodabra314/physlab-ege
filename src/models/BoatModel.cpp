#include "models/BoatModel.h"

#include <algorithm>
#include <cmath>

#include "utils/MathText.h"

namespace lab::models {
namespace {

constexpr double kEps = 1e-9;

}  // namespace

BoatModel::BoatModel(QObject* parent) : Simulation(parent) {
    seedDefaults();
}

QString BoatModel::title() const {
    return QStringLiteral("Движение теплоходов по реке");
}

QString BoatModel::subtitle() const {
    return QStringLiteral("Собственная скорость, скорость течения, время рейса");
}

QString BoatModel::statement() const {
    return QStringLiteral(
        "Между двумя пристанями A и B ходят теплоходы. Определите время "
        "рейса каждого теплохода с учётом скорости течения и момент его "
        "прибытия к пристани.");
}

QString BoatModel::problemText() const {
    using utils::formatNumber;
    QString text = QStringLiteral("Расстояние между пристанями — %1 км, скорость течения — %2 км/ч. ")
                       .arg(formatNumber(distanceBetweenPorts(), 0))
                       .arg(formatNumber(currentSpeed(), 1));
    for (int i = 0; i < 2; ++i) {
        text += QStringLiteral("Теплоход %1 с собственной скоростью %2 км/ч отправляется через %3 ч и идёт %4")
                    .arg(i + 1)
                    .arg(formatNumber(ownSpeedOf(i), 1))
                    .arg(formatNumber(departureOf(i), 2))
                    .arg(directionOf(i) == Direction::Downstream ? QStringLiteral("по течению")
                                                                 : QStringLiteral("против течения"));
        text += i == 0 ? QStringLiteral("; ") : QStringLiteral(".");
    }
    return text;
}

QVector<ParamSpec> BoatModel::paramSpecs() const {
    const auto slider = [](QString id, QString label, QString unit, double mn, double mx, double step,
                           double decimals, bool allowZero, double def, QString group) {
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
        return s;
    };
    const auto choice = [](QString id, QString label, QStringList variants, QString group) {
        ParamSpec s;
        s.id = std::move(id);
        s.label = std::move(label);
        s.kind = ParamKind::Choice;
        s.choices = std::move(variants);
        s.group = std::move(group);
        s.defaultValue = 0.0;
        return s;
    };

    const QString river = QStringLiteral("Река");
    const QString boat1 = QStringLiteral("Теплоход 1");
    const QString boat2 = QStringLiteral("Теплоход 2");

    QVector<ParamSpec> specs;
    auto dist = slider(QStringLiteral("L"), QStringLiteral("Расстояние между пристанями"),
                       QStringLiteral("км"), 10.0, 600.0, 1.0, 0.0, false, 120.0, river);
    dist.helpText = QStringLiteral("Пристань Б можно перетащить мышью на сцене.");
    specs.push_back(dist);
    specs.push_back(slider(QStringLiteral("current"), QStringLiteral("Скорость течения реки"),
                           QStringLiteral("км/ч"), 0.0, 7.0, 0.1, 1.0, true, 3.0, river));

    specs.push_back(slider(QStringLiteral("own1"), QStringLiteral("Собственная скорость"),
                           QStringLiteral("км/ч"), 2.0, 40.0, 0.5, 1.0, false, 15.0, boat1));
    specs.push_back(choice(QStringLiteral("dir1"), QStringLiteral("Направление рейса"),
                           {QStringLiteral("По течению (A → B)"), QStringLiteral("Против течения (B → A)")}, boat1));
    specs.push_back(slider(QStringLiteral("x1"), QStringLiteral("Пункт отправления"),
                           QStringLiteral("км"), 0.0, 600.0, 0.5, 1.0, true, 0.0, boat1));
    specs.push_back(slider(QStringLiteral("dep1"), QStringLiteral("Отправление через"),
                           QStringLiteral("ч"), 0.0, 8.0, 0.25, 2.0, true, 0.0, boat1));

    specs.push_back(slider(QStringLiteral("own2"), QStringLiteral("Собственная скорость"),
                           QStringLiteral("км/ч"), 2.0, 40.0, 0.5, 1.0, false, 12.0, boat2));
    auto dir2 = choice(QStringLiteral("dir2"), QStringLiteral("Направление рейса"),
                       {QStringLiteral("По течению (A → B)"), QStringLiteral("Против течения (B → A)")}, boat2);
    // По умолчанию второй теплоход идёт из B в A: иначе он стартовал бы
    // прямо у своей причальной пристани и задача была бы тривиальна.
    dir2.defaultValue = 1.0;
    specs.push_back(dir2);
    specs.push_back(slider(QStringLiteral("x2"), QStringLiteral("Пункт отправления"),
                           QStringLiteral("км"), 0.0, 600.0, 0.5, 1.0, true, 120.0, boat2));
    specs.push_back(slider(QStringLiteral("dep2"), QStringLiteral("Отправление через"),
                           QStringLiteral("ч"), 0.0, 8.0, 0.25, 2.0, true, 0.0, boat2));
    return specs;
}

double BoatModel::distanceBetweenPorts() const {
    return std::max(1.0, param(QStringLiteral("L")));
}

double BoatModel::currentSpeed() const {
    return param(QStringLiteral("current"));
}

double BoatModel::ownSpeedOf(int number) const {
    return param(number == 0 ? QStringLiteral("own1") : QStringLiteral("own2"));
}

BoatModel::Direction BoatModel::directionOf(int number) const {
    return paramChoiceIndex(number == 0 ? QStringLiteral("dir1") : QStringLiteral("dir2")) == 0
               ? Direction::Downstream
               : Direction::Upstream;
}

double BoatModel::departureOf(int number) const {
    return param(number == 0 ? QStringLiteral("dep1") : QStringLiteral("dep2"));
}

double BoatModel::portOf(int number) const {
    const double L = distanceBetweenPorts();
    const double raw = param(number == 0 ? QStringLiteral("x1") : QStringLiteral("x2"));
    return std::clamp(raw, 0.0, L);
}

double BoatModel::berthOf(int number) const {
    // Причал — пристань в конце маршрута: ниже течения (B) или выше (A).
    return directionOf(number) == Direction::Downstream ? distanceBetweenPorts() : 0.0;
}

double BoatModel::shoreSpeedOf(int number) const {
    // Знакованная скорость вдоль оси X (ось направлена по течению).
    const double own = ownSpeedOf(number);
    const double current = currentSpeed();
    if (directionOf(number) == Direction::Downstream)
        return own + current;                      // «по течению»
    return -(own - current);                       // «против течения»: движение влево, скорость может быть ≤ 0
}

double BoatModel::travelTimeOf(int number) const {
    const double speed = std::abs(shoreSpeedOf(number));
    if (speed < kEps)
        return std::numeric_limits<double>::infinity();
    return std::abs(berthOf(number) - portOf(number)) / speed;
}

double BoatModel::arrivalTimeOf(int number) const {
    const double travel = travelTimeOf(number);
    if (!std::isfinite(travel))
        return travel;
    return departureOf(number) + travel;
}

double BoatModel::positionOf(int number, double t) const {
    const double start = portOf(number);
    const double velocity = shoreSpeedOf(number);
    const double departure = departureOf(number);
    if (t <= departure || std::abs(velocity) < kEps)
        return start;
    double coordinate = start + velocity * (t - departure);
    // Физическое ограничение: теплоход останавливается у своей пристани,
    // течение не может «протащить» его дальше или сдвинуть назад после швартовки.
    const double berth = berthOf(number);
    coordinate = velocity > 0 ? std::min(coordinate, berth) : std::max(coordinate, berth);
    return std::clamp(coordinate, 0.0, distanceBetweenPorts());
}

double BoatModel::lastArrivalTime() const {
    const double t1 = arrivalTimeOf(0);
    const double t2 = arrivalTimeOf(1);
    if (!std::isfinite(t1) && !std::isfinite(t2))
        return std::numeric_limits<double>::quiet_NaN();
    if (!std::isfinite(t1))
        return t2;
    if (!std::isfinite(t2))
        return t1;
    return std::max(t1, t2);
}

double BoatModel::eventTime() const {
    if (!validate().ok)
        return std::numeric_limits<double>::quiet_NaN();
    // Наблюдение завершается, когда завершится последний возможный рейс.
    return lastArrivalTime();
}

SimState BoatModel::stateAt(double t) const {
    SimState state;
    state.time = t;
    state.primary = std::abs(positionOf(0, t) - positionOf(1, t));
    state.secondary = distanceBetweenPorts();

    const auto makeBoat = [&](int i) {
        EntityVisual boat;
        boat.id = QStringLiteral("boat%1").arg(i + 1);
        boat.label = QStringLiteral("Теплоход %1").arg(i + 1);
        boat.position = positionOf(i, t);
        boat.value1 = shoreSpeedOf(i);
        boat.value2 = departureOf(i);
        const bool departed = t > departureOf(i) + kEps;
        const bool moored = std::abs(positionOf(i, t) - berthOf(i)) < kEps && departed;
        boat.active = departed && !moored;
        boat.variant = i;
        return boat;
    };
    state.entities = {makeBoat(0), makeBoat(1)};

    const double last = lastArrivalTime();
    if (std::isfinite(last) && t >= last - 1e-9) {
        state.resolved = true;
        const double a1 = arrivalTimeOf(0);
        const double a2 = arrivalTimeOf(1);
        const bool oneFirst = std::isfinite(a1) && (!std::isfinite(a2) || a1 <= a2);
        state.eventLabel = QStringLiteral("Первым прибыл теплоход %1; все рейсы завершены")
                               .arg(oneFirst ? QStringLiteral("1") : QStringLiteral("2"));
    }
    return state;
}

double BoatModel::timePerSecond() const {
    const double event = eventTime();
    if (std::isfinite(event) && event > 0.1)
        return std::clamp(event / 15.0, 0.05, 20.0);
    const double speed = std::max(1.0, std::max(std::abs(shoreSpeedOf(0)), std::abs(shoreSpeedOf(1))));
    return std::clamp(distanceBetweenPorts() / speed / 15.0, 0.05, 20.0);
}

double BoatModel::duration() const {
    const double event = eventTime();
    double horizon = 2.0;
    if (std::isfinite(event))
        horizon = std::max(horizon, event * 1.2 + 0.5);
    else
        horizon = std::max(horizon, distanceBetweenPorts() / 5.0);
    return std::min(horizon, 2000.0);
}

double BoatModel::stepSize() const {
    return duration() / 40.0;
}

ValidationResult BoatModel::validate() const {
    ValidationResult result;
    if (distanceBetweenPorts() < 1.0) {
        result.ok = false;
        result.message = QStringLiteral("Расстояние между пристанями слишком мало.");
        result.hint = QStringLiteral("Увеличьте расстояние между пристанями.");
        return result;
    }
    for (int i = 0; i < 2; ++i) {
        if (directionOf(i) == Direction::Upstream && ownSpeedOf(i) <= currentSpeed() + kEps) {
            result.ok = false;
            result.message = QStringLiteral("Теплоход %1 не может идти против течения: его собственная скорость (%2 км/ч) "
                                            "не больше скорости течения (%3 км/ч).")
                                 .arg(i + 1)
                                 .arg(utils::formatNumber(ownSpeedOf(i), 1))
                                 .arg(utils::formatNumber(currentSpeed(), 1));
            result.hint = QStringLiteral("Увеличьте собственную скорость теплохода или уменьшите скорость течения.");
            return result;
        }
        if (std::abs(portOf(i) - berthOf(i)) < kEps) {
            result.ok = false;
            result.message = QStringLiteral("Теплоход %1 уже стоит у той пристани, к которой направлен.").arg(i + 1);
            result.hint = QStringLiteral("Переместите теплоход мышью или измените направление рейса.");
            return result;
        }
    }
    return result;
}

// ------------------------------------------------------------------
// Запись решения
// ------------------------------------------------------------------
Derivation BoatModel::derivation() const {
    using utils::formatNumber;
    Derivation d;
    const ValidationResult check = validate();
    d.valid = check.ok;
    d.problemText = problemText();
    d.unknown = QStringLiteral("Время рейса каждого теплохода и время его прибытия к пристани");

    if (!check.ok) {
        d.steps.push_back({QStringLiteral("Внимание"), QStringLiteral("\\text{%1}").arg(check.message)});
        d.answerTemplate = check.hint;
        return d;
    }

    const double L = distanceBetweenPorts();
    const double w = currentSpeed();

    d.steps.push_back({QStringLiteral("1. Исходные данные"),
                       QStringLiteral("L = %1 \\text{ км},\\quad v_{\\text{т}} = %2 \\text{ км/ч},\\quad "
                                      "v_{1} = %3 \\text{ км/ч},\\quad v_{2} = %4 \\text{ км/ч}")
                           .arg(formatNumber(L, 0), formatNumber(w, 1), formatNumber(ownSpeedOf(0), 1),
                                formatNumber(ownSpeedOf(1), 1))});

    d.steps.push_back({QStringLiteral("2. Скорость относительно берега"),
                       QStringLiteral("v_{\\text{по}} = v_{\\text{соб}} + v_{\\text{т}},\\qquad "
                                      "v_{\\text{пр}} = v_{\\text{соб}} - v_{\\text{т}}")});
    d.steps.push_back({QStringLiteral("Ограничение"),
                       QStringLiteral("Против течения теплоход идёт только при "
                                      "v_{\\text{соб}} > v_{\\text{т}}")});

    for (int i = 0; i < 2; ++i) {
        const QString idx = QString::number(i + 1);
        const double own = ownSpeedOf(i);
        const double shore = std::abs(shoreSpeedOf(i));
        const bool down = directionOf(i) == Direction::Downstream;
        const double path = std::abs(berthOf(i) - portOf(i));
        const QString vLabel = down ? QStringLiteral("v_{\\text{по}}") : QStringLiteral("v_{\\text{пр}}");
        const QString sign = down ? QStringLiteral("+") : QStringLiteral("-");

        // Формулы собираем последовательными .arg() — каждый подставляет
        // следующий по счёту маркер, порядок строгий и проверяемый.
        d.steps.push_back({QStringLiteral("3.%1 Теплоход %2").arg(idx).arg(idx),
                           QStringLiteral("%1 = v_{%2} %3 v_{\\text{т}} = %4 %5 %6 = %7 \\text{ км/ч}")
                               .arg(vLabel)
                               .arg(idx)
                               .arg(sign)
                               .arg(formatNumber(own, 1))
                               .arg(sign)
                               .arg(formatNumber(w, 1))
                               .arg(formatNumber(shore, 1))});
        d.steps.push_back({QStringLiteral("Путь рейса"),
                           QStringLiteral("s_{%1} = |x_{\\text{прич}} - x_{\\text{отпр}}| = %2 \\text{ км}")
                               .arg(idx)
                               .arg(formatNumber(path, 1))});
        d.steps.push_back({QStringLiteral("Время рейса"),
                           QStringLiteral("t_{\\text{рейса},%1} = \\frac{s_{%1}}{v} = "
                                          "\\frac{%2}{%3} = %4 \\text{ ч}")
                               .arg(idx)
                               .arg(formatNumber(path, 1))
                               .arg(formatNumber(shore, 1))
                               .arg(formatNumber(travelTimeOf(i), 3))});
        d.steps.push_back({QStringLiteral("Время прибытия с учётом задержки отправления"),
                           QStringLiteral("t_{\\text{приб},%1} = t_{\\text{отпр},%1} + t_{\\text{рейса},%1} = "
                                          "%2 + %3 = %4 \\text{ ч}")
                               .arg(idx)
                               .arg(formatNumber(departureOf(i), 2))
                               .arg(formatNumber(travelTimeOf(i), 3))
                               .arg(formatNumber(arrivalTimeOf(i), 3))});
    }

    d.steps.push_back({QStringLiteral("4. Сравнение прибытий"),
                       QStringLiteral("t_{\\text{приб,1}} = %1 ч,\\quad t_{\\text{приб,2}} = %2 ч")
                           .arg(formatNumber(arrivalTimeOf(0), 3), formatNumber(arrivalTimeOf(1), 3))});

    const double first = std::min(arrivalTimeOf(0), arrivalTimeOf(1));
    d.steps.push_back({QStringLiteral("5. Ответ"),
                       QStringLiteral("Раньше всех к пристани прибудет теплоход %1 — через %2 ч после начала "
                                      "отсчёта")
                           .arg(arrivalTimeOf(0) <= arrivalTimeOf(1) ? QStringLiteral("1") : QStringLiteral("2"),
                                formatNumber(first, 3))});

    d.answerTemplate = QStringLiteral("Теплоход %1 прибывает первым — через %2")
                           .arg(arrivalTimeOf(0) <= arrivalTimeOf(1) ? QStringLiteral("1") : QStringLiteral("2"),
                                utils::formatDuration(first, timeUnit()));
    return d;
}

QVector<Readout> BoatModel::readouts() const {
    using utils::formatNumber;
    QVector<Readout> list;
    const SimState state = currentState();
    const double t = time();

    list.push_back({QStringLiteral("Время эксперимента"), QStringLiteral("%1 ч").arg(formatNumber(t, 2)), false});

    for (int i = 0; i < 2; ++i) {
        const QString name = QStringLiteral("Теплоход %1").arg(i + 1);
        list.push_back({name + QStringLiteral(": координата"),
                        QStringLiteral("%1 км").arg(formatNumber(positionOf(i, t), 2)), false});
        list.push_back({name + QStringLiteral(": скорость относительно берега"),
                        QStringLiteral("%1 км/ч").arg(formatNumber(shoreSpeedOf(i), 2)), false});
        const double travel = travelTimeOf(i);
        list.push_back({name + QStringLiteral(": время рейса"),
                        std::isfinite(travel) ? QStringLiteral("%1 ч").arg(formatNumber(travel, 3))
                                              : QStringLiteral("рейс невозможен"),
                        true});
        const double arrival = arrivalTimeOf(i);
        list.push_back({name + QStringLiteral(": время прибытия"),
                        std::isfinite(arrival) ? QStringLiteral("%1 ч").arg(formatNumber(arrival, 3))
                                               : QStringLiteral("—"),
                        true});
    }

    list.push_back({QStringLiteral("Расстояние между теплоходами"),
                    QStringLiteral("%1 км").arg(formatNumber(state.primary, 2)), false});
    if (!state.eventLabel.isEmpty())
        list.push_back({QStringLiteral("Событие"), state.eventLabel, true});
    return list;
}

QVector<HandleInfo> BoatModel::handles() const {
    QVector<HandleInfo> list;
    const double L = distanceBetweenPorts();

    HandleInfo pier;
    pier.id = QStringLiteral("L");
    pier.label = QStringLiteral("Пристань Б");
    pier.value = L;
    pier.minValue = 10.0;
    pier.maxValue = 600.0;
    pier.step = 1.0;
    pier.unit = QStringLiteral("км");
    pier.readbackLabel = QStringLiteral("Расстояние между пристанями");
    pier.readbackValue = QStringLiteral("%1 км").arg(utils::formatNumber(L, 0));
    list.push_back(pier);

    for (int i = 0; i < 2; ++i) {
        HandleInfo boat;
        boat.id = QStringLiteral("x%1").arg(i + 1);
        boat.label = QStringLiteral("Теплоход %1").arg(i + 1);
        boat.value = portOf(i);
        boat.minValue = 0.0;
        boat.maxValue = L;
        boat.step = 0.5;
        boat.unit = QStringLiteral("км");
        boat.readbackLabel = QStringLiteral("Путь до причала");
        boat.readbackValue = QStringLiteral("%1 км").arg(utils::formatNumber(std::abs(berthOf(i) - portOf(i)), 1));
        list.push_back(boat);
    }
    return list;
}

}  // namespace lab::models
