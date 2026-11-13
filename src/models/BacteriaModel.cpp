#include "models/BacteriaModel.h"

#include <algorithm>
#include <cmath>

#include "utils/MathText.h"

namespace lab::models {
namespace {

/** Логарифм двух: он связывает период удвоения и постоянную роста. */
constexpr double kLnTwo = 0.6931471805599453;
/** Запас времени после конца наблюдения: сцена показывает итог. */
constexpr double kTailHours = 0.8;
/** Часов в сутках: справочные периоды удвоения длиннее суток. */
constexpr double kHoursPerDay = 24.0;
/** Предельное число клеток: выше него закон ещё верен, но числа уже
 *  не помещаются ни на шкалу, ни в чашку. */
constexpr double kMaxReadableCells = 1.0e9;

/**
 * Справочные периоды удвоения в часах. Время модели — часы, поэтому
 * переводить единицы в подстановках не приходится, а названия видов говорят,
 * откуда взято число. Стартовое число клеток вид не задаёт: посев вносят в
 * чашку независимо от микроорганизма.
 */
struct Species {
    const char* name;
    double doublingHours;
};

const Species kSpecies[] = {
    {"кишечная палочка (20 мин)", 1.0 / 3.0},
    {"дрожжевая клетка (2 ч)", 2.0},
    {"сенная палочка (4 ч)", 4.0},
    {"туберкулёзная палочка (13 ч)", 13.0},
};

/** Сколько делений показывает наблюдение по умолчанию: ~6 периодов удвоения. */
constexpr int kDefaultDivisionCount = 6;

/** Вспомогательный слайдер. */
ParamSpec slider(const QString& id, const QString& label, const QString& unit, double minValue, double maxValue,
                 double step, int decimals, bool allowZero, double defaultValue, const QString& group,
                 const QString& help) {
    ParamSpec spec;
    spec.id = id;
    spec.label = label;
    spec.unit = unit;
    spec.kind = ParamKind::Slider;
    spec.minValue = minValue;
    spec.maxValue = maxValue;
    spec.stepValue = step;
    spec.decimals = decimals;
    spec.allowZero = allowZero;
    spec.defaultValue = defaultValue;
    spec.group = group;
    spec.helpText = help;
    return spec;
}

/** Вспомогательный выпадающий список. */
ParamSpec choice(const QString& id, const QString& label, const QStringList& options, const QString& group,
                 const QString& help, int defaultValue = 0) {
    ParamSpec spec;
    spec.id = id;
    spec.label = label;
    spec.kind = ParamKind::Choice;
    spec.minValue = 0.0;
    spec.maxValue = double(options.size() - 1);
    spec.stepValue = 1.0;
    spec.decimals = 0;
    spec.allowZero = true;
    spec.defaultValue = double(defaultValue);
    spec.group = group;
    spec.helpText = help;
    spec.choices = options;
    return spec;
}

}  // namespace

QString BacteriaModel::durationText(double hours) {
    if (!std::isfinite(hours))
        return QStringLiteral("—");
    if (hours <= 0.0)
        return QStringLiteral("0 ч");
    if (hours < 1.0)
        return utils::formatNumber(hours * 60.0, 1) + QStringLiteral(" мин");
    if (hours < 48.0) {
        const double whole = std::floor(hours);
        const double minutes = std::round((hours - whole) * 60.0);
        if (minutes < 0.5)
            return utils::formatNumber(whole, 0) + QStringLiteral(" ч");
        if (minutes > 59.5)
            return utils::formatNumber(whole + 1.0, 0) + QStringLiteral(" ч");
        return utils::formatNumber(whole, 0) + QStringLiteral(" ч ") + utils::formatNumber(minutes, 0) +
               QStringLiteral(" мин");
    }
    return utils::formatNumber(hours / kHoursPerDay, 2) + QStringLiteral(" сут");
}

QString BacteriaModel::cellsText(double count) {
    if (!std::isfinite(count))
        return QStringLiteral("—");
    /*
     * Число клеток всегда целое, поэтому дробную часть убирают. Ниже миллиона
     * его пишут как есть — так его можно сверить с делениями; выше — степенной
     * записью: 2^20 делений подряд дали бы тридцать разрядов, и ни в подписи,
     * ни в подстановке такое число не читается.
     */
    const double whole = std::round(std::abs(count));
    if (whole < 1.0e6)
        return (count < 0.0 ? QStringLiteral("-") : QString()) + utils::formatNumber(whole, 0);
    const int exponent = int(std::floor(std::log10(whole)));
    const double mantissa = whole / std::pow(10.0, double(exponent));
    return QStringLiteral("%1·10^%2").arg(utils::formatNumber(mantissa, 2), QString::number(exponent));
}

QString BacteriaModel::ratioText(double value) {
    /*
     * Малые безразмерные величины (k, n, доли) в подстановках и на панелях.
     * Постоянная роста бывает порядка 0,001 ч⁻¹, и обычное форматирование
     * оставило бы ноль, поэтому число знаков берут по порядку величины.
     */
    if (!std::isfinite(value))
        return QStringLiteral("—");
    const double magnitude = std::abs(value);
    if (magnitude == 0.0)
        return QStringLiteral("0");
    if (magnitude >= 100.0)
        return utils::formatNumber(value, 0);
    if (magnitude >= 1.0)
        return utils::formatNumber(value, 3);
    const int digits = std::clamp(int(std::ceil(-std::log10(magnitude))) + 4, 3, 10);
    return utils::formatNumber(value, digits);
}

BacteriaModel::BacteriaModel(QObject* parent) : Simulation(parent) { seedDefaults(); }

QString BacteriaModel::title() const { return QStringLiteral("Деление бактерий в чашке Петри"); }

QString BacteriaModel::subtitle() const {
    return QStringLiteral("N = N₀·2^(t/T_d), n = log₂(N/N₀), N′ = kN; рост колонии");
}

QString BacteriaModel::statement() const {
    return QStringLiteral(
        "В чашку Петри поместили N₀ бактерий, период удвоения культуры — T_d. Число клеток "
        "удваивается через каждый интервал T_d, поэтому N = N₀·2^(t/T_d). Найти постоянную "
        "скорости размножения k = ln2/T_d, число делений, прошедших к заданному моменту, число "
        "клеток в этот момент, а также через сколько времени клеток станет заданное число и как "
        "скоро растёт колония (N′ = kN).");
}

QString BacteriaModel::problemText() const {
    return QStringLiteral(
               "В чашку Петри поместили %1 клеток («%2»), период удвоения — %3. Наблюдение длится %4. "
               "Найти постоянную скорости размножения, число делений к концу наблюдения, число клеток "
               "в этот момент и время, когда клеток станет %5.")
        .arg(utils::formatNumber(initialCells(), 0), speciesTitle(), durationText(doublingTime()),
             durationText(observationTime()), cellsText(targetCells()));
}

// ------------------------------------------------------------------
// Параметры
// ------------------------------------------------------------------

QVector<ParamSpec> BacteriaModel::paramSpecs() const {
    QVector<ParamSpec> specs;
    const QString colony = QStringLiteral("Колония");
    const QString process = QStringLiteral("Наблюдение");

    QStringList species;
    for (int index = 0; index < customSpeciesIndex(); ++index)
        species.push_back(speciesName(index));
    // По умолчанию — свой период удвоения: с ним наблюдение и шкала опыта
    // согласованы (T_d = 2 ч, N₀ = 1000, T = 12 ч), и на чашке видно несколько
    // делений подряд. Справочные виды задают и T_d, и разумное окно.
    species.push_back(QStringLiteral("свой период удвоения — задать T_d"));
    specs.push_back(choice(QStringLiteral("species"), QStringLiteral("Вид микроорганизма"), species, colony,
                           QStringLiteral("Период удвоения T_d определяет постоянную роста "
                                          "k = ln2/T_d; свой пункт задаёт T_d вручную"),
                           customSpeciesIndex()));

    specs.push_back(slider(QStringLiteral("doubling"), QStringLiteral("Период удвоения T_d"), QStringLiteral("ч"),
                           0.25, 24.0, 0.25, 2, false, 2.0, colony,
                           QStringLiteral("Через каждый интервал T_d число клеток удваивается: N = N₀·2^(t/T_d)")));

    specs.push_back(slider(QStringLiteral("N0"), QStringLiteral("Начальное число клеток N₀"), QStringLiteral("шт"), 1.0,
                           100000.0, 1.0, 0, false, 1000.0, colony,
                           QStringLiteral("Стартовая численность культуры: её умножают на 2 за каждое деление")));

    specs.push_back(slider(QStringLiteral("T"), QStringLiteral("Длительность наблюдения T"), QStringLiteral("ч"), 1.0,
                           96.0, 1.0, 0, false, 12.0, process,
                           QStringLiteral("Сколько времени идёт опыт; шкала чашки охватывает всё наблюдение")));

    specs.push_back(slider(QStringLiteral("step"), QStringLiteral("Интервал учёта Δt"), QStringLiteral("ч"), 0.25,
                           12.0, 0.25, 2, false, 1.0, process,
                           QStringLiteral("Через какие промежутки наблюдатель считает клетки: по ним строят "
                                          "столбцы прироста и среднюю скорость ΔN/Δt")));

    specs.push_back(slider(QStringLiteral("target"), QStringLiteral("Целевое число клеток N"), QStringLiteral("шт"),
                           1.0, 100000000.0, 1.0, 0, false, 32000.0, process,
                           QStringLiteral("Отметка на шкале: момент, когда клеток станет столько, ищут как "
                                          "t = T_d·log₂(N/N₀)")));
    return specs;
}

ValidationResult BacteriaModel::validate() const {
    ValidationResult check;
    if (initialCells() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("В посеве должно быть хотя бы одна клетка");
        check.hint = QStringLiteral("Задайте N₀ не меньше 1");
        return check;
    }
    if (doublingTime() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Период удвоения должен быть больше нуля");
        check.hint = QStringLiteral("Задайте T_d не меньше 0,25 ч или выберите вид из списка");
        return check;
    }
    if (observationTime() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Длительность наблюдения должна быть больше нуля");
        check.hint = QStringLiteral("Задайте T не меньше 1 ч");
        return check;
    }
    if (observationInterval() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Интервал учёта должен быть больше нуля");
        check.hint = QStringLiteral("Задайте Δt не меньше 0,25 ч");
        return check;
    }
    if (targetCells() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Целевое число клеток должно быть больше нуля");
        check.hint = QStringLiteral("Задайте N не меньше 1");
        return check;
    }
    if (!std::isfinite(growthRate()) || growthRate() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Постоянная роста не вычисляется: период удвоения задан неверно");
        check.hint = QStringLiteral("Уменьшите T_d");
        return check;
    }
    /*
     * Экспоненциальный закон формально работает всегда, но за разумные
     * пределы наблюдение уходит в числа, которые нельзя ни записать, ни
     * нанести на шкалу: 10^9 клеток — тот предел, при котором колония уже
     * давно легла бы сплошным слоем.
     */
    if (theoryCellsAt(observationTime()) > kMaxReadableCells) {
        check.ok = false;
        check.message = QStringLiteral("За %1 клеток стало бы %2: закон верен, но такое число не помещается "
                           "ни на шкалу, ни в чашку")
                .arg(durationText(observationTime()), cellsText(theoryCellsAt(observationTime())));
        check.hint = QStringLiteral("Сократите наблюдение, возьмите N₀ меньше или задайте T_d побольше");
        return check;
    }
    // Интервал учёта не должен быть крупнее периода удвоения слишком сильно:
    // иначе между двумя подсчётами проходит много делений, и столбцы прироста
    // перестают показывать ход опыта.
    if (observationInterval() > 4.0 * doublingTime()) {
        check.ok = false;
        check.message = QStringLiteral("Интервал учёта крупнее четырёх периодов удвоения: за один подсчёт "
                                       "проходит слишком много делений сразу");
        check.hint = QStringLiteral("Уменьшите Δt или задайте T_d побольше");
        return check;
    }
    return check;
}

// ------------------------------------------------------------------
// Виды из справочника
// ------------------------------------------------------------------

int BacteriaModel::speciesCount() { return int(std::size(kSpecies)) + 1; }

QString BacteriaModel::speciesName(int index) {
    if (index < 0 || index >= int(std::size(kSpecies)))
        return QStringLiteral("свой период удвоения");
    return QString::fromUtf8(kSpecies[size_t(index)].name);
}

double BacteriaModel::speciesDoubling(int index) {
    if (index < 0 || index >= int(std::size(kSpecies)))
        return 0.0;
    return kSpecies[size_t(index)].doublingHours;
}

double BacteriaModel::speciesObservation(int index) {
    const double life = speciesDoubling(index);
    if (life <= 0.0)
        return 0.0;
    // Наблюдение — шесть периодов удвоения: колония вырастает в 64 раза,
    // и деления видно и на чашке, и на шкале.
    return std::clamp(life * kDefaultDivisionCount, 1.0, 96.0);
}

int BacteriaModel::customSpeciesIndex() { return int(std::size(kSpecies)); }

QString BacteriaModel::speciesTitle() const {
    const int index = paramChoiceIndex(QStringLiteral("species"));
    if (index < 0 || index >= customSpeciesIndex())
        return QStringLiteral("свой период удвоения");
    return speciesName(index);
}

// ------------------------------------------------------------------
// Параметры в величинах процесса
// ------------------------------------------------------------------

double BacteriaModel::initialCells() const { return param(QStringLiteral("N0")); }

double BacteriaModel::doublingTime() const {
    const int index = paramChoiceIndex(QStringLiteral("species"));
    if (index >= 0 && index < customSpeciesIndex())
        return speciesDoubling(index);
    return param(QStringLiteral("doubling"));
}

double BacteriaModel::growthRate() const { return kLnTwo / doublingTime(); }

double BacteriaModel::observationTime() const {
    const int index = paramChoiceIndex(QStringLiteral("species"));
    if (index >= 0 && index < customSpeciesIndex())
        return speciesObservation(index);
    return param(QStringLiteral("T"));
}

double BacteriaModel::observationInterval() const { return param(QStringLiteral("step")); }

double BacteriaModel::targetCells() const { return param(QStringLiteral("target")); }

// ------------------------------------------------------------------
// Время показа
// ------------------------------------------------------------------

double BacteriaModel::duration() const { return observationTime() + kTailHours; }

double BacteriaModel::stepSize() const { return duration() / 600.0; }

double BacteriaModel::timePerSecond() const { return std::clamp(observationTime() / 10.0, 0.05, 24.0); }

double BacteriaModel::eventTime() const {
    const double moment = targetTime();
    if (!std::isfinite(moment) || moment > observationTime())
        return doublingTime();
    return moment;
}

// ------------------------------------------------------------------
// Закон роста
// ------------------------------------------------------------------

double BacteriaModel::theoryCellsAt(double t) const {
    if (!std::isfinite(t))
        return std::numeric_limits<double>::quiet_NaN();
    const double divisions = t / doublingTime();
    // 2^(t/T_d) считают через exp от ln2: так показательная степень остаётся
    // точной и при дробном числе делений.
    return initialCells() * std::exp(kLnTwo * divisions);
}

double BacteriaModel::theoryCellsByRate(double t) const {
    if (!std::isfinite(t))
        return std::numeric_limits<double>::quiet_NaN();
    return initialCells() * std::exp(growthRate() * t);
}

double BacteriaModel::divisionsElapsedAt(double t) const {
    if (!std::isfinite(t))
        return std::numeric_limits<double>::quiet_NaN();
    return t / doublingTime();
}

double BacteriaModel::completedDivisionsAt(double t) const {
    if (!std::isfinite(t))
        return std::numeric_limits<double>::quiet_NaN();
    return std::floor(divisionsElapsedAt(t));
}

double BacteriaModel::steppedCellsAt(double t) const {
    if (!std::isfinite(t))
        return std::numeric_limits<double>::quiet_NaN();
    return initialCells() * std::exp2(completedDivisionsAt(t));
}

double BacteriaModel::countedCellsAt(double t) const {
    const double theory = theoryCellsAt(t);
    if (!std::isfinite(theory))
        return theory;
    // Наблюдатель видит целое число клеток: дробная часть теории — это
    // усреднение по времени, а не половина клетки.
    return std::round(theory);
}

double BacteriaModel::growthTheoryAt(double t) const {
    const double cells = theoryCellsAt(t);
    return std::isfinite(cells) ? cells - initialCells() : cells;
}

bool BacteriaModel::countingIntervalBefore(double t, double& left, double& right) const {
    /*
     * Интервал учёта, завершившийся не позже момента t: [left, right], где
     * right — ближайшая отметка подсчёта. Берут именно его, а не интервал
     * «вперёд»: наблюдатель сравнивает два числа, которые он уже снял.
     */
    const double step = observationInterval();
    if (!(step > 0.0) || !std::isfinite(t))
        return false;
    const double node = std::floor(std::min(std::max(0.0, t), observationTime()) / step + 1e-9);
    right = node * step;
    left = std::max(0.0, right - step);
    return right > left;
}

double BacteriaModel::growthInLastStepAt(double t) const {
    double left = 0.0;
    double right = 0.0;
    if (!countingIntervalBefore(t, left, right)) {
        /*
         * Первого подсчёта ещё не было: наблюдатель видит исходный посев,
         * поэтому прирост нулевой, а не «неизвестный».
         */
        return 0.0;
    }
    const double now = countedCellsAt(right);
    const double previous = countedCellsAt(left);
    if (!std::isfinite(now) || !std::isfinite(previous))
        return std::numeric_limits<double>::quiet_NaN();
    return now - previous;
}

double BacteriaModel::growthRateOfCellsAt(double t) const {
    const double cells = theoryCellsAt(t);
    return std::isfinite(cells) ? growthRate() * cells : cells;
}

double BacteriaModel::averageGrowthAt(double t) const {
    /*
     * Средняя скорость на последнем завершённом интервале учёта: ΔN/Δt. По
     * ней судят о росте, когда клетки считают не непрерывно, а через шаг. Она
     * меньше мгновенной k·N в конце того же интервала: клетки делились по
     * весь интервал, и их число росло постепенно, а не сразу.
     */
    double left = 0.0;
    double right = 0.0;
    if (!countingIntervalBefore(t, left, right))
        return 0.0;   // интервала ещё нет — наблюдатель нечего усреднять
    const double now = theoryCellsAt(right);
    const double before = theoryCellsAt(left);
    if (!std::isfinite(now) || !std::isfinite(before))
        return std::numeric_limits<double>::quiet_NaN();
    return (now - before) / (right - left);
}

double BacteriaModel::multiplicationFactorAt(double t) const {
    const double cells = theoryCellsAt(t);
    return std::isfinite(cells) ? cells / initialCells() : cells;
}

double BacteriaModel::targetTime() const {
    /*
     * Время, за которое клеток станет N: из N = N₀·2^(t/T_d) логарифмированием
     * по основанию 2 получается t = T_d·log₂(N/N₀). Целевое число может быть
     * меньше стартового — тогда делений не нужно вовсе, и ответ 0 ч.
     */
    const double ratio = targetCells() / initialCells();
    if (!(ratio > 0.0) || !std::isfinite(ratio))
        return std::numeric_limits<double>::quiet_NaN();
    const double divisions = std::log2(ratio);
    if (divisions <= 0.0)
        return 0.0;
    return doublingTime() * divisions;
}

double BacteriaModel::divisionsToTarget() const {
    const double ratio = targetCells() / initialCells();
    if (!(ratio > 0.0) || !std::isfinite(ratio))
        return std::numeric_limits<double>::quiet_NaN();
    return std::max(0.0, std::log2(ratio));
}

double BacteriaModel::dishCapacity() const {
    /*
     * Вместимость поля зрения — число клеток, которые лежат в кадре слоем
     * в одну клетку. Посев покрывает площадь S = N·s₀, поэтому кадр
     * заполняется, когда N достигает N_мест = S_кадра/s₀: это фиксированное
     * число, не зависящее от посевного материала. Всё, что выше, колония
     * кладёт «вторым слоем»: чашка трескается от переполнения, а удвоение
     * идёт по-прежнему — рост задаёт T_d, а не теснота кадра.
     */
    return kDishCapacity;
}

double BacteriaModel::overflowFactorAt(double t) const {
    const double cells = theoryCellsAt(t);
    const double capacity = dishCapacity();
    if (!std::isfinite(cells) || !(capacity > 0.0))
        return std::numeric_limits<double>::quiet_NaN();
    return cells / capacity;
}

// ------------------------------------------------------------------
// Состояние сцены
// ------------------------------------------------------------------

SimState BacteriaModel::stateAt(double t) const {
    SimState state;
    const double theory = theoryCellsAt(t);
    const double counted = countedCellsAt(t);
    const double cells = std::isfinite(counted) ? counted : theory;
    const bool finished = t >= observationTime() - 1e-12;

    EntityVisual colony;
    colony.id = QStringLiteral("colony");
    colony.label = QStringLiteral("Колония");
    colony.position = cells;
    colony.value1 = theory;                                  // теория: N₀·2^(t/T_d)
    colony.value2 = std::isfinite(counted) ? counted : 0.0;  // подсчёт: целое число
    colony.active = true;
    colony.variant = 0;
    state.entities.push_back(colony);

    EntityVisual divisions;
    divisions.id = QStringLiteral("divisions");
    divisions.label = QStringLiteral("Деления");
    divisions.position = divisionsElapsedAt(t);
    divisions.value1 = completedDivisionsAt(t);              // завершённые деления
    divisions.value2 = growthRate();                         // k = ln2/T_d
    divisions.active = true;
    divisions.variant = 1;
    state.entities.push_back(divisions);

    EntityVisual dish;
    dish.id = QStringLiteral("dish");
    dish.label = QStringLiteral("Поле зрения чашки");
    dish.position = overflowFactorAt(t);                     // во сколько раз кадр переполнен
    dish.value1 = dishCapacity();
    dish.value2 = growthRateOfCellsAt(t);                    // N′ = kN
    dish.active = overflowFactorAt(t) > 1.0;
    dish.variant = 2;
    state.entities.push_back(dish);

    state.primary = cells;
    state.secondary = theory;
    state.resolved = finished;
    if (finished) {
        state.eventLabel = QStringLiteral("Конец наблюдения: %1 клеток, %2 делений, прирост %3 за интервал учёта")
                               .arg(cellsText(cells), utils::formatNumber(completedDivisionsAt(t), 0),
                                    cellsText(growthInLastStepAt(t)));
    }
    return state;
}

// ------------------------------------------------------------------
// Запись решения
// ------------------------------------------------------------------

Derivation BacteriaModel::derivation() const {
    using utils::formatNumber;
    Derivation d;
    const ValidationResult check = validate();
    d.valid = check.ok;
    d.problemText = problemText();
    d.unknown = QStringLiteral("постоянную роста k, число делений n, число клеток N(t), "
                               "время достижения заданного числа и скорость прироста");

    if (!check.ok) {
        d.steps.push_back({QStringLiteral("Внимание"), QStringLiteral("\\text{%1}").arg(check.message)});
        d.steps.push_back({QStringLiteral("Что сделать"), QStringLiteral("\\text{%1}").arg(check.hint)});
        d.answerTemplate = QStringLiteral("Расчёт невозможен: %1").arg(check.message);
        return d;
    }

    const double n0 = initialCells();
    const double life = doublingTime();
    const double k = growthRate();
    const double now = time();
    const double moment = targetTime();
    const double target = targetCells();
    const double step = observationInterval();

    QVector<DerivationStep>& st = d.steps;
    st.push_back({QStringLiteral("1. Исходные данные"),
                  QStringLiteral("N_{0} = %1 \\text{ клеток},\\quad T_{d} = %2,\\quad t = %3,\\quad "
                                 "N = %4 \\text{ клеток}")
                      .arg(formatNumber(n0, 0), durationText(life), durationText(now), formatNumber(target, 0))});

    st.push_back({QStringLiteral("2. Закон деления: число клеток через степень двойки"),
                  QStringLiteral("N = N_{0} \\cdot 2^{n},\\qquad n = \\frac{t}{T_{d}} \\quad \\Rightarrow \\quad "
                                 "N(t) = N_{0} \\cdot 2^{t/T_{d}}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("t = %1:\\quad n = \\frac{%1}{%2} = %3,\\qquad 2^{n} = %4,\\qquad N = %5 \\cdot %4 = "
                                 "%6 \\text{ клеток}")
                      .arg(durationText(now), durationText(life), formatNumber(divisionsElapsedAt(now), 4),
                           formatNumber(multiplicationFactorAt(now), 4), formatNumber(n0, 0),
                           cellsText(theoryCellsAt(now)))});

    st.push_back({QStringLiteral("3. Число делений из измеренного числа клеток"),
                  QStringLiteral("N = N_{0} \\cdot 2^{n} \\quad \\Rightarrow \\quad 2^{n} = \\frac{N}{N_{0}} "
                                 "\\quad \\Rightarrow \\quad n = \\log_{2}\\frac{N}{N_{0}} = "
                                 "\\frac{\\ln (N/N_{0})}{\\ln 2}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("N = %1:\\quad \\frac{N}{N_{0}} = %2,\\quad \\ln %2 = %3,\\quad n = "
                                 "\\frac{%3}{%4} = %5 \\text{ делений}")
                      .arg(formatNumber(target, 0), formatNumber(target / n0, 4),
                           formatNumber(std::log(target / n0), 4), formatNumber(kLnTwo, 4),
                           formatNumber(divisionsToTarget(), 4))});

    st.push_back({QStringLiteral("4. Постоянная скорости размножения"),
                  QStringLiteral("N = N_{0} e^{kt},\\qquad 2^{t/T_{d}} = e^{\\ln 2 \\cdot t/T_{d}} \\quad "
                                 "\\Rightarrow \\quad k = \\frac{\\ln 2}{T_{d}}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("k = \\frac{%3}{%1} = %2 \\text{ ч}^{-1}")
                      .arg(durationText(life), ratioText(k), formatNumber(kLnTwo, 3))});

    st.push_back({QStringLiteral("Почему обе записи равны"),
                  QStringLiteral("2^{t/T_{d}} = e^{\\ln\\left(2^{t/T_{d}}\\right)} = e^{\\frac{\\ln 2}{T_{d}} t} = "
                                 "e^{kt} \\quad \\Rightarrow \\quad N_{0} \\cdot 2^{t/T_{d}} = N_{0} e^{kt}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("t = %1:\\quad kt = %2 \\cdot %3 = %4,\\quad e^{kt} = %5,\\quad N_{0} e^{kt} = %6 "
                                 "\\text{ — то же число клеток}")
                      .arg(durationText(now), ratioText(k), durationText(now), ratioText(k * now),
                           formatNumber(multiplicationFactorAt(now), 4), cellsText(theoryCellsByRate(now)))});

    st.push_back({QStringLiteral("5. Искомое время: когда клеток станет N"),
                  QStringLiteral("N_{0} \\cdot 2^{t/T_{d}} = N \\quad \\Rightarrow \\quad t = T_{d} "
                                 "\\log_{2}\\frac{N}{N_{0}} = \\frac{\\ln (N/N_{0})}{k}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("t = \\frac{\\ln (%1/%2)}{%3} = \\frac{%4}{%3} = %5")
                      .arg(formatNumber(target, 0), formatNumber(n0, 0), ratioText(k),
                           formatNumber(std::log(target / n0), 4), durationText(moment))});

    {
        // Число клеток через целое число делений: его считают устно, и это
        // независимая проверка формулы с e^(kt).
        QString list;
        for (int n = 1; n <= 3; ++n) {
            if (n > 1)
                list += QStringLiteral(",\\qquad ");
            list += QStringLiteral("t = %1 T_{d} = %2: N = %3 \\cdot 2^{%1} = %4")
                        .arg(QString::number(n), durationText(life * n), formatNumber(n0, 0),
                             cellsText(theoryCellsAt(life * n)));
        }
        st.push_back({QStringLiteral("Подстановка"), list + QStringLiteral("\\text{ клеток}")});
    }

    st.push_back({QStringLiteral("6. Скорость прироста: она пропорциональна числу клеток"),
                  QStringLiteral("N(t) = N_{0} e^{kt} \\quad \\Rightarrow \\quad N'(t) = k N_{0} e^{kt} = k N(t)")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("N'(t) = %1 \\cdot %2 = %3 \\text{ клеток/ч},\\quad N'(0) = %1 \\cdot %4 = %5")
                      .arg(ratioText(k), cellsText(theoryCellsAt(now)),
                           formatNumber(growthRateOfCellsAt(now), 1), formatNumber(n0, 0),
                           formatNumber(growthRate(), 1))});

    st.push_back({QStringLiteral("7. Прирост за интервал учёта"),
                  QStringLiteral("N(t + \\Delta t) = N(t) \\cdot 2^{\\Delta t/T_{d}},\\qquad \\Delta N = N(t) "
                                 "\\left(2^{\\Delta t/T_{d}} - 1\\right)")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("\\frac{\\Delta t}{T_{d}} = %1,\\quad 2^{\\Delta t/T_{d}} = %2,\\quad \\Delta N = %3 "
                                 "\\cdot (%2 - 1) = %4 \\text{ клеток за } %5")
                      .arg(formatNumber(step / life, 4), formatNumber(std::exp2(step / life), 4),
                           cellsText(theoryCellsAt(now)), cellsText(theoryCellsAt(now) * (std::exp2(step / life) - 1.0)),
                           durationText(step))});

    st.push_back({QStringLiteral("8. Средняя скорость по наблюдениям меньше мгновенной"),
                  QStringLiteral("\\frac{\\Delta N}{\\Delta t} = \\frac{N(t + \\Delta t) - N(t)}{\\Delta t} < k N(t + "
                                 "\\Delta t) \\quad \\text{(число клеток за интервал выросло)}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("\\frac{\\Delta N}{\\Delta t} = %1 \\text{ клеток/ч},\\quad k N = %2 \\text{ "
                                 "клеток/ч}")
                      .arg(formatNumber(averageGrowthAt(now), 1), formatNumber(growthRateOfCellsAt(now), 1))});

    st.push_back({QStringLiteral("9. Проверка: T_d из двух наблюдений"),
                  QStringLiteral("T_{d} = \\frac{t \\ln 2}{\\ln (N/N_{0})}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("T_{d} = \\frac{%1 \\cdot %2}{%3} = %4")
                      .arg(durationText(now), formatNumber(kLnTwo, 4), formatNumber(std::log(multiplicationFactorAt(now)), 4),
                           durationText(now * kLnTwo / std::max(1e-12, std::log(multiplicationFactorAt(now)))))});

    {
        const double capacity = dishCapacity();
        st.push_back({QStringLiteral("10. Переполнение чашки: рост не останавливается"),
                      QStringLiteral("S = N s_{0} \\quad \\Rightarrow \\quad R = \\sqrt{\\frac{N s_{0}}{\\pi}} "
                                     "\\propto \\sqrt{N}")});
        st.push_back({QStringLiteral("Подстановка"),
                      QStringLiteral("N_{\\text{мест}} = %1:\\quad \\frac{N}{N_{\\text{мест}}} = %2 — при %3 "
                                     "клеток слой становится толще, чашка трескается, а удвоение идёт дальше")
                          .arg(cellsText(capacity), formatNumber(overflowFactorAt(now), 2), cellsText(theoryCellsAt(now)))});
    }

    const double cellsNow = theoryCellsAt(now);
    d.answerTemplate =
        QStringLiteral("k = %1 ч⁻¹; к моменту %2 прошло %3 делений, клеток %4 (наблюдатель насчитает %5); "
                       "до N = %6 осталось %7; скорость прироста N′ = kN = %8 клеток/ч")
            .arg(ratioText(k), durationText(now), formatNumber(divisionsElapsedAt(now), 2), cellsText(cellsNow),
                 cellsText(countedCellsAt(now)), formatNumber(target, 0), durationText(moment),
                 formatNumber(growthRateOfCellsAt(now), 1));
    return d;
}

QVector<Readout> BacteriaModel::readouts() const {
    QVector<Readout> list;
    const ValidationResult check = validate();
    if (!check.ok) {
        list.push_back({QStringLiteral("Постановка"), check.message, true});
        return list;
    }

    const double now = time();
    const double theory = theoryCellsAt(now);
    const double counted = countedCellsAt(now);
    const double capacity = dishCapacity();

    list.push_back({QStringLiteral("Прошло времени"), durationText(now)});
    list.push_back({QStringLiteral("Делений прошло n = t/T_d"), utils::formatNumber(divisionsElapsedAt(now), 3)});
    list.push_back({QStringLiteral("Завершённых делений ⌊t/T_d⌋"), utils::formatNumber(completedDivisionsAt(now), 0)});
    list.push_back({QStringLiteral("Клеток по теории N₀·2^(t/T_d)"), cellsText(theory), true});
    list.push_back({QStringLiteral("Клеток по подсчёту (целое)"), cellsText(counted)});
    list.push_back({QStringLiteral("Клеток ступенькой N₀·2^⌊t/T_d⌋"), cellsText(steppedCellsAt(now))});
    list.push_back({QStringLiteral("Во сколько раз выросло"), utils::formatNumber(multiplicationFactorAt(now), 3)});
    list.push_back({QStringLiteral("Постоянная роста k = ln2/T_d"), ratioText(growthRate()) + QStringLiteral(" ч⁻¹")});
    list.push_back({QStringLiteral("Прирост за интервал Δt"), cellsText(growthInLastStepAt(now))});
    list.push_back({QStringLiteral("Скорость прироста N′ = kN"),
                    utils::formatNumber(growthRateOfCellsAt(now), 1) + QStringLiteral(" клеток/ч"), true});
    list.push_back({QStringLiteral("Средняя скорость ΔN/Δt"),
                    utils::formatNumber(averageGrowthAt(now), 1) + QStringLiteral(" клеток/ч")});
    list.push_back({QStringLiteral("Радиус колонии R ∝ √N"),
                    QStringLiteral("%1 % радиуса поля зрения").arg(utils::formatNumber(100.0 * std::sqrt(std::max(0.0, overflowFactorAt(now))), 1))});
    list.push_back({QStringLiteral("Клеток умещается в кадре"), cellsText(capacity)});
    list.push_back({QStringLiteral("Переполнение чашки N/N_мест"), utils::formatNumber(overflowFactorAt(now), 2),
                    overflowFactorAt(now) > 1.0});
    list.push_back({QStringLiteral("Время до N = %1").arg(cellsText(targetCells())), durationText(targetTime()), true});
    list.push_back({QStringLiteral("Делений до цели log₂(N/N₀)"), utils::formatNumber(divisionsToTarget(), 3)});
    return list;
}

// ------------------------------------------------------------------
// Ряды данных
// ------------------------------------------------------------------

QVector<QPointF> BacteriaModel::theorySeries() const {
    QVector<QPointF> points;
    const double total = observationTime();
    points.reserve(kPlotPoints);
    for (int i = 0; i < kPlotPoints; ++i) {
        const double t = total * double(i) / double(kPlotPoints - 1);
        points.append(QPointF(t, theoryCellsAt(t)));
    }
    return points;
}

QVector<QPointF> BacteriaModel::steppedSeries() const {
    QVector<QPointF> points;
    const double total = observationTime();
    const double life = doublingTime();
    const int divisions = int(std::ceil(divisionsElapsedAt(total))) + 1;
    points.reserve(2 * divisions + 2);
    points.append(QPointF(0.0, steppedCellsAt(0.0)));
    for (int n = 1; n <= divisions; ++n) {
        const double t = life * double(n);
        if (t > total)
            break;
        // Ступенька: до деления клеток столько же, после — вдвое больше.
        points.append(QPointF(t, steppedCellsAt(std::nextafter(t, 0.0))));
        points.append(QPointF(t, steppedCellsAt(t)));
    }
    points.append(QPointF(total, steppedCellsAt(total)));
    return points;
}

QVector<QPointF> BacteriaModel::growthSeries() const {
    QVector<QPointF> points;
    const double total = observationTime();
    points.reserve(kPlotPoints);
    for (int i = 0; i < kPlotPoints; ++i) {
        const double t = total * double(i) / double(kPlotPoints - 1);
        points.append(QPointF(t, growthTheoryAt(t)));
    }
    return points;
}

QVector<QPointF> BacteriaModel::growthPerStepSeries() const {
    /*
     * Прирост клеток между двумя подсчётами: абсцисса — середина интервала
     * учёта, потому что клетки считали на его концах.
     */
    QVector<QPointF> points;
    const double step = observationInterval();
    const double total = observationTime();
    if (!(step > 0.0))
        return points;
    const int intervals = int(std::floor(total / step + 1e-9));
    points.reserve(intervals);
    for (int i = 1; i <= intervals; ++i) {
        const double right = step * double(i);
        const double left = step * double(i - 1);
        points.append(QPointF(0.5 * (left + right), countedCellsAt(right) - countedCellsAt(left)));
    }
    return points;
}

QVector<QPointF> BacteriaModel::instantaneousGrowthSeries() const {
    QVector<QPointF> points;
    const double total = observationTime();
    points.reserve(kPlotPoints);
    for (int i = 0; i < kPlotPoints; ++i) {
        const double t = total * double(i) / double(kPlotPoints - 1);
        points.append(QPointF(t, growthRateOfCellsAt(t)));
    }
    return points;
}

QVector<QPointF> BacteriaModel::averageGrowthSeries() const {
    QVector<QPointF> points;
    const double step = observationInterval();
    const double total = observationTime();
    if (!(step > 0.0))
        return points;
    const int intervals = int(std::floor(total / step + 1e-9));
    points.reserve(intervals + 1);
    for (int i = 1; i <= intervals; ++i) {
        const double right = step * double(i);
        const double left = step * double(i - 1);
        points.append(QPointF(0.5 * (left + right), (theoryCellsAt(right) - theoryCellsAt(left)) / step));
    }
    return points;
}

QVector<QPointF> BacteriaModel::divisionsSeries() const {
    QVector<QPointF> points;
    const double total = observationTime();
    points.reserve(kPlotPoints);
    for (int i = 0; i < kPlotPoints; ++i) {
        const double t = total * double(i) / double(kPlotPoints - 1);
        points.append(QPointF(t, divisionsElapsedAt(t)));
    }
    return points;
}

QVector<QPointF> BacteriaModel::logarithmSeries() const {
    QVector<QPointF> points;
    const double total = observationTime();
    points.reserve(kPlotPoints);
    for (int i = 0; i < kPlotPoints; ++i) {
        const double t = total * double(i) / double(kPlotPoints - 1);
        points.append(QPointF(t, std::log(theoryCellsAt(t))));
    }
    return points;
}

QVector<QPointF> BacteriaModel::growthVersusCells() const {
    /*
     * Скорость прироста против числа клеток: N′ = kN — прямая, проходящая
     * через начало координат. Её наклон и есть постоянная роста k.
     */
    QVector<QPointF> points;
    const double total = observationTime();
    points.reserve(kPlotPoints);
    for (int i = 0; i < kPlotPoints; ++i) {
        const double t = total * double(i) / double(kPlotPoints - 1);
        points.append(QPointF(theoryCellsAt(t), growthRateOfCellsAt(t)));
    }
    return points;
}

// ------------------------------------------------------------------
// Ручки
// ------------------------------------------------------------------

QVector<HandleInfo> BacteriaModel::handles() const {
    QVector<HandleInfo> list;

    HandleInfo cells;
    cells.id = QStringLiteral("n0");
    cells.label = QStringLiteral("Посев N₀");
    cells.unit = QStringLiteral("клеток");
    cells.minValue = 1.0;
    cells.maxValue = 100000.0;
    cells.step = 1.0;
    cells.axis = QStringLiteral("y");
    cells.value = initialCells();
    cells.readbackLabel = QStringLiteral("Клеток в посеве");
    cells.readbackValue = cellsText(initialCells());
    list.push_back(cells);

    HandleInfo life;
    life.id = QStringLiteral("doubling");
    life.label = QStringLiteral("Период удвоения T_d");
    life.unit = QStringLiteral("ч");
    life.minValue = 0.25;
    life.maxValue = 24.0;
    life.step = 0.25;
    life.axis = QStringLiteral("x");
    life.value = std::clamp(doublingTime(), 0.25, 24.0);
    life.readbackLabel = QStringLiteral("Период удвоения");
    life.readbackValue = durationText(doublingTime());
    list.push_back(life);

    HandleInfo window;
    window.id = QStringLiteral("window");
    window.label = QStringLiteral("Конец наблюдения T");
    window.unit = QStringLiteral("ч");
    window.minValue = 1.0;
    window.maxValue = 96.0;
    window.step = 1.0;
    window.axis = QStringLiteral("x");
    window.value = observationTime();
    window.readbackLabel = QStringLiteral("Длительность наблюдения");
    window.readbackValue = durationText(observationTime());
    list.push_back(window);

    HandleInfo target;
    target.id = QStringLiteral("target");
    target.label = QStringLiteral("Целевое число клеток N");
    target.unit = QStringLiteral("клеток");
    target.minValue = 1.0;
    target.maxValue = 100000000.0;
    target.step = 1.0;
    target.axis = QStringLiteral("y");
    target.value = targetCells();
    target.readbackLabel = QStringLiteral("Клеток станет");
    target.readbackValue = cellsText(targetCells());
    list.push_back(target);
    return list;
}

void BacteriaModel::setHandleValue(const QString& id, double value) {
    if (id == QLatin1String("doubling")) {
        /*
         * Для вида из справочника период удвоения взят из данных, поэтому
         * перетаскивание переводит постановку на свой период, сохраняя окно
         * наблюдения: опыт остаётся тем же, меняется только T_d.
         */
        const int index = paramChoiceIndex(QStringLiteral("species"));
        if (index >= 0 && index < customSpeciesIndex()) {
            // Для вида из справочника T_d взят из данных, поэтому
            // перетаскивание переводит постановку на свой период, сохраняя
            // окно наблюдения: опыт остаётся тем же, меняется только T_d.
            setParamRaw(QStringLiteral("T"), observationTime());
            setParamRaw(QStringLiteral("species"), double(customSpeciesIndex()));
        }
        setParam(QStringLiteral("doubling"), value);
        return;
    }
    if (id == QLatin1String("window")) {
        const int index = paramChoiceIndex(QStringLiteral("species"));
        if (index >= 0 && index < customSpeciesIndex()) {
            setParamRaw(QStringLiteral("doubling"), doublingTime());
            setParamRaw(QStringLiteral("species"), double(customSpeciesIndex()));
        }
        setParam(QStringLiteral("T"), value);
        return;
    }
    if (id == QLatin1String("n0")) {
        // Посев — свободный параметр даже для вида из справочника: число
        // клеток вносят в чашку независимо от того, какой микроорганизм
        // взяли для опыта.
        setParam(QStringLiteral("N0"), value);
        return;
    }
    if (id == QLatin1String("target")) {
        setParam(QStringLiteral("target"), value);
        return;
    }
    Simulation::setHandleValue(id, value);
}

void BacteriaModel::beginInteraction() {
    if (runState() == RunState::Running)
        pause();
}

}  // namespace lab::models
