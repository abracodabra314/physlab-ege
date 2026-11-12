#include "models/SpringHorizontalModel.h"

#include <algorithm>
#include <cmath>

#include "utils/MathText.h"

namespace lab::models {
namespace {

// Ускорение свободного падения постоянно: в школьной задаче им управляют
// не пользователю, а выбор модели поверхности.
constexpr double kGravity = 9.8;
// Скорость ниже порога считается нулевой: дальше решает условие покоя.
constexpr double kStopEpsilon = 1e-7;
// Точность «равно нулю» при сравнении сил покоя.
constexpr double kForceEpsilon = 1e-12;

}  // namespace

SpringHorizontalModel::SpringHorizontalModel(QObject* parent) : Simulation(parent) { seedDefaults(); }

QString SpringHorizontalModel::title() const { return QStringLiteral("Пружина с грузом: горизонтальные колебания"); }

QString SpringHorizontalModel::subtitle() const {
    return QStringLiteral("Сила упругости, сухое трение и затухание колебаний");
}

QString SpringHorizontalModel::statement() const {
    return QStringLiteral(
        "Груз, скользящий по горизонтальной поверхности, соединён с неподвижной стеной "
        "пружиной. Груз оттягивают из положения равновесия и отпускают. Опишите его "
        "движение, если между грузом и поверхностью есть трение.");
}

QString SpringHorizontalModel::problemText() const {
    using utils::formatNumber;
    return QStringLiteral(
               "Груз массой m = %1 кг лежит на горизонтальной поверхности и соединён с "
               "неподвижной стеной пружиной жёсткостью k = %2 Н/м. Груз отклоняют на "
               "x₀ = %3 м от положения равновесия и отпускают без толчка. Коэффициент "
               "трения между грузом и поверхностью μ = %4. Опишите движение груза и "
               "найдите период колебаний, если трением пренебречь.")
        .arg(formatNumber(mass(), 2), formatNumber(stiffness(), 0), formatNumber(initialDisplacement(), 3),
             formatNumber(frictionCoeff(), 2));
}

// ------------------------------------------------------------------
// Параметры
// ------------------------------------------------------------------

QVector<ParamSpec> SpringHorizontalModel::paramSpecs() const {
    QVector<ParamSpec> specs;

    ParamSpec mass;
    mass.id = QStringLiteral("m");
    mass.label = QStringLiteral("Масса груза m");
    mass.unit = QStringLiteral("кг");
    mass.kind = ParamKind::Slider;
    mass.minValue = 0.1;
    mass.maxValue = 5.0;
    mass.stepValue = 0.05;
    mass.decimals = 2;
    mass.allowZero = false;
    mass.defaultValue = 1.0;
    mass.group = QStringLiteral("Система");
    mass.helpText = QStringLiteral("Масса блока, который скользит по поверхности");
    specs.push_back(mass);

    ParamSpec stiffness;
    stiffness.id = QStringLiteral("k");
    stiffness.label = QStringLiteral("Жёсткость пружины k");
    stiffness.unit = QStringLiteral("Н/м");
    stiffness.kind = ParamKind::Slider;
    stiffness.minValue = 5.0;
    stiffness.maxValue = 400.0;
    stiffness.stepValue = 5.0;
    stiffness.decimals = 0;
    stiffness.allowZero = false;
    stiffness.defaultValue = 120.0;
    stiffness.group = QStringLiteral("Система");
    stiffness.helpText = QStringLiteral("Чем больше жёсткость, тем короче период колебаний");
    specs.push_back(stiffness);

    ParamSpec friction;
    friction.id = QStringLiteral("mu");
    friction.label = QStringLiteral("Коэффициент трения μ");
    friction.kind = ParamKind::Slider;
    friction.minValue = 0.0;
    friction.maxValue = 0.6;
    friction.stepValue = 0.01;
    friction.decimals = 2;
    friction.allowZero = true;
    friction.defaultValue = 0.08;
    friction.group = QStringLiteral("Взаимодействие с поверхностью");
    friction.helpText = QStringLiteral("μ = 0 — идеальная система, колебания незатухающие");
    specs.push_back(friction);

    ParamSpec offset;
    offset.id = QStringLiteral("x0");
    offset.label = QStringLiteral("Начальное смещение x₀");
    offset.unit = QStringLiteral("м");
    offset.kind = ParamKind::Slider;
    offset.minValue = -0.5;
    offset.maxValue = 0.5;
    offset.stepValue = 0.005;
    offset.decimals = 3;
    offset.allowZero = true;
    offset.defaultValue = 0.24;
    offset.group = QStringLiteral("Начальные условия");
    offset.helpText = QStringLiteral("Отклонение от положения равновесия; знак задаёт сторону");
    specs.push_back(offset);

    return specs;
}

ValidationResult SpringHorizontalModel::validate() const {
    ValidationResult check;
    if (mass() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Масса груза должна быть положительной");
        check.hint = QStringLiteral("Задайте массу не меньше 0,1 кг");
        return check;
    }
    if (stiffness() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Жёсткость пружины должна быть положительной");
        check.hint = QStringLiteral("Задайте жёсткость не меньше 5 Н/м");
        return check;
    }
    if (frictionCoeff() < 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Коэффициент трения не может быть отрицательным");
        check.hint = QStringLiteral("Задайте μ в диапазоне от 0 до 0,6");
        return check;
    }
    const double x0 = initialDisplacement();
    if (std::abs(x0) <= deadZone() + 1e-9 && frictionCoeff() > 0.0) {
        check.ok = false;
        check.message =
            QStringLiteral("Сила упругости при таком смещении не превосходит силу трения покоя — груз не тронется");
        check.hint = QStringLiteral("Увеличьте смещение больше %1 м или уменьшите μ")
                         .arg(utils::formatNumber(deadZone(), 3));
        return check;
    }
    if (std::abs(x0) <= 1e-9) {
        check.ok = false;
        check.message = QStringLiteral("Начальное смещение равно нулю: груз уже в положении равновесия");
        check.hint = QStringLiteral("Перетащите груз или задайте ненулевое x₀");
        return check;
    }
    return check;
}

// ------------------------------------------------------------------
// Параметры и величины, считаемые по формулам
// ------------------------------------------------------------------

double SpringHorizontalModel::mass() const { return param(QStringLiteral("m")); }
double SpringHorizontalModel::stiffness() const { return param(QStringLiteral("k")); }
double SpringHorizontalModel::frictionCoeff() const { return param(QStringLiteral("mu")); }
double SpringHorizontalModel::initialDisplacement() const { return param(QStringLiteral("x0")); }
double SpringHorizontalModel::gravity() const { return kGravity; }

double SpringHorizontalModel::normalForce() const { return mass() * kGravity; }
double SpringHorizontalModel::frictionForce() const { return frictionCoeff() * normalForce(); }

double SpringHorizontalModel::omega() const {
    const double m = mass(), k = stiffness();
    if (m <= 0.0 || k <= 0.0)
        return 0.0;
    return std::sqrt(k / m);
}

double SpringHorizontalModel::period() const {
    const double w = omega();
    return w > 0.0 ? 2.0 * M_PI / w : 0.0;
}

double SpringHorizontalModel::deadZone() const {
    const double k = stiffness();
    return k > 0.0 ? frictionForce() / k : 0.0;
}

// ------------------------------------------------------------------
// Время наблюдения
// ------------------------------------------------------------------

double SpringHorizontalModel::predictedRestTime() const {
    const double limit = frictionForce();
    const double k = stiffness();
    if (limit <= 0.0 || k <= 0.0 || !validate().ok)
        return std::numeric_limits<double>::quiet_NaN();

    // При каждом полу колебании амплитуда убывает ровно на 2μmg/k, поэтому
    // число полу колебаний до остановки находится из условия |x| ≤ μmg/k.
    const double decayPerHalfPeriod = 2.0 * limit / k;
    const double threshold = limit / k;
    const double amplitude = std::abs(initialDisplacement());
    if (amplitude <= threshold)
        return 0.0;
    const double halfPeriod = period() / 2.0;
    return (std::floor((amplitude - threshold) / decayPerHalfPeriod) + 1.0) * halfPeriod;
}

double SpringHorizontalModel::duration() const {
    const double periodValue = period();
    if (!std::isfinite(periodValue) || periodValue <= 0.0)
        return 12.0;
    // Без трения наблюдаем двенадцать периодов — так виден устойчивый закон.
    const double stop = predictedRestTime();
    if (frictionCoeff() <= 0.0 || !std::isfinite(stop))
        return std::clamp(12.0 * periodValue, 1.0, 600.0);
    // С трением горизонт наблюдения строится вокруг момента остановки:
    // шкала времени и графики не заполняются «пустотой» простоя.
    return std::clamp(std::max(4.0 * periodValue, stop + 1.2 * periodValue), periodValue, 600.0);
}

double SpringHorizontalModel::timePerSecond() const {
    return std::clamp(duration() / 16.0, 0.02, 6.0);
}

double SpringHorizontalModel::stepSize() const { return duration() / 240.0; }

// ------------------------------------------------------------------
// Численное интегрирование
// ------------------------------------------------------------------

bool SpringHorizontalModel::checkSticking(double t, QVector<double>& y, const QVector<double>& previous) const {
    // Без трения перехода к покою не бывает: груз проходит точку остановки
    // насквозь, и вмешиваться в решение нельзя.
    const double limit = frictionForce();
    if (limit <= kForceEpsilon)
        return false;

    const double k = stiffness();
    const double vBefore = previous.value(1, 0.0);
    const double vAfter = y.value(1, 0.0);

    // Груз прошёл точку остановки: координату в этот момент уточняем
    // интерполяцией по скорости, после чего скорость обнуляем.
    if (vBefore * vAfter < 0.0) {
        const double span = std::abs(vBefore - vAfter);
        const double weight = span < kForceEpsilon ? 0.5 : std::abs(vBefore) / span;
        y[0] = previous.value(0, 0.0) + weight * (y.value(0, 0.0) - previous.value(0, 0.0));
        y[1] = 0.0;
    }

    if (std::abs(y.value(1, 0.0)) > kStopEpsilon * std::max(1.0, initialDisplacement() * omega()))
        return false;

    // Физическое условие покоя при сухом трении: удерживающая сила пружины
    // не больше максимума силы трения покоя. Иначе груз начинает движение
    // в сторону положения равновесия.
    const double holding = std::abs(k * y.value(0, 0.0));
    if (holding <= limit + kForceEpsilon) {
        y[1] = 0.0;
        m_restTime = t;
        m_restPosition = y.value(0, 0.0);
        return true;
    }
    return false;
}

void SpringHorizontalModel::ensureSimulated() const {
    const double m = mass();
    const double k = stiffness();
    const double mu = frictionCoeff();
    const double x0 = initialDisplacement();
    const double horizon = duration();

    const bool cacheHit = !m_trajectory.empty() && std::abs(m_cacheMass - m) < 1e-12 &&
                          std::abs(m_cacheStiffness - k) < 1e-9 && std::abs(m_cacheFriction - mu) < 1e-12 &&
                          std::abs(m_cacheOffset - x0) < 1e-12 && std::abs(m_cacheHorizon - horizon) < 1e-9;
    if (cacheHit)
        return;

    m_restTime = std::numeric_limits<double>::quiet_NaN();
    m_restPosition = std::numeric_limits<double>::quiet_NaN();
    m_cacheMass = m;
    m_cacheStiffness = k;
    m_cacheFriction = mu;
    m_cacheOffset = x0;
    m_cacheHorizon = horizon;

    if (!validate().ok) {
        m_trajectory = Trajectory();
        m_trajectory.simulate(horizon, std::max(1e-3, horizon / 1200.0),
                              [](double, const QVector<double>& y, QVector<double>& dydt) {
                                  dydt[0] = 0.0;
                                  dydt[1] = 0.0;
                                  (void)y;
                              },
                              {x0, 0.0}, 1200);
        return;
    }

    // Шаг: 160 узлов на период идеальных колебаний — с запасом для RK4
    // и достаточно для гладких графиков x(t), v(t), a(t).
    const double periodValue = period();
    const double requestedStep = std::max(1e-5, periodValue / 160.0);
    const double limit = mu * m * kGravity;

    const Trajectory::Rhs rhs = [k, m, limit](double /*t*/, const QVector<double>& y, QVector<double>& dydt) {
        const double x = y.value(0, 0.0);
        const double v = y.value(1, 0.0);
        const double spring = -k * x;
        double force = spring;
        if (std::abs(v) > kStopEpsilon) {
            // Скольжение: трение направлено против скорости.
            force -= std::copysign(limit, v);
        } else if (std::abs(spring) > limit) {
            // Срыв с места: трение максимально и направлено против силы пружины.
            force -= std::copysign(limit, spring);
        } else {
            // Равновесие, которое удерживает трение: ускорения нет.
            force = 0.0;
        }
        dydt[0] = v;
        dydt[1] = force / m;
    };

    const Trajectory::StepHook hook = [this](double t, QVector<double>& y, const QVector<double>& before) {
        return checkSticking(t, y, before);
    };

    m_trajectory.simulate(horizon, requestedStep, rhs, {x0, 0.0}, 2400, hook);
}

double SpringHorizontalModel::displacementAt(double t) const {
    ensureSimulated();
    return m_trajectory.componentAt(0, t);
}

double SpringHorizontalModel::velocityAt(double t) const {
    ensureSimulated();
    return m_trajectory.componentAt(1, t);
}

double SpringHorizontalModel::accelerationOf(double x, double v) const {
    const double m = mass(), k = stiffness();
    if (m <= 0.0)
        return 0.0;
    const double limit = frictionForce();
    const double spring = -k * x;
    double force = spring;
    if (std::abs(v) > kStopEpsilon)
        force -= std::copysign(limit, v);
    else if (std::abs(spring) > limit)
        force -= std::copysign(limit, spring);
    else
        force = 0.0;
    return force / m;
}

double SpringHorizontalModel::accelerationAt(double t) const { return accelerationOf(displacementAt(t), velocityAt(t)); }

double SpringHorizontalModel::elasticForceAt(double t) const { return -stiffness() * displacementAt(t); }

double SpringHorizontalModel::frictionForceAt(double t) const {
    const double v = velocityAt(t);
    const double limit = frictionForce();
    if (std::abs(v) > kStopEpsilon)
        return -std::copysign(limit, v);
    // Покой: сила трения не равна нулю, а уравновешивает силу пружины,
    // не превышая максимума μN (трение покоя).
    const double spring = -stiffness() * displacementAt(t);
    if (std::abs(spring) > limit)
        return -std::copysign(limit, spring);
    return -spring;
}

double SpringHorizontalModel::restTime() const {
    ensureSimulated();
    return m_restTime;
}

double SpringHorizontalModel::restPosition() const {
    ensureSimulated();
    return m_restPosition;
}

const Trajectory& SpringHorizontalModel::trajectory() const {
    ensureSimulated();
    return m_trajectory;
}

QVector<QPointF> SpringHorizontalModel::displacementSeries() const {
    ensureSimulated();
    return m_trajectory.samples(0);
}

QVector<QPointF> SpringHorizontalModel::velocitySeries() const {
    ensureSimulated();
    return m_trajectory.samples(1);
}

QVector<QPointF> SpringHorizontalModel::accelerationSeries() const {
    ensureSimulated();
    const QVector<QPointF> displacement = m_trajectory.samples(0);
    const QVector<QPointF> velocity = m_trajectory.samples(1);
    QVector<QPointF> points;
    points.reserve(displacement.size());
    for (int i = 0; i < displacement.size(); ++i)
        points.append(QPointF(displacement.at(i).x(), accelerationOf(displacement.at(i).y(), velocity.value(i).y())));
    return points;
}

double SpringHorizontalModel::maxDisplacement() const {
    ensureSimulated();
    double largest = std::abs(initialDisplacement());
    const QVector<QPointF> displacement = m_trajectory.samples(0);
    for (const QPointF& p : displacement)
        largest = std::max(largest, std::abs(p.y()));
    return std::max(0.01, largest);
}

double SpringHorizontalModel::maxSpeed() const {
    ensureSimulated();
    double largest = 0.0;
    const QVector<QPointF> velocity = m_trajectory.samples(1);
    for (const QPointF& p : velocity)
        largest = std::max(largest, std::abs(p.y()));
    return largest;
}

double SpringHorizontalModel::maxAcceleration() const {
    ensureSimulated();
    double largest = 0.0;
    const QVector<QPointF> acceleration = accelerationSeries();
    for (const QPointF& p : acceleration)
        largest = std::max(largest, std::abs(p.y()));
    return largest;
}

// ------------------------------------------------------------------
// Состояние и ключевое событие
// ------------------------------------------------------------------

SimState SpringHorizontalModel::stateAt(double t) const {
    SimState state;
    state.time = t;
    if (!validate().ok)
        return state;

    const double x = displacementAt(t);
    const double v = velocityAt(t);
    const double a = accelerationOf(x, v);
    const double stop = restTime();
    const bool atRest = std::isfinite(stop) && t >= stop - 1e-9;

    EntityVisual load;
    load.id = QStringLiteral("load");
    load.label = QStringLiteral("Груз");
    load.position = x;
    load.value1 = v;
    load.value2 = a;
    load.active = !atRest;
    state.entities.push_back(load);

    state.primary = x;
    state.secondary = v;
    state.resolved = atRest;
    state.eventLabel = atRest ? QStringLiteral("Груз остановился") : QString();
    return state;
}

double SpringHorizontalModel::eventTime() const {
    const double stop = restTime();
    if (!std::isfinite(stop) || stop <= 1e-9)
        return std::numeric_limits<double>::quiet_NaN();
    return stop;
}

// ------------------------------------------------------------------
// Панель вычислений
// ------------------------------------------------------------------

Derivation SpringHorizontalModel::derivation() const {
    using utils::formatNumber;
    Derivation d;
    const ValidationResult check = validate();
    d.valid = check.ok;
    d.problemText = problemText();
    d.unknown = QStringLiteral("Закон движения груза x(t) и период колебаний при μ = 0");

    if (!check.ok) {
        d.steps.push_back({QStringLiteral("Внимание"), QStringLiteral("\\text{%1}").arg(check.message)});
        d.steps.push_back({QStringLiteral("Что сделать"), QStringLiteral("\\text{%1}").arg(check.hint)});
        d.answerTemplate = QStringLiteral("Расчёт невозможен: %1").arg(check.message);
        return d;
    }

    const double m = mass(), k = stiffness(), mu = frictionCoeff(), x0 = initialDisplacement();
    const double normal = normalForce();
    const double frictionLimit = frictionForce();
    const double t = time();
    const double x = displacementAt(t);
    const double v = velocityAt(t);
    const double a = accelerationOf(x, v);
    const double springForce = -k * x;
    const double frictionNow = frictionForceAt(t);
    const double periodValue = period();
    const double omegaValue = omega();
    const double stop = restTime();

    QVector<DerivationStep>& st = d.steps;
    st.push_back({QStringLiteral("1. Исходные данные"),
                  QStringLiteral("m = %1 \\text{ кг},\\quad k = %2 \\text{ Н/м},\\quad \\mu = %3,\\quad x_{0} = %4 "
                                 "\\text{ м},\\quad g = %5 \\text{ м/с}^{2}")
                      .arg(formatNumber(m, 2), formatNumber(k, 0), formatNumber(mu, 2), formatNumber(x0, 3),
                           formatNumber(kGravity, 1))});

    st.push_back({QStringLiteral("2. Уравнение движения"),
                  QStringLiteral("m x'' = F_{\\text{упр}} + F_{\\text{тр}},\\qquad F_{\\text{упр}} = -k x,\\qquad "
                                 "F_{\\text{тр}} = -\\mu N \\text{sign}(v)")});

    st.push_back({QStringLiteral("Реакция опоры (горизонтальная поверхность)"),
                  QStringLiteral("N = m g = %1 \\cdot %2 = %3 \\text{ Н}")
                      .arg(formatNumber(m, 2), formatNumber(kGravity, 1), formatNumber(normal, 3))});
    st.push_back({QStringLiteral("Модуль силы трения скольжения"),
                  QStringLiteral("|F_{\\text{тр}}| = \\mu N = %1 \\cdot %2 = %3 \\text{ Н}")
                      .arg(formatNumber(mu, 2), formatNumber(normal, 3), formatNumber(frictionLimit, 4))});

    st.push_back({QStringLiteral("3. Состояние в текущий момент"),
                  QStringLiteral("t = %1 \\text{ с},\\quad x = %2 \\text{ м},\\quad v = %3 \\text{ м/с}")
                      .arg(formatNumber(t, 2), formatNumber(x, 4), formatNumber(v, 4))});
    st.push_back({QStringLiteral("Сила упругости"),
                  QStringLiteral("F_{\\text{упр}} = -k x = -%1 \\cdot %2 = %3 \\text{ Н}")
                      .arg(formatNumber(k, 0), formatNumber(x, 4), formatNumber(springForce, 4))});
    st.push_back({QStringLiteral("Сила трения"),
                  mu <= 0.0
                      ? QStringLiteral("F_{\\text{тр}} = 0 \\text{ Н} \\quad \\text{(трения нет)}")
                      : QStringLiteral("F_{\\text{тр}} = %1 \\text{ Н} \\quad \\text{%2}")
                            .arg(formatNumber(frictionNow, 4),
                                 std::abs(frictionNow) <= kForceEpsilon
                                     ? QStringLiteral("груз покоится, трение не проявляется")
                                     : QStringLiteral("направлена против скорости"))});
    st.push_back({QStringLiteral("Ускорение из уравнения движения"),
                  QStringLiteral("a = \\frac{{F_{\\text{упр}} + F_{\\text{тр}}}}{{m}} = \\frac{{%1 + %2}}{{%3}} = %4 "
                                 "\\text{ м/с}^{2}")
                      .arg(formatNumber(springForce, 4), formatNumber(frictionNow, 4), formatNumber(m, 2),
                           formatNumber(a, 4))});

    st.push_back({QStringLiteral("4. Идеальная система (μ = 0)"),
                  QStringLiteral("\\omega = \\sqrt{{\\frac{{k}}{{m}}}} = \\sqrt{{\\frac{{%1}}{{%2}}}} = %3 "
                                 "\\text{ с}^{{-1}}")
                      .arg(formatNumber(k, 0), formatNumber(m, 2), formatNumber(omegaValue, 4))});
    st.push_back({QStringLiteral("Период колебаний"),
                  QStringLiteral("T = 2\\pi\\sqrt{{\\frac{{m}}{{k}}}} = 2\\pi\\sqrt{{\\frac{{%1}}{{%2}}}} = %3 "
                                 "\\text{ с}")
                      .arg(formatNumber(m, 2), formatNumber(k, 0), formatNumber(periodValue, 4))});
    st.push_back({QStringLiteral("Закон колебаний"),
                  QStringLiteral("x(t) = A \\cos(\\omega t + \\varphi),\\qquad A = |x_{0}| = %1 \\text{ м},\\qquad "
                                 "\\varphi = %2")
                      .arg(formatNumber(std::abs(x0), 3), x0 >= 0.0 ? QStringLiteral("0") : QStringLiteral("\\pi"))});

    if (mu > 0.0) {
        st.push_back({QStringLiteral("5. Движение с трением"),
                      QStringLiteral("Уравнение нелинейно из-за \\text{{sign}}(v), поэтому решение "
                                     "ищется численно: метод Рунге–Кутты 4-го порядка, шаг h = T/160 = %1 \\text{ с}")
                          .arg(formatNumber(periodValue / 160.0, 5))});
        st.push_back({QStringLiteral("Закон затухания"),
                      QStringLiteral("Амплитуда убывает линейно: за один период она уменьшается на "
                                     "\\frac{{4 \\mu m g}}{{k}} = %1 \\text{ м}")
                          .arg(formatNumber(4.0 * frictionLimit / k, 4))});
        st.push_back({QStringLiteral("Условие покоя"),
                      QStringLiteral("Груз останавливается, когда силу пружины удерживает трение покоя: "
                                     "|k x| \\le \\mu m g \\Rightarrow |x| \\le \\frac{{\\mu m g}}{{k}} = %1 \\text{ м}")
                          .arg(formatNumber(deadZone(), 4))});
        if (std::isfinite(stop)) {
            st.push_back({QStringLiteral("Момент остановки"),
                          QStringLiteral("t^{*} = %1 \\text{ с},\\qquad x(t^{*}) = %2 \\text{ м}")
                              .arg(formatNumber(stop, 3), formatNumber(restPosition(), 4))});
        }
    }

    if (mu <= 0.0) {
        d.answerTemplate = QStringLiteral("T = %1 с; x(t) = %2 cos(%3 t)%4")
                               .arg(formatNumber(periodValue, 3), formatNumber(std::abs(x0), 3),
                                    formatNumber(omegaValue, 3), x0 >= 0.0 ? QString() : QStringLiteral(" + \\pi"));
    } else if (std::isfinite(stop)) {
        d.answerTemplate = QStringLiteral("При μ = %1 груз остановится через t = %2 с в точке x = %3 м "
                                          "(период незатухающих колебаний T = %4 с)")
                               .arg(formatNumber(mu, 2), formatNumber(stop, 3), formatNumber(restPosition(), 4),
                                    formatNumber(periodValue, 3));
    } else {
        d.answerTemplate = QStringLiteral("За %1 с груз не остановится; амплитуда убывает на %2 м за период")
                               .arg(formatNumber(duration(), 2), formatNumber(4.0 * frictionLimit / k, 4));
    }
    return d;
}

QVector<Readout> SpringHorizontalModel::readouts() const {
    QVector<Readout> list;
    if (!validate().ok)
        return list;

    const double t = time();
    const double x = displacementAt(t);
    const double v = velocityAt(t);
    const double a = accelerationOf(x, v);
    const double stop = restTime();
    const bool atRest = std::isfinite(stop) && t >= stop - 1e-9;

    list.push_back({QStringLiteral("Смещение x"), QStringLiteral("%1 м").arg(utils::formatNumber(x, 4))});
    list.push_back({QStringLiteral("Скорость v"), QStringLiteral("%1 м/с").arg(utils::formatNumber(v, 4))});
    list.push_back({QStringLiteral("Ускорение a"), QStringLiteral("%1 м/с²").arg(utils::formatNumber(a, 4))});
    list.push_back({QStringLiteral("Сила упругости Fупр"), QStringLiteral("%1 Н").arg(utils::formatNumber(elasticForceAt(t), 4))});
    list.push_back({QStringLiteral("Сила трения Fтр"), QStringLiteral("%1 Н").arg(utils::formatNumber(frictionForceAt(t), 4))});
    list.push_back({QStringLiteral("Реакция опоры N"), QStringLiteral("%1 Н").arg(utils::formatNumber(normalForce(), 3))});
    list.push_back({QStringLiteral("Период T при μ = 0"), QStringLiteral("%1 с").arg(utils::formatNumber(period(), 3))});
    list.push_back({QStringLiteral("Состояние груза"),
                    atRest ? QStringLiteral("покой с t = %1 с").arg(utils::formatNumber(stop, 2))
                           : QStringLiteral("движется"),
                    atRest});
    return list;
}

// ------------------------------------------------------------------
// Точки взаимодействия
// ------------------------------------------------------------------

QVector<HandleInfo> SpringHorizontalModel::handles() const {
    QVector<HandleInfo> list;
    HandleInfo block;
    block.id = QStringLiteral("x0");
    block.label = QStringLiteral("Положение груза");
    block.value = initialDisplacement();
    block.minValue = -0.5;
    block.maxValue = 0.5;
    block.step = 0.005;
    block.unit = QStringLiteral("м");
    block.axis = QStringLiteral("x");
    block.readbackLabel = QStringLiteral("Сила упругости при таком смещении");
    block.readbackValue = QStringLiteral("%1 Н").arg(utils::formatNumber(stiffness() * std::abs(block.value), 2));
    list.push_back(block);
    return list;
}

void SpringHorizontalModel::beginInteraction() {
    Simulation::beginInteraction();
    // Груз отводят рукой из положения равновесия, поэтому расчёт начинается
    // сначала: новое смещение становится началом движения, а не кадром
    // уже идущего эксперимента.
    setStateTime(0.0);
}

void SpringHorizontalModel::setHandleValue(const QString& id, double value) {
    if (id == QLatin1String("x0")) {
        setParam(QStringLiteral("x0"), value);
        return;
    }
    Simulation::setHandleValue(id, value);
}

}  // namespace lab::models
