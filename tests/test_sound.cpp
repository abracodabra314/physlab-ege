#include <gtest/gtest.h>

#include <cmath>

#include "TestSupport.h"
#include "models/SoundModel.h"
#include "utils/MathText.h"

using lab::models::SoundModel;
using lab::test::isInvalid;
using lab::test::setSetup;

namespace {

/**
 * Учебная постановка: температура 20 °C (c = 343 м/с), сигналы подаются
 * через T = 3 с, суда сближаются со скоростями 12 и 8 м/с, начальное
 * расстояние 6800 м.
 */
void configure(SoundModel& model) {
    setSetup(model, QStringLiteral("temp=20; T=3; L0=6800; dir1=0; v1=12; dir2=0; v2=8"));
}

/** Постановка «суда удаляются»: те же скорости, обратные направления. */
void configureReceding(SoundModel& model) {
    setSetup(model, QStringLiteral("temp=20; T=3; L0=6800; dir1=1; v1=12; dir2=1; v2=8"));
}

constexpr double kSoundAt20 = 343.0;   // 331 + 0,6·20

}  // namespace

// ------------------------------------------------------------------
// Скорость звука и знак скоростей
// ------------------------------------------------------------------

TEST(Sound, SoundSpeedFollowsTemperature) {
    SoundModel model;

    setSetup(model, QStringLiteral("temp=0; T=3; L0=1000; dir1=0; v1=10; dir2=0; v2=5"));
    EXPECT_NEAR(model.soundSpeed(), 331.0, 1e-12);

    configure(model);
    EXPECT_NEAR(model.soundSpeed(), kSoundAt20, 1e-12);

    setSetup(model, QStringLiteral("temp=-30; T=3; L0=1000; dir1=0; v1=10; dir2=0; v2=5"));
    EXPECT_NEAR(model.soundSpeed(), 331.0 - 18.0, 1e-12);
    // Тёплый воздух — звук быстрее: от этого зависят все времена задачи.
    SoundModel warm;
    setSetup(warm, QStringLiteral("temp=40; T=3; L0=1000; dir1=0; v1=10; dir2=0; v2=5"));
    EXPECT_GT(warm.soundSpeed(), model.soundSpeed());
}

TEST(Sound, DirectionChoiceSetsSignOfSpeed) {
    SoundModel towards;
    configure(towards);
    // Источник идёт к наблюдателю — вдоль оси X в сторону возрastania x.
    EXPECT_NEAR(towards.sourceSpeed(), 12.0, 1e-12);
    // Наблюдатель идёт навстречу звуку — в сторону уменьшения x.
    EXPECT_NEAR(towards.observerSpeed(), -8.0, 1e-12);

    SoundModel receding;
    configureReceding(receding);
    EXPECT_NEAR(receding.sourceSpeed(), -12.0, 1e-12);
    EXPECT_NEAR(receding.observerSpeed(), 8.0, 1e-12);
    // Величины скоростей при этом те же: меняется только направление.
    EXPECT_NEAR(std::abs(receding.sourceSpeed()), std::abs(towards.sourceSpeed()), 1e-12);
}

TEST(Sound, DistanceBetweenShipsChangesByRelativeSpeed) {
    SoundModel model;
    configure(model);

    EXPECT_NEAR(model.gapAt(0.0), 6800.0, 1e-12);
    // Суда сближаются: расстояние уменьшается со скоростью (v₂ − v₁).
    EXPECT_NEAR(model.gapAt(10.0), 6800.0 + (-8.0 - 12.0) * 10.0, 1e-9);
    EXPECT_LT(model.gapAt(10.0), model.gapAt(0.0));

    SoundModel receding;
    configureReceding(receding);
    EXPECT_GT(receding.gapAt(10.0), receding.gapAt(0.0));
}

// ------------------------------------------------------------------
// Фронты: центр в точке излучения, радиус r = c(t − tₖ)
// ------------------------------------------------------------------

TEST(Sound, EmissionMomentsAreEquidistant) {
    SoundModel model;
    configure(model);

    for (int index = 0; index < SoundModel::signalCount(); ++index)
        EXPECT_NEAR(model.emissionTime(index), 3.0 * index, 1e-12) << "сигнал " << index;
}

TEST(Sound, EmissionPointIsWhereShipWas) {
    SoundModel model;
    configure(model);

    // Точка излучения — положение судна в момент сигнала: xₖ = v₁tₖ.
    for (int index = 0; index < SoundModel::signalCount(); ++index) {
        EXPECT_NEAR(model.emissionPoint(index), 12.0 * model.emissionTime(index), 1e-9)
            << "сигнал " << index;
        // То же значение из закона движения судна.
        EXPECT_NEAR(model.emissionPoint(index), model.sourcePositionAt(model.emissionTime(index)), 1e-12);
    }
}

TEST(Sound, EmissionPointDoesNotDependOnObservationTime) {
    SoundModel model;
    configure(model);

    // Главный факт задачи: где звук родился, там его центр и остаётся.
    // Момент наблюдения на точку излучения повлиять не может.
    for (int index = 0; index < SoundModel::signalCount(); ++index) {
        const double fixed = model.emissionPoint(index);
        EXPECT_NEAR(fixed, model.emissionPoint(index), 1e-15);
        model.seekTo(model.duration() * 0.5);
        EXPECT_NEAR(model.emissionPoint(index), fixed, 1e-15);
    }
}

TEST(Sound, FrontRadiusGrowsWithSoundSpeedOnly) {
    SoundModel model;
    configure(model);

    // До излучения фронта нет.
    EXPECT_NEAR(model.frontRadius(1, 1.0), 0.0, 1e-15);
    // После — радиус растёт со скоростью звука: r = c(t − tₖ).
    const double t = 12.0;
    EXPECT_NEAR(model.frontRadius(1, t), kSoundAt20 * (t - model.emissionTime(1)), 1e-9);
    EXPECT_NEAR(model.frontRadius(0, t), kSoundAt20 * t, 1e-9);

    // За одинаковый промежуток все фронты растут одинаково: скорость звука
    // одна для всех, а не «у каждого своя».
    const double growth = model.frontRadius(2, 20.0) - model.frontRadius(2, 18.0);
    EXPECT_NEAR(growth, model.frontRadius(0, 20.0) - model.frontRadius(0, 18.0), 1e-9);
    EXPECT_NEAR(growth, kSoundAt20 * 2.0, 1e-9);
}

TEST(Sound, FrontCentreStaysWhenObserverMoves) {
    SoundModel slow;
    configure(slow);
    SoundModel fast;
    setSetup(fast, QStringLiteral("temp=20; T=3; L0=6800; dir1=0; v1=12; dir2=1; v2=30"));

    // Перестановка наблюдателя не двигает центры окружностей: они заданы
    // движением источника.
    for (int index = 0; index < SoundModel::signalCount(); ++index) {
        EXPECT_NEAR(slow.emissionPoint(index), fast.emissionPoint(index), 1e-12);
        EXPECT_NEAR(slow.emissionTime(index), fast.emissionTime(index), 1e-12);
        // Радиусы в один и тот же момент тоже совпадают.
        EXPECT_NEAR(slow.frontRadius(index, 15.0), fast.frontRadius(index, 15.0), 1e-12);
    }
}

// ------------------------------------------------------------------
// Моменты приёма
// ------------------------------------------------------------------

TEST(Sound, ArrivalTimeFollowsClosingCondition) {
    SoundModel model;
    configure(model);

    const double c = model.soundSpeed();
    const double v1 = model.sourceSpeed();
    const double v2 = model.observerSpeed();
    for (int index = 0; index < SoundModel::signalCount(); ++index) {
        const double emitted = model.emissionTime(index);
        const double expected = (6800.0 + (c - v1) * emitted) / (c - v2);
        EXPECT_NEAR(model.arrivalTime(index), expected, 1e-9) << "сигнал " << index;
    }
}

TEST(Sound, ArrivalMeansFrontReachesObserver) {
    SoundModel model;
    configure(model);

    // Геометрическая проверка условия приёма: правая граница фронта
    // совпадает с положением наблюдателя.
    for (int index = 0; index < SoundModel::signalCount(); ++index) {
        const double arrival = model.arrivalTime(index);
        const double frontEdge = model.emissionPoint(index) + model.frontRadius(index, arrival);
        EXPECT_NEAR(frontEdge, model.observerPositionAt(arrival), 1e-9) << "сигнал " << index;
    }
}

TEST(Sound, ArrivalTimesGrowWithEmissionIndex) {
    SoundModel model;
    configure(model);

    double previous = -1.0;
    for (int index = 0; index < SoundModel::signalCount(); ++index) {
        const double arrival = model.arrivalTime(index);
        EXPECT_TRUE(std::isfinite(arrival));
        // Сигналы принимаются в том же порядке, в каком поданы.
        EXPECT_GT(arrival, previous) << "сигнал " << index;
        // Принять сигнал раньше, чем он подан, невозможно.
        EXPECT_GT(arrival, model.emissionTime(index));
        previous = arrival;
    }
}

TEST(Sound, ApproachingShipsReceiveSignalsMoreOften) {
    SoundModel towards;
    configure(towards);
    SoundModel receding;
    configureReceding(receding);

    // Сближение: промеждutок между приёмами меньше промежутка излучения.
    EXPECT_LT(towards.receptionPeriod(), towards.signalPeriod());
    // Удаление — больше.
    EXPECT_GT(receding.receptionPeriod(), receding.signalPeriod());
}

TEST(Sound, ReceptionPeriodMatchesFormulaAndArrivalDifference) {
    SoundModel model;
    configure(model);

    const double c = model.soundSpeed();
    const double expected = 3.0 * (c - model.sourceSpeed()) / (c - model.observerSpeed());
    EXPECT_NEAR(model.receptionPeriod(), expected, 1e-12);

    // Тот же промежуток — разность соседних моментов приёма: двух
    // независимых способов его получить в модели нет.
    for (int index = 0; index + 1 < SoundModel::signalCount(); ++index) {
        const double difference = model.arrivalTime(index + 1) - model.arrivalTime(index);
        EXPECT_NEAR(difference, model.receptionPeriod(), 1e-9) << "сигналы " << index << " и " << index + 1;
    }
}

TEST(Sound, ObserverRunningAwayStretchesReceptionPeriod) {
    SoundModel model;
    configure(model);

    // Наблюдатель убегает быстрее — приёмы реже.
    const double slowRun = model.receptionPeriodFor(12.0, 4.0);
    const double fastRun = model.receptionPeriodFor(12.0, 30.0);
    EXPECT_GT(fastRun, slowRun);

    // Источник едет навстречу наблюдателю (скорость положительна) —
    // числитель (c − v₁) меньше, приёмы чаще.
    EXPECT_LT(model.receptionPeriodFor(12.0, 8.0), model.receptionPeriodFor(-12.0, 8.0));

    // Обе скорости нулевые: промежуток приёма равен промежутку излучения.
    EXPECT_NEAR(model.receptionPeriodFor(0.0, 0.0), model.signalPeriod(), 1e-12);
}

// ------------------------------------------------------------------
// Счётчики и наблюдение
// ------------------------------------------------------------------

TEST(Sound, CountsGrowAtEmissionAndArrivalMoments) {
    SoundModel model;
    configure(model);

    EXPECT_EQ(model.emittedCount(0.0), 1);
    EXPECT_EQ(model.emittedCount(model.emissionTime(1) - 0.5), 1);
    EXPECT_EQ(model.emittedCount(model.emissionTime(1) + 0.5), 2);
    EXPECT_EQ(model.emittedCount(model.duration()), SoundModel::signalCount());

    EXPECT_EQ(model.receivedCount(0.0), 0);
    const double first = model.arrivalTime(0);
    EXPECT_EQ(model.receivedCount(first - 0.5), 0);
    EXPECT_EQ(model.receivedCount(first + 0.5), 1);
    EXPECT_EQ(model.receivedCount(model.arrivalTime(1) + 0.5), 2);
    EXPECT_EQ(model.receivedCount(model.duration()), SoundModel::signalCount());
}

TEST(Sound, ObservationEndsAfterLastReception) {
    SoundModel model;
    configure(model);

    const double last = model.arrivalTime(SoundModel::signalCount() - 1);
    EXPECT_GT(model.duration(), last);
    EXPECT_NEAR(model.eventTime(), last, 1e-12);
    EXPECT_GT(model.stepSize(), 0.0);
    EXPECT_GT(model.timePerSecond(), 0.0);

    const lab::SimState before = model.stateAt(last - 0.5);
    EXPECT_FALSE(before.resolved);
    const lab::SimState after = model.stateAt(model.duration());
    EXPECT_TRUE(after.resolved);
    EXPECT_FALSE(after.eventLabel.isEmpty());
}

// ------------------------------------------------------------------
// Допустимость постановки
// ------------------------------------------------------------------

TEST(Sound, RejectsShipMovingAtSoundSpeed) {
    SoundModel runaway;
    // Скорость наблюдателя ≥ c: фронт его никогда не догонит.
    setSetup(runaway, QStringLiteral("temp=20; T=3; L0=6800; dir1=0; v1=12; dir2=1; v2=343"));
    EXPECT_TRUE(isInvalid(runaway));

    // Судно с гудком, летящее навстречу наблюдателю со скоростью звука,
    // обгоняет свои сигналы — fronts начинают «догонять» наблюдателя в
    // обратном порядке, школьной постановки здесь нет.
    SoundModel supersonic;
    setSetup(supersonic, QStringLiteral("temp=20; T=3; L0=6800; dir1=0; v1=343; dir2=0; v2=8"));
    EXPECT_TRUE(isInvalid(supersonic));

    // Тот же модуль скорости, но движение «удаляясь»: сигналы остаются в
    //переди, и все приёмы наступают — постановка корректна.
    SoundModel recedingSupersonic;
    setSetup(recedingSupersonic, QStringLiteral("temp=20; T=3; L0=6800; dir1=1; v1=343; dir2=0; v2=8"));
    EXPECT_FALSE(isInvalid(recedingSupersonic));
}

TEST(Sound, MovingTowardsSoundIsValidEvenAtHighSpeed) {
    SoundModel model;
    // Наблюдатель летит навстречу звуку: приём наступает быстрее, и
    // постановка остаётся корректной.
    setSetup(model, QStringLiteral("temp=20; T=3; L0=6800; dir1=0; v1=12; dir2=0; v2=300"));
    EXPECT_FALSE(isInvalid(model));
    EXPECT_TRUE(std::isfinite(model.arrivalTime(0)));
    EXPECT_LT(model.arrivalTime(0), 6800.0 / kSoundAt20);
}

TEST(Sound, DegenerateValuesAreClampedByThePanel) {
    // Ползунки панели не пропускают ноль: значение поднимается до минимума
    // диапазона, и расчёт остаётся корректным (как в других моделях).
    SoundModel noGap;
    setSetup(noGap, QStringLiteral("temp=20; T=3; L0=0; dir1=0; v1=12; dir2=0; v2=8"));
    EXPECT_GE(noGap.initialGap(), 100.0);
    EXPECT_FALSE(isInvalid(noGap));
    EXPECT_TRUE(std::isfinite(noGap.arrivalTime(0)));

    SoundModel noPeriod;
    setSetup(noPeriod, QStringLiteral("temp=20; T=0; L0=6800; dir1=0; v1=12; dir2=0; v2=8"));
    EXPECT_GE(noPeriod.signalPeriod(), 0.5);
    EXPECT_FALSE(isInvalid(noPeriod));
    // Сигналы всё равно подаются через равные промежутки.
    EXPECT_NEAR(noPeriod.emissionTime(1), noPeriod.signalPeriod(), 1e-12);

    SoundModel ordinary;
    configure(ordinary);
    EXPECT_FALSE(isInvalid(ordinary));
}

// ------------------------------------------------------------------
// Состояние системы
// ------------------------------------------------------------------

TEST(Sound, StateCarriesShipsAndEveryFront) {
    SoundModel model;
    configure(model);
    const double t = model.duration() * 0.5;

    const lab::SimState state = model.stateAt(t);
    ASSERT_GE(state.entities.size(), 2 + SoundModel::signalCount());

    EXPECT_EQ(state.entities.at(0).id, QStringLiteral("source"));
    EXPECT_NEAR(state.entities.at(0).position, model.sourcePositionAt(t), 1e-12);
    EXPECT_NEAR(state.entities.at(0).value1, model.sourceSpeed(), 1e-12);

    EXPECT_EQ(state.entities.at(1).id, QStringLiteral("observer"));
    EXPECT_NEAR(state.entities.at(1).position, model.observerPositionAt(t), 1e-12);

    // Каждый фронт — отдельный объект с неподвижным центром и растущим радиусом.
    for (int index = 0; index < SoundModel::signalCount(); ++index) {
        const lab::EntityVisual& front = state.entities.at(2 + index);
        EXPECT_EQ(front.id, QStringLiteral("front_%1").arg(index + 1));
        EXPECT_NEAR(front.position, model.emissionPoint(index), 1e-12);
        EXPECT_NEAR(front.value1, model.frontRadius(index, t), 1e-9);
        EXPECT_EQ(front.active, t >= model.emissionTime(index) - 1e-12);
    }

    EXPECT_NEAR(state.primary, model.gapAt(t), 1e-9);
}

TEST(Sound, FrontPositionsStayAtEqualSpacing) {
    SoundModel model;
    configure(model);

    // Источники движется равномерно, поэтому и центры фронтов стоят через
    // равные расстояния v₁T.
    for (int index = 1; index < SoundModel::signalCount(); ++index) {
        const double spacing = model.emissionPoint(index) - model.emissionPoint(index - 1);
        EXPECT_NEAR(spacing, model.sourceSpeed() * model.signalPeriod(), 1e-9);
    }
}

// ------------------------------------------------------------------
// Запись решения
// ------------------------------------------------------------------

TEST(Sound, DerivationContainsKeyFormulasAndNumbers) {
    SoundModel model;
    configure(model);

    const lab::Derivation derivation = model.derivation();
    ASSERT_TRUE(derivation.valid);
    ASSERT_FALSE(derivation.steps.isEmpty());

    QString all;
    for (const lab::DerivationStep& step : derivation.steps)
        all += step.content;

    EXPECT_TRUE(all.contains(QStringLiteral("c = 331 + 0,6")));
    EXPECT_TRUE(all.contains(QStringLiteral("t_{k} = kT")));
    EXPECT_TRUE(all.contains(QStringLiteral("r_{k}(t) = c(t - t_{k})")));
    EXPECT_TRUE(all.contains(QStringLiteral("x_{k} + c(t - t_{k}) = L_{0} + v_{2} t")));
    EXPECT_TRUE(all.contains(QStringLiteral("t_{k}^{\\prime} =")));
    EXPECT_TRUE(all.contains(QStringLiteral("\\frac{(c - v_{1}) T}{c - v_{2}}")));

    // Подстановка: скорость звука и первый момент приёма — из модели.
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.soundSpeed(), 1)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.arrivalTime(0), 3)));
}

TEST(Sound, DerivationRendersWithoutUnknownCommands) {
    SoundModel model;
    configure(model);

    // Каждая строка проходит через преобразователь формул: неподдержанная
    // команда оставляет обратный слэш, и пользователь увидит «\frac».
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

TEST(Sound, AnswerQuotesReceptionPeriodAndFirstArrival) {
    SoundModel model;
    configure(model);

    const lab::Derivation derivation = model.derivation();
    EXPECT_TRUE(derivation.answerTemplate.contains(lab::utils::formatNumber(model.arrivalTime(0), 2)));
    EXPECT_TRUE(derivation.answerTemplate.contains(lab::utils::formatNumber(model.receptionPeriod(), 3)));
}

TEST(Sound, DerivationReportsInvalidSetup) {
    SoundModel model;
    setSetup(model, QStringLiteral("temp=20; T=3; L0=6800; dir1=1; v1=12; dir2=1; v2=400"));

    const lab::Derivation derivation = model.derivation();
    EXPECT_FALSE(derivation.valid);
    EXPECT_FALSE(derivation.answerTemplate.isEmpty());
}

TEST(Sound, ReadoutsFollowModelState) {
    SoundModel model;
    configure(model);
    model.seekTo(model.arrivalTime(0) + 0.5);
    const double t = model.time();

    const QVector<lab::Readout> readouts = model.readouts();
    ASSERT_FALSE(readouts.isEmpty());

    QString all;
    for (const lab::Readout& readout : readouts)
        all += readout.label + readout.value;
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.soundSpeed(), 1)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.gapAt(t), 1)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.arrivalTime(0), 2)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.receptionPeriod(), 3)));
}

// ------------------------------------------------------------------
// Ряды данных
// ------------------------------------------------------------------

TEST(Sound, GapSeriesIsLinear) {
    SoundModel model;
    configure(model);

    const QVector<QPointF> series = model.gapSeries();
    ASSERT_GE(series.size(), 100);
    EXPECT_NEAR(series.first().x(), 0.0, 1e-12);
    EXPECT_NEAR(series.first().y(), 6800.0, 1e-9);
    EXPECT_NEAR(series.last().x(), model.duration(), 1e-9);

    const double slope = (series.last().y() - series.first().y()) / (series.last().x() - series.first().x());
    EXPECT_NEAR(slope, model.observerSpeed() - model.sourceSpeed(), 1e-6);
}

TEST(Sound, FrontSeriesStartsAtEmissionMoment) {
    SoundModel model;
    configure(model);

    for (int index = 0; index < SoundModel::signalCount(); ++index) {
        const QVector<QPointF> series = model.frontSeries(index);
        ASSERT_GE(series.size(), 100) << "сигнал " << index;
        // До излучения фронта нет, поэтому ряд начинается с момента tₖ.
        EXPECT_NEAR(series.first().x(), model.emissionTime(index), 1e-12);
        EXPECT_NEAR(series.first().y(), 0.0, 1e-12);
        // Наклон — скорость звука, одинаковая для всех фронтов.
        const double slope = (series.last().y() - series.first().y()) / (series.last().x() - series.first().x());
        EXPECT_NEAR(slope, model.soundSpeed(), 1e-6) << "сигнал " << index;
    }
}

TEST(Sound, PathSeriesMatchMotionLaws) {
    SoundModel model;
    configure(model);

    const QVector<QPointF> source = model.sourcePathSeries();
    const QVector<QPointF> observer = model.observerPathSeries();
    ASSERT_GE(source.size(), 50);
    ASSERT_EQ(observer.size(), source.size());

    // Путь источника — x₁(t) = v₁t; ряд наблюдателя отсчитан от L₀,
    // поэтому начинается с нуля и идёт по модулю скорости наблюдателя.
    for (int i = 0; i < source.size(); i += 40) {
        EXPECT_NEAR(source.at(i).y(), model.sourceSpeed() * source.at(i).x(), 1e-9);
        EXPECT_NEAR(observer.at(i).y(), model.observerSpeed() * observer.at(i).x(), 1e-9);
        // Суда сближаются: наблюдатель движется в сторону уменьшения x.
        EXPECT_LE(observer.at(i).y(), 0.0);
    }
    EXPECT_NEAR(source.last().y(), model.sourcePositionAt(model.duration()), 1e-9);
}

TEST(Sound, ReceptionPeriodSeriesAreMonotonic) {
    SoundModel model;
    configure(model);

    // T′(v₂) растёт с ростом скорости наблюдателя: знаменатель (c − v₂) уменьшается.
    const QVector<QPointF> versusObserver = model.periodVersusObserverSpeed();
    ASSERT_GE(versusObserver.size(), 100);
    for (int i = 1; i < versusObserver.size(); ++i) {
        ASSERT_TRUE(std::isfinite(versusObserver.at(i).y())) << "точка " << i;
        EXPECT_GE(versusObserver.at(i).y(), versusObserver.at(i - 1).y() - 1e-12);
    }
    EXPECT_GT(versusObserver.last().y(), model.receptionPeriod());
    EXPECT_LT(versusObserver.first().y(), model.receptionPeriod());

    // T′(v₁) убывает с ростом скорости источника: числитель (c − v₁) уменьшается.
    const QVector<QPointF> versusSource = model.periodVersusSourceSpeed();
    ASSERT_EQ(versusSource.size(), versusObserver.size());
    for (int i = 1; i < versusSource.size(); ++i)
        EXPECT_LE(versusSource.at(i).y(), versusSource.at(i - 1).y() + 1e-12);
}

TEST(Sound, SeriesShareTheSameSpeedAxis) {
    SoundModel model;
    configure(model);

    // Оба графика наложены на одну ось: их можно сравнивать напрямую.
    const QVector<QPointF> observerSeries = model.periodVersusObserverSpeed();
    const QVector<QPointF> sourceSeries = model.periodVersusSourceSpeed();
    ASSERT_EQ(observerSeries.size(), sourceSeries.size());
    EXPECT_NEAR(observerSeries.first().x(), sourceSeries.first().x(), 1e-12);
    EXPECT_NEAR(observerSeries.last().x(), sourceSeries.last().x(), 1e-12);
    // Ни одна кривая не уходит за скорость звука: при v ≥ c приёма нет.
    EXPECT_LT(sourceSeries.last().x(), model.soundSpeed());
}

// ------------------------------------------------------------------
// Точки взаимодействия
// ------------------------------------------------------------------

TEST(Sound, HandlesCoverGapAndTemperature) {
    SoundModel model;
    configure(model);

    const QVector<lab::HandleInfo> handles = model.handles();
    ASSERT_EQ(handles.size(), 2);
    EXPECT_EQ(handles.at(0).id, QStringLiteral("gap"));
    EXPECT_EQ(handles.at(1).id, QStringLiteral("temp"));

    // Перетаскивание судна наблюдателя меняет расстояние — и все времена.
    const double before = model.arrivalTime(0);
    model.setHandleValue(QStringLiteral("gap"), 3400.0);
    EXPECT_NEAR(model.initialGap(), 3400.0, 1e-9);
    EXPECT_LT(model.arrivalTime(0), before);

    // Перетаскивание термометра меняет скорость звука.
    model.setHandleValue(QStringLiteral("temp"), 0.0);
    EXPECT_NEAR(model.soundSpeed(), 331.0, 1e-12);
}

TEST(Sound, ReadbackShowsReceptionAndSoundSpeed) {
    SoundModel model;
    configure(model);

    const QVector<lab::HandleInfo> handles = model.handles();
    ASSERT_EQ(handles.size(), 2);
    EXPECT_TRUE(handles.at(0).readbackValue.contains(QStringLiteral("с")));
    EXPECT_TRUE(handles.at(1).readbackValue.contains(QStringLiteral("м/с")));
}

TEST(Sound, DraggingRestartsObservation) {
    SoundModel model;
    configure(model);
    model.seekTo(model.duration());
    ASSERT_GT(model.time(), 0.0);

    model.beginInteraction();
    EXPECT_NEAR(model.time(), 0.0, 1e-12);
}
