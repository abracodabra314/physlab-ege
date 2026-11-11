#include "models/MotionModel.h"

#include <algorithm>
#include <cmath>

#include "utils/MathText.h"

namespace lab::models {
namespace {

/**
 * Линейное движение с задержкой старта:
 *   s(t) = s0, при t <= tStart;
 *   s(t) = s0 + v*(t - tStart), при t > tStart.
 */
double linearWithDelay(double s0, double v, double tStart, double t) {
    const double dt = std::max(0.0, t - tStart);
    return s0 + v * dt;
}

bool positive(double value) { return value > 1e-9; }

}  // namespace

MotionModel::MotionModel(QObject* parent) : Simulation(parent) {
    seedDefaults();
}

QString MotionModel::title() const {
    return QStringLiteral("Два движущихся автомобиля");
}

QString MotionModel::subtitle() const {
    return QStringLiteral("Встречное движение, движение вдогонку, задержка старта");
}

QString MotionModel::statement() const {
    return QStringLiteral(
        "Из двух пунктов, расстояние между которыми известно, выехали два "
        "автомобиля. Определите, через сколько времени они встретятся "
        "(или один догонит другого).");
}

QString MotionModel::problemText() const {
    using utils::formatNumber;
    const double distance = std::abs(startOf(1) - startOf(0));
    const double v1 = speedOf(0);
    const double v2 = speedOf(1);
    const double delay = delayOf(1);
    QString text = QStringLiteral("Начальное расстояние между автомобилями — %1 км; ")
                       .arg(formatNumber(distance, 1));
    text += QStringLiteral("скорости автомобилей %1 км/ч и %2 км/ч")
                .arg(formatNumber(v1, 1))
                .arg(formatNumber(v2, 1));
    const bool opposite = (directionOf(0) == Direction::Right) != (directionOf(1) == Direction::Right);
    text += opposite ? QStringLiteral(" (автомобили движутся навстречу)")
                     : QStringLiteral(" (автомобили движутся в одном направлении)");
    text += QStringLiteral(".");
    if (delay > 1e-9)
        text += QStringLiteral(" Автомобиль 2 выехал на %1 ч позже.")
                    .arg(formatNumber(delay, 2));
    const double delay1 = delayOf(0);
    if (delay1 > 1e-9)
        text += QStringLiteral(" Автомобиль 1 выехал на %1 ч позже.")
                    .arg(formatNumber(delay1, 2));
    return text;
}

QVector<ParamSpec> MotionModel::paramSpecs() const {
    const auto makeSlider = [](QString id, QString label, QString unit, double mn, double mx, double step,
                               double decimals, bool allowZero, double defaultValue) {
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
        s.defaultValue = defaultValue;
        return s;
    };
    const auto makeChoice = [](QString id, QString label, QStringList choices, QString group) {
        ParamSpec s;
        s.id = std::move(id);
        s.label = std::move(label);
        s.kind = ParamKind::Choice;
        s.choices = std::move(choices);
        s.group = std::move(group);
        s.defaultValue = 0.0;
        return s;
    };

    const QString groupPositions = QStringLiteral("Положения на дороге");
    const QString groupSpeeds = QStringLiteral("Скорости");
    const QString groupDelays = QStringLiteral("Время начала движения");

    QVector<ParamSpec> specs;

    auto x1 = makeSlider(QStringLiteral("x1"), QStringLiteral("Начальная координата автомобиля 1"),
                         QStringLiteral("км"), 0.0, 600.0, 0.5, 1.0, true, 0.0);
    x1.group = groupPositions;
    x1.helpText = QStringLiteral("Перетащите автомобиль на сцене — координата и расстояние пересчитаются.");
    specs.push_back(x1);

    auto x2 = makeSlider(QStringLiteral("x2"), QStringLiteral("Начальная координата автомобиля 2"),
                         QStringLiteral("км"), 0.0, 600.0, 0.5, 1.0, true, 120.0);
    x2.group = groupPositions;
    specs.push_back(x2);

    auto v1 = makeSlider(QStringLiteral("v1"), QStringLiteral("Скорость автомобиля 1"),
                         QStringLiteral("км/ч"), 1.0, 150.0, 1.0, 1.0, false, 60.0);
    v1.group = groupSpeeds;
    specs.push_back(v1);

    auto v2 = makeSlider(QStringLiteral("v2"), QStringLiteral("Скорость автомобиля 2"),
                         QStringLiteral("км/ч"), 1.0, 150.0, 1.0, 1.0, false, 80.0);
    v2.group = groupSpeeds;
    specs.push_back(v2);

    specs.push_back(makeChoice(QStringLiteral("dir1"), QStringLiteral("Направление автомобиля 1"),
                               {QStringLiteral("Вправо (+)"), QStringLiteral("Влево (−)")}, groupSpeeds));
    auto dir2 = makeChoice(QStringLiteral("dir2"), QStringLiteral("Направление автомобиля 2"),
                           {QStringLiteral("Вправо (+)"), QStringLiteral("Влево (−)")}, groupSpeeds);
    // По умолчанию автомобили движутся навстречу: это базовый сценарий задачи.
    dir2.defaultValue = 1.0;
    specs.push_back(dir2);

    auto d1 = makeSlider(QStringLiteral("delay1"), QStringLiteral("Старт автомобиля 1 через"),
                         QStringLiteral("ч"), 0.0, 5.0, 0.25, 2.0, true, 0.0);
    d1.group = groupDelays;
    specs.push_back(d1);

    auto d2 = makeSlider(QStringLiteral("delay2"), QStringLiteral("Старт автомобиля 2 через"),
                         QStringLiteral("ч"), 0.0, 5.0, 0.25, 2.0, true, 0.0);
    d2.group = groupDelays;
    specs.push_back(d2);

    return specs;
}

double MotionModel::startOf(int number) const {
    return param(number == 0 ? QStringLiteral("x1") : QStringLiteral("x2"));
}

double MotionModel::speedOf(int number) const {
    return param(number == 0 ? QStringLiteral("v1") : QStringLiteral("v2"));
}

MotionModel::Direction MotionModel::directionOf(int number) const {
    return paramChoiceIndex(number == 0 ? QStringLiteral("dir1") : QStringLiteral("dir2")) == 0
               ? Direction::Right
               : Direction::Left;
}

double MotionModel::delayOf(int number) const {
    return param(number == 0 ? QStringLiteral("delay1") : QStringLiteral("delay2"));
}

double MotionModel::signedVelocityOf(int number) const {
    const double speed = speedOf(number);
    return directionOf(number) == Direction::Right ? speed : -speed;
}

MotionModel::Kinematics MotionModel::kinematics() const {
    Kinematics k;
    k.s10 = startOf(0);
    k.s20 = startOf(1);
    k.v1 = signedVelocityOf(0);
    k.v2 = signedVelocityOf(1);
    k.t1 = delayOf(0);
    k.t2 = delayOf(1);
    return k;
}

double MotionModel::positionOf(int number, double t) const {
    const Kinematics k = kinematics();
    return number == 0 ? linearWithDelay(k.s10, k.v1, k.t1, t)
                       : linearWithDelay(k.s20, k.v2, k.t2, t);
}

double MotionModel::distanceAt(double t) const {
    return std::abs(positionOf(0, t) - positionOf(1, t));
}

double MotionModel::meetingTime() const {
    if (!validate().ok)
        return std::numeric_limits<double>::quiet_NaN();
    const Kinematics k = kinematics();

    // Встреча (обгон) возможны только когда оба автомобиля уже в пути:
    // с момента t* = max(t1, t2) разность координат линейна,
    // поэтому корень ищется аналитически на луче [t*, +inf).
    const double tStar = std::max(k.t1, k.t2);
    const double d0 = linearWithDelay(k.s10, k.v1, k.t1, tStar) -
                      linearWithDelay(k.s20, k.v2, k.t2, tStar);
    const double closing = k.v1 - k.v2;
    if (std::abs(closing) < 1e-12)
        return std::numeric_limits<double>::quiet_NaN();
    const double dt = -d0 / closing;
    if (dt < -1e-9)  // «встреча» была бы раньше старта медлившего автомобиля
        return std::numeric_limits<double>::quiet_NaN();
    return tStar + std::max(0.0, dt);
}

double MotionModel::eventTime() const {
    return meetingTime();
}

SimState MotionModel::stateAt(double t) const {
    const Kinematics k = kinematics();
    SimState state;
    state.time = t;

    const double s1 = linearWithDelay(k.s10, k.v1, k.t1, t);
    const double s2 = linearWithDelay(k.s20, k.v2, k.t2, t);
    state.primary = std::abs(s1 - s2);

    EntityVisual car1;
    car1.id = QStringLiteral("car1");
    car1.label = QStringLiteral("Автомобиль 1");
    car1.position = s1;
    car1.value1 = k.v1;
    car1.value2 = k.t1;
    car1.active = t > k.t1 + 1e-9 && std::abs(k.v1) > 1e-12;
    car1.variant = 0;

    EntityVisual car2;
    car2.id = QStringLiteral("car2");
    car2.label = QStringLiteral("Автомобиль 2");
    car2.position = s2;
    car2.value1 = k.v2;
    car2.value2 = k.t2;
    car2.active = t > k.t2 + 1e-9 && std::abs(k.v2) > 1e-12;
    car2.variant = 1;

    state.entities = {car1, car2};

    const double event = meetingTime();
    if (std::isfinite(event) && t >= event - 1e-9) {
        state.resolved = true;
        const bool opposite = (k.v1 > 0) != (k.v2 > 0);
        state.eventLabel = opposite ? QStringLiteral("Встреча: координаты автомобилей равны")
                                   : QStringLiteral("Обгон: координаты автомобилей равны");
    }
    return state;
}

double MotionModel::timePerSecond() const {
    const double v1 = std::abs(signedVelocityOf(0));
    const double v2 = std::abs(signedVelocityOf(1));
    const double maxSpeed = std::max(1.0, std::max(v1, v2));
    const double distance = std::max(1.0, std::abs(startOf(1) - startOf(0)));
    // Темп подобран так, чтобы характерное событие длилось ~15 секунд просмотра.
    return std::clamp(distance / maxSpeed / 15.0, 0.05, 40.0);
}

double MotionModel::duration() const {
    const double event = meetingTime();
    const double t1 = delayOf(0);
    const double t2 = delayOf(1);
    double horizon = std::max({2.0, t1, t2});
    if (std::isfinite(event)) {
        horizon = std::max(horizon, event * 1.3 + 0.5);
    } else {
        // События нет: снимаем столько, чтобы машины проехали заметный участок.
        const double maxSpeed = std::max({1e-6, std::abs(signedVelocityOf(0)), std::abs(signedVelocityOf(1))});
        horizon = std::max(horizon, 400.0 / maxSpeed);
    }
    return std::min(horizon, 2000.0);
}

double MotionModel::stepSize() const {
    return duration() / 40.0;
}

ValidationResult MotionModel::validate() const {
    ValidationResult result;
    const double distance = std::abs(startOf(1) - startOf(0));
    if (distance < 1e-6) {
        result.ok = false;
        result.message = QStringLiteral("Автомобили находятся в одной точке: начальное расстояние равно нулю.");
        result.hint = QStringLiteral("Переместите один из автомобилей мышью или измените его координату.");
        return result;
    }
    if (!positive(speedOf(0)) || !positive(speedOf(1))) {
        result.ok = false;
        result.message = QStringLiteral("Скорость автомобиля должна быть больше нуля.");
        result.hint = QStringLiteral("Увеличьте скорость — иначе движение не состоится.");
        return result;
    }
    return result;
}

// ------------------------------------------------------------------
// Запись решения
// ------------------------------------------------------------------
Derivation MotionModel::derivation() const {
    using utils::formatNumber;
    Derivation d;
    const ValidationResult check = validate();
    d.valid = check.ok;
    d.problemText = problemText();

    const Kinematics k = kinematics();
    const double s0 = std::abs(k.s20 - k.s10);
    const double v1abs = std::abs(k.v1);
    const double v2abs = std::abs(k.v2);
    const double closingRate = std::abs(k.v1 - k.v2);
    const bool opposite = (k.v1 > 0) != (k.v2 > 0);
    const double delay1 = k.t1;
    const double delay2 = k.t2;

    d.unknown = opposite
                    ? QStringLiteral("Время до встречи t, когда s1(t) = s2(t)")
                    : QStringLiteral("Время обгона t, когда s1(t) = s2(t)");

    if (!check.ok) {
        d.steps.push_back({QStringLiteral("Внимание"), QStringLiteral("\\text{%1}").arg(check.message)});
        d.answerTemplate = QStringLiteral("Задача не решается: %1").arg(check.hint);
        return d;
    }

    QVector<DerivationStep>& st = d.steps;
    st.push_back({QStringLiteral("1. Исходные данные"),
                  QStringLiteral("s_{0} = %1 \\text{ км},\\quad v_{1} = %2 \\text{ км/ч},\\quad "
                                 "v_{2} = %3 \\text{ км/ч}")
                      .arg(formatNumber(s0, 1), formatNumber(v1abs, 1), formatNumber(v2abs, 1))});

    QString delayLine = QStringLiteral("t_{1} = %1 ч,\\quad t_{2} = %2 ч")
                            .arg(formatNumber(delay1, 2), formatNumber(delay2, 2));
    st.push_back({QStringLiteral("Время начала движения"), delayLine});

    st.push_back({QStringLiteral("2. Математическая модель"),
                  QStringLiteral("s_{1}(t) = s_{10} + v_{1}(t - t_{1}),\\qquad "
                                 "s_{2}(t) = s_{20} + v_{2}(t - t_{2})")});

    if (delay1 > 1e-9 || delay2 > 1e-9) {
        st.push_back({QStringLiteral("Уточнение"),
                      QStringLiteral("Пока t \\le t_{i}, автомобиль %1 стоит: %2")
                          .arg(QStringLiteral("(i = 1, 2)"), QStringLiteral("s_{i}(t) = s_{i0}"))});
    }

    const double event = meetingTime();
    const double startTime = std::max(delay1, delay2);
    const double gapAtStart = std::abs(linearWithDelay(k.s10, k.v1, delay1, startTime) -
                                       linearWithDelay(k.s20, k.v2, delay2, startTime));

    if (opposite) {
        st.push_back({QStringLiteral("3. Уравнение встречи"),
                      QStringLiteral("Пусть t^{*} = \\max(t_{1}, t_{2}) — момент, когда оба автомобиля "
                                     "в пути. К этому моменту разрыв сокращается до D, а дальше машины "
                                     "сближаются со скоростью v_{1} + v_{2}:")});
        st.push_back({QStringLiteral("Скорость сближения"),
                      QStringLiteral("v_{\\text{сбл}} = v_{1} + v_{2} = %1 + %2 = %3 \\text{ км/ч}")
                          .arg(formatNumber(v1abs, 1), formatNumber(v2abs, 1), formatNumber(v1abs + v2abs, 1))});
        if (startTime > 1e-9) {
            st.push_back({QStringLiteral("Разрыв к моменту t^{*}"),
                          QStringLiteral("D = s_{0} - v_{1}(t^{*} - t_{1}) - v_{2}(t^{*} - t_{2}) = %1")
                              .arg(formatNumber(gapAtStart, 2))});
            st.push_back({QStringLiteral("4. Подстановка"),
                          QStringLiteral("t = t^{*} + \\frac{D}{v_{1} + v_{2}} = %1 + \\frac{%2}{%3}")
                              .arg(formatNumber(startTime, 2), formatNumber(gapAtStart, 2),
                                   formatNumber(v1abs + v2abs, 1))});
        } else {
            st.push_back({QStringLiteral("4. Подстановка"),
                          QStringLiteral("t = \\frac{s_{0}}{v_{1} + v_{2}} = \\frac{%1}{%2}")
                              .arg(formatNumber(s0, 1), formatNumber(v1abs + v2abs, 1))});
        }
    } else {
        const QString chaserName = (closingRate > 1e-9)
                                       ? QStringLiteral("1")
                                       : QStringLiteral("2");  // кто догоняет по знаку скоростей
        st.push_back({QStringLiteral("3. Уравнение обгона"),
                      QStringLiteral("Догоняющий автомобиль %1 должен сократить разрыв: "
                                     "|v_{2} - v_{1}| \\cdot (t - t^{*}) = D")
                          .arg(chaserName)});
        st.push_back({QStringLiteral("Скорость сближения"),
                      QStringLiteral("v_{\\text{сбл}} = |v_{2} - v_{1}| = |%1 - %2| = %3 \\text{ км/ч}")
                          .arg(formatNumber(v2abs, 1), formatNumber(v1abs, 1), formatNumber(closingRate, 1))});
        if (closingRate < 1e-9) {
            st.push_back({QStringLiteral("Анализ"),
                          QStringLiteral("v_{\\text{сбл}} = 0, разрыв не изменяется — обгон невозможен")});
            d.answerTemplate = QStringLiteral("Обгон невозможен: скорости равны");
            return d;
        }
        if (startTime > 1e-9) {
            st.push_back({QStringLiteral("Разрыв к моменту t^{*}"),
                          QStringLiteral("D = %1 \\text{ км}").arg(formatNumber(gapAtStart, 2))});
            st.push_back({QStringLiteral("4. Подстановка"),
                          QStringLiteral("t = t^{*} + \\frac{D}{|v_{2} - v_{1}|} = %1 + \\frac{%2}{%3}")
                              .arg(formatNumber(startTime, 2), formatNumber(gapAtStart, 2),
                                   formatNumber(closingRate, 1))});
        } else {
            st.push_back({QStringLiteral("4. Подстановка"),
                          QStringLiteral("t = \\frac{s_{0}}{|v_{2} - v_{1}|} = \\frac{%1}{%2}")
                              .arg(formatNumber(s0, 1), formatNumber(closingRate, 1))});
        }
    }

    if (!std::isfinite(event)) {
        st.push_back({QStringLiteral("Анализ"),
                      QStringLiteral("Уравнение s_{1}(t) = s_{2}(t) не имеет решения при t \\ge 0 — "
                                     "расстояние между автомобилями не обращается в ноль")});
        d.answerTemplate = QStringLiteral("Встречи (обгона) не будет");
        return d;
    }

    st.push_back({QStringLiteral("5. Ответ"),
                  QStringLiteral("t = %1 ч \\approx %2")
                      .arg(formatNumber(event, 3), utils::formatDuration(event, timeUnit()))});

    const double place = positionOf(0, event);
    st.push_back({QStringLiteral("Место события"),
                  QStringLiteral("x = %1 км (координата от начала отсчёта)").arg(formatNumber(place, 1))});

    d.answerTemplate = QStringLiteral("Через %1 после выезда первого автомобиля")
                           .arg(utils::formatDuration(event, timeUnit()));
    return d;
}

QVector<Readout> MotionModel::readouts() const {
    using utils::formatNumber;
    QVector<Readout> list;
    const SimState state = currentState();
    const double t = time();

    list.push_back({QStringLiteral("Время эксперимента"), QStringLiteral("%1 ч").arg(formatNumber(t, 2)), false});

    for (const EntityVisual& car : state.entities) {
        list.push_back({QStringLiteral("s (%1)").arg(car.label), QStringLiteral("%1 км").arg(formatNumber(car.position, 2)), false});
        list.push_back({QStringLiteral("v (%1)").arg(car.label),
                        QStringLiteral("%1 км/ч").arg(formatNumber(car.value1, 1)), false});
        const bool waiting = t <= car.value2 + 1e-9;
        if (waiting)
            list.push_back({QStringLiteral("Состояние «%1»").arg(car.label),
                            QStringLiteral("ожидает старта (%1 ч)").arg(formatNumber(car.value2, 2)), false});
    }

    list.push_back({QStringLiteral("Расстояние между автомобилями"),
                    QStringLiteral("%1 км").arg(formatNumber(state.primary, 2)), true});

    const double event = meetingTime();
    if (std::isfinite(event)) {
        list.push_back({state.eventLabel.isEmpty() ? QStringLiteral("Момент встречи (обгона)")
                                                   : state.eventLabel,
                        QStringLiteral("%1 ч").arg(formatNumber(event, 3)), true});
    } else {
        list.push_back({QStringLiteral("Встреча (обгон)"), QStringLiteral("не произойдёт"), true});
    }
    return list;
}

QVector<HandleInfo> MotionModel::handles() const {
    QVector<HandleInfo> list;
    const double x1 = startOf(0);
    const double x2 = startOf(1);

    HandleInfo h1;
    h1.id = QStringLiteral("x1");
    h1.label = QStringLiteral("Автомобиль 1");
    h1.value = x1;
    h1.minValue = 0.0;
    h1.maxValue = 600.0;
    h1.step = 0.5;
    h1.unit = QStringLiteral("км");
    h1.readbackLabel = QStringLiteral("s между автомобилями");
    h1.readbackValue = QStringLiteral("%1 км").arg(utils::formatNumber(std::abs(x2 - x1), 1));
    list.push_back(h1);

    HandleInfo h2;
    h2.id = QStringLiteral("x2");
    h2.label = QStringLiteral("Автомобиль 2");
    h2.value = x2;
    h2.minValue = 0.0;
    h2.maxValue = 600.0;
    h2.step = 0.5;
    h2.unit = QStringLiteral("км");
    h2.readbackLabel = QStringLiteral("s между автомобилями");
    h2.readbackValue = QStringLiteral("%1 км").arg(utils::formatNumber(std::abs(x2 - x1), 1));
    list.push_back(h2);

    return list;
}

}  // namespace lab::models
