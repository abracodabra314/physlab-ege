#include "models/AdiabaticModel.h"

#include <algorithm>
#include <cmath>

#include "utils/MathText.h"

namespace lab::models {
namespace {

/** Универсальная газовая постоянная, Дж/(моль·К). */
constexpr double kGasConstant = 8.314462618;
/** Число Авогадро, 1/моль. */
constexpr double kAvogadro = 6.02214076e23;
/** Литр в кубических метрах и см² в м². */
constexpr double kCubicMetersPerLiter = 1e-3;
constexpr double kSquareMetersPerSquareCentimeter = 1e-4;
constexpr double kPascalsPerKilopascal = 1e3;
constexpr double kGramsPerKilogram = 1e3;
/** Атмосферное давление, Па: им удобно показывать давление в скобках. */
constexpr double kAtmospherePascals = 101325.0;
/** Запас времени после конца процесса: схема показывает установившееся состояние. */
constexpr double kTailSeconds = 1.2;

/**
 * Справочные пары «показатель адиабаты — молярная масса» для газов, которые
 * встречаются в задачах. Моноатомный газ даёт γ = 5/3, двухатомный — 7/5,
 * трёхатомный — ближе к 4/3: лишние вращательные и колебательные степени
 * свободы увеличивают теплоёмкость и уменьшают γ.
 */
struct GasPreset {
    const char* name;
    double gamma;
    double molarMassGrams;
};

const GasPreset kPresets[] = {
    {"воздух (двухатомный)", 1.4, 29.0},
    {"азот N₂", 1.4, 28.0},
    {"кислород O₂", 1.4, 32.0},
    {"гелий He (одноатомный)", 5.0 / 3.0, 4.0},
    {"аргон Ar (одноатомный)", 5.0 / 3.0, 40.0},
    {"углекислый газ CO₂", 1.3, 44.0},
    {"метан CH₄", 1.32, 16.0},
};

/** Индекс пункта «свой газ», при котором γ и M берутся из полей. */
int customGasIndex() { return int(std::size(kPresets)); }

/**
 * Число молекул, которые рисуются в сосуде. Их скорости берутся из формулы
 * v = √(3RT/M), поэтому при сжатии молекулы заметно ускоряются; само число
 * молекул процессом не меняется — его задаёт ν·Nₐ.
 */
constexpr int kMoleculesOnScheme = 24;

/** Вспомогательный слайдер: значение в hiển thị-единицах, пересчёт в модели. */
ParamSpec slider(const QString& id, const QString& label, const QString& unit, double minValue, double maxValue,
                 double step, double decimals, bool allowZero, double defaultValue, const QString& group,
                 const QString& help) {
    ParamSpec spec;
    spec.id = id;
    spec.label = label;
    spec.unit = unit;
    spec.kind = ParamKind::Slider;
    spec.minValue = minValue;
    spec.maxValue = maxValue;
    spec.stepValue = step;
    spec.decimals = int(decimals);
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

/** Отношение степеней в виде, пригодном для подстановки: (V₁/V₂)^n. */
QString powerText(const QString& base, const QString& exponent) {
    return QStringLiteral("(%1)^{%2}").arg(base, exponent);
}

/** Степень с числовым показателем. */
QString powerText(const QString& base, double exponent) {
    return powerText(base, utils::formatNumber(exponent, 3));
}

}  // namespace

AdiabaticModel::AdiabaticModel(QObject* parent) : Simulation(parent) { seedDefaults(); }

QString AdiabaticModel::title() const { return QStringLiteral("Адиабатное сжатие газа"); }

QString AdiabaticModel::subtitle() const {
    return QStringLiteral("pVᵧ = const; A = −ΔU; v = √(3RT/M)");
}

QString AdiabaticModel::statement() const {
    return QStringLiteral(
        "Газ находится в теплоизолированном цилиндре под поршнем площадью S. Процесс идёт медленно, "
        "поэтому газ всё время равновесен, а теплообмен с средой равен нулю. При изменении объёма "
        "найти давление и температуру в конце процесса, работу газа, изменение его внутренней энергии "
        "и силу, которую нужно приложить к поршню.");
}

QString AdiabaticModel::problemText() const {
    return QStringLiteral(
               "В теплоизолированном цилиндре находится %1 моль газа (γ = %2, M = %3 г/моль) при "
               "температуре %4 К и объёме %5 л. Поршень площадью %6 см² перемещают так, что объём "
               "за %7 с медленно меняется до %8 л. Внешнее давление %9 кПа. Найти давление и "
               "температуру в конце процесса, работу газа и силу на поршень.")
        .arg(utils::formatNumber(substance(), 2), utils::formatNumber(gamma(), 2),
             utils::formatNumber(molarMass() * kGramsPerKilogram, 1),
             utils::formatNumber(initialTemperature(), 1),
             utils::formatNumber(initialVolume() / kCubicMetersPerLiter, 2),
             utils::formatNumber(pistonArea() / kSquareMetersPerSquareCentimeter, 1),
             utils::formatNumber(processTime(), 1),
             utils::formatNumber(finalVolume() / kCubicMetersPerLiter, 2),
             utils::formatNumber(outsidePressure() / kPascalsPerKilopascal, 1));
}

// ------------------------------------------------------------------
// Параметры: газ, начальное состояние и ход поршня
// ------------------------------------------------------------------

QVector<ParamSpec> AdiabaticModel::paramSpecs() const {
    QVector<ParamSpec> specs;
    const QString gasGroup = QStringLiteral("Газ");
    const QString stateGroup = QStringLiteral("Начальное состояние");
    const QString pistonGroup = QStringLiteral("Поршень и процесс");

    QStringList gases;
    for (const GasPreset& preset : kPresets)
        gases.push_back(QStringLiteral("%1  (γ = %2, M = %3 г/моль)")
                            .arg(QString::fromUtf8(preset.name), utils::formatNumber(preset.gamma, 2),
                                 utils::formatNumber(preset.molarMassGrams, 1)));
    gases.push_back(QStringLiteral("свой газ — γ и M задать вручную"));
    specs.push_back(choice(QStringLiteral("gas"), QStringLiteral("Газ в цилиндре"), gases, gasGroup,
                           QStringLiteral("γ и M определяют, как сильно разогреется газ при сжатии")));

    specs.push_back(slider(QStringLiteral("gamma"), QStringLiteral("Показатель адиабаты γ = Cp/Cv"),
                           QString(), 1.0, 2.0, 0.01, 2, false, 1.4, gasGroup,
                           QStringLiteral("Одноатомный газ — 1,67, двухатомный — 1,4, трёхатомный — 1,3. "
                                          "Значение используется, когда выбран «свой газ»")));
    specs.push_back(slider(QStringLiteral("mass"), QStringLiteral("Молярная масса газа M"),
                           QStringLiteral("г/моль"), 1.0, 80.0, 0.1, 1, false, 29.0, gasGroup,
                           QStringLiteral("От M зависит скорость молекул: v = √(3RT/M)")));
    specs.push_back(slider(QStringLiteral("nu"), QStringLiteral("Количество вещества ν"), QStringLiteral("моль"),
                           0.05, 5.0, 0.05, 2, false, 1.0, stateGroup,
                           QStringLiteral("Число молекул N = ν·Nₐ")));
    specs.push_back(slider(QStringLiteral("T1"), QStringLiteral("Начальная температура T₁"), QStringLiteral("К"),
                           100.0, 800.0, 1.0, 1, false, 293.0, stateGroup,
                           QStringLiteral("293 К — это 20 °C")));
    specs.push_back(slider(QStringLiteral("V1"), QStringLiteral("Начальный объём V₁"), QStringLiteral("л"), 0.5,
                           60.0, 0.1, 2, false, 8.0, stateGroup,
                           QStringLiteral("Объём под поршнем в начале процесса. Можно менять, перетаскивая "
                                          "поршень на схеме")));
    specs.push_back(slider(QStringLiteral("V2"), QStringLiteral("Объём в конце процесса V₂"), QStringLiteral("л"),
                           0.5, 60.0, 0.1, 2, false, 2.0, pistonGroup,
                           QStringLiteral("V₂ < V₁ — сжатие, V₂ > V₁ — расширение")));
    specs.push_back(slider(QStringLiteral("S"), QStringLiteral("Площадь поршня S"), QStringLiteral("см²"), 5.0,
                           600.0, 1.0, 1, false, 50.0, pistonGroup,
                           QStringLiteral("Сила давления газа F = p·S — от площади зависит сила, но не "
                                          "давление и не температура")));
    specs.push_back(slider(QStringLiteral("p0"), QStringLiteral("Внешнее давление p₀"), QStringLiteral("кПа"),
                           0.0, 300.0, 0.1, 1, true, 101.3, pistonGroup,
                           QStringLiteral("Атмосфера снаружи поршня: внешняя сила равна (p − p₀)·S")));
    specs.push_back(slider(QStringLiteral("tau"), QStringLiteral("Длительность процесса τ"), QStringLiteral("с"),
                           0.5, 30.0, 0.5, 1, false, 6.0, pistonGroup,
                           QStringLiteral("Процесс считается медленным (квазистатическим): уравнения "
                                          "адиабаты применимы в каждой промежуточной точке")));
    return specs;
}

ValidationResult AdiabaticModel::validate() const {
    ValidationResult check;
    if (substance() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Количество вещества должно быть больше нуля");
        check.hint = QStringLiteral("Задайте ν не меньше 0,05 моль");
        return check;
    }
    if (!std::isfinite(gamma()) || gamma() <= 1.0) {
        check.ok = false;
        check.message = QStringLiteral("Показатель адиабаты должен быть больше единицы: при γ = 1 теплоёмкость "
                                       "газа бесконечна, и температура при сжатии не менялась бы");
        check.hint = QStringLiteral("Для двухатомного газа γ = 1,4, для одноатомного — 1,67");
        return check;
    }
    if (molarMass() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Молярная масса должна быть больше нуля");
        check.hint = QStringLiteral("Возьмите газ из списка или задайте M в г/моль");
        return check;
    }
    if (initialTemperature() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Абсолютная температура должна быть больше нуля");
        check.hint = QStringLiteral("Задайте T₁ не меньше 100 К");
        return check;
    }
    if (initialVolume() <= 0.0 || finalVolume() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Объём газа не может быть нулевым");
        check.hint = QStringLiteral("Задайте объёмы не меньше 0,5 л");
        return check;
    }
    if (std::abs(initialVolume() - finalVolume()) <= 1e-12) {
        check.ok = false;
        check.message = QStringLiteral("Начальный и конечный объёмы совпадают: поршень не движется, процесса нет");
        check.hint = QStringLiteral("Сделайте V₂ меньше V₁ (сжатие) или больше него (расширение)");
        return check;
    }
    if (pistonArea() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Площадь поршня должна быть больше нуля");
        check.hint = QStringLiteral("Задайте S не меньше 5 см²");
        return check;
    }
    if (processTime() <= 0.0) {
        check.ok = false;
        check.message = QStringLiteral("Длительность процесса должна быть больше нуля");
        check.hint = QStringLiteral("Задайте τ не меньше 0,5 с");
        return check;
    }
    // Давление и температура из уравнений адиабаты обязаны остаться
    // конечными: иначе степень сжатия взята чрезмерно большой.
    if (!std::isfinite(finalPressure()) || !std::isfinite(finalTemperature())) {
        check.ok = false;
        check.message = QStringLiteral("При таком отношении объёмов давление и температура уходят в бесконечность");
        check.hint = QStringLiteral("Уменьшите степень сжатия: V₂ должно быть ближе к V₁");
        return check;
    }
    return check;
}

// ------------------------------------------------------------------
// Параметры в единицах СИ
// ------------------------------------------------------------------

double AdiabaticModel::substance() const { return param(QStringLiteral("nu")); }

double AdiabaticModel::initialTemperature() const { return param(QStringLiteral("T1")); }

double AdiabaticModel::initialVolume() const { return param(QStringLiteral("V1")) * kCubicMetersPerLiter; }

double AdiabaticModel::finalVolume() const { return param(QStringLiteral("V2")) * kCubicMetersPerLiter; }

QString AdiabaticModel::gasName() const {
    const int index = paramChoiceIndex(QStringLiteral("gas"));
    if (index < 0 || index >= customGasIndex())
        return QStringLiteral("свой газ");
    return QStringLiteral("%1").arg(QString::fromUtf8(kPresets[index].name));
}

double AdiabaticModel::gamma() const {
    const int index = paramChoiceIndex(QStringLiteral("gas"));
    if (index >= 0 && index < customGasIndex())
        return kPresets[index].gamma;
    return param(QStringLiteral("gamma"));
}

double AdiabaticModel::molarMass() const {
    const int index = paramChoiceIndex(QStringLiteral("gas"));
    if (index >= 0 && index < customGasIndex())
        return kPresets[index].molarMassGrams / kGramsPerKilogram;
    return param(QStringLiteral("mass")) / kGramsPerKilogram;
}

double AdiabaticModel::pistonArea() const {
    return param(QStringLiteral("S")) * kSquareMetersPerSquareCentimeter;
}

double AdiabaticModel::outsidePressure() const {
    return param(QStringLiteral("p0")) * kPascalsPerKilopascal;
}

double AdiabaticModel::processTime() const { return param(QStringLiteral("tau")); }

// ------------------------------------------------------------------
// Теплоёмкости и величины начального состояния
// ------------------------------------------------------------------

double AdiabaticModel::molarHeatCapacity() const {
    // Cv = R/(γ − 1): следует из U = νCvT и γ = Cp/Cv, Cp = Cv + R.
    return kGasConstant / (gamma() - 1.0);
}

double AdiabaticModel::molarHeatCapacityP() const {
    return gamma() * kGasConstant / (gamma() - 1.0);
}

double AdiabaticModel::moleculeCount() const { return substance() * kAvogadro; }

double AdiabaticModel::initialPressure() const {
    return substance() * kGasConstant * initialTemperature() / initialVolume();
}

double AdiabaticModel::compressionRatio() const { return initialVolume() / finalVolume(); }

double AdiabaticModel::finalPressure() const {
    return pressureForVolume(finalVolume());
}

double AdiabaticModel::finalTemperature() const {
    return temperatureForVolume(finalVolume());
}

double AdiabaticModel::pressureForVolume(double volume) const {
    if (volume <= 0.0)
        return std::numeric_limits<double>::quiet_NaN();
    // p(V) = p₁(V₁/V)^γ — интеграл от dA = p·dV вместе с dU = νCv·dT.
    return initialPressure() * std::pow(initialVolume() / volume, gamma());
}

double AdiabaticModel::temperatureForVolume(double volume) const {
    if (volume <= 0.0)
        return std::numeric_limits<double>::quiet_NaN();
    return initialTemperature() * std::pow(initialVolume() / volume, gamma() - 1.0);
}

// ------------------------------------------------------------------
// Ход процесса: объём задан законом движения поршня
// ------------------------------------------------------------------

double AdiabaticModel::volumeAt(double t) const {
    const double tau = processTime();
    // Медленный (квазистатический) процесс: объем меняется линейно, а
    // давление и температура считаются по уравнениям адиабаты.
    const double fraction = tau <= 0.0 ? 1.0 : std::clamp(t / tau, 0.0, 1.0);
    return initialVolume() + (finalVolume() - initialVolume()) * fraction;
}

double AdiabaticModel::pressureAt(double t) const { return pressureForVolume(volumeAt(t)); }

double AdiabaticModel::temperatureAt(double t) const { return temperatureForVolume(volumeAt(t)); }

double AdiabaticModel::internalEnergyAt(double t) const {
    return substance() * molarHeatCapacity() * temperatureAt(t);
}

double AdiabaticModel::energyChangeAt(double t) const {
    return substance() * molarHeatCapacity() * (temperatureAt(t) - initialTemperature());
}

double AdiabaticModel::workAt(double t) const {
    // A = ∫p dV = (p₁V₁ − p(V)·V)/(γ − 1): при сжатии работа газа отрицательна.
    const double volume = volumeAt(t);
    return (initialPressure() * initialVolume() - pressureForVolume(volume) * volume) / (gamma() - 1.0);
}

double AdiabaticModel::totalWork() const { return workAt(processTime()); }

double AdiabaticModel::gasForceAt(double t) const { return pressureAt(t) * pistonArea(); }

double AdiabaticModel::outsideForce() const { return outsidePressure() * pistonArea(); }

double AdiabaticModel::externalForceAt(double t) const {
    return (pressureAt(t) - outsidePressure()) * pistonArea();
}

double AdiabaticModel::pistonSpeedAt(double t) const {
    const double tau = processTime();
    if (tau <= 0.0 || t >= tau)
        return 0.0;
    // Скорость поршня из закона объёма: V = S·h ⇒ v = (dV/dt)/S.
    return (finalVolume() - initialVolume()) / tau / pistonArea();
}

double AdiabaticModel::rmsSpeedAt(double temperature) const {
    if (temperature <= 0.0 || molarMass() <= 0.0)
        return 0.0;
    return std::sqrt(3.0 * kGasConstant * temperature / molarMass());
}

double AdiabaticModel::rmsSpeedAtTime(double t) const { return rmsSpeedAt(temperatureAt(t)); }

double AdiabaticModel::timePerSecond() const { return std::clamp(duration() / 9.0, 0.05, 4.0); }

double AdiabaticModel::duration() const { return processTime() + kTailSeconds; }

double AdiabaticModel::stepSize() const { return duration() / 400.0; }

double AdiabaticModel::eventTime() const { return processTime(); }

// ------------------------------------------------------------------
// Состояние системы
// ------------------------------------------------------------------

SimState AdiabaticModel::stateAt(double t) const {
    SimState state;
    const double tau = processTime();
    const bool finished = t >= tau - 1e-12;
    const double pressure = pressureAt(t);
    const double temperature = temperatureAt(t);

    // Поршень: положение — высота столба газа h = V/S.
    EntityVisual piston;
    piston.id = QStringLiteral("piston");
    piston.label = QStringLiteral("Поршень");
    piston.position = volumeAt(t) / pistonArea();
    piston.value1 = pistonSpeedAt(t);
    piston.value2 = gasForceAt(t);
    piston.active = !finished;
    piston.variant = 0;
    state.entities.push_back(piston);

    // Газ: давление и температура в текущий момент.
    EntityVisual gas;
    gas.id = QStringLiteral("gas");
    gas.label = QStringLiteral("Газ");
    gas.position = 0.0;
    gas.value1 = pressure;
    gas.value2 = temperature;
    gas.active = true;
    gas.variant = 1;
    state.entities.push_back(gas);

    // Молекулы: направления фиксированы, скорость растёт как √T.
    const int count = kMoleculesOnScheme;
    for (int index = 0; index < count; ++index) {
        const double angle = M_PI * (index + 0.5) / count;
        EntityVisual molecule;
        molecule.id = QStringLiteral("molecule_%1").arg(index + 1);
        molecule.label = QStringLiteral("Молекула %1").arg(index + 1);
        molecule.position = 0.0;
        molecule.value1 = std::cos(2.0 * angle);   // направление движения
        molecule.value2 = std::sin(2.0 * angle);
        molecule.active = true;
        molecule.variant = index;
        state.entities.push_back(molecule);
    }

    state.primary = pressure;
    state.secondary = temperature;
    state.resolved = finished;
    if (finished) {
        state.eventLabel =
            compressionRatio() > 1.0
                ? QStringLiteral("Сжатие завершено: p₂ = %1 кПа, T₂ = %2 К")
                      .arg(utils::formatNumber(finalPressure() / kPascalsPerKilopascal, 1),
                           utils::formatNumber(finalTemperature(), 1))
                : QStringLiteral("Расширение завершено: p₂ = %1 кПа, T₂ = %2 К")
                      .arg(utils::formatNumber(finalPressure() / kPascalsPerKilopascal, 1),
                           utils::formatNumber(finalTemperature(), 1));
    }
    return state;
}

// ------------------------------------------------------------------
// Запись решения
// ------------------------------------------------------------------

Derivation AdiabaticModel::derivation() const {
    using utils::formatNumber;
    Derivation d;
    const ValidationResult check = validate();
    d.valid = check.ok;
    d.problemText = problemText();
    d.unknown = QStringLiteral("p₂, T₂, работу газа A и силу на поршень");

    if (!check.ok) {
        d.steps.push_back({QStringLiteral("Внимание"), QStringLiteral("\\text{%1}").arg(check.message)});
        d.steps.push_back({QStringLiteral("Что сделать"), QStringLiteral("\\text{%1}").arg(check.hint)});
        d.answerTemplate = QStringLiteral("Расчёт невозможен: %1").arg(check.message);
        return d;
    }

    const double nu = substance();
    const double g = gamma();
    const double molar = molarMass();
    const double cv = molarHeatCapacity();
    const double cp = molarHeatCapacityP();
    const double v1 = initialVolume();
    const double v2 = finalVolume();
    const double t1 = initialTemperature();
    const double p1 = initialPressure();
    const double p2 = finalPressure();
    const double t2 = finalTemperature();
    const double area = pistonArea();
    const double now = time();

    /** Объём в литрах — в таких единицах его задаёт панель. */
    auto liters = [](double cubicMeters) { return utils::formatNumber(cubicMeters / kCubicMetersPerLiter, 2); };
    /** Давление в килопаскалях: паскали дают семизначные числа. */
    auto kilopascals = [](double pascals) { return utils::formatNumber(pascals / kPascalsPerKilopascal, 1); };

    QVector<DerivationStep>& st = d.steps;
    // Все подстановки пишутся в единицах СИ (Дж, Па, м³, кг/моль), иначе
    // левая и правая части равенства были бы разноимёнными. Литры и
    // килопаскали остаются только там, где это единицы ввода на панели.
    st.push_back({QStringLiteral("1. Исходные данные"),
                  QStringLiteral("\\nu = %1 \\text{ моль},\\quad T_{1} = %2 \\text{ К},\\quad V_{1} = %3 \\text{ м}^{3}"
                                 "\\ (= %4 \\text{ л}),\\quad V_{2} = %5 \\text{ м}^{3},\\quad \\gamma = %6,\\quad M = "
                                 "%7 \\text{ кг/моль}")
                      .arg(formatNumber(nu, 2), formatNumber(t1, 1), formatNumber(v1, 4), liters(v1),
                           formatNumber(v2, 4), formatNumber(g, 2), formatNumber(molar, 4))});

    st.push_back({QStringLiteral("2. Теплоёмкости газа"),
                  QStringLiteral("C_{V} = \\frac{R}{\\gamma - 1},\\qquad C_{P} = \\frac{\\gamma R}{\\gamma - 1},"
                                 "\\qquad \\gamma = \\frac{C_{P}}{C_{V}}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("C_{V} = \\frac{%1}{%2 - 1} = %3 \\text{ Дж/(моль·К)},\\quad C_{P} = %4 "
                                 "\\text{ Дж/(моль·К)}")
                      .arg(formatNumber(kGasConstant, 3), formatNumber(g, 2), formatNumber(cv, 2),
                           formatNumber(cp, 2))});

    st.push_back({QStringLiteral("3. Начальное давление (уравнение Менделеева–Клапейрона)"),
                  QStringLiteral("p_{1} = \\frac{\\nu R T_{1}}{V_{1}}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("p_{1} = \\frac{%1 \\cdot %2 \\cdot %3}{%4} = %5 \\text{ Па} = %6 \\text{ кПа}")
                      .arg(formatNumber(nu, 2), formatNumber(kGasConstant, 3), formatNumber(t1, 1),
                           formatNumber(v1, 4), formatNumber(p1, 0), kilopascals(p1))});

    st.push_back({QStringLiteral("4. Уравнение адиабаты (Пуассона)"),
                  QStringLiteral("Q = 0,\\quad A = -\\Delta U,\\quad dA = p\\,dV,\\quad dU = \\nu C_{V} dT "
                                 "\\quad \\Rightarrow \\quad pV^{\\gamma} = \\text{const}")});
    st.push_back({QStringLiteral("Отсюда конечные состояния"),
                  QStringLiteral("p_{2} = p_{1} %1, \\qquad T_{2} = T_{1} %2, \\qquad "
                                 "\\frac{p_{2}}{p_{1}} = \\left(\\frac{T_{2}}{T_{1}}\\right)^{\\frac{\\gamma}{"
                                 "\\gamma - 1}}")
                      .arg(powerText(QStringLiteral("V_{1}/V_{2}"), QStringLiteral("\\gamma")),
                           powerText(QStringLiteral("V_{1}/V_{2}"), QStringLiteral("\\gamma - 1")))});
    {
        const double ratio = compressionRatio();
        st.push_back({QStringLiteral("Подстановка"),
                      QStringLiteral("V_{1}/V_{2} = %1, \\quad p_{2} = %2 \\cdot %3 = %4 \\text{ кПа},\\quad T_{2} = "
                                     "%5 \\cdot %6 = %7 \\text{ К}")
                          .arg(formatNumber(ratio, 3), kilopascals(p1), powerText(formatNumber(ratio, 3), g),
                               kilopascals(p2), formatNumber(t1, 1), powerText(formatNumber(ratio, 3), g - 1.0),
                               formatNumber(t2, 1))});
    }

    st.push_back({QStringLiteral("5. Проверка: две дороги к одному ответу"),
                  QStringLiteral("T_{2} = \\frac{p_{2} V_{2}}{\\nu R} \\text{ — из уравнения Менделеева–Клапейрона, "
                                 "а не из адиабаты}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("T_{2} = \\frac{%1 \\cdot %2}{%3 \\cdot %4} = %5 \\text{ К} = "
                                 "T_{1}\\left(V_{1}/V_{2}\\right)^{\\gamma - 1}")
                      .arg(formatNumber(p2, 0), formatNumber(v2, 4), formatNumber(nu, 2),
                           formatNumber(kGasConstant, 3), formatNumber(p2 * v2 / (nu * kGasConstant), 1))});

    st.push_back({QStringLiteral("6. Работа газа"),
                  QStringLiteral("A = \\int_{V_{1}}^{V_{2}} p\\,dV = \\frac{p_{1} V_{1} - p_{2} V_{2}}{\\gamma - 1}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("A = \\frac{%1 - %2}{%3 - 1} = %4 \\text{ Дж}")
                      .arg(formatNumber(p1 * v1, 1), formatNumber(p2 * v2, 1), formatNumber(g, 2),
                           formatNumber(totalWork(), 1))});
    st.push_back({QStringLiteral("Знак работы"),
                  QStringLiteral("\\text{%1}")
                      .arg(totalWork() < 0.0
                               ? QStringLiteral("работа газа отрицательна: газ сжимают внешние силы, их работа "
                                                "равна %1 Дж")
                                     .arg(formatNumber(-totalWork(), 1))
                               : QStringLiteral("работа газа положительна: газ сам толкает поршень"))});

    st.push_back({QStringLiteral("7. Первое начало термодинамики"),
                  QStringLiteral("Q = 0 \\Rightarrow A = -\\Delta U,\\qquad \\Delta U = \\nu C_{V}(T_{2} - T_{1})")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("\\Delta U = %1 \\cdot %2 \\cdot (%3 - %4) = %5 \\text{ Дж},\\quad -\\Delta U = %6 "
                                 "\\text{ Дж} = A")
                      .arg(formatNumber(nu, 2), formatNumber(cv, 2), formatNumber(t2, 1), formatNumber(t1, 1),
                           formatNumber(energyChangeAt(processTime()), 1), formatNumber(-energyChangeAt(processTime()), 1))});

    st.push_back({QStringLiteral("8. Сила на поршень"),
                  QStringLiteral("F = pS,\\qquad F_{\\text{внеш}} = (p - p_{0}) S")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("F_{1} = p_{1} S = %1 \\text{ Н},\\quad F_{2} = p_{2} S = %2 \\text{ Н},\\quad "
                                 "F_{\\text{внеш}2} = (p_{2} - p_{0}) S = (%3 - %4) \\cdot %5 = %6 \\text{ Н}")
                      // Давление в паскалях, площадь в м²: произведение сразу
                      // даёт ньютоны, пересчитывать единицы не приходится.
                      .arg(formatNumber(gasForceAt(0.0), 1), formatNumber(gasForceAt(processTime()), 1),
                           formatNumber(pressureAt(processTime()), 0), formatNumber(outsidePressure(), 0),
                           formatNumber(area, 4) + QStringLiteral(" \\text{ м}^{2}"),
                           formatNumber(std::abs(externalForceAt(processTime())), 1))});

    st.push_back({QStringLiteral("9. Скорость молекул"),
                  QStringLiteral("v = \\sqrt{\\frac{3RT}{M}},\\qquad \\frac{v_{2}}{v_{1}} = "
                                 "\\sqrt{\\frac{T_{2}}{T_{1}}} = \\left(\\frac{V_{1}}{V_{2}}\\right)^{\\frac{"
                                 "\\gamma - 1}{2}}")});
    st.push_back({QStringLiteral("Подстановка"),
                  QStringLiteral("v_{1} = %1 \\text{ м/с},\\quad v_{2} = %2 \\text{ м/с},\\quad "
                                 "\\frac{v_{2}}{v_{1}} = \\sqrt{\\frac{%3}{%4}} = %5")
                      .arg(formatNumber(rmsSpeedAt(t1), 0), formatNumber(rmsSpeedAt(t2), 0), formatNumber(t2, 1),
                           formatNumber(t1, 1), formatNumber(std::sqrt(t2 / t1), 3))});

    st.push_back({QStringLiteral("10. Состояние в текущий момент t = %1 с").arg(formatNumber(now, 2)),
                  QStringLiteral("V(t) = V_{1} + (V_{2} - V_{1}) t/\\tau = %1 \\text{ л},\\quad p(t) = %2 \\text{ кПа},"
                                 "\\quad T(t) = %3 \\text{ К},\\quad A(t) = %4 \\text{ Дж},\\quad \\Delta U(t) = %5 "
                                 "\\text{ Дж}")
                      .arg(liters(volumeAt(now)), kilopascals(pressureAt(now)), formatNumber(temperatureAt(now), 1),
                           formatNumber(workAt(now), 1), formatNumber(energyChangeAt(now), 1))});

    d.answerTemplate =
        QStringLiteral("p₂ = %1 кПа, T₂ = %2 К; A = %3 Дж (ΔU = %4 Дж); Fвнеш = %5 Н")
            .arg(kilopascals(p2), formatNumber(t2, 1), formatNumber(totalWork(), 1),
                 formatNumber(energyChangeAt(processTime()), 1),
                 formatNumber(std::abs(externalForceAt(processTime())), 1));
    return d;
}

QVector<Readout> AdiabaticModel::readouts() const {
    QVector<Readout> list;
    if (!validate().ok)
        return list;

    const double t = time();
    list.push_back({QStringLiteral("Время процесса"), QStringLiteral("%1 с из %2 с")
                                                      .arg(utils::formatNumber(t, 2),
                                                           utils::formatNumber(processTime(), 1))});
    list.push_back({QStringLiteral("Объём V(t)"),
                    QStringLiteral("%1 л").arg(utils::formatNumber(volumeAt(t) / kCubicMetersPerLiter, 2))});
    list.push_back({QStringLiteral("Давление p(t)"), QStringLiteral("%1 кПа (%2 атм)")
                                                         .arg(utils::formatNumber(pressureAt(t) / kPascalsPerKilopascal, 1),
                                                              utils::formatNumber(pressureAt(t) / kAtmospherePascals, 2))});
    list.push_back({QStringLiteral("Температура T(t)"), QStringLiteral("%1 К (%2 °C)")
                                                           .arg(utils::formatNumber(temperatureAt(t), 1),
                                                                utils::formatNumber(temperatureAt(t) - 273.15, 1))});
    list.push_back({QStringLiteral("Скорость поршня"), QStringLiteral("%1 м/с").arg(utils::formatNumber(pistonSpeedAt(t), 3))});
    list.push_back({QStringLiteral("Работа газа A(t)"), QStringLiteral("%1 Дж").arg(utils::formatNumber(workAt(t), 1))});
    list.push_back({QStringLiteral("Изменение внутренней энергии"),
                    QStringLiteral("%1 Дж").arg(utils::formatNumber(energyChangeAt(t), 1))});
    list.push_back({QStringLiteral("Теплообмен Q"), QStringLiteral("0 Дж (сосуд теплоизолирован)")});
    list.push_back({QStringLiteral("Сила давления газа"), QStringLiteral("%1 Н").arg(utils::formatNumber(gasForceAt(t), 1))});
    list.push_back({QStringLiteral("Внешняя сила на поршень"),
                    QStringLiteral("%1 Н").arg(utils::formatNumber(std::abs(externalForceAt(t)), 1))});
    list.push_back({QStringLiteral("Скорость молекул √(3RT/M)"), QStringLiteral("%1 м/с").arg(utils::formatNumber(rmsSpeedAtTime(t), 0))});
    list.push_back({QStringLiteral("Число молекул N = νNₐ"),
                    QStringLiteral("%1").arg(utils::formatNumber(moleculeCount() / 1e23, 3) + QStringLiteral("·10²³"))});
    list.push_back({QStringLiteral("Состояние"),
                    t >= processTime() - 1e-12 ? QStringLiteral("процесс завершён") : QStringLiteral("идёт процесс")});
    return list;
}

// ------------------------------------------------------------------
// Точки взаимодействия
// ------------------------------------------------------------------

QVector<HandleInfo> AdiabaticModel::handles() const {
    QVector<HandleInfo> list;

    HandleInfo piston;
    piston.id = QStringLiteral("piston");
    piston.label = QStringLiteral("Объём под поршнем");
    piston.value = param(QStringLiteral("V1"));
    piston.minValue = 0.5;
    piston.maxValue = 60.0;
    piston.step = 0.1;
    piston.unit = QStringLiteral("л");
    piston.axis = QStringLiteral("y");
    piston.readbackLabel = QStringLiteral("Давление");
    piston.readbackValue = QStringLiteral("%1 кПа").arg(utils::formatNumber(initialPressure() / kPascalsPerKilopascal, 1));
    list.push_back(piston);

    HandleInfo target;
    target.id = QStringLiteral("target");
    target.label = QStringLiteral("Объём в конце процесса");
    target.value = param(QStringLiteral("V2"));
    target.minValue = 0.5;
    target.maxValue = 60.0;
    target.step = 0.1;
    target.unit = QStringLiteral("л");
    target.axis = QStringLiteral("y");
    target.readbackLabel = QStringLiteral("Конечное состояние");
    target.readbackValue = QStringLiteral("%1 К · p₂ %2 кПа")
                               .arg(utils::formatNumber(finalTemperature(), 1),
                                    utils::formatNumber(finalPressure() / kPascalsPerKilopascal, 1));
    list.push_back(target);

    HandleInfo size;
    size.id = QStringLiteral("area");
    size.label = QStringLiteral("Площадь поршня");
    size.value = param(QStringLiteral("S"));
    size.minValue = 5.0;
    size.maxValue = 600.0;
    size.step = 1.0;
    size.unit = QStringLiteral("см²");
    size.axis = QStringLiteral("both");
    size.readbackLabel = QStringLiteral("Сила газа");
    size.readbackValue = QStringLiteral("%1 Н при p₂").arg(utils::formatNumber(gasForceAt(processTime()), 1));
    list.push_back(size);
    return list;
}

void AdiabaticModel::setHandleValue(const QString& id, double value) {
    // Ручки называются иначе, чем параметры панели: без явного соответствия
    // перетаскивание попало бы в несуществующий параметр.
    if (id == QLatin1String("piston")) {
        setParam(QStringLiteral("V1"), value);
        return;
    }
    if (id == QLatin1String("target")) {
        setParam(QStringLiteral("V2"), value);
        return;
    }
    if (id == QLatin1String("area")) {
        setParam(QStringLiteral("S"), value);
        return;
    }
    Simulation::setHandleValue(id, value);
}

void AdiabaticModel::beginInteraction() {
    Simulation::beginInteraction();
    // Перетаскивание поршня задаёт новое начальное состояние, поэтому
    // отсчёт процесса начинается заново.
    setStateTime(0.0);
}

// ------------------------------------------------------------------
// Ряды данных
// ------------------------------------------------------------------

void AdiabaticModel::volumeRange(double& minVolume, double& maxVolume) const {
    minVolume = std::min(initialVolume(), finalVolume());
    maxVolume = std::max(initialVolume(), finalVolume());
    if (!(maxVolume > minVolume)) {
        // Процесса нет (такое постановку отсекает validate), но ряд должен
        // остаться конечным: берём окрестность единственного объёма.
        minVolume = initialVolume() * 0.5;
        maxVolume = initialVolume() * 1.5;
    }
}

QVector<QPointF> AdiabaticModel::pressureVersusVolume() const {
    QVector<QPointF> points;
    double low = 0.0, high = 0.0;
    volumeRange(low, high);
    for (int i = 0; i <= 200; ++i) {
        const double volume = low + (high - low) * i / 200.0;
        points.append(QPointF(volume / kCubicMetersPerLiter, pressureForVolume(volume) / kPascalsPerKilopascal));
    }
    return points;
}

QVector<QPointF> AdiabaticModel::temperatureVersusVolume() const {
    QVector<QPointF> points;
    double low = 0.0, high = 0.0;
    volumeRange(low, high);
    for (int i = 0; i <= 200; ++i) {
        const double volume = low + (high - low) * i / 200.0;
        points.append(QPointF(volume / kCubicMetersPerLiter, temperatureForVolume(volume)));
    }
    return points;
}

QVector<QPointF> AdiabaticModel::isothermVersusVolume() const {
    QVector<QPointF> points;
    double low = 0.0, high = 0.0;
    volumeRange(low, high);
    const double constant = initialPressure() * initialVolume();
    for (int i = 0; i <= 200; ++i) {
        const double volume = low + (high - low) * i / 200.0;
        points.append(QPointF(volume / kCubicMetersPerLiter, constant / volume / kPascalsPerKilopascal));
    }
    return points;
}

QVector<QPointF> AdiabaticModel::pressureVersusTemperature() const {
    QVector<QPointF> points;
    double low = 0.0, high = 0.0;
    volumeRange(low, high);
    for (int i = 0; i <= 200; ++i) {
        const double volume = low + (high - low) * i / 200.0;
        points.append(QPointF(temperatureForVolume(volume), pressureForVolume(volume) / kPascalsPerKilopascal));
    }
    return points;
}

QVector<QPointF> AdiabaticModel::volumeSeries() const {
    QVector<QPointF> points;
    const double total = duration();
    for (int i = 0; i <= 240; ++i) {
        const double t = total * i / 240.0;
        points.append(QPointF(t, volumeAt(t) / kCubicMetersPerLiter));
    }
    return points;
}

QVector<QPointF> AdiabaticModel::pressureSeries() const {
    QVector<QPointF> points;
    const double total = duration();
    for (int i = 0; i <= 240; ++i) {
        const double t = total * i / 240.0;
        points.append(QPointF(t, pressureAt(t) / kPascalsPerKilopascal));
    }
    return points;
}

QVector<QPointF> AdiabaticModel::temperatureSeries() const {
    QVector<QPointF> points;
    const double total = duration();
    for (int i = 0; i <= 240; ++i) {
        const double t = total * i / 240.0;
        points.append(QPointF(t, temperatureAt(t)));
    }
    return points;
}

QVector<QPointF> AdiabaticModel::workSeries() const {
    QVector<QPointF> points;
    const double total = duration();
    for (int i = 0; i <= 240; ++i) {
        const double t = total * i / 240.0;
        points.append(QPointF(t, workAt(t)));
    }
    return points;
}

QVector<QPointF> AdiabaticModel::energyChangeSeries() const {
    QVector<QPointF> points;
    const double total = duration();
    for (int i = 0; i <= 240; ++i) {
        const double t = total * i / 240.0;
        points.append(QPointF(t, energyChangeAt(t)));
    }
    return points;
}

}  // namespace lab::models
