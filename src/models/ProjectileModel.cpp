#include "models/ProjectileModel.h"

#include <algorithm>
#include <cmath>

#include "utils/MathText.h"

namespace lab::models {
namespace {

constexpr double kGravity = 9.8;

}  // namespace

ProjectileModel::ProjectileModel(QObject* parent) : Simulation(parent) { seedDefaults(); }

QString ProjectileModel::title() const { return QStringLiteral("Полёт снаряда по параболе"); }

QString ProjectileModel::subtitle() const {
    return QStringLiteral("Две независимые проекции: x = v₀t cos θ, y = y₀ + v₀t sin θ − gt²/2");
}

QString ProjectileModel::statement() const {
    return QStringLiteral(
        "Снаряд вылетает из точки на высоте y₀ со скоростью v₀ под углом θ к "
        "горизонту. Сопротивлением воздуха пренебречь. Найти время полёта, "
        "дальность и максимальную высоту подъёма.");
}

QString ProjectileModel::problemText() const {
    using utils::formatNumber;
    return QStringLiteral(
               "Снаряд вылетает из точки с координатами x₀ = %1 м, y₀ = %2 м со "
               "скоростью v₀ = %3 м/с под углом θ = %4° к горизонту. Сопротивлением "
               "воздуха пренебречь. Найти закон движения снаряда, время полёта до "
               "земли, дальность полёта и наибольшую высоту подъёма.")
        .arg(formatNumber(launchPosition(), 1), formatNumber(launchHeight(), 1), formatNumber(speed(), 1),
             formatNumber(angleDegrees(), 1));
}

// ------------------------------------------------------------------
// Параметры
// ------------------------------------------------------------------

QVector<ParamSpec> ProjectileModel::paramSpecs() const {
    QVector<ParamSpec> specs;

    ParamSpec speed;
    speed.id = QStringLiteral("v0");
    speed.label = QStringLiteral("Начальная скорость v₀");
    speed.unit = QStringLiteral("м/с");
    speed.kind = ParamKind::Slider;
    speed.minValue = 1.0;
    speed.maxValue = 120.0;
    speed.stepValue = 0.5;
    speed.decimals = 1;
    speed.allowZero = false;
    speed.defaultValue = 32.0;
    speed.group = QStringLiteral("Выстрел");
    speed.helpText = QStringLiteral("Модуль скорости в момент вылета");
    specs.push_back(speed);

    ParamSpec angle;
    angle.id = QStringLiteral("angle");
    angle.label = QStringLiteral("Угол выстрела θ");
    angle.unit = QStringLiteral("°");
    angle.kind = ParamKind::Slider;
    angle.minValue = 0.0;
    angle.maxValue = 90.0;
    angle.stepValue = 0.5;
    angle.decimals = 1;
    angle.allowZero = true;
    angle.defaultValue = 45.0;
    angle.group = QStringLiteral("Выстрел");
    angle.helpText = QStringLiteral("Можно менять и перетаскиванием направления выстрела на схеме");
    specs.push_back(angle);

    ParamSpec height;
    height.id = QStringLiteral("y0");
    height.label = QStringLiteral("Высота вылета y₀");
    height.unit = QStringLiteral("м");
    height.kind = ParamKind::Slider;
    height.minValue = 0.0;
    height.maxValue = 80.0;
    height.stepValue = 0.5;
    height.decimals = 1;
    height.allowZero = true;
    height.defaultValue = 12.0;
    height.group = QStringLiteral("Точка выстрела");
    height.helpText = QStringLiteral("Высота над землёй: время полёта считается с её учётом");
    specs.push_back(height);

    ParamSpec position;
    position.id = QStringLiteral("x0");
    position.label = QStringLiteral("Положение выстрела x₀");
    position.unit = QStringLiteral("м");
    position.kind = ParamKind::Slider;
    position.minValue = 0.0;
    position.maxValue = 60.0;
    position.stepValue = 0.5;
    position.decimals = 1;
    position.allowZero = true;
    position.defaultValue = 0.0;
    position.group = QStringLiteral("Точка выстрела");
    position.helpText = QStringLiteral("Смещение точки выстрела вдоль земли");
    specs.push_back(position);

    return specs;
}

ValidationResult ProjectileModel::validate() const {
    ValidationResult check;
    if (speed() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Скорость выстрела должна быть больше нуля");
        check.hint = QStringLiteral("Задайте v₀ не меньше 1 м/с");
        return check;
    }
    if (angleDegrees() >= 90.0 - 1e-9) {
        check.ok = false;
        check.message = QStringLiteral("Выстрел вертикальный: координата x не меняется, параболы не получается");
        check.hint = QStringLiteral("Задайте угол меньше 90°");
        return check;
    }
    // Частный, но важный случай: горизонтальный выстрел (θ = 0) с высоты
    // y₀ > 0 — это тоже парабола, он допустим. Движения нет только тогда,
    // когда и угол, и высота равны нулю.
    if (flightTime() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Снаряд не летит: угол и высота вылета равны нулю");
        check.hint = QStringLiteral("Задайте угол больше 0° или поднимите точку вылета над землёй");
        return check;
    }
    return check;
}

// ------------------------------------------------------------------
// Параметры
// ------------------------------------------------------------------

double ProjectileModel::speed() const { return param(QStringLiteral("v0")); }
double ProjectileModel::angleDegrees() const { return param(QStringLiteral("angle")); }
double ProjectileModel::launchHeight() const { return param(QStringLiteral("y0")); }
double ProjectileModel::launchPosition() const { return param(QStringLiteral("x0")); }
double ProjectileModel::gravity() const { return kGravity; }

double ProjectileModel::angleRadians() const { return angleDegrees() * M_PI / 180.0; }

double ProjectileModel::initialVx() const { return speed() * std::cos(angleRadians()); }
double ProjectileModel::initialVy() const { return speed() * std::sin(angleRadians()); }

// ------------------------------------------------------------------
// Ключевые величины
// ------------------------------------------------------------------

double ProjectileModel::flightTime() const {
    const double vy = initialVy();
    const double y0 = launchHeight();
    if (vy <= 0.0 && y0 <= 0.0)
        return 0.0;
    // y(T) = 0: T = (v₀y + √(v₀y² + 2gy₀)) / g.
    const double discriminant = vy * vy + 2.0 * kGravity * std::max(0.0, y0);
    if (discriminant < 0.0)
        return 0.0;
    return (vy + std::sqrt(discriminant)) / kGravity;
}

double ProjectileModel::range() const { return initialVx() * flightTime(); }

double ProjectileModel::apexTime() const {
    const double vy = initialVy();
    return vy > 0.0 ? vy / kGravity : 0.0;
}

double ProjectileModel::maxHeight() const {
    const double vy = initialVy();
    return launchHeight() + (vy > 0.0 ? vy * vy / (2.0 * kGravity) : 0.0);
}

// ------------------------------------------------------------------
// Движение
// ------------------------------------------------------------------

double ProjectileModel::clampedTime(double t) const {
    const double flight = flightTime();
    if (!std::isfinite(flight))
        return std::max(0.0, t);
    return std::clamp(t, 0.0, flight);
}

double ProjectileModel::xAt(double t) const {
    const double time = clampedTime(t);
    return launchPosition() + initialVx() * time;
}

double ProjectileModel::yAt(double t) const {
    const double time = clampedTime(t);
    return launchHeight() + initialVy() * time - 0.5 * kGravity * time * time;
}

double ProjectileModel::vxAt(double /*t*/) const {
    // Горизонтальная проекция не меняется всё время полёта: на неё не
    // действует ни одна сила (aₓ = 0).
    return initialVx();
}

double ProjectileModel::vyAt(double t) const {
    // Вертикальная проекция убывает линейно: vᵧ = v₀ᵧ − g·t. Время
    // ограничено моментом падения, поэтому «движения под землёй» нет,
    // а в конце полёта показывается скорость в момент удара — именно её
    // ищут в задаче.
    return initialVy() - kGravity * clampedTime(t);
}

bool ProjectileModel::isFlying(double t) const { return t < flightTime() - 1e-12; }

double ProjectileModel::impactVx() const { return initialVx(); }

double ProjectileModel::impactVy() const { return initialVy() - kGravity * flightTime(); }

double ProjectileModel::impactSpeed() const { return std::hypot(impactVx(), impactVy()); }

double ProjectileModel::impactAngleDegrees() const {
    return std::atan2(std::abs(impactVy()), std::abs(impactVx())) * 180.0 / M_PI;
}

double ProjectileModel::speedAt(double t) const { return std::hypot(vxAt(t), vyAt(t)); }

double ProjectileModel::trajectoryY(double x) const {
    const double v0 = speed();
    const double theta = angleRadians();
    const double cosTheta = std::cos(theta);
    if (v0 <= 0.0 || std::abs(cosTheta) < 1e-9)
        return launchHeight();
    const double dx = x - launchPosition();
    const double tangent = std::tan(theta);
    return launchHeight() + dx * tangent - kGravity * dx * dx / (2.0 * v0 * v0 * cosTheta * cosTheta);
}

// ------------------------------------------------------------------
// Время наблюдения
// ------------------------------------------------------------------

double ProjectileModel::duration() const {
    const double flight = flightTime();
    if (!std::isfinite(flight) || flight <= 0.0)
        return 5.0;
    return flight;
}

double ProjectileModel::timePerSecond() const { return std::clamp(duration() / 5.0, 0.05, 8.0); }

double ProjectileModel::stepSize() const { return duration() / 120.0; }

// ------------------------------------------------------------------
// Состояние и событие
// ------------------------------------------------------------------

SimState ProjectileModel::stateAt(double t) const {
    SimState state;
    state.time = t;
    if (!validate().ok)
        return state;

    const double flight = flightTime();
    const bool landed = t >= flight - 1e-9;
    // Скорость покоящегося снаряда равна нулю: анимация после падения
    // не должна показывать движение.
    const double currentSpeed = landed ? 0.0 : speedAt(t);

    EntityVisual projectile;
    projectile.id = QStringLiteral("shell");
    projectile.label = QStringLiteral("Снаряд");
    projectile.position = xAt(t);
    projectile.value1 = yAt(t);
    projectile.value2 = currentSpeed;
    projectile.active = !landed;
    state.entities.push_back(projectile);

    // Проекцы движения на оси — отдельные объекты схемы.
    EntityVisual xProjection;
    xProjection.id = QStringLiteral("proj_x");
    xProjection.label = QStringLiteral("Проекция на ось X");
    xProjection.position = xAt(t);
    xProjection.value1 = vxAt(t);
    state.entities.push_back(xProjection);

    EntityVisual yProjection;
    yProjection.id = QStringLiteral("proj_y");
    yProjection.label = QStringLiteral("Проекция на ось Y");
    yProjection.position = yAt(t);
    yProjection.value1 = vyAt(t);
    state.entities.push_back(yProjection);

    state.primary = xAt(t);
    state.secondary = yAt(t);
    state.resolved = landed;
    state.eventLabel = landed ? QStringLiteral("Снаряд упал в точку x = %1 м").arg(utils::formatNumber(xAt(t), 1))
                              : QString();
    return state;
}

double ProjectileModel::eventTime() const {
    const double flight = flightTime();
    return flight > 0.0 ? flight : std::numeric_limits<double>::quiet_NaN();
}

// ------------------------------------------------------------------
// Ряды данных для графиков и трассы
// ------------------------------------------------------------------

QVector<QPointF> ProjectileModel::trajectoryPoints() const {
    QVector<QPointF> points;
    const double flight = flightTime();
    if (!std::isfinite(flight) || flight <= 0.0)
        return points;
    const int steps = 240;
    points.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const double t = flight * i / steps;
        points.append(QPointF(xAt(t), yAt(t)));
    }
    return points;
}

QVector<QPointF> ProjectileModel::xSeries() const {
    QVector<QPointF> points;
    const double flight = flightTime();
    if (!std::isfinite(flight) || flight <= 0.0)
        return points;
    const int steps = 240;
    points.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const double t = flight * i / steps;
        points.append(QPointF(t, xAt(t)));
    }
    return points;
}

QVector<QPointF> ProjectileModel::ySeries() const {
    QVector<QPointF> points;
    const double flight = flightTime();
    if (!std::isfinite(flight) || flight <= 0.0)
        return points;
    const int steps = 240;
    points.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const double t = flight * i / steps;
        points.append(QPointF(t, yAt(t)));
    }
    return points;
}

QVector<QPointF> ProjectileModel::vxSeries() const {
    QVector<QPointF> points;
    const double flight = flightTime();
    if (!std::isfinite(flight) || flight <= 0.0)
        return points;
    const int steps = 120;
    points.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const double t = flight * i / steps;
        points.append(QPointF(t, vxAt(t)));
    }
    return points;
}

QVector<QPointF> ProjectileModel::vySeries() const {
    QVector<QPointF> points;
    const double flight = flightTime();
    if (!std::isfinite(flight) || flight <= 0.0)
        return points;
    const int steps = 240;
    points.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const double t = flight * i / steps;
        points.append(QPointF(t, vyAt(t)));
    }
    return points;
}

// ------------------------------------------------------------------
// Панель вычислений
// ------------------------------------------------------------------

Derivation ProjectileModel::derivation() const {
    using utils::formatNumber;
    Derivation d;
    const ValidationResult check = validate();
    d.valid = check.ok;
    d.problemText = problemText();
    d.unknown = QStringLiteral("x(t), y(t), время полёта T, дальность L и наибольшая высота H");

    if (!check.ok) {
        d.steps.push_back({QStringLiteral("Внимание"), QStringLiteral("\\text{%1}").arg(check.message)});
        d.steps.push_back({QStringLiteral("Что сделать"), QStringLiteral("\\text{%1}").arg(check.hint)});
        d.answerTemplate = QStringLiteral("Расчёт невозможен: %1").arg(check.message);
        return d;
    }

    const double v0 = speed();
    const double theta = angleDegrees();
    const double y0 = launchHeight();
    const double x0 = launchPosition();
    const double vx = initialVx();
    const double vy = initialVy();
    const double t = time();
    const double flight = flightTime();
    const double apex = apexTime();
    const double height = maxHeight();
    const double length = range();

    QVector<DerivationStep>& st = d.steps;
    st.push_back({QStringLiteral("1. Исходные данные"),
                  QStringLiteral("v_{0} = %1 \\text{ м/с},\\quad \\theta = %2^{\\circ},\\quad x_{0} = %3 "
                                 "\\text{ м},\\quad y_{0} = %4 \\text{ м},\\quad g = %5 \\text{ м/с}^{2}")
                      .arg(formatNumber(v0, 1), formatNumber(theta, 1), formatNumber(x0, 1), formatNumber(y0, 1),
                           formatNumber(kGravity, 1))});

    st.push_back({QStringLiteral("2. Проекции начальной скорости"),
                  QStringLiteral("v_{0x} = v_{0} \\cos\\theta = %1 \\cdot %2 = %3 \\text{ м/с},\\qquad v_{0y} = v_{0} "
                                 "\\sin\\theta = %1 \\cdot %4 = %5 \\text{ м/с}")
                      .arg(formatNumber(v0, 1), formatNumber(std::cos(angleRadians()), 4), formatNumber(vx, 3),
                           formatNumber(std::sin(angleRadians()), 4), formatNumber(vy, 3))});

    st.push_back({QStringLiteral("3. Закон движения по проекциям"),
                  QStringLiteral("x(t) = x_{0} + v_{0} \\cos\\theta \\cdot t,\\qquad y(t) = y_{0} + v_{0} "
                                 "\\sin\\theta \\cdot t - \\frac{g t^{2}}{2}")});
    st.push_back({QStringLiteral("Скорости по проекциям"),
                  QStringLiteral("v_{x}(t) = v_{0} \\cos\\theta,\\qquad v_{y}(t) = v_{0} \\sin\\theta - g t")});
    st.push_back({QStringLiteral("Ускорение"),
                  QStringLiteral("a_{x} = 0,\\qquad a_{y} = -g = -%1 \\text{ м/с}^{2}")
                      .arg(formatNumber(kGravity, 1))});

    st.push_back({QStringLiteral("4. Подстановка t = %1 с").arg(formatNumber(t, 2)),
                  QStringLiteral("x = %1 + %2 \\cdot %3 = %4 \\text{ м},\\qquad y = %5 + %6 \\cdot %3 - "
                                 "\\frac{%7 \\cdot %3^{2}}{2} = %8 \\text{ м}")
                      .arg(formatNumber(x0, 1), formatNumber(vx, 3), formatNumber(t, 2), formatNumber(xAt(t), 3),
                           formatNumber(y0, 1), formatNumber(vy, 3), formatNumber(kGravity, 1),
                           formatNumber(std::max(0.0, yAt(t)), 3))});
    st.push_back({QStringLiteral("Скорость в этот момент"),
                  QStringLiteral("v_{x} = %1 \\text{ м/с},\\qquad v_{y} = %2 - %3 \\cdot %4 = %5 \\text{ м/с}")
                      .arg(formatNumber(vx, 3), formatNumber(vy, 3), formatNumber(kGravity, 1), formatNumber(t, 2),
                           formatNumber(vyAt(t), 3))});

    st.push_back({QStringLiteral("5. Момент падения (y = 0)"),
                  QStringLiteral("y_{0} + v_{0y} T - \\frac{g T^{2}}{2} = 0 \\quad \\Rightarrow \\quad T = "
                                 "\\frac{v_{0y} + \\sqrt{v_{0y}^{2} + 2 g y_{0}}}{g}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("T = \\frac{%1 + \\sqrt{%1^{2} + 2 \\cdot %2 \\cdot %3}}{%2} = %4 \\text{ с}")
                      .arg(formatNumber(vy, 3), formatNumber(kGravity, 1), formatNumber(y0, 1), formatNumber(flight, 3))});

    st.push_back({QStringLiteral("6. Дальность полёта"),
                  QStringLiteral("L = v_{0x} T = %1 \\cdot %2 = %3 \\text{ м}")
                      .arg(formatNumber(vx, 3), formatNumber(flight, 3), formatNumber(length, 2))});

    st.push_back({QStringLiteral("7. Наибольшая высота"),
                  QStringLiteral("v_{y} = 0 \\Rightarrow t_{h} = \\frac{v_{0y}}{g} = %1 \\text{ с},\\qquad H = y_{0} + "
                                 "\\frac{v_{0y}^{2}}{2g} = %2 \\text{ м}")
                      .arg(formatNumber(apex, 3), formatNumber(height, 2))});

    st.push_back({QStringLiteral("8. Скорость в момент падения"),
                  QStringLiteral("v_{x} = %1 \\text{ м/с},\\qquad v_{y} = v_{0y} - gT = %2 - %3 \\cdot %4 = %5 "
                                 "\\text{ м/с},\\qquad v = \\sqrt{{v_{x}^{2} + v_{y}^{2}}} = %6 \\text{ м/с}")
                      .arg(formatNumber(impactVx(), 3), formatNumber(vy, 3), formatNumber(kGravity, 1),
                           formatNumber(flight, 3), formatNumber(impactVy(), 3), formatNumber(impactSpeed(), 3))});
    st.push_back({QStringLiteral("Угол траектории к горизонту при падении"),
                  QStringLiteral("\\tan\\beta = \\frac{{|v_{y}|}}{{v_{x}}} \\quad \\Rightarrow \\quad \\beta "
                                 "= %1^{\\circ}")
                      .arg(formatNumber(impactAngleDegrees(), 1))});

    st.push_back({QStringLiteral("9. Уравнение траектории"),
                  QStringLiteral("y(x) = y_{0} + (x - x_{0}) \\tan\\theta - \\frac{g (x - x_{0})^{2}}{2 v_{0}^{2} "
                                 "\\cos^{2}\\theta}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("y(x) = %1 + %2 \u00b7 (x - %3) - %4 \u00b7 (x - %3)^{2}")
                      .arg(formatNumber(y0, 1), formatNumber(std::tan(angleRadians()), 4), formatNumber(x0, 1),
                           formatNumber(kGravity / (2.0 * v0 * v0 * std::cos(angleRadians()) * std::cos(angleRadians())), 5))});

    d.answerTemplate = QStringLiteral("T = %1 с; L = %2 м; H = %3 м; x(t) = %4 + %5 t; y(t) = %6 + %7 t − %8 t²")
                           .arg(formatNumber(flight, 3), formatNumber(length, 2), formatNumber(height, 2),
                                formatNumber(x0, 1), formatNumber(vx, 3), formatNumber(y0, 1), formatNumber(vy, 3),
                                formatNumber(kGravity / 2.0, 2));
    return d;
}

QVector<Readout> ProjectileModel::readouts() const {
    QVector<Readout> list;
    if (!validate().ok)
        return list;

    const double t = time();
    list.push_back({QStringLiteral("Координата x"), QStringLiteral("%1 м").arg(utils::formatNumber(xAt(t), 2))});
    list.push_back({QStringLiteral("Координата y"), QStringLiteral("%1 м").arg(utils::formatNumber(std::max(0.0, yAt(t)), 2))});
    list.push_back({QStringLiteral("Скорость vₓ"), QStringLiteral("%1 м/с").arg(utils::formatNumber(vxAt(t), 3))});
    list.push_back({QStringLiteral("Скорость vᵧ"), QStringLiteral("%1 м/с").arg(utils::formatNumber(vyAt(t), 3))});
    list.push_back({QStringLiteral("Время полёта T"), QStringLiteral("%1 с").arg(utils::formatNumber(flightTime(), 3))});
    list.push_back({QStringLiteral("Дальность L"), QStringLiteral("%1 м").arg(utils::formatNumber(range(), 2))});
    list.push_back({QStringLiteral("Наибольшая высота H"), QStringLiteral("%1 м").arg(utils::formatNumber(maxHeight(), 2))});
    list.push_back({QStringLiteral("Скорость при падении v"),
                    QStringLiteral("%1 м/с под β = %2° к горизонту")
                        .arg(utils::formatNumber(impactSpeed(), 2), utils::formatNumber(impactAngleDegrees(), 1))});
    list.push_back({QStringLiteral("Состояние"),
                    t >= flightTime() - 1e-9
                        ? QStringLiteral("падение: значения приведены для момента удара")
                        : QStringLiteral("в полёте"),
                    t >= flightTime() - 1e-9});
    return list;
}

// ------------------------------------------------------------------
// Точки взаимодействия
// ------------------------------------------------------------------

QVector<HandleInfo> ProjectileModel::handles() const {
    QVector<HandleInfo> list;

    HandleInfo angle;
    angle.id = QStringLiteral("angle");
    angle.label = QStringLiteral("Направление выстрела");
    angle.value = angleDegrees();
    angle.minValue = 0.0;
    angle.maxValue = 90.0;
    angle.step = 0.5;
    angle.unit = QStringLiteral("°");
    angle.axis = QStringLiteral("y");
    angle.readbackLabel = QStringLiteral("Проекции скорости");
    angle.readbackValue = QStringLiteral("vₓ = %1, vᵧ = %2 м/с").arg(utils::formatNumber(initialVx(), 1),
                                                                     utils::formatNumber(initialVy(), 1));
    list.push_back(angle);

    HandleInfo height;
    height.id = QStringLiteral("y0");
    height.label = QStringLiteral("Высота вылета");
    height.value = launchHeight();
    height.minValue = 0.0;
    height.maxValue = 80.0;
    height.step = 0.5;
    height.unit = QStringLiteral("м");
    height.axis = QStringLiteral("y");
    height.readbackLabel = QStringLiteral("Время полёта");
    height.readbackValue = QStringLiteral("%1 с").arg(utils::formatNumber(flightTime(), 2));
    list.push_back(height);

    HandleInfo position;
    position.id = QStringLiteral("x0");
    position.label = QStringLiteral("Положение выстрела");
    position.value = launchPosition();
    position.minValue = 0.0;
    position.maxValue = 60.0;
    position.step = 0.5;
    position.unit = QStringLiteral("м");
    position.axis = QStringLiteral("x");
    position.readbackLabel = QStringLiteral("Точка падения");
    position.readbackValue = QStringLiteral("x = %1 м").arg(utils::formatNumber(launchPosition() + range(), 1));
    list.push_back(position);

    return list;
}

void ProjectileModel::setHandleValue(const QString& id, double value) {
    if (id == QLatin1String("angle") || id == QLatin1String("y0") || id == QLatin1String("x0")) {
        setParam(id, value);
        return;
    }
    Simulation::setHandleValue(id, value);
}

void ProjectileModel::beginInteraction() {
    Simulation::beginInteraction();
    setStateTime(0.0);
}

}  // namespace lab::models
