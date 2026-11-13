#include "models/DecayModel.h"

#include <algorithm>
#include <cmath>

#include "core/Trajectory.h"
#include "utils/MathText.h"

namespace lab::models {
namespace {

/** Логарифм двух: он связывает период полураспада и постоянную распада. */
constexpr double kLnTwo = 0.6931471805599453;
/** Доля ядер, которая остаётся после одного периода полураспада. */
constexpr double kHalfFraction = 0.5;
/** Запас времени после конца наблюдения: сцена показывает итог. */
constexpr double kTailHours = 0.6;
/** Семя генератора по умолчанию: опыт воспроизводится до последнего ядра. */
constexpr quint64 kBaseSeed = 0x5DEECE66DULL;
/** Максимум наблюдения для изотопов со справочным периодом, ч. */
constexpr double kMaxListedObservationHours = 72.0;

/**
 * Справочные периоды полураспада в часах: шкала времени модели — часы,
 * поэтому переводить единицы в подстановках не приходится.
 */
struct Isotope {
    const char* name;
    double halfLifeHours;
};

const Isotope kIsotopes[] = {
    {"полоний-218 (3,1 мин)", 3.1 / 60.0},
    {"натрий-24 (15 ч)", 15.0},
    {"радон-222 (3,82 сут)", 91.8},
    {"йод-131 (8,02 сут)", 192.5},
    {"кобальт-60 (5,27 года)", 46217.0},
    {"углерод-14 (5730 лет)", 5.026e7},
    {"калий-40 (1,25·10⁹ лет)", 1.095e10},
};

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

QString DecayModel::durationText(double hours) {
    if (!std::isfinite(hours))
        return QStringLiteral("—");
    if (hours <= 0.0)
        return QStringLiteral("0 ч");
    if (hours < 1.0)
        return utils::formatNumber(hours * 60.0, 1) + QStringLiteral(" мин");
    const double whole = std::floor(hours);
    const double minutes = std::round((hours - whole) * 60.0);
    if (minutes < 0.5)
        return utils::formatNumber(whole, 0) + QStringLiteral(" ч");
    if (minutes > 59.5)
        return utils::formatNumber(whole + 1.0, 0) + QStringLiteral(" ч");
    return utils::formatNumber(whole, 0) + QStringLiteral(" ч ") + utils::formatNumber(minutes, 0) +
           QStringLiteral(" мин");
}

/** Целое число ядер в подписях: дробная часть здесь не нужна. */
QString DecayModel::nucleiText(double count) {
    if (!std::isfinite(count))
        return QStringLiteral("—");
    return utils::formatNumber(std::round(count), 0);
}

namespace {

/**
 * Малые безразмерные величины (λ, Δp, среднее число распадов за шаг) в
 * подстановках. Для долгоживущих изотопов постоянная распада порядка
 * 10⁻⁸ ч⁻¹, и обычное округление до шести знаков показало бы просто ноль,
 * поэтому число знаков подбирают по порядку величины.
 */
QString ratioText(double value) {
    if (!std::isfinite(value))
        return QStringLiteral("—");
    const double magnitude = std::abs(value);
    if (magnitude == 0.0)
        return QStringLiteral("0");
    if (magnitude >= 1e-3)
        return utils::formatNumber(value, 6);
    const int decimals = std::min(12, int(std::ceil(-std::log10(magnitude))) + 5);
    return utils::formatNumber(value, decimals);
}

}  // namespace

DecayModel::DecayModel(QObject* parent) : Simulation(parent) { seedDefaults(); }

// ------------------------------------------------------------------
// Справочник изотопов
// ------------------------------------------------------------------

int DecayModel::customIsotopeIndex() { return int(std::size(kIsotopes)); }

int DecayModel::isotopeCount() { return customIsotopeIndex() + 1; }

QString DecayModel::isotopeName(int index) {
    if (index < 0 || index >= customIsotopeIndex())
        return QStringLiteral("свой период полураспада");
    return QStringLiteral("%1").arg(QString::fromUtf8(kIsotopes[index].name));
}

double DecayModel::isotopeHalfLife(int index) {
    if (index < 0 || index >= customIsotopeIndex())
        return 0.0;
    return kIsotopes[index].halfLifeHours;
}

double DecayModel::isotopeObservation(int index) {
    // Наблюдение берут равным десяти периодам полураспада: за это время
    // ядер остаётся около 0,1 % — видно и быстрое начало, и почти полное
    // затухание. Для долгоживущих изотопов наблюдение ограничивают трое
    // суток: за него успевает прореагировать малая часть ядер, и счётчик
    // показывает почти constant число — это тоже физический факт.
    const double life = isotopeHalfLife(index);
    if (life <= 0.0)
        return 40.0;
    return std::clamp(life * 10.0, 1.0, kMaxListedObservationHours);
}

// ------------------------------------------------------------------
// Текст постановки
// ------------------------------------------------------------------

QString DecayModel::title() const { return QStringLiteral("Радиоактивный распад ядер"); }

QString DecayModel::subtitle() const {
    return QStringLiteral("N = N₀·2^(−t/T½), λ = ln2/T½, A = λN; счёт против теории");
}

QString DecayModel::statement() const {
    return QStringLiteral(
        "Образец содержит N₀ радиоактивных ядер с периодом полураспада T½. Распад каждого ядра "
        "случаен, но для большого числа ядер закономерен: за малый промежуток Δt ядро распадается "
        "с вероятностью λ·Δt. Найти постоянную распада, число ядер в заданный момент, активность "
        "образца и сравнить теоретическую кривую с целым числом ядер, которое показывает счётчик.");
}

QString DecayModel::problemText() const {
    return QStringLiteral(
               "В образце %1 радиоактивных ядер («%2»), период полураспада — %3. Наблюдение длится %4. "
               "Найти постоянную распада, через сколько времени останется заданная доля %5 от начального "
               "числа ядер, активность образца и показания счётчика в этот момент.")
        .arg(utils::formatNumber(initialNuclei(), 0), isotopeTitle(), durationText(halfLife()),
             durationText(observationTime()),
             utils::formatNumber(targetFraction() * 100.0, 0) + QStringLiteral(" %"));
}

// ------------------------------------------------------------------
// Параметры
// ------------------------------------------------------------------

QVector<ParamSpec> DecayModel::paramSpecs() const {
    QVector<ParamSpec> specs;
    const QString sample = QStringLiteral("Образец");
    const QString process = QStringLiteral("Наблюдение");

    QStringList isotopes;
    for (int index = 0; index < customIsotopeIndex(); ++index)
        isotopes.push_back(isotopeName(index));
    isotopes.push_back(QStringLiteral("свой период полураспада — задать T½"));
    // По умолчанию — свой период полураспада: с ним наблюдение и шкала
    // опыта согласованы (T½ = 8 ч, T = 40 ч), и на схеме видно несколько
    // периодов полураспада. Справочные изотопы задают и T½, и длительность
    // наблюдения сами.
    specs.push_back(choice(QStringLiteral("isotope"), QStringLiteral("Изотоп"), isotopes, sample,
                           QStringLiteral("Период полураспада определяет постоянную распада "
                                          "λ = ln2/T½; свой пункт задаёт T½ вручную"),
                           customIsotopeIndex()));

    specs.push_back(slider(QStringLiteral("half"), QStringLiteral("Период полураспада T½"), QStringLiteral("ч"),
                           0.5, 240.0, 0.5, 1, false, 8.0, sample,
                           QStringLiteral("Время, за которое распадается ровно половина ядер. "
                                          "Можно менять, перетаскивая отметку T½ на схеме")));
    specs.push_back(slider(QStringLiteral("N0"), QStringLiteral("Начальное число ядер N₀"), QStringLiteral("ядер"),
                           50.0, 5000.0, 10.0, 0, false, 1000.0, sample,
                           QStringLiteral("Чем больше ядер, тем ближе показания счётчика к теории: "
                                          "разброс порядка √N")));

    specs.push_back(slider(QStringLiteral("T"), QStringLiteral("Длительность наблюдения T"), QStringLiteral("ч"),
                           1.0, 240.0, 1.0, 0, false, 40.0, process,
                           QStringLiteral("За это время число ядер уменьшается в 2^(T/T½) раз")));
    specs.push_back(slider(QStringLiteral("share"), QStringLiteral("Заданная доля нераспавшихся ядер d"),
                           QStringLiteral("%"), 1.0, 99.0, 1.0, 0, false, 25.0, process,
                           QStringLiteral("По ней находится момент t = ln(1/d)/λ, когда останется d % ядер")));
    specs.push_back(slider(QStringLiteral("seed"), QStringLiteral("Семя счётчика случайных чисел"), QString(), 1.0,
                           999.0, 1.0, 0, false, 7.0, process,
                           QStringLiteral("Опыт с тем же семенем идёт точно так же; меняя семя, видно, как "
                                          "показания счётчика колеблются около теоретической кривой")));
    return specs;
}

ValidationResult DecayModel::validate() const {
    ValidationResult check;
    if (initialNuclei() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("В образце должно быть хотя бы одно ядро");
        check.hint = QStringLiteral("Задайте N₀ не меньше 50");
        return check;
    }
    if (halfLife() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Период полураспада должен быть больше нуля");
        check.hint = QStringLiteral("Задайте T½ не меньше 0,5 ч или выберите изотоп из списка");
        return check;
    }
    if (observationTime() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Длительность наблюдения должна быть больше нуля");
        check.hint = QStringLiteral("Задайте T не меньше 1 ч");
        return check;
    }
    if (targetFraction() <= 0.0 || targetFraction() >= 1.0) {
        check.ok = false;
        check.message = QStringLiteral("Заданная доля нераспавшихся ядер должна лежать между 0 % и 100 %");
        check.hint = QStringLiteral("Возьмите долю от 1 % до 99 %");
        return check;
    }
    if (!std::isfinite(decayConstant()) || decayConstant() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Постоянная распада не вычисляется: период полураспада задан неверно");
        check.hint = QStringLiteral("Увеличьте T½");
        return check;
    }
    // Вероятность распада за один шаг опыта не должна быть большой: иначе
    // наблюдение идёт слишком крупными кусками и ступенька счёта перестает
    // походить на плавный закон.
    if (stepProbability() > 0.25) {
        check.ok = false;
        check.message = QStringLiteral("Шаг наблюдения слишком крупный для такого периода полураспада: "
                                       "за один шаг распадается заметная доля оставшихся ядер");
        check.hint = QStringLiteral("Увеличьте длительность наблюдения или задайте T½ побольше");
        return check;
    }
    return check;
}

// ------------------------------------------------------------------
// Параметры в величинах процесса
// ------------------------------------------------------------------

QString DecayModel::isotopeTitle() const {
    const int index = paramChoiceIndex(QStringLiteral("isotope"));
    if (index < 0 || index >= customIsotopeIndex())
        return QStringLiteral("свой период полураспада");
    return isotopeName(index);
}

double DecayModel::initialNuclei() const { return param(QStringLiteral("N0")); }

double DecayModel::halfLife() const {
    const int index = paramChoiceIndex(QStringLiteral("isotope"));
    if (index >= 0 && index < customIsotopeIndex())
        return isotopeHalfLife(index);
    return param(QStringLiteral("half"));
}

double DecayModel::decayConstant() const { return kLnTwo / halfLife(); }

double DecayModel::meanLifetime() const { return 1.0 / decayConstant(); }

QString DecayModel::decayConstantText() const { return ratioText(decayConstant()); }

QString DecayModel::stepProbabilityText() const { return ratioText(stepProbability()); }

double DecayModel::observationTime() const {
    // Для изотопа из списка наблюдение задаёт справочник: иначе для
    // минутного или тысячелетнего периода полураспада окно наблюдения
    // оказалось бы бессмысленным. Свою длительность задают, когда T½
    // задан вручную.
    const int index = paramChoiceIndex(QStringLiteral("isotope"));
    if (index >= 0 && index < customIsotopeIndex())
        return isotopeObservation(index);
    return param(QStringLiteral("T"));
}

double DecayModel::targetFraction() const { return param(QStringLiteral("share")) / 100.0; }

double DecayModel::targetTime() const {
    // d = e^(−λt) ⇒ t = ln(1/d)/λ.
    const double fraction = targetFraction();
    if (fraction <= 0.0 || fraction >= 1.0)
        return std::numeric_limits<double>::quiet_NaN();
    return std::log(1.0 / fraction) / decayConstant();
}

int DecayModel::sampleNodes() const {
    /*
     * Шаг опыта подбирается так, чтобы вероятность распада одного ядра за
     * шаг осталась малой (Δp ≈ λΔt ≪ 1) и чтобы за шаг в среднем распадалось
     * не меньше одного ядра — иначе счётчик молчал бы целыми шагами.
     * Границы 301…3001 удерживают стоимость опыта постоянной.
     */
    const double life = halfLife();
    if (life <= 0.0 || !std::isfinite(life))
        return kMinSampleNodes;
    // Nodes, at which Δp stays under the cap: Δt = cap·T½/ln2.
    const double capNodes = observationTime() * kLnTwo / (kStepProbabilityCap * life);
    // Nodes, at which the counter still sees decays: всего распадов за
    // наблюдение делится на число шагов.
    const double totalDecays = std::max(1.0, initialNuclei() * (1.0 - survivingFractionAt(observationTime())));
    const double activeNodes = totalDecays;
    const double wanted = std::min(capNodes, activeNodes);
    return std::clamp(int(std::llround(wanted / 10.0) * 10 + 1), kMinSampleNodes, kMaxSampleNodes);
}

double DecayModel::sampleInterval() const {
    const int nodes = sampleNodes();
    return nodes > 1 ? observationTime() / double(nodes - 1) : observationTime();
}

double DecayModel::stepProbability() const {
    // p = 1 − e^(−λΔt): вероятность распада ядра именно за этот шаг. При
    // малом λΔt она равна λΔt — той самой Δp = λΔt из условия задачи.
    return 1.0 - std::exp(-decayConstant() * sampleInterval());
}

double DecayModel::expectedDecaysPerStepAt(double t) const {
    return stepProbability() * theoryNucleiAt(t);
}

// ------------------------------------------------------------------
// Теоретический закон
// ------------------------------------------------------------------

double DecayModel::survivingFractionAt(double t) const { return std::exp(-decayConstant() * std::max(0.0, t)); }

double DecayModel::theoryNucleiAt(double t) const { return initialNuclei() * survivingFractionAt(t); }

double DecayModel::theoryNucleiByHalfLife(double t) const {
    // Та же величина, записанная через период полураспада: e^(−λt) = 2^(−t/T½).
    return initialNuclei() * std::pow(kHalfFraction, std::max(0.0, t) / halfLife());
}

double DecayModel::decayedTheoryAt(double t) const { return initialNuclei() - theoryNucleiAt(t); }

double DecayModel::halfLivesElapsedAt(double t) const { return std::max(0.0, t) / halfLife(); }

double DecayModel::activityAt(double t) const { return decayConstant() * theoryNucleiAt(t); }

double DecayModel::expectedDeviationAt(double t) const { return std::sqrt(std::max(0.0, theoryNucleiAt(t))); }

// ------------------------------------------------------------------
// Дискретный опыт: перебор ядер с фиксированным семенем
// ------------------------------------------------------------------

quint64 DecayModel::cacheKey() const {
    // Опыт зависит только от этих величин: если они не менялись, ряд
    // наблюдений переиспользуется, и перематывать время можно бесплатно.
    auto quantize = [](double value) { return quint64(std::llround(value * 1e6)); };
    quint64 key = kBaseSeed;
    key = key * 1000003ULL + quantize(initialNuclei());
    key = key * 1000003ULL + quantize(halfLife());
    key = key * 1000003ULL + quantize(observationTime());
    key = key * 1000003ULL + quantize(param(QStringLiteral("seed")));
    return key;
}

void DecayModel::ensureSamples() const {
    const quint64 key = cacheKey();
    const int nodes = sampleNodes();
    if (m_cacheKey == key && m_alive.size() == nodes)
        return;

    m_timeNodes.clear();
    m_alive.clear();
    m_stepDecays.clear();
    if (!validate().ok) {
        m_cacheKey = 0;
        return;
    }

    const double step = sampleInterval();
    const double probability = stepProbability();
    // Семя задаёт пользователь: с ним опыт полностью воспроизводим, и те же
    // параметры дают те же показания счётчика.
    const quint64 seed = kBaseSeed + quint64(std::llround(param(QStringLiteral("seed")))) * 0x1000003ULL;
    SeededRandom random(seed == 0 ? kBaseSeed : seed);

    double alive = initialNuclei();
    for (int node = 0; node < nodes; ++node) {
        m_timeNodes.push_back(step * node);
        if (node == 0) {
            m_alive.push_back(alive);
            m_stepDecays.push_back(0.0);
            continue;
        }
        // Каждое уцелевшее ядро проверяется независимо: шанс распада за шаг
        // одинаков для всех, поэтому число распадов подчиняется биномиальному
        // распределению со средним λΔt·N и разбросом √(λΔt·N).
        int decayed = 0;
        const int candidates = int(std::llround(alive));
        for (int index = 0; index < candidates; ++index) {
            if (random.next() < probability)
                ++decayed;
        }
        alive = double(candidates - decayed);
        m_alive.push_back(alive);
        m_stepDecays.push_back(double(decayed));
    }
    m_cacheKey = key;
}

int DecayModel::nodeIndexBefore(double t) const {
    if (m_timeNodes.isEmpty())
        return 0;
    const double step = sampleInterval();
    if (step <= 0.0)
        return 0;
    const double clamped = std::clamp(t, 0.0, observationTime());
    /*
     * Узел, которому принадлежит момент. Частное округляют с малым допуском:
     * (Δt·n)/Δt в арифметике с плавающей точкой иногда даёт n − ε, и у
     * последнего узла счётчик отстал бы на шаг.
     */
    const int node = int(std::floor(clamped / step + 1e-9));
    return std::clamp(node, 0, int(m_timeNodes.size()) - 1);
}

double DecayModel::countedNucleiAt(double t) const {
    ensureSamples();
    if (m_alive.isEmpty())
        return std::numeric_limits<double>::quiet_NaN();
    return m_alive.at(nodeIndexBefore(t));
}

double DecayModel::countedDecaysAt(double t) const {
    const double alive = countedNucleiAt(t);
    if (!std::isfinite(alive))
        return std::numeric_limits<double>::quiet_NaN();
    return initialNuclei() - alive;
}

double DecayModel::decaysInLastStepAt(double t) const {
    ensureSamples();
    if (m_stepDecays.isEmpty())
        return std::numeric_limits<double>::quiet_NaN();
    const int node = nodeIndexBefore(t);
    if (node <= 0)
        return 0.0;
    return m_stepDecays.at(node);
}

double DecayModel::countedActivityAt(double t) const {
    const double decays = decaysInLastStepAt(t);
    if (!std::isfinite(decays))
        return std::numeric_limits<double>::quiet_NaN();
    // Активность — число распадов в единицу времени: распады шага делим на
    // его длительность.
    const double step = sampleInterval();
    return step > 0.0 ? decays / step : 0.0;
}

double DecayModel::relativeDeviationAt(double t) const {
    const double counted = countedNucleiAt(t);
    const double theory = theoryNucleiAt(t);
    if (!std::isfinite(counted) || initialNuclei() <= 0.0)
        return std::numeric_limits<double>::quiet_NaN();
    return 100.0 * (counted - theory) / initialNuclei();
}

// ------------------------------------------------------------------
// Время наблюдения
// ------------------------------------------------------------------

double DecayModel::duration() const { return observationTime() + kTailHours; }

double DecayModel::stepSize() const { return duration() / 600.0; }

double DecayModel::timePerSecond() const { return std::clamp(observationTime() / 10.0, 0.05, 24.0); }

double DecayModel::eventTime() const {
    const double moment = targetTime();
    if (!std::isfinite(moment) || moment > observationTime())
        return halfLife();
    return moment;
}

// ------------------------------------------------------------------
// Состояние системы
// ------------------------------------------------------------------

SimState DecayModel::stateAt(double t) const {
    SimState state;
    const double theory = theoryNucleiAt(t);
    const double counted = countedNucleiAt(t);
    const double alive = std::isfinite(counted) ? counted : theory;
    const bool finished = t >= observationTime() - 1e-12;

    EntityVisual nuclei;
    nuclei.id = QStringLiteral("nuclei");
    nuclei.label = QStringLiteral("Нераспавшиеся ядра");
    nuclei.position = alive;
    nuclei.value1 = theory;                              // теория: N₀·2^(−t/T½)
    nuclei.value2 = std::isfinite(counted) ? counted : 0.0;   // счётчик: целое число
    nuclei.active = true;
    nuclei.variant = 0;
    state.entities.push_back(nuclei);

    EntityVisual daughters;
    daughters.id = QStringLiteral("daughters");
    daughters.label = QStringLiteral("Дочерние ядра");
    daughters.position = initialNuclei() - alive;
    daughters.value1 = decayedTheoryAt(t);
    daughters.value2 = std::isfinite(counted) ? initialNuclei() - counted : 0.0;
    daughters.active = true;
    daughters.variant = 1;
    state.entities.push_back(daughters);

    EntityVisual counter;
    counter.id = QStringLiteral("counter");
    counter.label = QStringLiteral("Счётчик распадов");
    counter.position = alive;
    counter.value1 = activityAt(t);                      // активность по теории
    counter.value2 = decaysInLastStepAt(t);              // распады за последний шаг
    counter.active = true;
    counter.variant = 2;
    state.entities.push_back(counter);

    state.primary = alive;
    state.secondary = theory;
    state.resolved = finished;
    if (finished) {
        state.eventLabel = QStringLiteral("Опыт завершён: осталось %1 ядер, распалось %2")
                               .arg(DecayModel::nucleiText(alive), DecayModel::nucleiText(initialNuclei() - alive));
    }
    return state;
}

// ------------------------------------------------------------------
// Решение
// ------------------------------------------------------------------

Derivation DecayModel::derivation() const {
    using utils::formatNumber;
    Derivation d;
    const ValidationResult check = validate();
    d.valid = check.ok;
    d.problemText = problemText();
    d.unknown = QStringLiteral("постоянную распада λ, время, когда останется заданная доля ядер, "
                               "активность и показания счётчика");

    if (!check.ok) {
        d.steps.push_back({QStringLiteral("Внимание"), QStringLiteral("\\text{%1}").arg(check.message)});
        d.steps.push_back({QStringLiteral("Что сделать"), QStringLiteral("\\text{%1}").arg(check.hint)});
        d.answerTemplate = QStringLiteral("Расчёт невозможен: %1").arg(check.message);
        return d;
    }

    const double n0 = initialNuclei();
    const double life = halfLife();
    const double lambda = decayConstant();
    const double now = time();
    const double moment = targetTime();
    const double fraction = targetFraction();

    QVector<DerivationStep>& st = d.steps;
    st.push_back({QStringLiteral("1. Исходные данные"),
                  QStringLiteral("N_{0} = %1 \\text{ ядер},\\quad T_{1/2} = %2,\\quad d = %3 \\text{ от начального "
                                 "числа}")
                      .arg(formatNumber(n0, 0), durationText(life), formatNumber(fraction, 3))});

    st.push_back({QStringLiteral("2. Постоянная распада из периода полураспада"),
                  QStringLiteral("N(T_{1/2}) = \\frac{N_{0}}{2} \\quad \\Rightarrow \\quad e^{-\\lambda T_{1/2}} = "
                                 "\\frac{1}{2} \\quad \\Rightarrow \\quad \\lambda = \\frac{\\ln 2}{T_{1/2}}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("\\lambda = \\frac{%3}{%1} = %2 \\text{ ч}^{-1}")
                      .arg(durationText(life), ratioText(lambda), formatNumber(kLnTwo, 3))});

    st.push_back({QStringLiteral("3. Закон распада"),
                  QStringLiteral("N(t) = N_{0} e^{-\\lambda t} = N_{0} \\cdot 2^{-t/T_{1/2}},\\quad "
                                 "\\frac{N(t)}{N_{0}} = e^{-\\lambda t}")});
    st.push_back({QStringLiteral("Почему обе записи равны"),
                  QStringLiteral("-\\lambda t = -\\frac{\\ln 2}{T_{1/2}} t = \\ln 2 \\cdot "
                                 "\\left(-\\frac{t}{T_{1/2}}\\right) \\quad \\Rightarrow \\quad e^{-\\lambda t} = "
                                 "2^{-t/T_{1/2}}")});

    st.push_back({QStringLiteral("4. Искомый момент: когда останется доля d"),
                  QStringLiteral("e^{-\\lambda t} = d \\quad \\Rightarrow \\quad t = \\frac{\\ln (1/d)}{\\lambda}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("t = \\frac{\\ln (1/%1)}{%2} = \\frac{%3}{%4} = %5")
                      .arg(formatNumber(fraction, 3), ratioText(lambda), formatNumber(std::log(1.0 / fraction), 4),
                           ratioText(lambda), durationText(moment))});

    {
        // Число ядер через целое число периодов полураспада: его считают
        // устно, и это независимая проверка формулы с e^(−λt).
        QString list;
        for (int n = 1; n <= 3; ++n) {
            if (n > 1)
                list += QStringLiteral(",\\qquad ");
            list += QStringLiteral("t = %1 T_{1/2} = %2: N = %3 \\cdot (1/2)^{%1} = %4 \\text{ ядер}")
                        .arg(QString::number(n), durationText(life * n), formatNumber(n0, 0),
                             DecayModel::nucleiText(theoryNucleiAt(life * n)));
        }
        st.push_back({QStringLiteral("Подстановка"), list});
    }

    st.push_back({QStringLiteral("5. Проверка: та же доля двумя способами"),
                  QStringLiteral("\\frac{N(t)}{N_{0}} = 2^{-t/T_{1/2}} = e^{-\\lambda t}")});
    {
        const double ratio = survivingFractionAt(now);
        // Показатель степени — число безразмерное: время стоит и в t, и в T½,
        // поэтому подписывать его единицами не нужно.
        st.push_back({QStringLiteral("Подстановка"),
                      QStringLiteral("t = %1: \\quad \\frac{t}{T_{1/2}} = %2, \\quad 2^{-t/T_{1/2}} = 2^{-%2} = %3,\\quad "
                                     "\\lambda t = %4 \\cdot %2 \\cdot T_{1/2} = %5, \\quad e^{-\\lambda t} = %3")
                          .arg(durationText(now), formatNumber(now / life, 3), formatNumber(ratio, 5),
                               ratioText(lambda), ratioText(lambda * now))});
    }

    st.push_back({QStringLiteral("6. Среднее время жизни"),
                  QStringLiteral("\\tau = \\frac{1}{\\lambda} = \\frac{T_{1/2}}{\\ln 2} = %1 \\cdot T_{1/2}")
                      .arg(formatNumber(1.0 / kLnTwo, 3))});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("\\tau = \\frac{1}{%1} = %2 \\text{ — ядро живёт в среднем в 1,443 раза "
                                 "дольше, чем } T_{1/2}")
                      .arg(ratioText(lambda), durationText(meanLifetime()))});

    st.push_back({QStringLiteral("7. Активность образца"),
                  QStringLiteral("A = \\lambda N,\\qquad A(t) = A_{0} e^{-\\lambda t}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("A_{0} = %1 \\cdot %2 = %3 \\text{ распадов/ч},\\quad A(t) = %1 \\cdot %4 = %5 "
                                 "\\text{ распадов/ч}")
                      .arg(ratioText(lambda), formatNumber(n0, 0), formatNumber(activityAt(0.0), 1),
                           DecayModel::nucleiText(theoryNucleiAt(now)), formatNumber(activityAt(now), 1))});
    st.push_back({QStringLiteral("Смысл активности"),
                  QStringLiteral("A_{0}/N_{0} = \\lambda \\text{ — в среднем за час распадается доля } %1 "
                                 "\\text{ от оставшихся ядер}")
                      .arg(ratioText(lambda))});

    st.push_back({QStringLiteral("8. Как считает счётчик"),
                  QStringLiteral("\\Delta p = \\lambda \\Delta t,\\qquad \\Delta N_{\\text{ср}} = \\lambda "
                                 "\\Delta t \\cdot N,\\qquad \\sigma = \\sqrt{N}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("\\Delta t = \\frac{T}{n} = \\frac{%7}{%1} = %2,\\quad \\Delta p = %3 \\cdot %2 = %4,"
                                 "\\quad \\Delta N_{\\text{ср}} = %4 \\cdot %5 = %6 \\text{ ядер за шаг}")
                      .arg(QString::number(sampleNodes() - 1), formatNumber(sampleInterval(), 4), ratioText(lambda),
                           ratioText(stepProbability()), DecayModel::nucleiText(theoryNucleiAt(now)),
                           ratioText(expectedDecaysPerStepAt(now)), durationText(observationTime()))});
    st.push_back({QStringLiteral("Оценка разброса"),
                  QStringLiteral("\\sigma = \\sqrt{N} = %1 \\text{ ядер},\\quad \\text{это } %2 \\text{ от "
                                 "начального числа}")
                      .arg(formatNumber(expectedDeviationAt(now), 1),
                           formatNumber(100.0 * expectedDeviationAt(now) / n0, 2) + QStringLiteral(" %"))});

    st.push_back({QStringLiteral("9. Состояние в текущий момент t = %1").arg(durationText(now)),
                  QStringLiteral("\\text{по теории } N = %1 \\text{ ядер},\\quad \\text{счётчик показал } %2 "
                                 "\\text{ ядер},\\quad \\text{распалось } %3,\\quad A = %4 \\text{ распадов/ч}")
                      .arg(DecayModel::nucleiText(theoryNucleiAt(now)), DecayModel::nucleiText(countedNucleiAt(now)),
                           DecayModel::nucleiText(countedDecaysAt(now)), formatNumber(activityAt(now), 1))});
    st.push_back({QStringLiteral("Расхождение счёта и теории"),
                  QStringLiteral("\\text{счёт} - \\text{теория} = %1 - %2 = %3 \\text{ ядер},\\quad \\sigma = \\sqrt{N} = %4 "
                                 "\\text{ ядер — }%5")
                      .arg(DecayModel::nucleiText(countedNucleiAt(now)), DecayModel::nucleiText(theoryNucleiAt(now)),
                           formatNumber(countedNucleiAt(now) - theoryNucleiAt(now), 0),
                           formatNumber(expectedDeviationAt(now), 1),
                           std::abs(countedNucleiAt(now) - theoryNucleiAt(now)) <= expectedDeviationAt(now) * 3.0
                               ? QStringLiteral("разброс в пределах ожидаемого")
                               : QStringLiteral("разброс больше обычного: семя дало редкий опыт"))});

    d.answerTemplate =
        QStringLiteral("λ = %1 ч⁻¹; к t = %2 останется %3 ядер (счётчик: %4); A₀ = %5 распадов/ч")
            .arg(ratioText(lambda), durationText(moment), DecayModel::nucleiText(theoryNucleiAt(moment)),
                 DecayModel::nucleiText(countedNucleiAt(moment)), formatNumber(activityAt(0.0), 1));
    return d;
}

QVector<Readout> DecayModel::readouts() const {
    QVector<Readout> list;
    if (!validate().ok)
        return list;

    const double t = time();
    list.push_back({QStringLiteral("Время наблюдения"),
                    QStringLiteral("%1 из %2").arg(durationText(t), durationText(observationTime()))});
    list.push_back({QStringLiteral("Период полураспада T½"), durationText(halfLife())});
    list.push_back({QStringLiteral("Постоянная распада λ"), QStringLiteral("%1 ч⁻¹").arg(decayConstantText())});
    list.push_back({QStringLiteral("Прошло периодов полураспада"), utils::formatNumber(halfLivesElapsedAt(t), 3)});
    list.push_back({QStringLiteral("Ядер по теории N₀·2^(−t/T½)"), DecayModel::nucleiText(theoryNucleiAt(t))});
    list.push_back({QStringLiteral("Ядра по счёту счётчика"), DecayModel::nucleiText(countedNucleiAt(t))});
    list.push_back({QStringLiteral("Распалось ядер"), DecayModel::nucleiText(countedDecaysAt(t))});
    list.push_back({QStringLiteral("Доля нераспавшихся"),
                    QStringLiteral("%1 %").arg(utils::formatNumber(survivingFractionAt(t) * 100.0, 2))});
    list.push_back({QStringLiteral("Активность A = λN"),
                    QStringLiteral("%1 распадов/ч").arg(utils::formatNumber(activityAt(t), 1))});
    list.push_back({QStringLiteral("Распадов за шаг наблюдения"), DecayModel::nucleiText(decaysInLastStepAt(t))});
    list.push_back({QStringLiteral("Ожидаемый разброс √N"),
                    QStringLiteral("± %1 ядер").arg(utils::formatNumber(expectedDeviationAt(t), 1))});
    list.push_back({QStringLiteral("Расхождение счёта и теории"),
                    QStringLiteral("%1 %").arg(utils::formatNumber(relativeDeviationAt(t), 2))});
    list.push_back({QStringLiteral("Среднее время жизни τ = 1/λ"), durationText(meanLifetime())});
    list.push_back({QStringLiteral("Состояние"),
                    t >= observationTime() - 1e-12 ? QStringLiteral("наблюдение завершено")
                                                   : QStringLiteral("идёт наблюдение")});
    return list;
}

// ------------------------------------------------------------------
// Точки взаимодействия
// ------------------------------------------------------------------

QVector<HandleInfo> DecayModel::handles() const {
    QVector<HandleInfo> list;

    HandleInfo nuclei;
    nuclei.id = QStringLiteral("nuclei");
    nuclei.label = QStringLiteral("Начальное число ядер");
    nuclei.value = initialNuclei();
    nuclei.minValue = 50.0;
    nuclei.maxValue = 5000.0;
    nuclei.step = 10.0;
    nuclei.unit = QStringLiteral("ядер");
    nuclei.axis = QStringLiteral("both");
    nuclei.readbackLabel = QStringLiteral("Активность");
    nuclei.readbackValue = QStringLiteral("%1 распадов/ч").arg(utils::formatNumber(activityAt(0.0), 1));
    list.push_back(nuclei);

    HandleInfo life;
    life.id = QStringLiteral("half");
    life.label = QStringLiteral("Период полураспада");
    // Ручка отвечает за шкалу времени на схеме, поэтому показывает тот же
    // период полураспада, что и линейка T½; для долгоживущих изотопов её
    // значение упирается в границу слайдера.
    life.value = std::clamp(halfLife(), 0.5, 240.0);
    life.minValue = 0.5;
    life.maxValue = 240.0;
    life.step = 0.5;
    life.unit = QStringLiteral("ч");
    life.axis = QStringLiteral("x");
    life.readbackLabel = QStringLiteral("λ");
    life.readbackValue = QStringLiteral("%1 ч⁻¹").arg(decayConstantText());
    list.push_back(life);

    HandleInfo window;
    window.id = QStringLiteral("window");
    window.label = QStringLiteral("Конец наблюдения");
    window.value = observationTime();
    window.minValue = 1.0;
    window.maxValue = 240.0;
    window.step = 1.0;
    window.unit = QStringLiteral("ч");
    window.axis = QStringLiteral("x");
    window.readbackLabel = QStringLiteral("Останется к концу");
    window.readbackValue = QStringLiteral("%1 ядер").arg(DecayModel::nucleiText(theoryNucleiAt(observationTime())));
    list.push_back(window);

    HandleInfo share;
    share.id = QStringLiteral("share");
    share.label = QStringLiteral("Заданная доля ядер");
    share.value = param(QStringLiteral("share"));
    share.minValue = 1.0;
    share.maxValue = 99.0;
    share.step = 1.0;
    share.unit = QStringLiteral("%");
    share.axis = QStringLiteral("y");
    share.readbackLabel = QStringLiteral("Момент доли d");
    share.readbackValue = durationText(targetTime());
    list.push_back(share);

    return list;
}

void DecayModel::setHandleValue(const QString& id, double value) {
    // Ручки называются иначе, чем параметры панели: без явного соответствия
    // перетаскивание ушло бы в несуществующий параметр.
    if (id == QLatin1String("nuclei")) {
        setParam(QStringLiteral("N0"), value);
        return;
    }
    if (id == QLatin1String("half")) {
        // Период полураспада задают вручную — переключаем список на свой пункт.
        setParamRaw(QStringLiteral("isotope"), double(customIsotopeIndex()));
        setParam(QStringLiteral("half"), value);
        return;
    }
    if (id == QLatin1String("window")) {
        // Для изотопа из списка длительность наблюдения берётся из
        // справочника, поэтому перетаскивание конца наблюдения переводит
        // постановку на свой период полураспада, сохраняя T½ образца.
        const int index = paramChoiceIndex(QStringLiteral("isotope"));
        if (index >= 0 && index < customIsotopeIndex()) {
            const double life = halfLife();
            if (life >= 0.5 && life <= 240.0) {
                setParamRaw(QStringLiteral("half"), life);
                setParamRaw(QStringLiteral("isotope"), double(customIsotopeIndex()));
            }
        }
        setParam(QStringLiteral("T"), value);
        return;
    }
    if (id == QLatin1String("share")) {
        setParam(QStringLiteral("share"), value);
        return;
    }
    Simulation::setHandleValue(id, value);
}

void DecayModel::beginInteraction() {
    Simulation::beginInteraction();
    // Изменение постановки — это новый опыт, поэтому отсчёт начинается заново.
    setStateTime(0.0);
}

// ------------------------------------------------------------------
// Ряды данных
// ------------------------------------------------------------------

int DecayModel::seriesStride() const {
    // Ряд наблюдений может содержать несколько тысяч шагов, а на графике
    // различимы лишь сотни переломов: лишние узлы пропускают, сохраняя
    // ступенчатую форму счёта.
    const int nodes = m_alive.size();
    const int budget = kPlotPoints / 2;
    return budget > 0 ? std::max(1, nodes / budget) : 1;
}

QVector<QPointF> DecayModel::countedSeries() const {
    ensureSamples();
    QVector<QPointF> points;
    const int stride = seriesStride();
    // Ступенька: между узлами ряда число ядер не меняется, поэтому узел
    // повторяется — так видно, что счёт идёт целыми ядрами.
    for (int node = 0; node < m_timeNodes.size(); node += stride) {
        const int next = std::min(node + stride, int(m_timeNodes.size()) - 1);
        points.append(QPointF(m_timeNodes.at(node), m_alive.at(node)));
        // Последний узел повторяет себя: вторую точку того же интервала не
        // рисуют, иначе ряд закончился бы вертикальным отрезком нулевой длины.
        if (next > node)
            points.append(QPointF(m_timeNodes.at(next), m_alive.at(node)));
    }
    return points;
}

QVector<QPointF> DecayModel::countedDecaysSeries() const {
    ensureSamples();
    QVector<QPointF> points;
    const int stride = seriesStride();
    for (int node = 0; node < m_timeNodes.size(); node += stride) {
        const int next = std::min(node + stride, int(m_timeNodes.size()) - 1);
        const double decayed = initialNuclei() - m_alive.at(node);
        points.append(QPointF(m_timeNodes.at(node), decayed));
        if (next > node)
            points.append(QPointF(m_timeNodes.at(next), decayed));
    }
    return points;
}

QVector<QPointF> DecayModel::theorySeries() const {
    QVector<QPointF> points;
    const double total = duration();
    for (int i = 0; i < kPlotPoints; ++i) {
        const double t = total * i / double(kPlotPoints - 1);
        points.append(QPointF(t, theoryNucleiAt(t)));
    }
    return points;
}

QVector<QPointF> DecayModel::decayedTheorySeries() const {
    QVector<QPointF> points;
    const double total = duration();
    for (int i = 0; i < kPlotPoints; ++i) {
        const double t = total * i / double(kPlotPoints - 1);
        points.append(QPointF(t, decayedTheoryAt(t)));
    }
    return points;
}

QVector<QPointF> DecayModel::activitySeries() const {
    QVector<QPointF> points;
    const double total = duration();
    for (int i = 0; i < kPlotPoints; ++i) {
        const double t = total * i / double(kPlotPoints - 1);
        points.append(QPointF(t, activityAt(t)));
    }
    return points;
}

QVector<QPointF> DecayModel::countsPerStepSeries() const {
    ensureSamples();
    // Распады объединяют по stride шагов: при сотнях шагов наблюдения
    // отдельный штрих не помещается на графике, а сумма по интервалу —
    // то же показание счётчика, только снятое реже.
    const int stride = seriesStride();
    QVector<QPointF> points;
    double sum = 0.0;
    double left = 0.0;
    int taken = 0;
    for (int node = 1; node < m_timeNodes.size(); ++node) {
        if (taken == 0)
            left = m_timeNodes.at(node - 1);
        sum += m_stepDecays.at(node);
        ++taken;
        if (taken < stride && node != int(m_timeNodes.size()) - 1)
            continue;
        points.append(QPointF(0.5 * (left + m_timeNodes.at(node)), sum));
        sum = 0.0;
        taken = 0;
    }
    return points;
}

QVector<QPointF> DecayModel::countedActivitySeries() const {
    /*
     * Активность по опыту: число распадов на интервале отображения, делённое
     * на его длительность. Это ровно то, что делает счётчик Гейгера, — и
     * ступенька этого ряда скачет около гладкой кривой A = λN.
     */
    ensureSamples();
    const int stride = seriesStride();
    QVector<QPointF> points;
    double sum = 0.0;
    double left = 0.0;
    int taken = 0;
    for (int node = 1; node < m_timeNodes.size(); ++node) {
        if (taken == 0)
            left = m_timeNodes.at(node - 1);
        sum += m_stepDecays.at(node);
        ++taken;
        if (taken < stride && node != int(m_timeNodes.size()) - 1)
            continue;
        const double width = m_timeNodes.at(node) - left;
        points.append(QPointF(0.5 * (left + m_timeNodes.at(node)), width > 0.0 ? sum / width : 0.0));
        sum = 0.0;
        taken = 0;
    }
    return points;
}

QVector<QPointF> DecayModel::countedLogarithmSeries() const {
    ensureSamples();
    /*
     * Логарифм показаний счётчика: та же ступенька, только по ней видно, что
     * опыт идёт вдоль прямой с наклоном −λ. Там, где ядер не осталось, логарифм
     * не определён — ряд обрывается, а не уходит в минус бесконечность.
     */
    const int stride = seriesStride();
    QVector<QPointF> points;
    for (int node = 0; node < m_timeNodes.size(); node += stride) {
        const double alive = m_alive.at(node);
        if (alive <= 0.0)
            break;
        points.append(QPointF(m_timeNodes.at(node), std::log(alive)));
    }
    return points;
}

QVector<QPointF> DecayModel::logarithmSeries() const {
    /*
     * Логарифм числа ядер по теории: ln N = ln N₀ − λt. Это прямая, и её
     * наклон равен −λ — так постоянную распада находят из опыта, отложив
     * ln N по вертикали.
     */
    QVector<QPointF> points;
    const double total = observationTime();
    for (int i = 0; i < kPlotPoints; ++i) {
        const double t = total * i / double(kPlotPoints - 1);
        points.append(QPointF(t, std::log(theoryNucleiAt(t))));
    }
    return points;
}

QVector<QPointF> DecayModel::deviationSeries() const {
    ensureSamples();
    QVector<QPointF> points;
    const int stride = seriesStride();
    for (int node = 0; node < m_timeNodes.size(); node += stride)
        points.append(QPointF(m_timeNodes.at(node), relativeDeviationAt(m_timeNodes.at(node))));
    return points;
}

}  // namespace lab::models
