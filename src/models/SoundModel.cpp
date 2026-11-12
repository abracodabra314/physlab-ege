#include "models/SoundModel.h"

#include <algorithm>
#include <cmath>

#include "utils/MathText.h"

namespace lab::models {
namespace {

/** Скорость звука при 0 °C, м/с, и её прирост на один градус. */
constexpr double kSoundSpeedAtZero = 331.0;
constexpr double kSoundSpeedPerDegree = 0.6;

/**
 * Направления движения кодируются знаком скорости. По условию наблюдатель
 * лежит на оси X правее источника, поэтому:
 *  - судно с гудком идёт «навстречу наблюдателю» в сторону роста x, то есть
 *    его скорость положительна, а «удаляясь» — отрицательна;
 *  - судно наблюдателя идёт «навстречу звуку» в сторону убывания x, то есть
 *    его скорость отрицательна, а «удаляясь от звука» — положительна.
 * Обе скорости входят в одно и то же условие приёма фронта, поэтому знак
 * здесь важнее модуля: он и определяет, приёмы станут чаще или реже.
 */
constexpr double kSourceTowardsObserverSign = 1.0;
constexpr double kObserverTowardsSoundSign = -1.0;

/** Запас времени после приёма последнего сигнала: сцена показывает и «эхо-паузу». */
constexpr double kTailSeconds = 1.5;

}  // namespace

SoundModel::SoundModel(QObject* parent) : Simulation(parent) { seedDefaults(); }

QString SoundModel::title() const { return QStringLiteral("Гудок судна: распространение звука"); }

QString SoundModel::subtitle() const {
    return QStringLiteral("Фронты r = c(t − tₖ) из точек излучения; приём фронтом настигает наблюдателя");
}

QString SoundModel::statement() const {
    return QStringLiteral(
        "Судно с гудком движется к наблюдателю и через равные промежутки времени подаёт звуковые "
        "сигналы. Каждый сигнал расходится из той точки, где он был подан. Найти момент приёма "
        "каждого сигнала наблюдателем и промежуток между приёмами, если суда движутся навстречу "
        "или удаляются.");
}

QString SoundModel::problemText() const {
    return QStringLiteral(
               "Судно с гудком движется со скоростью %1 м/с, наблюдатель на другом судне — со "
               "скоростью %2 м/с; в начальный момент суда разделяет %3 м. Гудок подаёт сигналы через "
               "T = %4 с. Температура воздуха %5 °C. Найти моменты приёма первых %6 сигналов и "
               "промежуток между приёмами.")
        .arg(utils::formatNumber(std::abs(sourceSpeed()), 1), utils::formatNumber(std::abs(observerSpeed()), 1),
             utils::formatNumber(initialGap(), 0), utils::formatNumber(signalPeriod(), 1),
             utils::formatNumber(temperatureCelsius(), 1), QString::number(kSignals));
}

// ------------------------------------------------------------------
// Параметры
// ------------------------------------------------------------------

QVector<ParamSpec> SoundModel::paramSpecs() const {
    auto slider = [](const QString& id, const QString& label, const QString& unit, double minimum, double maximum,
                     double step, double decimals, bool allowZero, double defaultValue, const QString& group,
                     const QString& help) {
        ParamSpec spec;
        spec.id = id;
        spec.label = label;
        spec.unit = unit;
        spec.kind = ParamKind::Slider;
        spec.minValue = minimum;
        spec.maxValue = maximum;
        spec.stepValue = step;
        spec.decimals = decimals;
        spec.allowZero = allowZero;
        spec.defaultValue = defaultValue;
        spec.group = group;
        spec.helpText = help;
        return spec;
    };
    auto choice = [](const QString& id, const QString& label, QStringList variants, const QString& group,
                     const QString& help) {
        ParamSpec spec;
        spec.id = id;
        spec.label = label;
        spec.kind = ParamKind::Choice;
        spec.choices = std::move(variants);
        spec.defaultValue = 0.0;
        spec.group = group;
        spec.helpText = help;
        return spec;
    };

    const QString sound = QStringLiteral("Звук");
    const QString source = QStringLiteral("Судно с гудком");
    const QString observer = QStringLiteral("Судно наблюдателя");

    QVector<ParamSpec> specs;
    specs.push_back(slider(QStringLiteral("temp"), QStringLiteral("Температура воздуха"), QStringLiteral("°C"), -30.0,
                           40.0, 1.0, 0.0, true, 20.0, sound,
                           QStringLiteral("Скорость звука c = 331 + 0,6·t. Можно менять и перетаскиванием "
                                          "термометра на схеме.")));
    specs.push_back(slider(QStringLiteral("T"), QStringLiteral("Промежуток между сигналами T"), QStringLiteral("с"), 0.5,
                           30.0, 0.5, 1.0, false, 3.0, sound,
                           QStringLiteral("Гудок подаёт сигналы через равные промежутки времени")));
    specs.push_back(slider(QStringLiteral("L0"), QStringLiteral("Начальное расстояние между судами"),
                           QStringLiteral("м"), 100.0, 20000.0, 10.0, 0.0, false, 6800.0, QStringLiteral("Суда"),
                           QStringLiteral("Судно наблюдателя можно перетащить мышью на сцене")));
    specs.push_back(choice(QStringLiteral("dir1"), QStringLiteral("Куда движется судно с гудком"),
                           QStringList{QStringLiteral("навстречу наблюдателю"), QStringLiteral("удаляясь от него")},
                           source, QStringLiteral("Знак скорости входит в ту же формулу приёма сигнала")));
    specs.push_back(slider(QStringLiteral("v1"), QStringLiteral("Скорость судна с гудком v₁"), QStringLiteral("м/с"),
                           0.0, 400.0, 0.5, 1.0, true, 12.0, source,
                           QStringLiteral("Должна быть меньше скорости звука, иначе судно обгоняет свой гудок")));
    specs.push_back(choice(QStringLiteral("dir2"), QStringLiteral("Куда движется судно наблюдателя"),
                           QStringList{QStringLiteral("навстречу звуку"), QStringLiteral("удаляясь от звука")},
                           observer, QStringLiteral("При удалении скорость может превысить скорость звука — "
                                                   "тогда сигнал наблюдателя не догонит")));
    specs.push_back(slider(QStringLiteral("v2"), QStringLiteral("Скорость судна наблюдателя v₂"), QStringLiteral("м/с"),
                           0.0, 400.0, 0.5, 1.0, true, 8.0, observer,
                           QStringLiteral("Должна быть меньше скорости звука: иначе фронт не настигнет наблюдателя")));
    return specs;
}

ValidationResult SoundModel::validate() const {
    ValidationResult check;
    if (soundSpeed() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Скорость звука должна быть больше нуля");
        check.hint = QStringLiteral("Задайте температуру не ниже −30 °C");
        return check;
    }
    if (initialGap() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Суда находятся в одной точке: сигнал приходит сразу");
        check.hint = QStringLiteral("Задайте расстояние не меньше 100 м");
        return check;
    }
    if (signalPeriod() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Промежуток между сигналами равен нулю: сигналы сливаются в один");
        check.hint = QStringLiteral("Задайте T не меньше 0,5 с");
        return check;
    }
    // Скорости берутся со знаком: наблюдатель, движущийся «удаляясь», может
    // обогнать фронт, и тогда приём наступит позже или не наступит вовсе.
    if (observerSpeed() >= soundSpeed()) {
        check.ok = false;
        check.message = QStringLiteral("Судно наблюдателя движется со скоростью звука или быстрее — фронт его не догонит");
        check.hint = QStringLiteral("Уменьшите скорость наблюдателя или выберите движение навстречу звуку");
        return check;
    }
    if (sourceSpeed() >= soundSpeed()) {
        check.ok = false;
        check.message = QStringLiteral("Судно с гудком движется со скоростью звука или быстрее — оно обгоняет свои сигналы");
        check.hint = QStringLiteral("Уменьшите скорость судна с гудком");
        return check;
    }
    return check;
}

double SoundModel::temperatureCelsius() const { return param(QStringLiteral("temp")); }

double SoundModel::soundSpeed() const { return kSoundSpeedAtZero + kSoundSpeedPerDegree * temperatureCelsius(); }

double SoundModel::initialGap() const { return param(QStringLiteral("L0")); }

double SoundModel::sourceSpeed() const {
    const double magnitude = param(QStringLiteral("v1"));
    // Наблюдатель стоит правее: «навстречу ему» — движение в сторону роста x.
    return paramChoiceIndex(QStringLiteral("dir1")) == 0 ? kSourceTowardsObserverSign * magnitude
                                                         : -kSourceTowardsObserverSign * magnitude;
}

double SoundModel::observerSpeed() const {
    const double magnitude = param(QStringLiteral("v2"));
    // Наблюдатель стоит правее источника: движение «навстречу звуку» — это
    // движение в сторону уменьшения x, то есть скорость со знаком минус.
    return paramChoiceIndex(QStringLiteral("dir2")) == 0 ? kObserverTowardsSoundSign * magnitude
                                                         : -kObserverTowardsSoundSign * magnitude;
}

double SoundModel::signalPeriod() const { return param(QStringLiteral("T")); }

// ------------------------------------------------------------------
// Закон движения судов
// ------------------------------------------------------------------

double SoundModel::sourcePositionAt(double t) const { return sourceSpeed() * t; }

double SoundModel::observerPositionAt(double t) const { return initialGap() + observerSpeed() * t; }

double SoundModel::gapAt(double t) const { return observerPositionAt(t) - sourcePositionAt(t); }

// ------------------------------------------------------------------
// Фронты звуковой волны
// ------------------------------------------------------------------

double SoundModel::emissionTime(int index) const {
    return index <= 0 ? 0.0 : signalPeriod() * double(index);
}

double SoundModel::emissionPoint(int index) const { return sourcePositionAt(emissionTime(index)); }

double SoundModel::frontRadius(int index, double t) const {
    if (t < emissionTime(index) - 1e-12)
        return 0.0;   // фронт ещё не родился
    return soundSpeed() * (t - emissionTime(index));
}

int SoundModel::emittedCount(double t) const {
    int count = 0;
    for (int index = 0; index < kSignals; ++index) {
        if (t >= emissionTime(index) - 1e-12)
            ++count;
    }
    return count;
}

double SoundModel::arrivalTime(int index) const {
    // Условие приёма: правая граница фронта совпадает с положением наблюдателя
    //   xₖ + c(t − tₖ) = L₀ + v₂t   ⇒   t = (L₀ + (c − v₁)tₖ)/(c − v₂)
    const double closing = soundSpeed() - observerSpeed();
    if (closing <= 1e-12)
        return std::numeric_limits<double>::quiet_NaN();
    const double emitted = emissionTime(index);
    return (initialGap() + (soundSpeed() - sourceSpeed()) * emitted) / closing;
}

int SoundModel::receivedCount(double t) const {
    int count = 0;
    for (int index = 0; index < kSignals; ++index) {
        const double arrival = arrivalTime(index);
        if (std::isfinite(arrival) && t >= arrival - 1e-12)
            ++count;
    }
    return count;
}

double SoundModel::receptionPeriodFor(double source, double observer) const {
    // T′ = T(c − v₁)/(c − v₂): множитель меньше единицы при сближении и
    // больше — при удалении наблюдателя.
    const double closing = soundSpeed() - observer;
    if (closing <= 1e-12)
        return std::numeric_limits<double>::quiet_NaN();
    return signalPeriod() * (soundSpeed() - source) / closing;
}

double SoundModel::receptionPeriod() const { return receptionPeriodFor(sourceSpeed(), observerSpeed()); }

// ------------------------------------------------------------------
// Наблюдение: от старта до приёма последнего сигнала
// ------------------------------------------------------------------

double SoundModel::duration() const {
    const double last = arrivalTime(kSignals - 1);
    if (!std::isfinite(last))
        return kTailSeconds;
    return last + kTailSeconds;
}

double SoundModel::timePerSecond() const { return std::clamp(duration() / 9.0, 0.2, 20.0); }

double SoundModel::stepSize() const { return duration() / 400.0; }

double SoundModel::eventTime() const {
    const double last = arrivalTime(kSignals - 1);
    return std::isfinite(last) ? last : std::numeric_limits<double>::quiet_NaN();
}

// ------------------------------------------------------------------
// Состояние системы в момент t
// ------------------------------------------------------------------

SimState SoundModel::stateAt(double t) const {
    SimState state;
    const int emitted = emittedCount(t);
    const int received = receivedCount(t);

    EntityVisual source;
    source.id = QStringLiteral("source");
    source.label = QStringLiteral("Судно с гудком");
    source.position = sourcePositionAt(t);
    source.value1 = sourceSpeed();
    source.value2 = double(emitted);           // сколько сигналов уже подано
    source.active = true;
    source.variant = 0;
    state.entities.push_back(source);

    EntityVisual observer;
    observer.id = QStringLiteral("observer");
    observer.label = QStringLiteral("Судно наблюдателя");
    observer.position = observerPositionAt(t);
    observer.value1 = observerSpeed();
    observer.value2 = double(received);        // сколько сигналов принято
    observer.active = true;
    observer.variant = 2;
    state.entities.push_back(observer);

    // Каждый фронт — отдельный объект: его центр зафиксирован в точке
    // излучения, а радиус растёт со скоростью звука.
    for (int index = 0; index < kSignals; ++index) {
        EntityVisual front;
        front.id = QStringLiteral("front_%1").arg(index + 1);
        front.label = QStringLiteral("Фронт сигнала %1").arg(index + 1);
        front.position = emissionPoint(index);   // центр окружности не меняется
        front.value1 = frontRadius(index, t);    // радиус фронта
        front.value2 = arrivalTime(index);       // момент приёма
        front.active = t >= emissionTime(index) - 1e-12;
        front.visible = front.active;
        front.variant = 3;
        state.entities.push_back(front);
    }

    state.primary = gapAt(t);
    state.secondary = double(received);
    state.resolved = received >= kSignals;
    state.eventLabel = received >= kSignals
                           ? QStringLiteral("Приняты все сигналы: последний в t = %1 с").arg(utils::formatNumber(arrivalTime(kSignals - 1), 2))
                           : (received > 0 ? QStringLiteral("Принят сигнал %1 из %2").arg(received).arg(kSignals)
                                           : QString());
    return state;
}

// ------------------------------------------------------------------
// Решение
// ------------------------------------------------------------------

Derivation SoundModel::derivation() const {
    using utils::formatNumber;
    Derivation d;
    const ValidationResult check = validate();
    d.valid = check.ok;
    d.problemText = problemText();
    d.unknown = QStringLiteral("моменты приёма сигналов и промежуток между приёмами T′");

    if (!check.ok) {
        d.steps.push_back({QStringLiteral("Внимание"), QStringLiteral("\\text{%1}").arg(check.message)});
        d.steps.push_back({QStringLiteral("Что сделать"), QStringLiteral("\\text{%1}").arg(check.hint)});
        d.answerTemplate = QStringLiteral("Расчёт невозможен: %1").arg(check.message);
        return d;
    }

    const double c = soundSpeed();
    const double v1 = sourceSpeed();
    const double v2 = observerSpeed();
    const double length = initialGap();
    const double period = signalPeriod();
    const double t = time();

    /**
     * Разность в записи подстановки: «343 + 8» вместо «343 − −8». Формула
     * при этом остаётся той же (c − v₂), просто отрицательный знак скорости
     * раскрывается по правилу вычитания, как это пишут в решении.
     */
    auto difference = [](double left, double right) {
        const QString leftText = utils::formatNumber(left, 1);
        return right < 0.0 ? QStringLiteral("%1 + %2").arg(leftText, formatNumber(-right, 1))
                           : QStringLiteral("%1 - %2").arg(leftText, formatNumber(right, 1));
    };

    QVector<DerivationStep>& st = d.steps;
    st.push_back({QStringLiteral("1. Исходные данные"),
                  QStringLiteral("L_{0} = %1 \\text{ м},\\quad |v_{1}| = %2 \\text{ м/с},\\quad |v_{2}| = %3 "
                                 "\\text{ м/с},\\quad T = %4 \\text{ с},\\quad t^{\\circ} = %5")
                      .arg(formatNumber(length, 0), formatNumber(std::abs(v1), 1), formatNumber(std::abs(v2), 1),
                           formatNumber(period, 1), formatNumber(temperatureCelsius(), 1))});

    st.push_back({QStringLiteral("2. Скорость звука при данной температуре"),
                  QStringLiteral("c = 331 + 0,6 \\cdot t^{\\circ} = 331 + 0,6 \\cdot %1 = %2 \\text{ м/с}")
                      .arg(formatNumber(temperatureCelsius(), 1), formatNumber(c, 1))});

    // Направление задается знаком: без этого шага формула приёма даёт
    // неверный ответ, хотя все модули скоростей верны.
    {
        const double relative = v2 - v1;   // скорость изменения расстояния между судами
        st.push_back({QStringLiteral("3. Знаки скоростей"),
                      QStringLiteral("\\text{Ось } X \\text{ направлена от источника к наблюдателю, поэтому } v_{1} = "
                                     "%1, \\quad v_{2} = %2.\\ \\text{Расстояние между судами меняется со скоростью } "
                                     "v_{2} - v_{1} = %3 \\text{ м/с, то есть %4}")
                          .arg(formatNumber(v1, 1) + QStringLiteral(" \\text{ м/с}"),
                               formatNumber(v2, 1) + QStringLiteral(" \\text{ м/с}"), formatNumber(relative, 1),
                               relative < -1e-9
                                   ? QStringLiteral("суда сближаются")
                                   : (relative > 1e-9 ? QStringLiteral("суда удаляются")
                                                      : QStringLiteral("расстояние не меняется")))});
    }

    st.push_back({QStringLiteral("4. Моменты и точки излучения сигналов"),
                  QStringLiteral("t_{k} = kT, \\qquad x_{k} = v_{1} t_{k}")});
    {
        QString list;
        for (int index = 0; index < kSignals; ++index) {
            if (index > 0)
                list += QStringLiteral(",\\qquad ");
            list += QStringLiteral("t_{%1} = %2 \\text{ с},\\ x_{%1} = %3 \\text{ м}")
                        .arg(index + 1)
                        .arg(formatNumber(emissionTime(index), 2), formatNumber(emissionPoint(index), 1));
        }
        st.push_back({QStringLiteral("Подстановка"), list});
    }

    st.push_back({QStringLiteral("5. Распространение фронта"),
                  QStringLiteral("r_{k}(t) = c(t - t_{k}) \\text{ — радиус растёт из точки } x_{k},"
                                 "\\quad \\text{центр окружности не перемещается}")});

    st.push_back({QStringLiteral("6. Условие приёма: фронт настигает наблюдателя"),
                  QStringLiteral("x_{k} + c(t - t_{k}) = L_{0} + v_{2} t")});
    st.push_back({QStringLiteral("Выражаем момент приёма"),
                  QStringLiteral("t(c - v_{2}) = L_{0} + (c - v_{1}) t_{k} \\quad \\Rightarrow \\quad t_{k}^{\\prime} = "
                                 "\\frac{L_{0} + (c - v_{1}) t_{k}}{c - v_{2}}")});
    {
        QString list;
        for (int index = 0; index < kSignals; ++index) {
            if (index > 0)
                list += QStringLiteral(",\\qquad ");
            const double emission = emissionTime(index);
            // Скорость звука входит и в числитель, и в знаменатель: это одна
            // и та же величина, а не две разные.
            list += QStringLiteral("t_{%1}^{\\prime} = \\frac{%2 + (%3) \\cdot %4}{%5} = %6 \\text{ с}")
                        .arg(index + 1)
                        .arg(formatNumber(length, 0), difference(c, v1), formatNumber(emission, 2),
                             difference(c, v2), formatNumber(arrivalTime(index), 3));
        }
        st.push_back({QStringLiteral("Подстановка"), list});
    }

    st.push_back({QStringLiteral("7. Проверка: путь, пройденный фронтом"),
                  QStringLiteral("r_{k}(t_{k}^{\\prime}) = c(t_{k}^{\\prime} - t_{k}) \\text{ — ровно то расстояние, "
                                 "которое отделяло точку излучения от наблюдателя: } x_{2}(t_{k}^{\\prime}) - x_{k}")});
    {
        QString list;
        for (int index = 0; index < kSignals; ++index) {
            if (index > 0)
                list += QStringLiteral(",\\qquad ");
            const double arrival = arrivalTime(index);
            // Два независимых способа получить одно и то же число: радиус
            // фронта и разность положений наблюдателя и точки излучения.
            const double radius = frontRadius(index, arrival);
            const double separation = observerPositionAt(arrival) - emissionPoint(index);
            list += QStringLiteral("r_{%1} = %2 \\text{ м},\\ x_{2}(t_{%1}^{\\prime}) - x_{%1} = %3 \\text{ м}")
                        .arg(index + 1)
                        .arg(formatNumber(radius, 1), formatNumber(separation, 1));
        }
        st.push_back({QStringLiteral("Подстановка"), list});
    }

    st.push_back({QStringLiteral("8. Промежуток между приёмами"),
                  QStringLiteral("T' = t_{k+1}^{\\prime} - t_{k}^{\\prime} = \\frac{(c - v_{1}) T}{c - v_{2}}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("T' = \\frac{(%1) \\cdot %2}{%3} = %4 \\text{ с}")
                      .arg(difference(c, v1), formatNumber(period, 1), difference(c, v2),
                           formatNumber(receptionPeriod(), 3))});
    st.push_back({QStringLiteral("Сравнение с промежутком излучения"),
                  QStringLiteral("T' %1 T:\\quad \\text{%2}")
                      .arg(receptionPeriod() < period - 1e-9 ? QStringLiteral("<") : QStringLiteral(">"),
                           receptionPeriod() < period - 1e-9
                               ? QStringLiteral("суда сближаются — приёмы следуют чаще")
                               : QStringLiteral("суда удаляются — приёмы следуют реже"))});

    st.push_back({QStringLiteral("9. Состояние в текущий момент t = %1 с").arg(formatNumber(t, 2)),
                  QStringLiteral("L(t) = L_{0} + (v_{2} - v_{1}) t = %1 \\text{ м},\\quad \\text{подано сигналов: } %2,"
                                 "\\quad \\text{принято: } %3")
                      .arg(formatNumber(gapAt(t), 1), QString::number(emittedCount(t)), QString::number(receivedCount(t)))});

    d.answerTemplate = QStringLiteral("Приёмы в моменты %1 с; промежуток между приёмами T′ = %2 с")
                           .arg(formatNumber(arrivalTime(0), 2), formatNumber(receptionPeriod(), 3));
    return d;
}

QVector<Readout> SoundModel::readouts() const {
    QVector<Readout> list;
    if (!validate().ok)
        return list;

    const double t = time();
    list.push_back({QStringLiteral("Время наблюдения"), QStringLiteral("%1 с").arg(utils::formatNumber(t, 2))});
    list.push_back({QStringLiteral("Скорость звука c"), QStringLiteral("%1 м/с").arg(utils::formatNumber(soundSpeed(), 1))});
    list.push_back({QStringLiteral("Расстояние между судами L(t)"), QStringLiteral("%1 м").arg(utils::formatNumber(gapAt(t), 1))});
    list.push_back({QStringLiteral("Сигналов подано"), QString::number(emittedCount(t))});
    list.push_back({QStringLiteral("Сигналов принято"), QString::number(receivedCount(t))});
    for (int index = 0; index < kSignals; ++index) {
        const double arrival = arrivalTime(index);
        list.push_back({QStringLiteral("Приём сигнала %1").arg(index + 1),
                        QStringLiteral("%1 с (подан в %2 с из точки x = %3 м)")
                            .arg(utils::formatNumber(arrival, 2), utils::formatNumber(emissionTime(index), 2),
                                 utils::formatNumber(emissionPoint(index), 1)),
                        index == 0});
    }
    list.push_back({QStringLiteral("Радиус последнего фронта"),
                    QStringLiteral("%1 м").arg(utils::formatNumber(frontRadius(kSignals - 1, t), 1))});
    list.push_back({QStringLiteral("Промежуток между приёмами T′"), QStringLiteral("%1 с").arg(utils::formatNumber(receptionPeriod(), 3))});
    list.push_back({QStringLiteral("Состояние"),
                    receivedCount(t) >= kSignals ? QStringLiteral("приняты все сигналы") : QStringLiteral("ожидание сигналов")});
    return list;
}

// ------------------------------------------------------------------
// Ряды данных
// ------------------------------------------------------------------

QVector<QPointF> SoundModel::gapSeries() const {
    QVector<QPointF> points;
    const double total = duration();
    for (int i = 0; i <= 240; ++i) {
        const double t = total * i / 240.0;
        points.append(QPointF(t, gapAt(t)));
    }
    return points;
}

QVector<QPointF> SoundModel::frontSeries(int index) const {
    QVector<QPointF> points;
    if (index < 0 || index >= kSignals)
        return points;
    const double start = emissionTime(index);
    const double total = duration();
    for (int i = 0; i <= 240; ++i) {
        // Ряд начинается с момента излучения: до него фронта нет, и график
        // обрывается на оси времени, а не идёт нулём.
        const double t = start + (total - start) * i / 240.0;
        points.append(QPointF(t, frontRadius(index, t)));
    }
    return points;
}

QVector<QPointF> SoundModel::observerPathSeries() const {
    QVector<QPointF> points;
    const double total = duration();
    const double start = initialGap();
    for (int i = 0; i <= 240; ++i) {
        const double t = total * i / 240.0;
        points.append(QPointF(t, observerPositionAt(t) - start));
    }
    return points;
}

double SoundModel::seriesSpeedMax() const {
    // Ось скоростей одна для обоих графиков: от «навстречу звуку» до
    // скорости звука. За c наблюдатель уходит от фронта, и приём вообще
    // не наступает — эта точка график не рисует.
    const double c = soundSpeed();
    const double typical = std::max(std::abs(sourceSpeed()), std::abs(observerSpeed()));
    return std::max(1.0, std::min(typical * 4.0, c * 0.98));
}

QVector<QPointF> SoundModel::periodVersusObserverSpeed() const {
    QVector<QPointF> points;
    const double maximum = seriesSpeedMax();
    for (int i = 0; i <= 200; ++i) {
        const double speed = -maximum + 2.0 * maximum * i / 200.0;
        const double period = receptionPeriodFor(sourceSpeed(), speed);
        points.append(QPointF(speed, std::isfinite(period) ? period : std::numeric_limits<double>::quiet_NaN()));
    }
    return points;
}

QVector<QPointF> SoundModel::sourcePathSeries() const {
    QVector<QPointF> points;
    const double total = duration();
    for (int i = 0; i <= 240; ++i) {
        const double t = total * i / 240.0;
        points.append(QPointF(t, sourcePositionAt(t)));
    }
    return points;
}

QVector<QPointF> SoundModel::periodVersusSourceSpeed() const {
    QVector<QPointF> points;
    const double maximum = seriesSpeedMax();
    for (int i = 0; i <= 200; ++i) {
        const double speed = -maximum + 2.0 * maximum * i / 200.0;
        const double period = receptionPeriodFor(speed, observerSpeed());
        points.append(QPointF(speed, std::isfinite(period) ? period : std::numeric_limits<double>::quiet_NaN()));
    }
    return points;
}

// ------------------------------------------------------------------
// Точки взаимодействия
// ------------------------------------------------------------------

QVector<HandleInfo> SoundModel::handles() const {
    QVector<HandleInfo> list;

    HandleInfo gap;
    gap.id = QStringLiteral("gap");
    gap.label = QStringLiteral("Расстояние между судами");
    gap.value = initialGap();
    gap.minValue = 100.0;
    gap.maxValue = 20000.0;
    gap.step = 10.0;
    gap.unit = QStringLiteral("м");
    gap.axis = QStringLiteral("x");
    gap.readbackLabel = QStringLiteral("Приём 1-го");
    gap.readbackValue = QStringLiteral("t = %1 с").arg(utils::formatNumber(arrivalTime(0), 2));
    list.push_back(gap);

    HandleInfo temperature;
    temperature.id = QStringLiteral("temp");
    temperature.label = QStringLiteral("Температура воздуха");
    temperature.value = temperatureCelsius();
    temperature.minValue = -30.0;
    temperature.maxValue = 40.0;
    temperature.step = 1.0;
    temperature.unit = QStringLiteral("°C");
    temperature.axis = QStringLiteral("x");
    temperature.readbackLabel = QStringLiteral("Скорость звука");
    temperature.readbackValue = QStringLiteral("%1 м/с").arg(utils::formatNumber(soundSpeed(), 1));
    list.push_back(temperature);

    return list;
}

void SoundModel::setHandleValue(const QString& id, double value) {
    // Ручка на схеме называется «gap», а параметр постановки — L0: без
    // явного соответствия перетаскивание судна наблюдателя ушло бы в
    // несуществующий параметр и ничего бы не изменило.
    if (id == QLatin1String("gap")) {
        setParam(QStringLiteral("L0"), value);
        return;
    }
    if (id == QLatin1String("temp")) {
        setParam(id, value);
        return;
    }
    Simulation::setHandleValue(id, value);
}

void SoundModel::beginInteraction() {
    Simulation::beginInteraction();
    setStateTime(0.0);
}

}  // namespace lab::models
