#include <gtest/gtest.h>

#include <cmath>

#include "TestSupport.h"
#include "models/BacteriaModel.h"
#include "utils/MathText.h"

using lab::models::BacteriaModel;
using lab::test::isInvalid;
using lab::test::setSetup;

namespace {

/** Логарифм двух — та же константа, что в модели. */
constexpr double kLnTwo = 0.6931471805599453;

/**
 * Учебная постановка: посев 1000 клеток, T_d = 2 ч, наблюдение 12 ч
 * (шесть делений), интервал учёта 1 ч, цель — 32000 клеток.
 */
void configure(BacteriaModel& model) {
    setSetup(model, QStringLiteral("species=4; doubling=2; N0=1000; T=12; step=1; target=32000"));
}

/** Постановка с мелким посевом: видно каждое отдельное деление. */
void configureSmall(BacteriaModel& model) {
    setSetup(model, QStringLiteral("species=4; doubling=1; N0=1; T=8; step=1; target=128"));
}

/** Значение i-го ряда по абсциссе, ближайшей к t. */

/** Наклон ряда точек по крайним точкам. */
double seriesSlope(const QVector<QPointF>& series) {
    if (series.size() < 2)
        return std::numeric_limits<double>::quiet_NaN();
    const QPointF& first = series.first();
    const QPointF& last = series.last();
    if (last.x() == first.x())
        return std::numeric_limits<double>::quiet_NaN();
    return (last.y() - first.y()) / (last.x() - first.x());
}

}  // namespace

// ------------------------------------------------------------------
// Постоянная роста и две записи одного закона
// ------------------------------------------------------------------

TEST(Bacteria, GrowthRateComesFromDoublingTime) {
    BacteriaModel model;
    configure(model);

    // k = ln2/T_d: связь выводится из N(T_d) = 2N₀.
    EXPECT_NEAR(model.growthRate(), kLnTwo / 2.0, 1e-12);
    EXPECT_NEAR(model.growthRate() * model.doublingTime(), kLnTwo, 1e-12);
}

TEST(Bacteria, BothWritingsOfTheLawAgree) {
    BacteriaModel model;
    configure(model);

    // N₀·2^(t/T_d) и N₀·e^(kt) — одно и то же число при любом t.
    for (double fraction = 0.0; fraction <= 1.001; fraction += 0.1) {
        const double t = model.observationTime() * fraction;
        EXPECT_NEAR(model.theoryCellsAt(t), model.theoryCellsByRate(t), 1e-9 * model.theoryCellsAt(t))
            << "момент " << t;
    }
}

TEST(Bacteria, DoublingTimeExactlyDoublesTheColony) {
    BacteriaModel model;
    configure(model);

    // Через каждый период удвоения клеток ровно вдвое больше.
    for (int n = 1; n <= 5; ++n) {
        const double t = model.doublingTime() * double(n);
        EXPECT_NEAR(model.theoryCellsAt(t), model.initialCells() * std::exp2(double(n)), 1e-9)
            << "делений " << n;
        EXPECT_NEAR(model.multiplicationFactorAt(t), std::exp2(double(n)), 1e-9);
    }
}

TEST(Bacteria, ColonyGrowsMonotonically) {
    BacteriaModel model;
    configure(model);

    double previous = 0.0;
    for (double fraction = 0.0; fraction <= 1.001; fraction += 0.05) {
        const double cells = model.theoryCellsAt(model.observationTime() * fraction);
        EXPECT_GT(cells, previous) << "доля времени " << fraction;
        previous = cells;
    }
}

// ------------------------------------------------------------------
// Число делений: из времени и из числа клеток
// ------------------------------------------------------------------

TEST(Bacteria, DivisionsComeFromTimeAndFromCellCount) {
    BacteriaModel model;
    configure(model);

    for (double fraction = 0.05; fraction <= 1.001; fraction += 0.15) {
        const double t = model.observationTime() * fraction;
        const double fromTime = model.divisionsElapsedAt(t);
        // Тот же показатель получают логарифмированием числа клеток.
        const double fromCells = std::log2(model.theoryCellsAt(t) / model.initialCells());
        EXPECT_NEAR(fromTime, fromCells, 1e-9) << "момент " << t;
    }
}

TEST(Bacteria, DivisionsElapsedIsExactlyTimeOverDoubling) {
    BacteriaModel model;
    configure(model);

    EXPECT_NEAR(model.divisionsElapsedAt(3.0), 1.5, 1e-12);
    EXPECT_NEAR(model.divisionsElapsedAt(model.doublingTime()), 1.0, 1e-12);
    EXPECT_NEAR(model.completedDivisionsAt(3.0), 1.0, 1e-12);
    EXPECT_NEAR(model.completedDivisionsAt(2.0), 1.0, 1e-12);
    EXPECT_NEAR(model.completedDivisionsAt(1.999), 0.0, 1e-12);
}

TEST(Bacteria, SteppedCountDoublesAtWholeDivisionsOnly) {
    BacteriaModel model;
    configure(model);

    /*
     * Ступенька: в сам момент деления клеток уже вдвое больше, а непосред-
     * ственно перед ним — столько же, сколько было весь предыдущий интервал.
     */
    const double life = model.doublingTime();
    for (int n = 0; n <= 4; ++n) {
        const double moment = life * double(n);
        EXPECT_NEAR(model.steppedCellsAt(moment), model.initialCells() * std::exp2(double(n)), 1e-9);
        if (n > 0)
            EXPECT_NEAR(model.steppedCellsAt(moment - 1e-9), model.initialCells() * std::exp2(double(n - 1)), 1e-9);
        // На середине интервала деления ещё не случилось.
        EXPECT_NEAR(model.steppedCellsAt(moment + life * 0.5), model.initialCells() * std::exp2(double(n)), 1e-9);
    }
}

TEST(Bacteria, SteppedCountNeverExceedsTheSmoothLaw) {
    BacteriaModel model;
    configure(model);

    // Ступенька отстаёт от плавной кривой: деления ещё не завершились.
    for (double fraction = 0.0; fraction <= 1.001; fraction += 0.1) {
        const double t = model.observationTime() * fraction;
        EXPECT_LE(model.steppedCellsAt(t), model.theoryCellsAt(t) + 1e-9) << "момент " << t;
    }
}

TEST(Bacteria, CountedCellsAreWholeNumbers) {
    BacteriaModel model;
    configure(model);

    for (double fraction = 0.0; fraction <= 1.001; fraction += 0.1) {
        const double counted = model.countedCellsAt(model.observationTime() * fraction);
        EXPECT_EQ(counted, std::round(counted)) << "округление должно давать целое";
    }
    // На моментах делений наблюдатель видит в точности удвоенный посев.
    EXPECT_NEAR(model.countedCellsAt(model.doublingTime()), 2.0 * model.initialCells(), 0.5);
    EXPECT_NEAR(model.countedCellsAt(4.0), 4.0 * model.initialCells(), 0.5);
}

// ------------------------------------------------------------------
// Время достижения заданного числа клеток
// ------------------------------------------------------------------

TEST(Bacteria, TargetTimeIsDoublingTimesLogarithm) {
    BacteriaModel model;
    configure(model);

    // t = T_d·log₂(N/N₀) — логарифм от отношения чисел клеток.
    const double expected = 2.0 * std::log2(32000.0 / 1000.0);
    EXPECT_NEAR(model.targetTime(), expected, 1e-9);
    EXPECT_NEAR(model.divisionsToTarget(), std::log2(32.0), 1e-12);
    // Подстановка момента в закон даёт именно целевое число.
    EXPECT_NEAR(model.theoryCellsAt(model.targetTime()), model.targetCells(), 1e-6);
}

TEST(Bacteria, TargetTimeScalesWithDoublingTime) {
    BacteriaModel model;
    configure(model);
    const double base = model.targetTime();

    // Тот же опыт вдвое с более медленным удвоением длится вдвое дольше.
    model.setParam(QStringLiteral("doubling"), 4.0);
    EXPECT_NEAR(model.targetTime(), 2.0 * base, 1e-9);
    // Число делений до цели от скорости не зависит.
    EXPECT_NEAR(model.divisionsToTarget(), std::log2(32.0), 1e-12);
}

TEST(Bacteria, TargetBelowSeedNeedsNoDivisions) {
    BacteriaModel model;
    configure(model);

    // Если нужное число уже есть в посеве, делений не нужно вовсе.
    model.setParam(QStringLiteral("target"), model.initialCells() * 0.5);
    EXPECT_NEAR(model.divisionsToTarget(), 0.0, 1e-12);
    EXPECT_NEAR(model.targetTime(), 0.0, 1e-12);
}

// ------------------------------------------------------------------
// Скорость прироста
// ------------------------------------------------------------------

TEST(Bacteria, InstantaneousGrowthIsProportionalToColony) {
    BacteriaModel model;
    configure(model);

    // N′ = kN: скорость пропорциональна текущему числу клеток.
    for (double fraction = 0.05; fraction <= 1.001; fraction += 0.15) {
        const double t = model.observationTime() * fraction;
        EXPECT_NEAR(model.growthRateOfCellsAt(t), model.growthRate() * model.theoryCellsAt(t), 1e-9)
            << "момент " << t;
    }
    EXPECT_NEAR(model.growthRateOfCellsAt(0.0), model.growthRate() * model.initialCells(), 1e-9);
}

TEST(Bacteria, GrowthSpeedAlsoDoublesEveryPeriod) {
    BacteriaModel model;
    configure(model);

    const double life = model.doublingTime();
    for (int n = 1; n <= 4; ++n) {
        EXPECT_NEAR(model.growthRateOfCellsAt(life * double(n)), std::exp2(double(n)) * model.growthRateOfCellsAt(0.0),
                    1e-9)
            << "делений " << n;
        // Отношение соседних периодов — всегда два.
        EXPECT_NEAR(model.growthRateOfCellsAt(life * double(n)) / model.growthRateOfCellsAt(life * double(n - 1)), 2.0,
                    1e-9);
    }
}

TEST(Bacteria, AverageGrowthOverIntervalIsBelowInstantaneous) {
    BacteriaModel model;
    configure(model);

    /*
     * Средняя скорость ΔN/Δt меньше мгновенной kN в конце интервала: за
     * интервал число клеток выросло, и прирост «нарастал» внутри него.
     */
    for (double t = model.observationInterval(); t <= model.observationTime(); t += model.observationInterval()) {
        EXPECT_LT(model.averageGrowthAt(t), model.growthRateOfCellsAt(t)) << "момент " << t;
        EXPECT_GT(model.averageGrowthAt(t), 0.0);
    }
}

TEST(Bacteria, GrowthPerIntervalMatchesLaw) {
    BacteriaModel model;
    configure(model);

    // ΔN за интервал учёта = N·(2^(Δt/T_d) − 1).
    const double step = model.observationInterval();
    const double t = 6.0;
    const double expected = model.theoryCellsAt(t) * (std::exp2(step / model.doublingTime()) - 1.0);
    EXPECT_NEAR(model.growthInLastStepAt(t + step), expected, 1.0);
    EXPECT_GT(model.growthInLastStepAt(t), 0.0);
}

TEST(Bacteria, GrowthPerIntervalGrowsWithTime) {
    BacteriaModel model;
    configure(model);

    // Прирост за одно и то же время растёт: клеток становится больше.
    double previous = 0.0;
    for (double t = model.observationInterval(); t <= model.observationTime(); t += model.observationInterval()) {
        const double growth = model.growthInLastStepAt(t);
        EXPECT_GT(growth, previous) << "момент " << t;
        previous = growth;
    }
}

// ------------------------------------------------------------------
// Переполнение чашки: рост не останавливается
// ------------------------------------------------------------------

TEST(Bacteria, DishCapacityIsFixedAndPositive) {
    BacteriaModel model;
    configure(model);

    // Вместимость кадра — число клеток слоя в одну клетку: оно не зависит от
    // посевного материала и от T_d, поэтому переполнение видно по одному числу.
    const double capacity = model.dishCapacity();
    EXPECT_GT(capacity, 0.0);
    EXPECT_NEAR(capacity, model.dishCapacity(), 1e-12);

    BacteriaModel other;
    setSetup(other, QStringLiteral("species=4; doubling=8; N0=50000; T=48; step=4; target=1000000"));
    EXPECT_NEAR(other.dishCapacity(), capacity, 1e-12);
}

TEST(Bacteria, ColonyRadiusGrowsAsSquareRootOfCells) {
    BacteriaModel model;
    configure(model);

    // Площадь слоя пропорциональна числу клеток, поэтому R ∝ √N: при N,
    // возросшей вчетверо, радиус удваивается.
    const double base = std::sqrt(model.overflowFactorAt(0.0));
    for (int n = 1; n <= 3; ++n) {
        const double t = model.doublingTime() * double(n);
        const double ratio = std::sqrt(model.overflowFactorAt(t)) / base;
        EXPECT_NEAR(ratio, std::sqrt(std::exp2(double(n))), 1e-9) << "делений " << n;
    }
}

TEST(Bacteria, OverflowFactorReachesOneAtCapacity) {
    BacteriaModel model;
    configure(model);

    // Фактор переполнения — отношение N к вместимости кадра.
    for (double fraction = 0.0; fraction <= 1.001; fraction += 0.1) {
        const double t = model.observationTime() * fraction;
        EXPECT_NEAR(model.overflowFactorAt(t), model.theoryCellsAt(t) / model.dishCapacity(), 1e-12);
    }
    // Посев кадр ещё не заполняет, а к концу наблюдения он переполнен.
    EXPECT_LT(model.overflowFactorAt(0.0), 1.0);
    EXPECT_GT(model.overflowFactorAt(model.observationTime()), 1.0);
}

TEST(Bacteria, GrowthContinuesAfterTheDishOverflows) {
    BacteriaModel model;
    configure(model);

    /*
     * Теснота кадра на закон не влияет: после переполнения число клеток
     * по-прежнему удваивается через каждый T_d, а не замирает.
     */
    const double moment = model.observationTime();
    ASSERT_GT(model.overflowFactorAt(moment), 1.0);
    for (double t = 0.0; t <= moment; t += model.doublingTime()) {
        const double later = std::min(moment, t + model.doublingTime());
        if (later <= t)
            break;
        EXPECT_NEAR(model.theoryCellsAt(later) / model.theoryCellsAt(t), std::exp2((later - t) / model.doublingTime()),
                    1e-9)
            << "момент " << t;
    }
    // Модель ничего не «обрезает» по вместимости.
    EXPECT_GT(model.theoryCellsAt(moment), model.dishCapacity());
    EXPECT_NEAR(model.multiplicationFactorAt(moment), std::exp2(model.divisionsElapsedAt(moment)), 1e-9);
}

// ------------------------------------------------------------------
// Справочные виды
// ------------------------------------------------------------------

TEST(Bacteria, SpeciesChoiceSetsDoublingAndObservation) {
    BacteriaModel custom;
    configure(custom);
    EXPECT_NEAR(custom.doublingTime(), 2.0, 1e-12);
    EXPECT_NEAR(custom.observationTime(), custom.param(QStringLiteral("T")), 1e-12);

    BacteriaModel listed;
    // В поле стоит T = 12 ч, а у этого вида окно наблюдения другое.
    setSetup(listed, QStringLiteral("species=3; N0=1000; T=12; step=1; target=32000"));
    // Пункт списка задаёт и период удвоения, и разумное окно наблюдения.
    EXPECT_GT(listed.doublingTime(), 0.0);
    EXPECT_NE(listed.observationTime(), listed.param(QStringLiteral("T")));
    EXPECT_FALSE(isInvalid(listed));
    EXPECT_EQ(BacteriaModel::customSpeciesIndex(), BacteriaModel::speciesCount() - 1);
}

TEST(Bacteria, EverySpeciesLeavesSeveralDivisionsVisible) {
    for (int index = 0; index < BacteriaModel::speciesCount(); ++index) {
        BacteriaModel model;
        model.setParamRaw(QStringLiteral("species"), double(index));
        ASSERT_FALSE(isInvalid(model)) << BacteriaModel::speciesName(index).toUtf8().constData();
        // Шест делений — колония вырастает в 64 раза, это видно и на чашке.
        EXPECT_GE(model.divisionsElapsedAt(model.observationTime()), 3.0)
            << BacteriaModel::speciesName(index).toUtf8().constData();
        EXPECT_LE(model.theoryCellsAt(model.observationTime()), 1.0e9);
    }
}

TEST(Bacteria, SeedIsFreeParameterForListedSpecies) {
    // Посев вносят в чашку независимо от микроорганизма.
    BacteriaModel listed;
    setSetup(listed, QStringLiteral("species=0; N0=25; T=2; step=0.5; target=1600"));
    EXPECT_NEAR(listed.initialCells(), 25.0, 1e-12);
    EXPECT_NEAR(listed.doublingTime(), BacteriaModel::speciesDoubling(0), 1e-12);
}

TEST(Bacteria, DefaultSetupShowsSeveralDivisions) {
    BacteriaModel model;
    ASSERT_FALSE(isInvalid(model));
    // Постановка по умолчанию: её видно при первом открытии модели.
    EXPECT_GT(model.observationTime(), 3.0 * model.doublingTime());
    EXPECT_GE(model.divisionsElapsedAt(model.observationTime()), 5.0);
    EXPECT_GT(model.targetCells(), model.initialCells());
    EXPECT_LE(model.targetTime(), model.observationTime());
}

// ------------------------------------------------------------------
// Время модели
// ------------------------------------------------------------------

TEST(Bacteria, TimeIsMeasuredInHoursWithRoomForTheResult) {
    BacteriaModel model;
    configure(model);

    EXPECT_EQ(model.timeUnit().toStdString(), "h");
    // Длительность показа длиннее наблюдения: итог должен быть виден.
    EXPECT_GT(model.duration(), model.observationTime());
    EXPECT_GT(model.stepSize(), 0.0);
    EXPECT_LT(model.stepSize(), model.observationTime() / 100.0);
    EXPECT_GT(model.timePerSecond(), 0.0);
}

TEST(Bacteria, EventTimeIsTheTargetMoment) {
    BacteriaModel model;
    configure(model);

    // Цель достигается внутри наблюдения — её момент и есть событие.
    ASSERT_LT(model.targetTime(), model.observationTime());
    EXPECT_NEAR(model.eventTime(), model.targetTime(), 1e-12);

    // Если цель недостижима за опыт, событие смещается на первое деление.
    model.setParam(QStringLiteral("target"), 1.0e8);
    EXPECT_GT(model.eventTime(), 0.0);
    EXPECT_LE(model.eventTime(), model.observationTime());
}

// ------------------------------------------------------------------
// Ряды данных
// ------------------------------------------------------------------

TEST(Bacteria, TheorySeriesFollowsTheLaw) {
    BacteriaModel model;
    configure(model);

    const QVector<QPointF> series = model.theorySeries();
    ASSERT_GE(series.size(), 50);
    EXPECT_NEAR(series.first().x(), 0.0, 1e-12);
    EXPECT_NEAR(series.last().x(), model.observationTime(), 1e-9);
    for (const QPointF& point : series) {
        EXPECT_NEAR(point.y(), model.theoryCellsAt(point.x()), 1e-9);
        EXPECT_TRUE(std::isfinite(point.y()));
    }
}

TEST(Bacteria, SteppedSeriesHasAStepAtEveryDivision) {
    BacteriaModel model;
    configure(model);

    const QVector<QPointF> series = model.steppedSeries();
    ASSERT_GE(series.size(), 4);
    for (int i = 1; i < series.size(); ++i) {
        EXPECT_GE(series.at(i).x(), series.at(i - 1).x());
        EXPECT_GE(series.at(i).y(), series.at(i - 1).y());
    }
    // Ступенька доходит до того же конца наблюдения, что и кривая.
    EXPECT_NEAR(series.last().y(), model.steppedCellsAt(model.observationTime()), 1e-9);
}

TEST(Bacteria, LogarithmSeriesIsAStraightLineWithSlopeK) {
    BacteriaModel model;
    configure(model);

    // ln N = ln N₀ + kt: ряд логарифма — прямая, её наклон и есть k.
    const QVector<QPointF> series = model.logarithmSeries();
    ASSERT_GE(series.size(), 10);
    EXPECT_NEAR(seriesSlope(series), model.growthRate(), 1e-6);
    EXPECT_NEAR(series.first().y(), std::log(model.initialCells()), 1e-9);
    for (const QPointF& point : series) {
        EXPECT_NEAR(point.y(), std::log(model.initialCells()) + model.growthRate() * point.x(), 1e-9);
    }
}

TEST(Bacteria, GrowthVersusCellsIsAStraightLineThroughOrigin) {
    BacteriaModel model;
    configure(model);

    // N′(N) = kN — прямая, идущая из начала координат: по её наклону и судят
    // о том, что скорость пропорциональна самой колонии.
    const QVector<QPointF> series = model.growthVersusCells();
    ASSERT_GE(series.size(), 10);
    EXPECT_NEAR(seriesSlope(series), model.growthRate(), 1e-6);
    for (const QPointF& point : series)
        EXPECT_NEAR(point.y(), model.growthRate() * point.x(), 1e-9);
}

TEST(Bacteria, DivisionsSeriesIsAStraightLineOfSlopeOneOverDoubling) {
    BacteriaModel model;
    configure(model);

    const QVector<QPointF> series = model.divisionsSeries();
    ASSERT_GE(series.size(), 10);
    EXPECT_NEAR(seriesSlope(series), 1.0 / model.doublingTime(), 1e-9);
    EXPECT_NEAR(series.first().y(), 0.0, 1e-12);
}

TEST(Bacteria, PerStepGrowthSeriesHasOneBarPerInterval) {
    BacteriaModel model;
    configure(model);

    const QVector<QPointF> series = model.growthPerStepSeries();
    const int expected = int(std::floor(model.observationTime() / model.observationInterval()));
    EXPECT_EQ(series.size(), expected);
    ASSERT_FALSE(series.isEmpty());
    // Столбцы идут по возрастанию времени и их heights растут.
    for (int i = 1; i < series.size(); ++i) {
        EXPECT_GT(series.at(i).x(), series.at(i - 1).x());
        EXPECT_GT(series.at(i).y(), series.at(i - 1).y());
    }
    // Первый столбец — прирост за первый интервал учёта.
    EXPECT_NEAR(series.first().y(), model.countedCellsAt(model.observationInterval()) -
                                        model.countedCellsAt(0.0), 1.0);
}

TEST(Bacteria, AverageGrowthSeriesFollowsDifferences) {
    BacteriaModel model;
    configure(model);

    const QVector<QPointF> series = model.averageGrowthSeries();
    ASSERT_FALSE(series.isEmpty());
    const double step = model.observationInterval();
    EXPECT_NEAR(series.first().y(),
                (model.theoryCellsAt(step) - model.theoryCellsAt(0.0)) / step, 1e-9);
    for (int i = 1; i < series.size(); ++i)
        EXPECT_GT(series.at(i).y(), series.at(i - 1).y());
}

TEST(Bacteria, GrowthSeriesStartsAtZero) {
    BacteriaModel model;
    configure(model);

    const QVector<QPointF> series = model.growthSeries();
    ASSERT_GE(series.size(), 2);
    EXPECT_NEAR(series.first().y(), 0.0, 1e-12);
    EXPECT_NEAR(series.last().y(), model.theoryCellsAt(model.observationTime()) - model.initialCells(), 1e-9);
}

// ------------------------------------------------------------------
// Состояние сцены
// ------------------------------------------------------------------

TEST(Bacteria, StateCarriesCellsDivisionsAndOverflow) {
    BacteriaModel model;
    configure(model);
    model.seekTo(model.doublingTime() * 2.0);

    const lab::SimState state = model.currentState();
    ASSERT_EQ(state.entities.size(), 3);

    const double cells = model.countedCellsAt(model.time());
    EXPECT_NEAR(state.primary, cells, 1e-9);
    EXPECT_NEAR(state.secondary, model.theoryCellsAt(model.time()), 1e-9);

    for (const lab::EntityVisual& entity : state.entities) {
        if (entity.id == QLatin1String("colony")) {
            EXPECT_NEAR(entity.position, cells, 1e-9);
            EXPECT_NEAR(entity.value1, model.theoryCellsAt(model.time()), 1e-9);
        }
        if (entity.id == QLatin1String("divisions")) {
            EXPECT_NEAR(entity.position, model.divisionsElapsedAt(model.time()), 1e-9);
            EXPECT_NEAR(entity.value1, model.completedDivisionsAt(model.time()), 1e-12);
            EXPECT_NEAR(entity.value2, model.growthRate(), 1e-12);
        }
        if (entity.id == QLatin1String("dish")) {
            EXPECT_NEAR(entity.position, model.overflowFactorAt(model.time()), 1e-12);
            EXPECT_NEAR(entity.value1, model.dishCapacity(), 1e-12);
        }
    }
}

TEST(Bacteria, StateIsResolvedOnlyAfterTheObservationEnds) {
    BacteriaModel model;
    configure(model);

    model.seekTo(model.observationTime() * 0.5);
    EXPECT_FALSE(model.currentState().resolved);
    EXPECT_TRUE(model.currentState().eventLabel.isEmpty());

    model.seekTo(model.observationTime());
    EXPECT_TRUE(model.currentState().resolved);
    EXPECT_FALSE(model.currentState().eventLabel.isEmpty());
}

TEST(Bacteria, StateIsFiniteAcrossTheWholeShow) {
    BacteriaModel model;
    configure(model);

    for (double fraction = 0.0; fraction <= 1.001; fraction += 0.1) {
        const lab::SimState state = model.stateAt(model.duration() * fraction);
        for (const lab::EntityVisual& entity : state.entities) {
            EXPECT_TRUE(std::isfinite(entity.position)) << entity.id.toUtf8().constData();
            EXPECT_TRUE(std::isfinite(entity.value1)) << entity.id.toUtf8().constData();
        }
    }
}

// ------------------------------------------------------------------
// Допустимость постановки
// ------------------------------------------------------------------

TEST(Bacteria, SeedCannotBeEmptiedThroughTheField) {
    BacteriaModel model;
    configure(model);

    // Пустой посев бессмысленн: поле не принимает ноль, и значение уходит
    // в нижнюю границу, а не остаётся нулевым.
    model.setParam(QStringLiteral("N0"), 0.0);
    EXPECT_GE(model.initialCells(), 1.0);
    EXPECT_FALSE(isInvalid(model));

    // Нулевой период удвоения задать нельзя — он упирается в границу поля.
    model.setParam(QStringLiteral("doubling"), 0.0);
    EXPECT_GT(model.doublingTime(), 0.0);
}

TEST(Bacteria, ZeroDoublingTimeIsRejected) {
    BacteriaModel model;
    configure(model);
    model.setParamRaw(QStringLiteral("doubling"), 0.0);
    EXPECT_TRUE(isInvalid(model));
}

TEST(Bacteria, RunawayGrowthIsRejectedWithReadableNumber) {
    BacteriaModel model;
    // 96 ч при T_d = 15 мин — 384 деления подряд: число не читается.
    setSetup(model, QStringLiteral("species=4; doubling=0.25; N0=1000; T=96; step=1; target=32000"));
    ASSERT_TRUE(isInvalid(model));
    EXPECT_FALSE(model.validate().message.isEmpty());
    EXPECT_FALSE(model.validate().hint.isEmpty());
    // В тексте — степенная запись, а не сорок разрядов подряд.
    EXPECT_NE(model.validate().message.toStdString().find("10^"), std::string::npos);
    EXPECT_LT(model.validate().message.size(), 200);
}

TEST(Bacteria, CoarseCountingIntervalIsRejected) {
    BacteriaModel model;
    // Интервал учёта крупнее четырёх периодов удвоения: деления сливаются.
    setSetup(model, QStringLiteral("species=4; doubling=0.25; N0=100; T=8; step=4; target=10000"));
    EXPECT_TRUE(isInvalid(model));
}

TEST(Bacteria, ValidSetupIsAccepted) {
    BacteriaModel model;
    configure(model);
    EXPECT_FALSE(isInvalid(model));
    EXPECT_TRUE(model.validate().message.isEmpty());
}

TEST(Bacteria, OutOfRangeValuesAreClampedNotRejected) {
    BacteriaModel model;
    setSetup(model, QStringLiteral("species=4; doubling=1000; N0=1e9; T=1000; step=100; target=0"));
    // Значения уходят в границы полей, а не остаются невозможными.
    EXPECT_LE(model.doublingTime(), 24.0);
    EXPECT_LE(model.initialCells(), 100000.0);
    EXPECT_LE(model.observationTime(), 96.0);
    EXPECT_GE(model.targetCells(), 1.0);
}

// ------------------------------------------------------------------
// Запись решения
// ------------------------------------------------------------------

TEST(Bacteria, DerivationContainsKeyFormulas) {
    BacteriaModel model;
    configure(model);

    const lab::Derivation derivation = model.derivation();
    ASSERT_TRUE(derivation.valid);
    ASSERT_FALSE(derivation.steps.isEmpty());

    QString all;
    for (const lab::DerivationStep& step : derivation.steps)
        all += step.content;

    EXPECT_TRUE(all.contains(QStringLiteral("N = N_{0} \\cdot 2^{n}")));
    EXPECT_TRUE(all.contains(QStringLiteral("N(t) = N_{0} \\cdot 2^{t/T_{d}}")));
    EXPECT_TRUE(all.contains(QStringLiteral("k = \\frac{\\ln 2}{T_{d}}")));
    EXPECT_TRUE(all.contains(QStringLiteral("N = N_{0} e^{kt}")));
    EXPECT_TRUE(all.contains(QStringLiteral("n = \\log_{2}\\frac{N}{N_{0}}")));
    EXPECT_TRUE(all.contains(QStringLiteral("t = T_{d} \\log_{2}\\frac{N}{N_{0}}")));
    EXPECT_TRUE(all.contains(QStringLiteral("N'(t) = k N_{0} e^{kt} = k N(t)")));
    EXPECT_TRUE(all.contains(QStringLiteral("R = \\sqrt{\\frac{N s_{0}}{\\pi}}")));
}

TEST(Bacteria, DerivationQuotesModelNumbers) {
    BacteriaModel model;
    configure(model);
    model.seekTo(model.observationTime() * 0.5);

    const lab::Derivation derivation = model.derivation();
    QString all;
    for (const lab::DerivationStep& step : derivation.steps)
        all += step.content;

    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.initialCells(), 0)));
    EXPECT_TRUE(all.contains(BacteriaModel::durationText(model.doublingTime())));
    EXPECT_TRUE(all.contains(model.ratioText(model.growthRate())));
    EXPECT_TRUE(all.contains(BacteriaModel::cellsText(std::round(model.theoryCellsAt(model.time())))));
    EXPECT_TRUE(all.contains(BacteriaModel::cellsText(std::round(model.countedCellsAt(model.time())))));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.growthRateOfCellsAt(model.time()), 1)));
}

TEST(Bacteria, DerivationChecksTheLawAtWholeDivisions) {
    BacteriaModel model;
    configure(model);

    const lab::Derivation derivation = model.derivation();
    QString all;
    for (const lab::DerivationStep& step : derivation.steps)
        all += step.content;

    // Счёт устно: через n делений — посев, умноженный на 2^n.
    EXPECT_TRUE(all.contains(QStringLiteral("2^{1}")));
    EXPECT_TRUE(all.contains(BacteriaModel::cellsText(model.initialCells() * 2.0)));
    EXPECT_TRUE(all.contains(BacteriaModel::cellsText(model.initialCells() * 8.0)));
}

TEST(Bacteria, DerivationSeparatesSmoothLawAndWholeSteps) {
    BacteriaModel model;
    configureSmall(model);

    const lab::Derivation derivation = model.derivation();
    QString all;
    for (const lab::DerivationStep& step : derivation.steps)
        all += step.content;

    // Делений за время — дробное число, а завершённых — целое: обе величины
    // стоят в записи, чтобы их нельзя было перепутать.
    EXPECT_TRUE(all.contains(QStringLiteral("\\frac{t}{T_{d}}")));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.divisionsElapsedAt(3.0), 4)));
}

TEST(Bacteria, DerivationRendersWithoutUnknownCommands) {
    BacteriaModel model;
    configure(model);

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

TEST(Bacteria, DerivationOfEverySpeciesIsComplete) {
    for (int index = 0; index < BacteriaModel::speciesCount(); ++index) {
        BacteriaModel model;
        model.setParamRaw(QStringLiteral("species"), double(index));
        model.seekTo(model.observationTime() * 0.5);
        ASSERT_FALSE(isInvalid(model)) << BacteriaModel::speciesName(index).toUtf8().constData();

        const lab::Derivation derivation = model.derivation();
        ASSERT_TRUE(derivation.valid);
        QString all;
        for (const lab::DerivationStep& step : derivation.steps) {
            all += step.content;
            const QString html = lab::utils::toRichMath(step.content);
            EXPECT_EQ(html.count(QLatin1Char('\\')), 0) << step.kind.toUtf8().constData();
        }
        // Постоянная роста мала у медленных видов — её всё равно должно быть видно.
        EXPECT_FALSE(all.contains(QStringLiteral("= 0 \\text{ ч}^{-1}")))
            << BacteriaModel::speciesName(index).toUtf8().constData();
        EXPECT_TRUE(all.contains(model.ratioText(model.growthRate())));
    }
}

TEST(Bacteria, DerivationOfInvalidSetupExplainsWhatToDo) {
    BacteriaModel model;
    setSetup(model, QStringLiteral("species=4; doubling=0.25; N0=100000; T=96; step=1; target=32000"));
    ASSERT_TRUE(isInvalid(model));

    const lab::Derivation derivation = model.derivation();
    EXPECT_FALSE(derivation.valid);
    EXPECT_FALSE(derivation.answerTemplate.isEmpty());
    EXPECT_TRUE(derivation.answerTemplate.contains(QStringLiteral("Расчёт невозможен")));
}

TEST(Bacteria, AnswerQuotesTheSameNumbersAsTheSteps) {
    BacteriaModel model;
    configure(model);
    model.seekTo(model.doublingTime() * 2.0);

    const lab::Derivation derivation = model.derivation();
    EXPECT_TRUE(derivation.answerTemplate.contains(model.ratioText(model.growthRate())));
    EXPECT_TRUE(derivation.answerTemplate.contains(BacteriaModel::cellsText(std::round(model.countedCellsAt(model.time())))));
    EXPECT_TRUE(derivation.answerTemplate.contains(BacteriaModel::durationText(model.targetTime())));
}

// ------------------------------------------------------------------
// Показания панели
// ------------------------------------------------------------------

TEST(Bacteria, ReadoutsCarryTheKeyQuantities) {
    BacteriaModel model;
    configure(model);
    model.seekTo(model.observationTime() * 0.75);

    const QVector<lab::Readout> readouts = model.readouts();
    ASSERT_GE(readouts.size(), 8);

    QString all;
    int highlighted = 0;
    for (const lab::Readout& readout : readouts) {
        all += readout.label + QStringLiteral(" ") + readout.value;
        highlighted += readout.highlight ? 1 : 0;
        EXPECT_FALSE(readout.value.isEmpty());
    }
    EXPECT_GE(highlighted, 2);

    EXPECT_TRUE(all.contains(BacteriaModel::cellsText(std::round(model.theoryCellsAt(model.time())))));
    EXPECT_TRUE(all.contains(model.ratioText(model.growthRate())));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.divisionsElapsedAt(model.time()), 3)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.growthRateOfCellsAt(model.time()), 1)));
    EXPECT_TRUE(all.contains(BacteriaModel::durationText(model.targetTime())));
    EXPECT_TRUE(all.contains(BacteriaModel::cellsText(model.dishCapacity())));
}

TEST(Bacteria, ReadoutsOfInvalidSetupWarnOnly) {
    BacteriaModel model;
    setSetup(model, QStringLiteral("species=4; doubling=0.25; N0=100000; T=96; step=1; target=32000"));
    ASSERT_TRUE(isInvalid(model));

    const QVector<lab::Readout> readouts = model.readouts();
    ASSERT_FALSE(readouts.isEmpty());
    EXPECT_TRUE(readouts.first().highlight);
    EXPECT_FALSE(readouts.first().value.isEmpty());
}

// ------------------------------------------------------------------
// Ручки
// ------------------------------------------------------------------

TEST(Bacteria, HandlesExposeSeedDoublingWindowAndTarget) {
    BacteriaModel model;
    configure(model);

    const QVector<lab::HandleInfo> handles = model.handles();
    ASSERT_EQ(handles.size(), 4);
    for (const lab::HandleInfo& handle : handles) {
        EXPECT_TRUE(std::isfinite(handle.value)) << handle.id.toUtf8().constData();
        EXPECT_GE(handle.value, handle.minValue) << handle.id.toUtf8().constData();
        EXPECT_LE(handle.value, handle.maxValue) << handle.id.toUtf8().constData();
        EXPECT_FALSE(handle.label.isEmpty()) << handle.id.toUtf8().constData();
        EXPECT_FALSE(handle.readbackValue.isEmpty()) << handle.id.toUtf8().constData();
    }
}

TEST(Bacteria, DraggingDoublingHandleKeepsTheObservation) {
    BacteriaModel listed;
    setSetup(listed, QStringLiteral("species=3; N0=1000; T=12; step=1; target=32000"));
    const double window = listed.observationTime();
    ASSERT_NE(window, 12.0);
    ASSERT_NE(window, listed.param(QStringLiteral("T")));

    // Перетаскивание T_d у вида из справочника сохраняет окно наблюдения.
    listed.setHandleValue(QStringLiteral("doubling"), 3.0);
    EXPECT_NEAR(listed.doublingTime(), 3.0, 1e-12);
    EXPECT_NEAR(listed.observationTime(), window, 1e-12);
    EXPECT_EQ(listed.paramChoiceIndex(QStringLiteral("species")), BacteriaModel::customSpeciesIndex());
}

TEST(Bacteria, DraggingWindowHandleKeepsTheDoublingTime) {
    BacteriaModel listed;
    setSetup(listed, QStringLiteral("species=2; N0=1000; T=24; step=1; target=32000"));
    const double life = listed.doublingTime();

    listed.setHandleValue(QStringLiteral("window"), 40.0);
    EXPECT_NEAR(listed.observationTime(), 40.0, 1e-12);
    EXPECT_NEAR(listed.doublingTime(), life, 1e-12);
}

TEST(Bacteria, DraggingSeedAndTargetChangeTheLawOnly) {
    BacteriaModel model;
    configure(model);

    model.setHandleValue(QStringLiteral("n0"), 2000.0);
    EXPECT_NEAR(model.initialCells(), 2000.0, 1e-12);
    // Целевое число ближе: делений нужно меньше, время меньше.
    const double before = model.targetTime();
    model.setHandleValue(QStringLiteral("target"), 8000.0);
    EXPECT_NEAR(model.targetCells(), 8000.0, 1e-12);
    EXPECT_LT(model.targetTime(), before);
    EXPECT_NEAR(model.targetTime(), model.doublingTime() * 2.0, 1e-9);
}

TEST(Bacteria, HandleValuesTrackTheModel) {
    BacteriaModel model;
    configure(model);
    model.setParam(QStringLiteral("doubling"), 5.0);
    model.setParam(QStringLiteral("target"), 64000.0);

    for (const lab::HandleInfo& handle : model.handles()) {
        if (handle.id == QLatin1String("doubling"))
            EXPECT_NEAR(handle.value, 5.0, 1e-12);
        if (handle.id == QLatin1String("target"))
            EXPECT_NEAR(handle.value, 64000.0, 1e-12);
        if (handle.id == QLatin1String("n0"))
            EXPECT_NEAR(handle.value, model.initialCells(), 1e-12);
        if (handle.id == QLatin1String("window"))
            EXPECT_NEAR(handle.value, model.observationTime(), 1e-12);
    }
}

// ------------------------------------------------------------------
// Форматирование
// ------------------------------------------------------------------

TEST(Bacteria, DurationTextUsesReadableUnits) {
    // Минуты — когда их меньше часа, сутки — когда времени больше двух суток.
    EXPECT_EQ(BacteriaModel::durationText(0.25).toStdString(), "15 мин");
    EXPECT_EQ(BacteriaModel::durationText(2.0).toStdString(), "2 ч");
    EXPECT_EQ(BacteriaModel::durationText(2.5).toStdString(), "2 ч 30 мин");
    EXPECT_EQ(BacteriaModel::durationText(72.0).toStdString(), "3 сут");
    EXPECT_EQ(BacteriaModel::durationText(0.0).toStdString(), "0 ч");
}

TEST(Bacteria, RatioTextKeepsSmallConstantsReadable) {
    // Постоянная роста медленных видов порядка 0,01 ч⁻¹: обычные три знака
    // оставили бы «0,01», а больше знаков — видно всегда.
    // Знаки значимы: первые ненулевые цифры обязаны остаться в тексте.
    EXPECT_TRUE(BacteriaModel::ratioText(0.0012).contains(QStringLiteral("12")));
    EXPECT_TRUE(BacteriaModel::ratioText(0.0533).contains(QStringLiteral("533")));
    EXPECT_EQ(BacteriaModel::ratioText(0.0).toStdString(), "0");
    EXPECT_TRUE(BacteriaModel::ratioText(std::numeric_limits<double>::quiet_NaN()).contains(QStringLiteral("—")));
}

TEST(Bacteria, CellsTextRoundsAndSwitchesToPowers) {
    EXPECT_EQ(BacteriaModel::cellsText(999.6).toStdString(), "1000");
    EXPECT_EQ(BacteriaModel::cellsText(1234.0).toStdString(), "1234");
    // Миллионы и выше — степенной записью: иначе не читается.
    EXPECT_TRUE(BacteriaModel::cellsText(2.0e9).contains(QStringLiteral("10^")));
    EXPECT_TRUE(BacteriaModel::cellsText(std::numeric_limits<double>::quiet_NaN()).contains(QStringLiteral("—")));
}

TEST(Bacteria, TimeUnitLabelMatchesHours) {
    BacteriaModel model;
    configure(model);
    // Время модели — часы, и подписи длительностей их используют.
    EXPECT_EQ(model.timeUnit().toStdString(), "h");
    EXPECT_TRUE(BacteriaModel::durationText(model.doublingTime()).contains(QStringLiteral("ч")));
}
