#include "models/WellModel.h"

#include <algorithm>
#include <cmath>

#include "utils/MathText.h"

namespace lab::models {
namespace {

constexpr double kEps = 1e-9;
constexpr double kDefaultGravity = 9.8;

}  // namespace

WellModel::WellModel(QObject* parent) : Simulation(parent) {
    seedDefaults();
}

QString WellModel::title() const {
    return QStringLiteral("Колодец: падающий камень и эхо");
}

QString WellModel::subtitle() const {
    return QStringLiteral("Свободное падение, звук, время до эха");
}

QString WellModel::statement() const {
    return QStringLiteral(
        "Из устья колодца уронили камень. Через какое время после броска "
        "будет услышан звук удара камня о воду? Найдите глубину колодца "
        "по измеренному времени эха.");
}

QString WellModel::problemText() const {
    using utils::formatNumber;
    return QStringLiteral("Глубина колодца до воды — %1 м, ускорение свободного падения g = %2 м/с², "
                          "скорость звука в воздухе c = %3 м/с. Камень отпускают без начальной скорости.")
        .arg(formatNumber(depth(), 2))
        .arg(formatNumber(gravity(), 2))
        .arg(formatNumber(soundSpeed(), 0));
}

QVector<ParamSpec> WellModel::paramSpecs() const {
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

    QVector<ParamSpec> specs;
    auto depthSpec = slider(QStringLiteral("depth"), QStringLiteral("Глубина колодца до воды"),
                            QStringLiteral("м"), 0.5, 60.0, 0.1, 2.0, 12.0,
                            QStringLiteral("Колодец"),
                            QStringLiteral("Перетащите уровень воды мышью — глубина пересчитается."));
    specs.push_back(depthSpec);
    auto releaseSpec = slider(QStringLiteral("release"), QStringLiteral("Глубина отпуска камня от устья"),
                              QStringLiteral("м"), 0.0, 59.5, 0.1, 2.0, 0.0,
                              QStringLiteral("Колодец"),
                              QStringLiteral("0 м — камень бросают из устья. Перетащите камень мышью."));
    releaseSpec.allowZero = true;
    specs.push_back(releaseSpec);
    specs.push_back(slider(QStringLiteral("g"), QStringLiteral("Ускорение свободного падения g"),
                           QStringLiteral("м/с²"), 1.0, 25.0, 0.1, 2.0, kDefaultGravity,
                           QStringLiteral("Физика")));
    specs.push_back(slider(QStringLiteral("c"), QStringLiteral("Скорость звука в воздухе c"),
                           QStringLiteral("м/с"), 200.0, 400.0, 1.0, 0.0, 331.0,
                           QStringLiteral("Физика")));
    return specs;
}

double WellModel::depth() const {
    return param(QStringLiteral("depth"));
}

double WellModel::releaseDepth() const {
    // Камень не может быть отпущен на уровне воды или ниже дна.
    return std::clamp(param(QStringLiteral("release")), 0.0, std::max(0.0, depth() - 0.1));
}

double WellModel::gravity() const {
    return param(QStringLiteral("g"));
}

double WellModel::soundSpeed() const {
    return param(QStringLiteral("c"));
}

double WellModel::fallTime() const {
    const double g = gravity();
    const double distance = std::max(0.0, depth() - releaseDepth());
    if (g < kEps)
        return std::numeric_limits<double>::infinity();
    return std::sqrt(2.0 * distance / g);
}

double WellModel::soundTime() const {
    const double c = soundSpeed();
    if (c < kEps)
        return std::numeric_limits<double>::infinity();
    return depth() / c;
}

double WellModel::echoTime() const {
    return fallTime() + soundTime();
}

double WellModel::stoneDepthAt(double t) const {
    const double tf = fallTime();
    if (t <= 0.0)
        return releaseDepth();
    if (t >= tf)
        return depth();
    return releaseDepth() + 0.5 * gravity() * t * t;
}

double WellModel::soundFrontAt(double t) const {
    const double tf = fallTime();
    if (t < tf)
        return std::numeric_limits<double>::quiet_NaN();
    // Фронт звука идёт от воды (глубина h) вверх к устью (глубина 0).
    const double traveled = soundSpeed() * (t - tf);
    return depth() - traveled;
}

double WellModel::depthFromEchoTime(double totalTime, double gravityValue, double soundSpeedValue) {
    if (totalTime <= kEps || gravityValue <= kEps || soundSpeedValue <= kEps)
        return std::numeric_limits<double>::quiet_NaN();

    // h/c + sqrt(2h/g) = T.  Замена u = sqrt(h):
    //   u²/c + sqrt(2/g)·u − T = 0  ⇒  u = (−b + sqrt(b² + 4T/c)) / (2/c),  b = sqrt(2/g).
    const double a = 1.0 / soundSpeedValue;
    const double b = std::sqrt(2.0 / gravityValue);
    const double c = -totalTime;
    const double discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0)
        return std::numeric_limits<double>::quiet_NaN();
    const double u = (-b + std::sqrt(discriminant)) / (2.0 * a);
    if (u <= 0.0)
        return std::numeric_limits<double>::quiet_NaN();
    return u * u;
}

double WellModel::eventTime() const {
    if (!validate().ok)
        return std::numeric_limits<double>::quiet_NaN();
    return echoTime();
}

SimState WellModel::stateAt(double t) const {
    SimState state;
    state.time = t;

    const double tf = fallTime();
    const double total = echoTime();
    const double depthValue = depth();

    state.primary = std::isnan(soundFrontAt(t)) ? depthValue : std::max(0.0, soundFrontAt(t));
    state.secondary = depthValue;

    EntityVisual stone;
    stone.id = QStringLiteral("stone");
    stone.label = QStringLiteral("Камень");
    stone.position = stoneDepthAt(t);       // глубина от устья
    // Скорость растёт до удара, после удара камень лежит на дне.
    stone.value1 = (t <= kEps || t >= tf) ? 0.0 : gravity() * t;
    stone.active = t > kEps && t < tf;
    stone.variant = 2;

    EntityVisual wave;
    wave.id = QStringLiteral("wave");
    wave.label = QStringLiteral("Звуковая волна");
    const double front = soundFrontAt(t);
    wave.position = std::isnan(front) ? depthValue : std::max(0.0, front);
    wave.value1 = soundSpeed();
    wave.active = !std::isnan(front) && front > kEps;
    wave.variant = 3;

    state.entities = {stone, wave};

    if (t >= tf - 1e-9 && t < tf + 1e-9) {
        state.eventLabel = QStringLiteral("Камень ударился о воду");
    }
    if (t >= total - 1e-9) {
        state.resolved = true;
        state.eventLabel = QStringLiteral("Эхо вернулось к устью");
    }
    return state;
}

double WellModel::timePerSecond() const {
    // Падение короткое, поэтому замедляем просмотр: 1 с реального времени ~ T/8.
    const double total = echoTime();
    if (total > kEps)
        return std::clamp(total / 8.0, 0.05, 10.0);
    return 0.25;
}

double WellModel::duration() const {
    // Наблюдение идёт до возвращения эха и чуть дольше: после удара камень
    // лежит на дне, фронт звука поднимается к устью.
    return std::max(1.0, echoTime() * 1.1 + 0.2);
}

double WellModel::stepSize() const {
    return duration() / 50.0;
}

ValidationResult WellModel::validate() const {
    ValidationResult result;
    if (depth() < kEps) {
        result.ok = false;
        result.message = QStringLiteral("Глубина колодца должна быть больше нуля.");
        result.hint = QStringLiteral("Увеличьте глубину колодца.");
        return result;
    }
    if (gravity() < kEps) {
        result.ok = false;
        result.message = QStringLiteral("Ускорение свободного падения должно быть больше нуля.");
        result.hint = QStringLiteral("Верните g ≈ 9,8 м/с².");
        return result;
    }
    if (soundSpeed() < kEps) {
        result.ok = false;
        result.message = QStringLiteral("Скорость звука должна быть больше нуля.");
        result.hint = QStringLiteral("Увеличьте скорость звука.");
        return result;
    }
    return result;
}

// ------------------------------------------------------------------
// Запись решения
// ------------------------------------------------------------------
Derivation WellModel::derivation() const {
    using utils::formatNumber;
    Derivation d;
    const ValidationResult check = validate();
    d.valid = check.ok;
    d.problemText = problemText();
    d.unknown = QStringLiteral("Полное время T до возвращения эха; глубина h по измеренному T");

    if (!check.ok) {
        d.steps.push_back({QStringLiteral("Внимание"), QStringLiteral("\\text{%1}").arg(check.message)});
        d.answerTemplate = check.hint;
        return d;
    }

    const double h = depth();
    const double g = gravity();
    const double c = soundSpeed();
    const double h0 = releaseDepth();
    const double fallDistance = std::max(0.0, h - h0);
    const double tf = fallTime();
    const double ts = soundTime();

    d.steps.push_back({QStringLiteral("1. Исходные данные"),
                       QStringLiteral("h = %1 \\text{ м},\\quad h_{0} = %4 \\text{ м (глубина отпуска камня)},\\quad "
                                      "g = %2 \\text{ м/с}^{2},\\quad c = %3 \\text{ м/с}")
                           .arg(formatNumber(h, 2), formatNumber(g, 2), formatNumber(c, 0), formatNumber(h0, 2))});

    d.steps.push_back({QStringLiteral("2. Закон свободного падения"),
                       QStringLiteral("h - h_{0} = \\frac{g t_{1}^{2}}{2} \\;\\Rightarrow\\; "
                                      "t_{1} = \\sqrt{\\frac{2(h - h_{0})}{g}}")});
    d.steps.push_back({QStringLiteral("3. Подстановка (время падения)"),
                       QStringLiteral("t_{1} = \\sqrt{\\frac{2 \\cdot %1}{%2}} = %3 \\text{ с}")
                           .arg(formatNumber(fallDistance, 2), formatNumber(g, 2), formatNumber(tf, 4))});

    d.steps.push_back({QStringLiteral("4. Распространение звука"),
                       QStringLiteral("t_{2} = \\frac{h}{c} = \\frac{%1}{%2} = %3 \\text{ с}")
                           .arg(formatNumber(h, 2), formatNumber(c, 0), formatNumber(ts, 4))});

    d.steps.push_back({QStringLiteral("5. Полное время"),
                       QStringLiteral("T = t_{1} + t_{2} = %1 + %2 = %3 \\text{ с}")
                           .arg(formatNumber(tf, 4), formatNumber(ts, 4), formatNumber(tf + ts, 4))});

    // Обратная задача: глубина по измеренному времени эха. Классическая
    // постановка предполагает, что камень отпущен из устья (h₀ = 0), поэтому
    // для неё используется время T = sqrt(2h/g) + h/c.
    const double T = std::sqrt(2.0 * h / g) + h / c;
    const double b = std::sqrt(2.0 / g);
    const double a = 1.0 / c;
    const double discriminant = b * b + 4.0 * a * T;
    const double u = (-b + std::sqrt(discriminant)) / (2.0 * a);

    d.steps.push_back({QStringLiteral("6. Обратная задача"),
                       QStringLiteral("Если измерено T, то \\frac{h}{c} + \\sqrt{\\frac{2h}{g}} = T. "
                                      "Замена u = \\sqrt{h} даёт квадратное уравнение")});
    d.steps.push_back({QStringLiteral("Подстановка"),
                       QStringLiteral("\\frac{1}{c} u^{2} + \\sqrt{\\frac{2}{g}}\\,u - T = 0:\\quad "
                                      "%1 u^{2} + %2 u - %3 = 0")
                           .arg(formatNumber(a, 5), formatNumber(b, 4), formatNumber(T, 4))});
    d.steps.push_back({QStringLiteral("Положительный корень"),
                       QStringLiteral("u = %1 \\;\\Rightarrow\\; h = u^{2} = %2 \\text{ м}")
                           .arg(formatNumber(u, 4), formatNumber(u * u, 3))});
    d.steps.push_back({QStringLiteral("Проверка"),
                       QStringLiteral("t_{1} + t_{2} = %1 + %2 = %3 \\text{ с} — совпадает с T")
                           .arg(formatNumber(std::sqrt(2.0 * u * u / g), 4), formatNumber(u * u / c, 4),
                                formatNumber(std::sqrt(2.0 * u * u / g) + u * u / c, 4))});

    d.steps.push_back({QStringLiteral("7. Ответ"),
                       QStringLiteral("Эхо будет услышано через T = %1 с; "
                                      "глубина по этому времени — h = %2 м")
                           .arg(formatNumber(T, 3), formatNumber(h, 2))});

    d.answerTemplate = QStringLiteral("T = %1 с").arg(utils::formatDuration(T, timeUnit()));
    return d;
}

QVector<Readout> WellModel::readouts() const {
    using utils::formatNumber;
    QVector<Readout> list;
    const SimState state = currentState();
    const double t = time();

    list.push_back({QStringLiteral("Время эксперимента"), QStringLiteral("%1 с").arg(formatNumber(t, 3)), false});
    list.push_back({QStringLiteral("Глубина колодца"), QStringLiteral("%1 м").arg(formatNumber(depth(), 2)), false});
    list.push_back({QStringLiteral("Глубина камня"), QStringLiteral("%1 м").arg(formatNumber(state.entities.first().position, 3)), false});
    list.push_back({QStringLiteral("Скорость камня"), QStringLiteral("%1 м/с").arg(formatNumber(state.entities.first().value1, 2)), false});

    const double front = state.entities.at(1).position;
    list.push_back({QStringLiteral("Фронт звука (от устья)"),
                    state.entities.at(1).active ? QStringLiteral("%1 м").arg(formatNumber(front, 3))
                                                : QStringLiteral("звук ещё не пошёл"),
                    false});

    list.push_back({QStringLiteral("Время падения t₁"), QStringLiteral("%1 с").arg(formatNumber(fallTime(), 4)), true});
    list.push_back({QStringLiteral("Время звука t₂"), QStringLiteral("%1 с").arg(formatNumber(soundTime(), 4)), true});
    list.push_back({QStringLiteral("Время до эха T"), QStringLiteral("%1 с").arg(formatNumber(echoTime(), 4)), true});

    if (!state.eventLabel.isEmpty())
        list.push_back({QStringLiteral("Событие"), state.eventLabel, true});
    return list;
}

QVector<HandleInfo> WellModel::handles() const {
    QVector<HandleInfo> list;
    HandleInfo water;
    water.id = QStringLiteral("depth");
    water.label = QStringLiteral("Уровень воды");
    water.value = depth();
    water.minValue = 0.5;
    water.maxValue = 60.0;
    water.step = 0.1;
    water.unit = QStringLiteral("м");
    water.axis = QStringLiteral("y");
    water.readbackLabel = QStringLiteral("Время до эха");
    water.readbackValue = QStringLiteral("%1 с").arg(utils::formatNumber(echoTime(), 3));
    list.push_back(water);

    HandleInfo stone;
    stone.id = QStringLiteral("release");
    stone.label = QStringLiteral("Камень: глубина отпуска");
    stone.value = releaseDepth();
    stone.minValue = 0.0;
    stone.maxValue = std::max(0.0, depth() - 0.1);
    stone.step = 0.1;
    stone.unit = QStringLiteral("м от устья");
    stone.axis = QStringLiteral("y");
    stone.readbackLabel = QStringLiteral("Время падения");
    stone.readbackValue = QStringLiteral("%1 с").arg(utils::formatNumber(fallTime(), 3));
    list.push_back(stone);
    return list;
}

}  // namespace lab::models
