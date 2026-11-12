#include "models/HorizonModel.h"

#include <algorithm>
#include <cmath>

#include "utils/MathText.h"

namespace lab::models {
namespace {

/** Перевод километров в метры (радиусы планет удобнее задавать в километрах). */
constexpr double kMetersPerKilometer = 1000.0;
/** Допуск, после которого подъём считается завершённым. */
constexpr double kAscentTolerance = 1e-9;

/** Разумное количество знаков для больших расстояний (км). */
int decimalsForKilometers(double valueInKilometers) {
    if (valueInKilometers >= 1000.0)
        return 1;
    if (valueInKilometers >= 100.0)
        return 2;
    if (valueInKilometers >= 1.0)
        return 3;
    return 5;
}

}  // namespace

HorizonModel::HorizonModel(QObject* parent) : Simulation(parent) { seedDefaults(); }

QString HorizonModel::title() const { return QStringLiteral("Дальность видимого горизонта"); }

QString HorizonModel::subtitle() const {
    return QStringLiteral("Касательная к поверхности: d = √(2Rh + h²), дуга s = R·arccos(R/(R+h))");
}

QString HorizonModel::statement() const {
    return QStringLiteral(
        "Наблюдатель поднимается на высоту h над поверхностью планеты радиусом R. Линия взгляда "
        "до самой дальней точки поверхности касается планеты, то есть перпендикулярна радиусу "
        "в точке касания. Найти расстояние до горизонта по прямой, расстояние вдоль поверхности "
        "и долю поверхности, которую видно с этой высоты.");
}

QString HorizonModel::problemText() const {
    return QStringLiteral(
               "Наблюдатель поднялся на высоту %1 м над поверхностью планеты радиусом %2 км. На каком "
               "расстоянии от него находится линия горизонта по прямой и вдоль поверхности? Какую часть "
               "поверхности планеты он видит? Наблюдатель поднимается равномерно со скоростью %3 м/с.")
        .arg(utils::formatNumber(maxHeight(), 0), utils::formatNumber(radiusKilometers(), 0),
             utils::formatNumber(ascentSpeed(), 1));
}

// ------------------------------------------------------------------
// Параметры: масштаб планеты и профиль подъёма
// ------------------------------------------------------------------

QVector<ParamSpec> HorizonModel::paramSpecs() const {
    QVector<ParamSpec> specs;

    ParamSpec radius;
    radius.id = QStringLiteral("radius");
    radius.label = QStringLiteral("Радиус планеты R");
    radius.unit = QStringLiteral("км");
    radius.kind = ParamKind::Slider;
    radius.minValue = 100.0;
    radius.maxValue = 605100.0;
    radius.stepValue = 1.0;
    radius.decimals = 0;
    radius.allowZero = false;
    radius.defaultValue = 6371.0;
    radius.group = QStringLiteral("Планета");
    radius.helpText = QStringLiteral("Земля — 6371 км, Луна — 1737 км, Марс — 3390 км, Солнце — 696000 км");
    specs.push_back(radius);

    ParamSpec height;
    height.id = QStringLiteral("h");
    height.label = QStringLiteral("Высота подъёма h");
    height.unit = QStringLiteral("м");
    height.kind = ParamKind::Slider;
    height.minValue = 1.0;
    height.maxValue = 120000.0;
    height.stepValue = 1.0;
    height.decimals = 0;
    height.allowZero = false;
    height.defaultValue = 2000.0;
    height.group = QStringLiteral("Подъём");
    height.helpText = QStringLiteral("Рост человека — 1,8 м, гора — километры, самолёт — около 10 км");
    specs.push_back(height);

    ParamSpec speed;
    speed.id = QStringLiteral("v");
    speed.label = QStringLiteral("Скорость подъёма v");
    speed.unit = QStringLiteral("м/с");
    speed.kind = ParamKind::Slider;
    speed.minValue = 0.5;
    speed.maxValue = 3000.0;
    speed.stepValue = 0.5;
    speed.decimals = 1;
    speed.allowZero = false;
    speed.defaultValue = 25.0;
    speed.group = QStringLiteral("Подъём");
    speed.helpText = QStringLiteral("Шар-зонд, подъёмная кабина, самолёт — скорость равномерного набора высоты");
    specs.push_back(speed);

    return specs;
}

ValidationResult HorizonModel::validate() const {
    ValidationResult check;
    if (radiusMeters() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Радиус планеты должен быть больше нуля");
        check.hint = QStringLiteral("Задайте радиус в километрах: для Земли это 6371");
        return check;
    }
    if (maxHeight() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Высота наблюдателя равна нулю: он стоит на поверхности, горизонт в нуле");
        check.hint = QStringLiteral("Задайте высоту хотя бы 1 м");
        return check;
    }
    if (maxHeight() >= radiusMeters()) {
        check.ok = false;
        check.message = QStringLiteral("Высота сопоставима с радиусом планеты: наблюдения у поверхности больше нет");
        check.hint = QStringLiteral("Уменьшите высоту или возьмите планету с бо́льшим радиусом");
        return check;
    }
    if (ascentSpeed() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Скорость подъёма должна быть больше нуля");
        check.hint = QStringLiteral("Задайте скорость не меньше 0,5 м/с");
        return check;
    }
    return check;
}

double HorizonModel::radiusKilometers() const { return param(QStringLiteral("radius")); }

double HorizonModel::radiusMeters() const { return radiusKilometers() * kMetersPerKilometer; }

double HorizonModel::maxHeight() const { return param(QStringLiteral("h")); }

double HorizonModel::ascentSpeed() const { return param(QStringLiteral("v")); }

// ------------------------------------------------------------------
// Геометрия: касательная к окружности
// ------------------------------------------------------------------

double HorizonModel::centralAngle(double height) const {
    const double radius = radiusMeters();
    if (radius <= 0.0)
        return 0.0;
    // cos α = прилежащий катет / гипотенуза = OB/OA = R/(R + h).
    const double ratio = radius / (radius + std::max(0.0, height));
    return std::acos(std::clamp(ratio, -1.0, 1.0));
}

double HorizonModel::horizonDistance(double height) const {
    const double radius = radiusMeters();
    const double lift = std::max(0.0, height);
    // d² = OA² − OB² = (R + h)² − R² = 2Rh + h² — теорема Пифагора для OAB.
    return std::sqrt(std::max(0.0, 2.0 * radius * lift + lift * lift));
}

double HorizonModel::dipAngleDegrees(double height) const {
    // В прямоугольном треугольнике OAB острые углы в сумме дают 90°, поэтому
    // линия взгляда опущена за горизонт ровно на центральный угол α.
    return centralAngle(height) * 180.0 / M_PI;
}

double HorizonModel::arcDistance(double height) const {
    // Длина дуги окружности радиусом R на центральный угол α (в радианах).
    return radiusMeters() * centralAngle(height);
}

double HorizonModel::visibleArea(double height) const {
    const double radius = radiusMeters();
    // Площадь сферического сегмента: S = 2πR²(1 − cos α).
    return 2.0 * M_PI * radius * radius * (1.0 - std::cos(centralAngle(height)));
}

double HorizonModel::visibleFraction(double height) const {
    const double radius = radiusMeters();
    if (radius <= 0.0)
        return 0.0;
    // S / (4πR²) = (1 − cos α)/2 = h / (2(R + h)) — после сокращения R.
    return 0.5 * (1.0 - radius / (radius + std::max(0.0, height)));
}

double HorizonModel::approximateDistance(double height) const {
    // Точная формула √(2Rh + h²) при h ≪ R переходит в √(2Rh): слагаемым h² пренебрегают.
    return std::sqrt(2.0 * radiusMeters() * std::max(0.0, height));
}

// ------------------------------------------------------------------
// Равномерный подъём: h(t) = v·t, но не выше заданной отметки
// ------------------------------------------------------------------

double HorizonModel::ascentTime() const {
    const double speed = ascentSpeed();
    return speed > 0.0 ? maxHeight() / speed : 0.0;
}

double HorizonModel::duration() const { return std::max(0.5, ascentTime()); }

double HorizonModel::timePerSecond() const {
    // Подъём занимает и минуты, и часы модельного времени: темп показа
    // подбирается так, чтобы наблюдение длилось несколько секунд.
    return std::clamp(duration() / 7.0, 1.0, 900.0);
}

double HorizonModel::stepSize() const { return duration() / 400.0; }

double HorizonModel::heightAt(double t) const {
    return std::clamp(ascentSpeed() * std::max(0.0, t), 0.0, maxHeight());
}

double HorizonModel::verticalVelocityAt(double t) const {
    // Подъём завершён — скорость равна нулю, иначе схема показывала бы
    // бесконечный рост высоты.
    return t < ascentTime() - 1e-12 ? ascentSpeed() : 0.0;
}

double HorizonModel::horizonDistanceAt(double t) const { return horizonDistance(heightAt(t)); }

double HorizonModel::arcDistanceAt(double t) const { return arcDistance(heightAt(t)); }

double HorizonModel::dipAngleAt(double t) const { return dipAngleDegrees(heightAt(t)); }

double HorizonModel::eventTime() const {
    const double ascent = ascentTime();
    return ascent > 0.0 ? ascent : std::numeric_limits<double>::quiet_NaN();
}

// ------------------------------------------------------------------
// Состояние системы в момент t
// ------------------------------------------------------------------

SimState HorizonModel::stateAt(double t) const {
    SimState state;
    const double height = heightAt(t);
    const double distance = horizonDistance(height);
    const double arc = arcDistance(height);
    const bool arrived = t >= ascentTime() - kAscentTolerance;

    EntityVisual observer;
    observer.id = QStringLiteral("observer");
    observer.label = QStringLiteral("Наблюдатель");
    observer.position = height;                  // высота над поверхностью, м
    observer.value1 = distance;                  // расстояние до горизонта, м
    observer.value2 = verticalVelocityAt(t);     // скорость подъёма, м/с
    observer.active = !arrived;
    observer.variant = 0;
    state.entities.push_back(observer);

    EntityVisual planet;
    planet.id = QStringLiteral("planet");
    planet.label = QStringLiteral("Планета");
    planet.position = radiusKilometers();        // радиус, км
    planet.value1 = arc;                         // расстояние по дуге, м
    planet.value2 = dipAngleDegrees(height);     // угол понижения горизонта, °
    planet.active = true;
    planet.variant = 2;
    state.entities.push_back(planet);

    state.primary = distance;                    // расстояние по прямой
    state.secondary = arc;                       // расстояние вдоль поверхности
    state.resolved = arrived;
    state.eventLabel = arrived ? QStringLiteral("Подъём завершён: h = %1 м, горизонт в %2 км")
                                     .arg(utils::formatNumber(height, 0),
                                          utils::formatNumber(distance / kMetersPerKilometer, 2))
                               : QString();
    return state;
}

// ------------------------------------------------------------------
// Решение: от теоремы Пифагора к дальности горизонта
// ------------------------------------------------------------------

Derivation HorizonModel::derivation() const {
    using utils::formatNumber;
    Derivation d;
    const ValidationResult check = validate();
    d.valid = check.ok;
    d.problemText = problemText();
    d.unknown = QStringLiteral("d — расстояние по прямой, s — расстояние по поверхности, видимая доля поверхности");

    if (!check.ok) {
        d.steps.push_back({QStringLiteral("Внимание"), QStringLiteral("\\text{%1}").arg(check.message)});
        d.steps.push_back({QStringLiteral("Что сделать"), QStringLiteral("\\text{%1}").arg(check.hint)});
        d.answerTemplate = QStringLiteral("Расчёт невозможен: %1").arg(check.message);
        return d;
    }

    const double radius = radiusMeters();
    const double height = maxHeight();
    const double alpha = centralAngle(height);
    const double distance = horizonDistance(height);
    const double arc = arcDistance(height);
    const double approx = approximateDistance(height);
    const double t = time();

    QVector<DerivationStep>& st = d.steps;
    st.push_back({QStringLiteral("1. Исходные данные"),
                  QStringLiteral("R = %1 \\text{ км} = %2 \\text{ м},\\qquad h = %3 \\text{ м},\\qquad v = %4 "
                                 "\\text{ м/с}")
                      .arg(formatNumber(radiusKilometers(), 0), formatNumber(radius, 0), formatNumber(height, 0),
                           formatNumber(ascentSpeed(), 1))});

    st.push_back({QStringLiteral("2. Геометрия задачи"),
                  QStringLiteral("O — центр планеты, A — наблюдатель, B — точка горизонта\n"
                                 "OB \\perp AB \\text{ (луч взгляда касается поверхности)},\\quad OB = R,\\quad OA = R + h")});

    st.push_back({QStringLiteral("3. Теорема Пифагора для треугольника OAB"),
                  QStringLiteral("OA^{2} = OB^{2} + AB^{2} \\quad \\Rightarrow \\quad AB^{2} = (R + h)^{2} - R^{2} = 2Rh + "
                                 "h^{2}")});
    st.push_back({QStringLiteral("Расстояние до горизонта по прямой"),
                  QStringLiteral("d = \\sqrt{2Rh + h^{2}}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("d = \\sqrt{2 \\cdot %1 \\cdot %2 + %3^{2}} = %4 \\text{ м} = %5 \\text{ км}")
                      .arg(formatNumber(radius, 0), formatNumber(height, 0), formatNumber(height, 0),
                           formatNumber(distance, 0), formatNumber(distance / kMetersPerKilometer, 3))});

    st.push_back({QStringLiteral("4. Приближённая формула (h ≪ R)"),
                  QStringLiteral("h^{2} \\ll 2Rh,\\quad d \\approx \\sqrt{2Rh} = %1 \\text{ км},\\quad "
                                 "\\text{погрешность} = %2 \\%")
                      .arg(formatNumber(approx / kMetersPerKilometer, 3),
                           formatNumber(100.0 * (distance - approx) / distance, 4))});

    st.push_back({QStringLiteral("5. Центральный угол до горизонта"),
                  QStringLiteral("\\cos\\alpha = \\frac{OB}{OA} = \\frac{R}{R + h} \\quad \\Rightarrow \\quad \\alpha = "
                                 "\\arccos \\frac{R}{R + h}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("\\cos\\alpha = \\frac{%1}{%1 + %2} = %3, \\quad \\alpha = %4 \\text{ рад} = %5^{\\circ}")
                      .arg(formatNumber(radius, 0), formatNumber(height, 0), formatNumber(radius / (radius + height), 8),
                           formatNumber(alpha, 6), formatNumber(alpha * 180.0 / M_PI, 4))});

    st.push_back({QStringLiteral("6. Расстояние вдоль поверхности (дуга)"),
                  QStringLiteral("s = R\\alpha = R \\arccos \\frac{R}{R + h}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("s = %1 \\cdot %2 = %3 \\text{ м} = %4 \\text{ км}")
                      .arg(formatNumber(radius, 0), formatNumber(alpha, 6), formatNumber(arc, 0),
                           formatNumber(arc / kMetersPerKilometer, 3))});
    st.push_back({QStringLiteral("Почему отрезок взгляда длиннее дуги"),
                  QStringLiteral("d = R\\tan\\alpha > R\\alpha = s \\;\\text{(при } 0 < \\alpha < 90^{\\circ}\\text{)},\\quad d - s = %1 "
                                 "\\text{ м} \\left( %2 \\% \\right):\n"
                                 "\\text{касательная отходит от поверхности, поэтому путь по поверхности короче}")
                      .arg(formatNumber(distance - arc, 1), formatNumber(100.0 * (distance - arc) / arc, 3))});

    st.push_back({QStringLiteral("7. Угол понижения горизонта"),
                  QStringLiteral("\\text{В прямоугольном треугольнике } \\alpha + \\angle OAB = 90^{\\circ},"
                                 "\\quad \\beta = \\alpha = %1^{\\circ}")
                      .arg(formatNumber(alpha * 180.0 / M_PI, 4))});

    st.push_back({QStringLiteral("8. Видимая часть поверхности"),
                  QStringLiteral("S = 2\\pi R^{2}(1 - \\cos\\alpha), \\qquad \\frac{S}{4\\pi R^{2}} = \\frac{1 - "
                                 "\\cos\\alpha}{2} = \\frac{h}{2(R + h)}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("\\frac{S}{4\\pi R^{2}} = \\frac{%1}{2 \\cdot %2} = %3, \\quad S = %4 \\text{ км}^{2}")
                      .arg(formatNumber(height, 0), formatNumber(radius + height, 0),
                           formatNumber(visibleFraction(height), 8),
                           formatNumber(visibleArea(height) / (kMetersPerKilometer * kMetersPerKilometer), 0))});

    st.push_back({QStringLiteral("9. Движение: как растёт дальность горизонта"),
                  QStringLiteral("h(t) = vt \\text{ при } t \\le T, \\qquad T = \\frac{h}{v} = \\frac{%1}{%2} = %3 "
                                 "\\text{ с}\nd(t) = \\sqrt{2R \\cdot h(t) + h(t)^{2}} \\quad \\text{— зависимость "
                                 "нелинейная: } d \\sim \\sqrt{h}")
                      .arg(formatNumber(height, 0), formatNumber(ascentSpeed(), 1), formatNumber(ascentTime(), 2))});
    st.push_back({QStringLiteral("Подстановка t = %1 с").arg(formatNumber(t, 2)),
                  QStringLiteral("h(%1) = %2 \\cdot %1 = %3 \\text{ м}, \\quad d = \\sqrt{2 \\cdot %4 \\cdot %3 + %3^{2}} = "
                                 "%5 \\text{ км}")
                      .arg(formatNumber(t, 2), formatNumber(ascentSpeed(), 1), formatNumber(heightAt(t), 1),
                           formatNumber(radius, 0),
                           formatNumber(horizonDistanceAt(t) / kMetersPerKilometer, 3))});

    d.answerTemplate =
        QStringLiteral("d = %1 км по прямой; s = %2 км вдоль поверхности; видно %3 \\% поверхности; подъём длится T = %4 с")
            .arg(formatNumber(distance / kMetersPerKilometer, 3), formatNumber(arc / kMetersPerKilometer, 3),
                 formatNumber(100.0 * visibleFraction(height), 5), formatNumber(ascentTime(), 2));
    return d;
}

QVector<Readout> HorizonModel::readouts() const {
    QVector<Readout> list;
    if (!validate().ok)
        return list;

    const double t = time();
    const double height = heightAt(t);
    const double distance = horizonDistance(height);
    const double arc = arcDistance(height);

    list.push_back({QStringLiteral("Время наблюдения"), QStringLiteral("%1 с").arg(utils::formatNumber(t, 2))});
    list.push_back({QStringLiteral("Высота наблюдателя h"), QStringLiteral("%1 м").arg(utils::formatNumber(height, 1))});
    list.push_back({QStringLiteral("Расстояние до горизонта d"),
                    QStringLiteral("%1 км (%2 м)")
                        .arg(utils::formatNumber(distance / kMetersPerKilometer, decimalsForKilometers(distance / kMetersPerKilometer)),
                             utils::formatNumber(distance, 0)),
                    true});
    list.push_back({QStringLiteral("Расстояние по поверхности s"),
                    QStringLiteral("%1 км").arg(utils::formatNumber(arc / kMetersPerKilometer, 3))});
    list.push_back({QStringLiteral("Угол понижения горизонта"),
                    QStringLiteral("%1°").arg(utils::formatNumber(dipAngleDegrees(height), 4))});
    list.push_back({QStringLiteral("Видимая доля поверхности"),
                    QStringLiteral("%1 %").arg(utils::formatNumber(100.0 * visibleFraction(height), 5))});
    list.push_back({QStringLiteral("Скорость подъёма"), QStringLiteral("%1 м/с").arg(utils::formatNumber(verticalVelocityAt(t), 2))});
    list.push_back({QStringLiteral("Время подъёма T"), QStringLiteral("%1 с").arg(utils::formatNumber(ascentTime(), 2))});
    list.push_back({QStringLiteral("Состояние"),
                    t >= ascentTime() - kAscentTolerance ? QStringLiteral("подъём завершён: дальность больше не растёт")
                                                         : QStringLiteral("набор высоты"),
                    t >= ascentTime() - kAscentTolerance});
    return list;
}

// ------------------------------------------------------------------
// Ряды данных для графиков
// ------------------------------------------------------------------

QVector<QPointF> HorizonModel::heightSeries() const {
    QVector<QPointF> points;
    const double total = duration();
    for (int i = 0; i <= 240; ++i) {
        const double t = total * i / 240.0;
        points.append(QPointF(t, heightAt(t)));
    }
    return points;
}

QVector<QPointF> HorizonModel::distanceSeries() const {
    QVector<QPointF> points;
    const double total = duration();
    for (int i = 0; i <= 240; ++i) {
        const double t = total * i / 240.0;
        points.append(QPointF(t, horizonDistanceAt(t) / kMetersPerKilometer));
    }
    return points;
}

QVector<QPointF> HorizonModel::arcSeries() const {
    QVector<QPointF> points;
    const double total = duration();
    for (int i = 0; i <= 240; ++i) {
        const double t = total * i / 240.0;
        points.append(QPointF(t, arcDistanceAt(t) / kMetersPerKilometer));
    }
    return points;
}

QVector<QPointF> HorizonModel::distanceVersusHeight() const {
    QVector<QPointF> points;
    const double top = std::max(1e-6, maxHeight());
    for (int i = 0; i <= 240; ++i) {
        const double height = top * i / 240.0;
        points.append(QPointF(height, horizonDistance(height) / kMetersPerKilometer));
    }
    return points;
}

QVector<QPointF> HorizonModel::arcVersusHeight() const {
    QVector<QPointF> points;
    const double top = std::max(1e-6, maxHeight());
    for (int i = 0; i <= 240; ++i) {
        const double height = top * i / 240.0;
        points.append(QPointF(height, arcDistance(height) / kMetersPerKilometer));
    }
    return points;
}

// ------------------------------------------------------------------
// Точки взаимодействия
// ------------------------------------------------------------------

QVector<HandleInfo> HorizonModel::handles() const {
    QVector<HandleInfo> list;

    HandleInfo lift;
    lift.id = QStringLiteral("height");
    lift.label = QStringLiteral("Высота наблюдателя");
    lift.value = maxHeight();
    lift.minValue = 1.0;
    lift.maxValue = 120000.0;
    lift.step = 1.0;
    lift.unit = QStringLiteral("м");
    lift.axis = QStringLiteral("y");
    lift.readbackLabel = QStringLiteral("Горизонт");
    lift.readbackValue = QStringLiteral("%1 км").arg(utils::formatNumber(horizonDistance(maxHeight()) / kMetersPerKilometer, 2));
    list.push_back(lift);

    HandleInfo size;
    size.id = QStringLiteral("radius");
    size.label = QStringLiteral("Радиус планеты");
    size.value = radiusKilometers();
    size.minValue = 100.0;
    size.maxValue = 605100.0;
    size.step = 1.0;
    size.unit = QStringLiteral("км");
    size.axis = QStringLiteral("both");
    size.readbackLabel = QStringLiteral("Горизонт при h");
    size.readbackValue = QStringLiteral("%1 км").arg(utils::formatNumber(horizonDistance(maxHeight()) / kMetersPerKilometer, 2));
    list.push_back(size);

    return list;
}

void HorizonModel::setHandleValue(const QString& id, double value) {
    if (id == QLatin1String("height")) {
        setParam(QStringLiteral("h"), value);
        return;
    }
    if (id == QLatin1String("radius")) {
        setParam(QStringLiteral("radius"), value);
        return;
    }
    Simulation::setHandleValue(id, value);
}

void HorizonModel::beginInteraction() {
    Simulation::beginInteraction();
    setStateTime(0.0);
}

}  // namespace lab::models
