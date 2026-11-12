#include <gtest/gtest.h>

#include <cmath>

#include "TestSupport.h"
#include "models/AdiabaticModel.h"
#include "utils/MathText.h"

using lab::models::AdiabaticModel;
using lab::test::isInvalid;
using lab::test::setSetup;

namespace {

/** Универсальная газовая постоянная и число Авогадро — как в модели. */
constexpr double kR = 8.314462618;
constexpr double kAvogadro = 6.02214076e23;

/**
 * Учебная постановка: 1 моль воздуха (γ = 1,4, M = 29 г/моль) при 293 К
 * занимает 8 л; поршень площадью 50 см² медленно сжимает газ до 2 л.
 */
void configure(AdiabaticModel& model) {
    setSetup(model, QStringLiteral("gas=0; gamma=1.4; mass=29; nu=1; T1=293; V1=8; V2=2; S=50; p0=101.3; tau=6"));
}

/** То же, но расширение: объём растёт с 2 л до 8 л. */
void configureExpansion(AdiabaticModel& model) {
    setSetup(model, QStringLiteral("gas=0; gamma=1.4; mass=29; nu=1; T1=293; V1=2; V2=8; S=50; p0=101.3; tau=6"));
}

/** Численный интеграл ∫p dV по рядам давления — независимая проверка работы. */
double integratePressureOverVolume(const AdiabaticModel& model) {
    const double v1 = model.initialVolume();
    const double v2 = model.finalVolume();
    constexpr int kSteps = 20000;
    double sum = 0.0;
    for (int i = 0; i < kSteps; ++i) {
        const double a = v1 + (v2 - v1) * i / kSteps;
        const double b = v1 + (v2 - v1) * (i + 1) / kSteps;
        sum += 0.5 * (model.pressureForVolume(a) + model.pressureForVolume(b)) * (b - a);
    }
    return sum;
}

}  // namespace

// ------------------------------------------------------------------
// Газ: показатель адиабаты, молярная масса и теплоёмкости
// ------------------------------------------------------------------

TEST(Adiabatic, GasChoiceSetsGammaAndMolarMass) {
    AdiabaticModel air;
    configure(air);
    // Воздух — двухатомный газ: γ = 1,4, M = 29 г/моль.
    EXPECT_NEAR(air.gamma(), 1.4, 1e-12);
    EXPECT_NEAR(air.molarMass() * 1000.0, 29.0, 1e-9);

    AdiabaticModel helium;
    setSetup(helium, QStringLiteral("gas=3; nu=1; T1=293; V1=8; V2=2; S=50; p0=101.3; tau=6"));
    // Одноатомный газ: γ = 5/3.
    EXPECT_NEAR(helium.gamma(), 5.0 / 3.0, 1e-12);
    EXPECT_NEAR(helium.molarMass() * 1000.0, 4.0, 1e-9);

    // «Свой газ» — γ и M берутся из полей.
    AdiabaticModel custom;
    setSetup(custom, QStringLiteral("gas=7; gamma=1.2; mass=40; nu=1; T1=300; V1=5; V2=1; S=50; p0=101.3; tau=6"));
    EXPECT_NEAR(custom.gamma(), 1.2, 1e-12);
    EXPECT_NEAR(custom.molarMass() * 1000.0, 40.0, 1e-9);
}

TEST(Adiabatic, HeatCapacitiesFollowGamma) {
    AdiabaticModel model;
    configure(model);

    // Cv = R/(γ − 1), Cp = γR/(γ − 1) — эти же выражения в записи решения.
    EXPECT_NEAR(model.molarHeatCapacity(), kR / 0.4, 1e-9);
    EXPECT_NEAR(model.molarHeatCapacityP(), 1.4 * kR / 0.4, 1e-9);
    // Mayer: Cp − Cv = R, и их отношение — тот же γ.
    EXPECT_NEAR(model.molarHeatCapacityP() - model.molarHeatCapacity(), kR, 1e-9);
    EXPECT_NEAR(model.molarHeatCapacityP() / model.molarHeatCapacity(), model.gamma(), 1e-12);
}

TEST(Adiabatic, MoleculeCountComesFromAmountOfSubstance) {
    AdiabaticModel model;
    configure(model);
    EXPECT_NEAR(model.moleculeCount(), 1.0 * kAvogadro, 1e-9);

    AdiabaticModel half;
    configure(half);
    half.setParam(QStringLiteral("nu"), 0.5);
    EXPECT_NEAR(half.moleculeCount(), 0.5 * model.moleculeCount(), 1e-9);
}

// ------------------------------------------------------------------
// Начальное состояние и уравнение адиабаты
// ------------------------------------------------------------------

TEST(Adiabatic, InitialPressureComesFromIdealGasLaw) {
    AdiabaticModel model;
    configure(model);

    // p₁ = νRT₁/V₁, объём — в м³ (8 л = 0,008 м³).
    EXPECT_NEAR(model.initialPressure(), 1.0 * kR * 293.0 / 0.008, 1e-6);
    EXPECT_GT(model.initialPressure(), 0.0);
}

TEST(Adiabatic, AdiabatKeepsPVToTheGammaConstant) {
    AdiabaticModel model;
    configure(model);

    const double gamma = model.gamma();
    // p·V^γ одинаков в начале, в конце и в любой промежуточной точке.
    const double constant = model.initialPressure() * std::pow(model.initialVolume(), gamma);
    EXPECT_NEAR(model.finalPressure() * std::pow(model.finalVolume(), gamma), constant, 1e-6 * constant);
    for (double fraction : {0.2, 0.5, 0.8}) {
        const double t = model.processTime() * fraction;
        const double value = model.pressureAt(t) * std::pow(model.volumeAt(t), gamma);
        EXPECT_NEAR(value, constant, 1e-6 * constant) << "доля времени " << fraction;
    }
}

TEST(Adiabatic, TemperatureFollowsVolumeLaw) {
    AdiabaticModel model;
    configure(model);

    const double expected = 293.0 * std::pow(8.0 / 2.0, model.gamma() - 1.0);
    EXPECT_NEAR(model.finalTemperature(), expected, 1e-9);
    EXPECT_GT(model.finalTemperature(), model.initialTemperature());

    // Тот же закон в промежуточных точках.
    for (double fraction : {0.25, 0.5, 0.75}) {
        const double t = model.processTime() * fraction;
        const double ratio = model.initialVolume() / model.volumeAt(t);
        EXPECT_NEAR(model.temperatureAt(t), 293.0 * std::pow(ratio, model.gamma() - 1.0), 1e-9);
    }
}

TEST(Adiabatic, EndStateAlsoSatisfiesIdealGasLaw) {
    AdiabaticModel model;
    configure(model);

    // Проверка «двумя дорогами»: p₂V₂ = νRT₂ следует и из адиабаты, и из МK.
    EXPECT_NEAR(model.finalPressure() * model.finalVolume(), 1.0 * kR * model.finalTemperature(), 1e-6);
    // В любой середине процесса — тоже.
    for (double fraction : {0.3, 0.6, 0.9}) {
        const double t = model.processTime() * fraction;
        EXPECT_NEAR(model.pressureAt(t) * model.volumeAt(t), 1.0 * kR * model.temperatureAt(t), 1e-6);
    }
}

TEST(Adiabatic, CompressionHeatsGasAndExpansionCoolsIt) {
    AdiabaticModel compression;
    configure(compression);
    AdiabaticModel expansion;
    configureExpansion(expansion);

    EXPECT_GT(compression.finalTemperature(), compression.initialTemperature());
    EXPECT_LT(expansion.finalTemperature(), expansion.initialTemperature());
    // Давление меняется согласно тому же закону.
    EXPECT_GT(compression.finalPressure(), compression.initialPressure());
    EXPECT_LT(expansion.finalPressure(), expansion.initialPressure());

    // Одноатомный газ разогревается сильнее: γ больше — показатель больше.
    AdiabaticModel helium;
    setSetup(helium, QStringLiteral("gas=3; nu=1; T1=293; V1=8; V2=2; S=50; p0=101.3; tau=6"));
    EXPECT_GT(helium.finalTemperature(), compression.finalTemperature());
}

TEST(Adiabatic, PressureAndTemperatureAreMonotonicAlongProcess) {
    AdiabaticModel model;
    configure(model);

    double previousPressure = 0.0;
    double previousTemperature = 0.0;
    for (int i = 0; i <= 60; ++i) {
        const double t = model.processTime() * i / 60.0;
        if (i > 0) {
            EXPECT_GT(model.pressureAt(t), previousPressure) << "момент " << t;
            EXPECT_GT(model.temperatureAt(t), previousTemperature) << "момент " << t;
        }
        previousPressure = model.pressureAt(t);
        previousTemperature = model.temperatureAt(t);
    }
}

// ------------------------------------------------------------------
// Закон движения поршня
// ------------------------------------------------------------------

TEST(Adiabatic, VolumeChangesLinearlyAndStopsAfterProcess) {
    AdiabaticModel model;
    configure(model);

    EXPECT_NEAR(model.volumeAt(0.0), 0.008, 1e-12);
    EXPECT_NEAR(model.volumeAt(model.processTime()), 0.002, 1e-12);
    // Линейный закон: середина времени — середина объёма.
    EXPECT_NEAR(model.volumeAt(model.processTime() * 0.5), (0.008 + 0.002) / 2.0, 1e-12);
    // После конца процесса объём не меняется.
    EXPECT_NEAR(model.volumeAt(model.duration()), 0.002, 1e-12);
}

TEST(Adiabatic, PistonSpeedIsConstantAndZeroAfterwards) {
    AdiabaticModel model;
    configure(model);

    // V = S·h ⇒ скорость поршня v = (ΔV/τ)/S.
    const double expected = (0.002 - 0.008) / 6.0 / 0.005;
    EXPECT_NEAR(model.pistonSpeedAt(0.0), expected, 1e-12);
    EXPECT_NEAR(model.pistonSpeedAt(3.0), expected, 1e-12);
    EXPECT_NEAR(model.pistonSpeedAt(model.duration()), 0.0, 1e-12);
    // При сжатии поршень идёт вниз.
    EXPECT_LT(model.pistonSpeedAt(1.0), 0.0);
}

TEST(Adiabatic, ObservationCoversWholeProcess) {
    AdiabaticModel model;
    configure(model);

    EXPECT_GT(model.duration(), model.processTime());
    EXPECT_NEAR(model.eventTime(), model.processTime(), 1e-12);
    EXPECT_GT(model.stepSize(), 0.0);
    EXPECT_GT(model.timePerSecond(), 0.0);

    const lab::SimState before = model.stateAt(model.processTime() * 0.5);
    EXPECT_FALSE(before.resolved);
    const lab::SimState after = model.stateAt(model.duration());
    EXPECT_TRUE(after.resolved);
    EXPECT_FALSE(after.eventLabel.isEmpty());
}

// ------------------------------------------------------------------
// Работа, внутренняя энергия и первое начало
// ------------------------------------------------------------------

TEST(Adiabatic, WorkEqualsIntegralOfPressure) {
    AdiabaticModel model;
    configure(model);

    // Независимое численное интегрирование ∫p dV (трапеции, 20000 шагов).
    EXPECT_NEAR(model.totalWork(), integratePressureOverVolume(model), 1e-3);
    // Замкнутое выражение A = (p₁V₁ − p₂V₂)/(γ − 1) даёт то же число.
    const double closed = (model.initialPressure() * model.initialVolume() -
                           model.finalPressure() * model.finalVolume()) / (model.gamma() - 1.0);
    EXPECT_NEAR(model.totalWork(), closed, 1e-6);
}

TEST(Adiabatic, WorkSignFollowsVolumeChange) {
    AdiabaticModel compression;
    configure(compression);
    AdiabaticModel expansion;
    configureExpansion(expansion);

    // Сжатие: газ работу не совершает, а её совершают над ним.
    EXPECT_LT(compression.totalWork(), 0.0);
    EXPECT_GT(expansion.totalWork(), 0.0);
    EXPECT_NEAR(compression.workAt(0.0), 0.0, 1e-12);
    EXPECT_NEAR(expansion.workAt(0.0), 0.0, 1e-12);
}

TEST(Adiabatic, FirstLawClosesAtEveryMoment) {
    AdiabaticModel model;
    configure(model);

    // Q = 0, поэтому A = −ΔU в любой момент, а не только в конце.
    for (int i = 0; i <= 40; ++i) {
        const double t = model.duration() * i / 40.0;
        EXPECT_NEAR(model.workAt(t), -model.energyChangeAt(t), 1e-6) << "момент " << t;
    }
    EXPECT_NEAR(model.heatAmount(), 0.0, 1e-15);

    // ΔU = νCv(T − T₁) — та же величина из теплоёмкости.
    const double expected = 1.0 * model.molarHeatCapacity() * (model.finalTemperature() - 293.0);
    EXPECT_NEAR(model.energyChangeAt(model.processTime()), expected, 1e-6);
    // При сжатии внутренняя энергия растёт.
    EXPECT_GT(model.energyChangeAt(model.processTime()), 0.0);
}

TEST(Adiabatic, InternalEnergyIsProportionalToTemperature) {
    AdiabaticModel model;
    configure(model);

    EXPECT_NEAR(model.internalEnergyAt(0.0), 1.0 * model.molarHeatCapacity() * 293.0, 1e-9);
    EXPECT_NEAR(model.internalEnergyAt(model.processTime()) / model.internalEnergyAt(0.0),
                model.finalTemperature() / model.initialTemperature(), 1e-12);
}

// ------------------------------------------------------------------
// Силы на поршень
// ------------------------------------------------------------------

TEST(Adiabatic, GasForceIsPressureTimesArea) {
    AdiabaticModel model;
    configure(model);

    EXPECT_NEAR(model.gasForceAt(0.0), model.initialPressure() * 0.005, 1e-9);
    EXPECT_NEAR(model.gasForceAt(model.processTime()), model.finalPressure() * 0.005, 1e-9);
    EXPECT_NEAR(model.outsideForce(), 101300.0 * 0.005, 1e-9);
    // Сила растёт вместе с давлением.
    EXPECT_GT(model.gasForceAt(model.processTime()), model.gasForceAt(0.0));
}

TEST(Adiabatic, ExternalForceBalancesThePiston) {
    AdiabaticModel model;
    configure(model);

    // Медленный процесс: поршень уравновешен, внешняя сила — разность давлений.
    EXPECT_NEAR(model.externalForceAt(0.0), model.gasForceAt(0.0) - model.outsideForce(), 1e-9);
    EXPECT_NEAR(model.externalForceAt(model.processTime()),
                model.gasForceAt(model.processTime()) - model.outsideForce(), 1e-9);

    // Давление выше атмосферного — силу приходится прикладывать вниз, то есть
    // она направлена против выталкивания газом.
    EXPECT_GT(model.externalForceAt(model.processTime()), 0.0);

    // Если внешнее давление больше внутреннего, знак силы меняется:
    // поршень приходится удерживать от опускания, а не от подъёма.
    AdiabaticModel outside;
    configure(outside);
    outside.setParam(QStringLiteral("V1"), 20.0);   // p₁ ≈ 122 кПа < p₀
    outside.setParam(QStringLiteral("p0"), 300.0);
    EXPECT_LT(outside.initialPressure(), outside.outsidePressure());
    EXPECT_LT(outside.externalForceAt(0.0), 0.0);
}

TEST(Adiabatic, AreaChangesForceButNotPressureOrTemperature) {
    AdiabaticModel model;
    configure(model);
    AdiabaticModel wide;
    configure(wide);
    wide.setParam(QStringLiteral("S"), 100.0);

    // Давление и температура от площади поршня не зависят.
    EXPECT_NEAR(wide.finalPressure(), model.finalPressure(), 1e-12);
    EXPECT_NEAR(wide.finalTemperature(), model.finalTemperature(), 1e-12);
    EXPECT_NEAR(wide.totalWork(), model.totalWork(), 1e-12);
    // А сила — пропорциональна площади.
    EXPECT_NEAR(wide.gasForceAt(model.processTime()), 2.0 * model.gasForceAt(model.processTime()), 1e-9);
}

// ------------------------------------------------------------------
// Скорость молекул
// ------------------------------------------------------------------

TEST(Adiabatic, RmsSpeedFollowsTemperatureAndMolarMass) {
    AdiabaticModel model;
    configure(model);

    // v = √(3RT/M), M — в кг/моль.
    EXPECT_NEAR(model.rmsSpeedAt(293.0), std::sqrt(3.0 * kR * 293.0 / 0.029), 1e-9);
    EXPECT_NEAR(model.rmsSpeedAtTime(0.0), model.rmsSpeedAt(293.0), 1e-12);

    // Отношение скоростей — корень из отношения температур (и степень сжатия).
    const double ratio = model.rmsSpeedAt(model.finalTemperature()) / model.rmsSpeedAt(293.0);
    EXPECT_NEAR(ratio, std::sqrt(model.finalTemperature() / 293.0), 1e-12);
    EXPECT_NEAR(ratio, std::pow(model.compressionRatio(), (model.gamma() - 1.0) / 2.0), 1e-12);

    // Более лёгкий газ движется быстрее при той же температуре.
    AdiabaticModel helium;
    setSetup(helium, QStringLiteral("gas=3; nu=1; T1=293; V1=8; V2=2; S=50; p0=101.3; tau=6"));
    EXPECT_GT(helium.rmsSpeedAt(293.0), model.rmsSpeedAt(293.0));
}

TEST(Adiabatic, MoleculeSpeedGrowsAlongCompression) {
    AdiabaticModel model;
    configure(model);

    double previous = 0.0;
    for (int i = 0; i <= 20; ++i) {
        const double t = model.processTime() * i / 20.0;
        if (i > 0)
            EXPECT_GT(model.rmsSpeedAtTime(t), previous) << "момент " << t;
        previous = model.rmsSpeedAtTime(t);
    }
}

// ------------------------------------------------------------------
// Состояние системы
// ------------------------------------------------------------------

TEST(Adiabatic, StateCarriesPistonGasAndMolecules) {
    AdiabaticModel model;
    configure(model);
    const double t = model.processTime() * 0.5;

    const lab::SimState state = model.stateAt(t);
    ASSERT_GE(state.entities.size(), 3);

    EXPECT_EQ(state.entities.at(0).id, QStringLiteral("piston"));
    // Положение поршня — высота столба газа h = V/S.
    EXPECT_NEAR(state.entities.at(0).position, model.volumeAt(t) / model.pistonArea(), 1e-12);
    EXPECT_NEAR(state.entities.at(0).value1, model.pistonSpeedAt(t), 1e-12);

    EXPECT_EQ(state.entities.at(1).id, QStringLiteral("gas"));
    EXPECT_NEAR(state.entities.at(1).value1, model.pressureAt(t), 1e-9);
    EXPECT_NEAR(state.entities.at(1).value2, model.temperatureAt(t), 1e-9);

    // Молекулы заданы: направление — единичный вектор, оно не зависит от времени.
    int molecules = 0;
    for (int i = 2; i < state.entities.size(); ++i) {
        const lab::EntityVisual& molecule = state.entities.at(i);
        if (!molecule.id.startsWith(QStringLiteral("molecule_")))
            continue;
        ++molecules;
        EXPECT_NEAR(std::hypot(molecule.value1, molecule.value2), 1.0, 1e-9) << molecule.id.toUtf8().constData();
        EXPECT_TRUE(molecule.active);
    }
    EXPECT_GT(molecules, 0);

    EXPECT_NEAR(state.primary, model.pressureAt(t), 1e-9);
    EXPECT_NEAR(state.secondary, model.temperatureAt(t), 1e-9);
}

TEST(Adiabatic, MoleculeDirectionsDoNotDriftWithTime) {
    AdiabaticModel model;
    configure(model);

    // Направления фиксированы: меняются только скорость и объём.
    const lab::SimState first = model.stateAt(0.0);
    const lab::SimState last = model.stateAt(model.duration());
    ASSERT_EQ(first.entities.size(), last.entities.size());
    for (int i = 2; i < first.entities.size(); ++i) {
        EXPECT_NEAR(first.entities.at(i).value1, last.entities.at(i).value1, 1e-12);
        EXPECT_NEAR(first.entities.at(i).value2, last.entities.at(i).value2, 1e-12);
    }
}

// ------------------------------------------------------------------
// Допустимость постановки
// ------------------------------------------------------------------

TEST(Adiabatic, RejectsMotionlessPiston) {
    AdiabaticModel model;
    setSetup(model, QStringLiteral("gas=0; nu=1; T1=293; V1=5; V2=5; S=50; p0=101.3; tau=6"));
    EXPECT_TRUE(isInvalid(model));
}

TEST(Adiabatic, RejectsGammaNotAboveOne) {
    AdiabaticModel model;
    // «Свой газ» с γ = 1: теплоёмкость бесконечна, температуры нет.
    setSetup(model, QStringLiteral("gas=7; gamma=1; mass=29; nu=1; T1=293; V1=8; V2=2; S=50; p0=101.3; tau=6"));
    EXPECT_TRUE(isInvalid(model));

    AdiabaticModel ordinary;
    configure(ordinary);
    EXPECT_FALSE(isInvalid(ordinary));
}

TEST(Adiabatic, OutOfRangeValuesAreClampedNotRejected) {
    // Панель не пропускает ноль и отрицательные значения: берётся минимум
    // диапазона, и расчёт остаётся корректным.
    AdiabaticModel model;
    setSetup(model, QStringLiteral("gas=0; nu=0; T1=0; V1=0; V2=2; S=0; p0=-5; tau=0"));
    EXPECT_GT(model.substance(), 0.0);
    EXPECT_GT(model.initialTemperature(), 0.0);
    EXPECT_GT(model.initialVolume(), 0.0);
    EXPECT_GT(model.pistonArea(), 0.0);
    EXPECT_GT(model.processTime(), 0.0);
    EXPECT_GE(model.outsidePressure(), 0.0);
    EXPECT_FALSE(isInvalid(model));
    EXPECT_TRUE(std::isfinite(model.finalPressure()));
}

TEST(Adiabatic, ExtremeCompressionIsRejectedNotCrashed) {
    AdiabaticModel model;
    // 60 л сжимаются в 0,5 л: степень сжатия 120, давление конечно, но
    // постановка на грани — проверка, что числа не превращаются в inf.
    setSetup(model, QStringLiteral("gas=3; nu=5; T1=800; V1=60; V2=0.5; S=600; p0=300; tau=30"));
    EXPECT_TRUE(std::isfinite(model.finalPressure()));
    EXPECT_TRUE(std::isfinite(model.finalTemperature()));
    EXPECT_TRUE(std::isfinite(model.totalWork()));
}

// ------------------------------------------------------------------
// Запись решения
// ------------------------------------------------------------------

TEST(Adiabatic, DerivationContainsKeyFormulas) {
    AdiabaticModel model;
    configure(model);

    const lab::Derivation derivation = model.derivation();
    ASSERT_TRUE(derivation.valid);
    ASSERT_FALSE(derivation.steps.isEmpty());

    QString all;
    for (const lab::DerivationStep& step : derivation.steps)
        all += step.content;

    EXPECT_TRUE(all.contains(QStringLiteral("pV^{\\gamma} = \\text{const}")));
    EXPECT_TRUE(all.contains(QStringLiteral("C_{V} = \\frac{R}{\\gamma - 1}")));
    EXPECT_TRUE(all.contains(QStringLiteral("p_{1} = \\frac{\\nu R T_{1}}{V_{1}}")));
    EXPECT_TRUE(all.contains(QStringLiteral("Q = 0")));
    EXPECT_TRUE(all.contains(QStringLiteral("A = -\\Delta U")));
    EXPECT_TRUE(all.contains(QStringLiteral("\\frac{p_{1} V_{1} - p_{2} V_{2}}{\\gamma - 1}")));
    EXPECT_TRUE(all.contains(QStringLiteral("\\sqrt{\\frac{3RT}{M}}")));
}

TEST(Adiabatic, DerivationQuotesModelNumbers) {
    AdiabaticModel model;
    configure(model);

    const lab::Derivation derivation = model.derivation();
    QString all;
    for (const lab::DerivationStep& step : derivation.steps)
        all += step.content;

    // Числа берутся из расчёта, а не «на глаз»: давление, температура, работа.
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.finalPressure() / 1e3, 1)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.finalTemperature(), 1)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.totalWork(), 1)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.energyChangeAt(model.processTime()), 1)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.rmsSpeedAt(model.finalTemperature()), 0)));
}

TEST(Adiabatic, DerivationRendersWithoutUnknownCommands) {
    AdiabaticModel model;
    configure(model);

    // Неподдержанная команда оставляет обратный слэш — пользователь увидел бы
    // «\gamma» вместо символа.
    const lab::Derivation derivation = model.derivation();
    ASSERT_TRUE(derivation.valid);
    for (const lab::DerivationStep& step : derivation.steps) {
        const QString html = lab::utils::toRichMath(step.content);
        EXPECT_EQ(html.count(QLatin1Char('\\')), 0) << step.kind.toUtf8().constData() << ": "
                                                    << html.toUtf8().constData();
        EXPECT_EQ(html.count(QLatin1Char('{')), 0) << step.kind.toUtf8().constData();
        EXPECT_EQ(html.count(QLatin1Char('}')), 0) << step.kind.toUtf8().constData();
    }
}

TEST(Adiabatic, DerivationIsDimensionallyConsistent) {
    AdiabaticModel model;
    configure(model);

    // Все подстановки — в единицах СИ, иначе равенства были бы разноимёнными:
    // объём в м³, давление в Па, площадь в м².
    const lab::Derivation derivation = model.derivation();
    QString all;
    for (const lab::DerivationStep& step : derivation.steps)
        all += step.content;
    EXPECT_TRUE(all.contains(QStringLiteral("\\text{ м}^{3}")));
    EXPECT_TRUE(all.contains(QStringLiteral("\\text{ м}^{2}")));
    EXPECT_TRUE(all.contains(QStringLiteral("\\text{ Па}")));
    // Числовая проверка работы: p₁V₁ в паскалях и м³ даёт джоули.
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.initialPressure() * model.initialVolume(), 1)));
}

TEST(Adiabatic, AnswerQuotesEndState) {
    AdiabaticModel model;
    configure(model);

    const lab::Derivation derivation = model.derivation();
    EXPECT_TRUE(derivation.answerTemplate.contains(lab::utils::formatNumber(model.finalPressure() / 1e3, 1)));
    EXPECT_TRUE(derivation.answerTemplate.contains(lab::utils::formatNumber(model.finalTemperature(), 1)));
    EXPECT_TRUE(derivation.answerTemplate.contains(lab::utils::formatNumber(model.totalWork(), 1)));
}

TEST(Adiabatic, DerivationReportsInvalidSetup) {
    AdiabaticModel model;
    setSetup(model, QStringLiteral("gas=0; nu=1; T1=293; V1=5; V2=5; S=50; p0=101.3; tau=6"));

    const lab::Derivation derivation = model.derivation();
    EXPECT_FALSE(derivation.valid);
    EXPECT_FALSE(derivation.answerTemplate.isEmpty());
}

TEST(Adiabatic, ReadoutsFollowModelState) {
    AdiabaticModel model;
    configure(model);
    model.seekTo(model.processTime() * 0.5);
    const double t = model.time();

    const QVector<lab::Readout> readouts = model.readouts();
    ASSERT_FALSE(readouts.isEmpty());

    QString all;
    for (const lab::Readout& readout : readouts)
        all += readout.label + readout.value;
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.pressureAt(t) / 1e3, 1)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.temperatureAt(t), 1)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.workAt(t), 1)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.rmsSpeedAtTime(t), 0)));
    // Теплообмен адиабаты — ноль, и это сказано в показаниях.
    EXPECT_TRUE(all.contains(QStringLiteral("теплоизолирован")));
}

// ------------------------------------------------------------------
// Точки взаимодействия
// ------------------------------------------------------------------

TEST(Adiabatic, HandlesCoverVolumesAndPistonArea) {
    AdiabaticModel model;
    configure(model);

    const QVector<lab::HandleInfo> handles = model.handles();
    ASSERT_EQ(handles.size(), 3);

    // Ручки называются иначе, чем параметры панели: проверка соответствия.
    QStringList ids;
    for (const lab::HandleInfo& handle : handles)
        ids.push_back(handle.id);
    EXPECT_TRUE(ids.contains(QStringLiteral("piston")));
    EXPECT_TRUE(ids.contains(QStringLiteral("target")));
    EXPECT_TRUE(ids.contains(QStringLiteral("area")));

    const double pressureBefore = model.finalPressure();
    model.setHandleValue(QStringLiteral("target"), 4.0);
    EXPECT_NEAR(model.finalVolume(), 0.004, 1e-9);
    EXPECT_NE(model.finalPressure(), pressureBefore);

    model.setHandleValue(QStringLiteral("piston"), 10.0);
    EXPECT_NEAR(model.initialVolume(), 0.010, 1e-9);

    model.setHandleValue(QStringLiteral("area"), 100.0);
    EXPECT_NEAR(model.pistonArea(), 0.010, 1e-12);
}

TEST(Adiabatic, HandleReadbackShowsComputedValues) {
    AdiabaticModel model;
    configure(model);

    const QVector<lab::HandleInfo> handles = model.handles();
    ASSERT_EQ(handles.size(), 3);
    for (const lab::HandleInfo& handle : handles) {
        EXPECT_FALSE(handle.readbackValue.isEmpty()) << handle.id.toUtf8().constData();
        EXPECT_FALSE(handle.axis.isEmpty()) << handle.id.toUtf8().constData();
    }
}

TEST(Adiabatic, DraggingRestartsTheProcess) {
    AdiabaticModel model;
    configure(model);
    model.seekTo(model.duration());
    ASSERT_GT(model.time(), 0.0);

    model.beginInteraction();
    EXPECT_NEAR(model.time(), 0.0, 1e-12);
}

// ------------------------------------------------------------------
// Ряды данных
// ------------------------------------------------------------------

TEST(Adiabatic, PressureVersusVolumeIsAdiabatAndIsothermIsFlatter) {
    AdiabaticModel model;
    configure(model);

    const QVector<QPointF> adiabat = model.pressureVersusVolume();
    ASSERT_GE(adiabat.size(), 100);
    // Ряд построен от меньшего объёма к большему.
    EXPECT_LT(adiabat.first().x(), adiabat.last().x());
    for (const QPointF& point : adiabat) {
        EXPECT_GT(point.x(), 0.0);
        EXPECT_GT(point.y(), 0.0);
    }
    // В одних и тех же точках адиабата круче изотермы: при сжатии газ ещё и
    // разогревается, поэтому давление растёт быстрее.
    const QVector<QPointF> isotherm = model.isothermVersusVolume();
    ASSERT_EQ(isotherm.size(), adiabat.size());
    // Адиабата проходит через то же начальное состояние, поэтому в точке V₁
    // кривые совпадают, а при меньших объёмах (сжатие) давление на адиабате
    // выше: газ кроме уменьшения объёма ещё и разогревается.
    const double startVolume = model.initialVolume() * 1e3;
    for (int i = 1; i < adiabat.size(); ++i) {
        if (std::abs(adiabat.at(i).x() - startVolume) < 1e-9) {
            EXPECT_NEAR(adiabat.at(i).y(), isotherm.at(i).y(), 1e-9) << "точка " << i;
        } else {
            EXPECT_GT(adiabat.at(i).y(), isotherm.at(i).y()) << "точка " << i;
        }
        EXPECT_LE(adiabat.at(i).y(), adiabat.at(i - 1).y() + 1e-9) << "точка " << i;
    }
    // Изотерма проходит через начальное состояние.
    int nearest = 0;
    for (int i = 1; i < isotherm.size(); ++i) {
        if (std::abs(isotherm.at(i).x() - startVolume) < std::abs(isotherm.at(nearest).x() - startVolume))
            nearest = i;
    }
    EXPECT_NEAR(isotherm.at(nearest).y(), model.initialPressure() / 1e3, 1e-6 * model.initialPressure() / 1e3);
}

TEST(Adiabatic, TemperatureVersusVolumeMatchesLaw) {
    AdiabaticModel model;
    configure(model);

    const QVector<QPointF> series = model.temperatureVersusVolume();
    ASSERT_GE(series.size(), 100);
    for (const QPointF& point : series) {
        const double expected = model.initialTemperature() *
                                std::pow(model.initialVolume() / (point.x() * 1e-3), model.gamma() - 1.0);
        EXPECT_NEAR(point.y(), expected, 1e-9 * expected) << "V = " << point.x();
    }
}

TEST(Adiabatic, PressureVersusTemperatureUsesAdiabatExponent) {
    AdiabaticModel model;
    configure(model);

    // p ∝ T^(γ/(γ−1)) — та же адиабата, только в других координатах.
    const QVector<QPointF> series = model.pressureVersusTemperature();
    ASSERT_GE(series.size(), 100);
    const double exponent = model.gamma() / (model.gamma() - 1.0);
    const double constant = model.initialPressure() / std::pow(model.initialTemperature(), exponent);
    for (const QPointF& point : series)
        EXPECT_NEAR(point.y(), constant * std::pow(point.x(), exponent) / 1e3, 1e-6 * point.y());
}

TEST(Adiabatic, TimeSeriesMatchStateValues) {
    AdiabaticModel model;
    configure(model);

    const QVector<QPointF> volume = model.volumeSeries();
    const QVector<QPointF> pressure = model.pressureSeries();
    const QVector<QPointF> temperature = model.temperatureSeries();
    const QVector<QPointF> work = model.workSeries();
    const QVector<QPointF> energy = model.energyChangeSeries();
    ASSERT_FALSE(volume.isEmpty());
    ASSERT_EQ(pressure.size(), volume.size());
    ASSERT_EQ(work.size(), volume.size());

    EXPECT_NEAR(volume.first().x(), 0.0, 1e-12);
    EXPECT_NEAR(volume.last().x(), model.duration(), 1e-9);
    for (int i = 0; i < volume.size(); ++i) {
        const double t = volume.at(i).x();
        EXPECT_NEAR(volume.at(i).y(), model.volumeAt(t) * 1e3, 1e-9);
        EXPECT_NEAR(pressure.at(i).y(), model.pressureAt(t) / 1e3, 1e-9);
        EXPECT_NEAR(temperature.at(i).y(), model.temperatureAt(t), 1e-9);
        EXPECT_NEAR(work.at(i).y(), model.workAt(t), 1e-9);
        // Первое начало выполняется и в рядах: A(t) = −ΔU(t).
        EXPECT_NEAR(work.at(i).y(), -energy.at(i).y(), 1e-9);
    }
}
