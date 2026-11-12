#include "models/SpringVerticalModel.h"

#include <algorithm>
#include <cmath>

#include "utils/MathText.h"

namespace lab::models {
namespace {

constexpr double kGravity = 9.8;
// Максимальное отклонение от положения равновесия: в пределах упругой
// деформации пружины, которую показывает схема.
constexpr double kMaxAmplitude = 0.25;

}  // namespace

SpringVerticalModel::SpringVerticalModel(QObject* parent) : Simulation(parent) { seedDefaults(); }

QString SpringVerticalModel::title() const { return QStringLiteral("Пружина с грузом: вертикальные колебания"); }

QString SpringVerticalModel::subtitle() const {
    return QStringLiteral("Тяжесть смещает равновесие: k·Δl = mg, m·x″ = −kx");
}

QString SpringVerticalModel::statement() const {
    return QStringLiteral(
        "Груз подвешен на вертикальной пружине, второй конец пружины закреплён. "
        "Груз оттягивают вниз от положения равновесия и отпускают. Опишите его "
        "движение и найдите период колебаний.");
}

QString SpringVerticalModel::problemText() const {
    using utils::formatNumber;
    return QStringLiteral(
               "Груз массой m = %1 кг подвешен на вертикальной пружине жёсткостью "
               "k = %2 Н/м, верхний конец пружины закреплён. Груз отклоняют на "
               "x₀ = %3 м от положения равновесия (направление задаётся знаком: "
               "вниз — положительно) и отпускают без толчка. "
               "Груз подвешен и с поверхностями не соприкасается, поэтому силы трения "
               "в модели нет. Найдите растяжение пружины в равновесии, период "
               "колебаний и закон движения груза.")
        .arg(formatNumber(mass(), 2), formatNumber(stiffness(), 0), formatNumber(initialDisplacement(), 3));
}

// ------------------------------------------------------------------
// Параметры
// ------------------------------------------------------------------

QVector<ParamSpec> SpringVerticalModel::paramSpecs() const {
    QVector<ParamSpec> specs;

    ParamSpec mass;
    mass.id = QStringLiteral("m");
    mass.label = QStringLiteral("Масса груза m");
    mass.unit = QStringLiteral("кг");
    mass.kind = ParamKind::Slider;
    mass.minValue = 0.1;
    mass.maxValue = 2.0;
    mass.stepValue = 0.05;
    mass.decimals = 2;
    mass.allowZero = false;
    mass.defaultValue = 0.8;
    mass.group = QStringLiteral("Система");
    mass.helpText = QStringLiteral("От массы зависят растяжение пружины в равновесии и период");
    specs.push_back(mass);

    ParamSpec stiffness;
    stiffness.id = QStringLiteral("k");
    stiffness.label = QStringLiteral("Жёсткость пружины k");
    stiffness.unit = QStringLiteral("Н/м");
    stiffness.kind = ParamKind::Slider;
    stiffness.minValue = 20.0;
    stiffness.maxValue = 400.0;
    stiffness.stepValue = 5.0;
    stiffness.decimals = 0;
    stiffness.allowZero = false;
    stiffness.defaultValue = 120.0;
    stiffness.group = QStringLiteral("Система");
    stiffness.helpText = QStringLiteral("Чем больше жёсткость, тем меньше растяжение и короче период");
    specs.push_back(stiffness);

    ParamSpec offset;
    offset.id = QStringLiteral("x0");
    offset.label = QStringLiteral("Начальное смещение x₀");
    offset.unit = QStringLiteral("м");
    offset.kind = ParamKind::Slider;
    offset.minValue = -kMaxAmplitude;
    offset.maxValue = kMaxAmplitude;
    offset.stepValue = 0.005;
    offset.decimals = 3;
    offset.allowZero = true;
    offset.defaultValue = 0.15;
    offset.group = QStringLiteral("Начальные условия");
    offset.helpText = QStringLiteral("Отклонение от положения равновесия; знак «+» — вниз");
    specs.push_back(offset);

    return specs;
}

ValidationResult SpringVerticalModel::validate() const {
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
        check.hint = QStringLiteral("Задайте жёсткость не меньше 20 Н/м");
        return check;
    }
    if (std::abs(initialDisplacement()) > kMaxAmplitude + 1e-9) {
        check.ok = false;
        check.message = QStringLiteral("Отклонение слишком велико: пружина выйдет из упругой области");
        check.hint = QStringLiteral("Задайте |x₀| не больше %1 м").arg(utils::formatNumber(kMaxAmplitude, 2));
        return check;
    }
    if (std::abs(initialDisplacement()) <= 1e-9) {
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

double SpringVerticalModel::mass() const { return param(QStringLiteral("m")); }
double SpringVerticalModel::stiffness() const { return param(QStringLiteral("k")); }
double SpringVerticalModel::initialDisplacement() const { return param(QStringLiteral("x0")); }
double SpringVerticalModel::gravity() const { return kGravity; }

double SpringVerticalModel::equilibriumElongation() const {
    const double k = stiffness();
    return k > 0.0 ? mass() * kGravity / k : 0.0;
}

double SpringVerticalModel::omega() const {
    const double m = mass(), k = stiffness();
    if (m <= 0.0 || k <= 0.0)
        return 0.0;
    return std::sqrt(k / m);
}

double SpringVerticalModel::period() const {
    const double w = omega();
    return w > 0.0 ? 2.0 * M_PI / w : 0.0;
}

double SpringVerticalModel::amplitude() const { return std::abs(initialDisplacement()); }

double SpringVerticalModel::initialPhase() const { return initialDisplacement() >= 0.0 ? 0.0 : M_PI; }

// ------------------------------------------------------------------
// Движение: точное решение линейного уравнения
// ------------------------------------------------------------------

double SpringVerticalModel::displacementAt(double t) const {
    if (!validate().ok)
        return initialDisplacement();
    return amplitude() * std::cos(omega() * t + initialPhase());
}

double SpringVerticalModel::velocityAt(double t) const {
    if (!validate().ok)
        return 0.0;
    return -amplitude() * omega() * std::sin(omega() * t + initialPhase());
}

double SpringVerticalModel::accelerationOf(double x) const {
    const double m = mass();
    return m > 0.0 ? -stiffness() * x / m : 0.0;
}

double SpringVerticalModel::accelerationAt(double t) const { return accelerationOf(displacementAt(t)); }

double SpringVerticalModel::elongationAt(double t) const { return equilibriumElongation() + displacementAt(t); }

double SpringVerticalModel::elasticForceAt(double t) const { return stiffness() * elongationAt(t); }

double SpringVerticalModel::weight() const { return mass() * kGravity; }

double SpringVerticalModel::maxElongation() const { return equilibriumElongation() + amplitude(); }

double SpringVerticalModel::minElongation() const { return equilibriumElongation() - amplitude(); }

// ------------------------------------------------------------------
// Время наблюдения
// ------------------------------------------------------------------

double SpringVerticalModel::duration() const {
    const double periodValue = period();
    if (!std::isfinite(periodValue) || periodValue <= 0.0)
        return 12.0;
    return std::clamp(8.0 * periodValue, 1.0, 200.0);
}

double SpringVerticalModel::timePerSecond() const { return std::clamp(duration() / 12.0, 0.02, 6.0); }

double SpringVerticalModel::stepSize() const { return duration() / 160.0; }

// ------------------------------------------------------------------
// Состояние и событие
// ------------------------------------------------------------------

SimState SpringVerticalModel::stateAt(double t) const {
    SimState state;
    state.time = t;
    if (!validate().ok)
        return state;

    const double x = displacementAt(t);
    const double v = velocityAt(t);
    const double a = accelerationOf(x);

    EntityVisual load;
    load.id = QStringLiteral("load");
    load.label = QStringLiteral("Груз");
    load.position = x;          // смещение относительно равновесия
    load.value1 = v;
    load.value2 = a;
    state.entities.push_back(load);

    EntityVisual spring;
    spring.id = QStringLiteral("spring");
    spring.label = QStringLiteral("Пружина");
    spring.position = elongationAt(t);   // фактическое растяжение
    spring.value1 = elasticForceAt(t);
    state.entities.push_back(spring);

    state.primary = x;
    state.secondary = elongationAt(t);
    return state;
}

double SpringVerticalModel::eventTime() const {
    // Ключевого события нет: груз колеблется неограниченно долго.
    return std::numeric_limits<double>::quiet_NaN();
}

// ------------------------------------------------------------------
// Ряды для графиков
// ------------------------------------------------------------------

QVector<QPointF> SpringVerticalModel::displacementSeries() const {
    QVector<QPointF> points;
    const double horizon = duration();
    const int steps = 600;
    points.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const double t = horizon * i / steps;
        points.append(QPointF(t, displacementAt(t)));
    }
    return points;
}

QVector<QPointF> SpringVerticalModel::velocitySeries() const {
    QVector<QPointF> points;
    const double horizon = duration();
    const int steps = 600;
    points.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const double t = horizon * i / steps;
        points.append(QPointF(t, velocityAt(t)));
    }
    return points;
}

QVector<QPointF> SpringVerticalModel::accelerationSeries() const {
    QVector<QPointF> points;
    const double horizon = duration();
    const int steps = 600;
    points.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const double t = horizon * i / steps;
        points.append(QPointF(t, accelerationAt(t)));
    }
    return points;
}

// ------------------------------------------------------------------
// Панель вычислений
// ------------------------------------------------------------------

Derivation SpringVerticalModel::derivation() const {
    using utils::formatNumber;
    Derivation d;
    const ValidationResult check = validate();
    d.valid = check.ok;
    d.problemText = problemText();
    d.unknown = QStringLiteral("Растяжение пружины в равновесии Δl₀, период T и закон движения x(t)");

    if (!check.ok) {
        d.steps.push_back({QStringLiteral("Внимание"), QStringLiteral("\\text{%1}").arg(check.message)});
        d.steps.push_back({QStringLiteral("Что сделать"), QStringLiteral("\\text{%1}").arg(check.hint)});
        d.answerTemplate = QStringLiteral("Расчёт невозможен: %1").arg(check.message);
        return d;
    }

    const double m = mass(), k = stiffness(), x0 = initialDisplacement();
    const double weight = this->weight();
    const double elongation0 = equilibriumElongation();
    const double t = time();
    const double x = displacementAt(t);
    const double v = velocityAt(t);
    const double a = accelerationOf(x);
    const double elongation = elongationAt(t);
    const double elastic = elasticForceAt(t);
    const double omegaValue = omega();
    const double periodValue = period();

    QVector<DerivationStep>& st = d.steps;
    st.push_back({QStringLiteral("1. Исходные данные"),
                  QStringLiteral("m = %1 \\text{ кг},\\quad k = %2 \\text{ Н/м},\\quad x_{0} = %3 \\text{ м},\\quad "
                                 "g = %4 \\text{ м/с}^{2}")
                      .arg(formatNumber(m, 2), formatNumber(k, 0), formatNumber(x0, 3), formatNumber(kGravity, 1))});

    st.push_back({QStringLiteral("2. Положение равновесия"),
                  QStringLiteral("В равновесии сила тяжести уравновешивает силу упругости:")});
    st.push_back({QStringLiteral("Условие равновесия"),
                  QStringLiteral("k \\Delta l_{0} = m g \\quad \\Rightarrow \\quad \\Delta l_{0} = "
                                 "\\frac{{m g}}{{k}} = \\frac{{%1 \\cdot %2}}{{%3}} = %4 \\text{ м}")
                      .arg(formatNumber(m, 2), formatNumber(kGravity, 1), formatNumber(k, 0),
                           formatNumber(elongation0, 4))});

    st.push_back({QStringLiteral("3. Уравнение движения"),
                  QStringLiteral("Пусть x — смещение от положения равновесия (ось Y вниз). Тогда растяжение "
                                 "пружины равно \\Delta l = \\Delta l_{0} + x, и")});
    st.push_back({QStringLiteral("Силы, действующие на груз"),
                  QStringLiteral("m x'' = m g - k(\\Delta l_{0} + x)")});
    st.push_back({QStringLiteral("Упрощение с учётом k Δl₀ = mg"),
                  QStringLiteral("m x'' = -k x,\\qquad x'' + \\frac{{k}}{{m}} x = 0")});

    st.push_back({QStringLiteral("4. Состояние в текущий момент"),
                  QStringLiteral("t = %1 \\text{ с},\\quad x = %2 \\text{ м},\\quad v = %3 \\text{ м/с}")
                      .arg(formatNumber(t, 2), formatNumber(x, 4), formatNumber(v, 4))});
    st.push_back({QStringLiteral("Растяжение пружины"),
                  QStringLiteral("\\Delta l = \\Delta l_{0} + x = %1 + %2 = %3 \\text{ м}")
                      .arg(formatNumber(elongation0, 4), formatNumber(x, 4), formatNumber(elongation, 4))});
    st.push_back({QStringLiteral("Сила упругости"),
                  QStringLiteral("F_{\\text{упр}} = k \\Delta l = %1 \\cdot %2 = %3 \\text{ Н} — направлена вверх")
                      .arg(formatNumber(k, 0), formatNumber(elongation, 4), formatNumber(elastic, 4))});
    st.push_back({QStringLiteral("Сила тяжести"),
                  QStringLiteral("mg = %1 \\cdot %2 = %3 \\text{ Н} — направлена вниз")
                      .arg(formatNumber(m, 2), formatNumber(kGravity, 1), formatNumber(weight, 4))});
    st.push_back({QStringLiteral("Ускорение"),
                  QStringLiteral("a = \\frac{{mg - F_{\\text{упр}}}}{{m}} = \\frac{{%1 - %2}}{{%3}} = %4 "
                                 "\\text{ м/с}^{2} = -\\frac{{k x}}{{m}}")
                      .arg(formatNumber(weight, 4), formatNumber(elastic, 4), formatNumber(m, 2), formatNumber(a, 4))});

    st.push_back({QStringLiteral("5. Период и закон колебаний"),
                  QStringLiteral("\\omega = \\sqrt{{\\frac{{k}}{{m}}}} = \\sqrt{{\\frac{{%1}}{{%2}}}} = %3 "
                                 "\\text{ с}^{{-1}}")
                      .arg(formatNumber(k, 0), formatNumber(m, 2), formatNumber(omegaValue, 4))});
    st.push_back({QStringLiteral("Период"),
                  QStringLiteral("T = 2\\pi\\sqrt{{\\frac{{m}}{{k}}}} = 2\\pi\\sqrt{{\\frac{{%1}}{{%2}}}} = %3 "
                                 "\\text{ с}")
                      .arg(formatNumber(m, 2), formatNumber(k, 0), formatNumber(periodValue, 4))});
    st.push_back({QStringLiteral("Начальные условия и решение"),
                  QStringLiteral("x(0) = x_{0} = A \\cos\\varphi,\\quad v(0) = 0 \\Rightarrow \\varphi = %1,\\quad A = "
                                 "|x_{0}| = %2 \\text{ м}")
                      .arg(x0 >= 0.0 ? QStringLiteral("0") : QStringLiteral("\\pi"), formatNumber(amplitude(), 3))});
    st.push_back({QStringLiteral("Закон движения"),
                  QStringLiteral("x(t) = %1 \\cos(%2 t%3),\\quad v(t) = -%4 \\sin(%2 t%3)")
                      .arg(formatNumber(amplitude(), 3), formatNumber(omegaValue, 3),
                           x0 >= 0.0 ? QString() : QStringLiteral(" + \\pi"),
                           formatNumber(amplitude() * omegaValue, 3))});

    d.answerTemplate =
        QStringLiteral("Δl₀ = %1 м; T = %2 с; x(t) = %3 cos(%4 t%5)")
            .arg(formatNumber(elongation0, 4), formatNumber(periodValue, 3), formatNumber(amplitude(), 3),
                 formatNumber(omegaValue, 3), x0 >= 0.0 ? QString() : QStringLiteral(" + \\pi"));
    return d;
}

QVector<Readout> SpringVerticalModel::readouts() const {
    QVector<Readout> list;
    if (!validate().ok)
        return list;

    const double t = time();
    list.push_back({QStringLiteral("Смещение x от равновесия"),
                    QStringLiteral("%1 м").arg(utils::formatNumber(displacementAt(t), 4))});
    list.push_back({QStringLiteral("Скорость v"), QStringLiteral("%1 м/с").arg(utils::formatNumber(velocityAt(t), 4))});
    list.push_back({QStringLiteral("Ускорение a"),
                    QStringLiteral("%1 м/с²").arg(utils::formatNumber(accelerationAt(t), 4))});
    list.push_back({QStringLiteral("Растяжение пружины Δl"),
                    QStringLiteral("%1 м").arg(utils::formatNumber(elongationAt(t), 4))});
    list.push_back({QStringLiteral("Сила упругости Fупр"),
                    QStringLiteral("%1 Н").arg(utils::formatNumber(elasticForceAt(t), 4))});
    list.push_back({QStringLiteral("Сила тяжести mg"), QStringLiteral("%1 Н").arg(utils::formatNumber(weight(), 4))});
    list.push_back({QStringLiteral("Равновесное растяжение Δl₀"),
                    QStringLiteral("%1 м").arg(utils::formatNumber(equilibriumElongation(), 4))});
    list.push_back({QStringLiteral("Период T"), QStringLiteral("%1 с").arg(utils::formatNumber(period(), 3))});
    return list;
}

// ------------------------------------------------------------------
// Точки взаимодействия
// ------------------------------------------------------------------

QVector<HandleInfo> SpringVerticalModel::handles() const {
    QVector<HandleInfo> list;
    HandleInfo block;
    block.id = QStringLiteral("x0");
    block.label = QStringLiteral("Положение груза");
    block.value = initialDisplacement();
    block.minValue = -kMaxAmplitude;
    block.maxValue = kMaxAmplitude;
    block.step = 0.005;
    block.unit = QStringLiteral("м");
    block.axis = QStringLiteral("y");
    block.readbackLabel = QStringLiteral("Сила упругости при таком отклонении");
    block.readbackValue = QStringLiteral("%1 Н").arg(utils::formatNumber(stiffness() * elongationAt(0.0), 2));
    list.push_back(block);
    return list;
}

void SpringVerticalModel::setHandleValue(const QString& id, double value) {
    if (id == QLatin1String("x0")) {
        setParam(QStringLiteral("x0"), value);
        return;
    }
    Simulation::setHandleValue(id, value);
}

void SpringVerticalModel::beginInteraction() {
    Simulation::beginInteraction();
    setStateTime(0.0);
}

}  // namespace lab::models
