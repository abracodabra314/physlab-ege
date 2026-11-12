#include <gtest/gtest.h>

#include <cmath>

#include "TestSupport.h"
#include "models/SpringHorizontalModel.h"
#include "utils/MathText.h"

using lab::models::SpringHorizontalModel;
using lab::test::isInvalid;
using lab::test::setSetup;

namespace {

/** Учебная установка: масса, жёсткость, трение, начальное смещение. */
void fill(SpringHorizontalModel& model, double m, double k, double mu, double x0) {
    setSetup(model, QStringLiteral("m=%1; k=%2; mu=%3; x0=%4")
                       .arg(m)
                       .arg(k)
                       .arg(mu)
                       .arg(x0));
}

/** Момент первого поворота (смены знака скорости) по рассчитанным узлам. */
double firstTurningTime(const SpringHorizontalModel& model) {
    const QVector<QPointF> velocity = model.velocitySeries();
    for (int i = 1; i < velocity.size(); ++i) {
        if (velocity.at(i - 1).y() < 0.0 && velocity.at(i).y() >= 0.0)
            return velocity.at(i).x();
    }
    return std::numeric_limits<double>::quiet_NaN();
}

}  // namespace

// ------------------------------------------------------------------
// Формулы идеальной системы и исходные силы
// ------------------------------------------------------------------

TEST(SpringHorizontal, DefaultSetupIsValid) {
    SpringHorizontalModel model;
    ASSERT_TRUE(model.isValid());
    EXPECT_GT(model.period(), 0.0);
    EXPECT_GT(model.duration(), 0.0);
    EXPECT_GT(model.maxSpeed(), 0.0);
}

TEST(SpringHorizontal, IdealOmegaAndPeriodFollowFormulas) {
    SpringHorizontalModel model;
    fill(model, 2.0, 200.0, 0.0, 0.2);

    // ω = √(k/m) = 10 с⁻¹, T = 2π/ω.
    EXPECT_NEAR(model.omega(), 10.0, 1e-12);
    EXPECT_NEAR(model.period(), 2.0 * M_PI / 10.0, 1e-12);
}

TEST(SpringHorizontal, NormalForceAndFrictionFollowFormulas) {
    SpringHorizontalModel model;
    fill(model, 2.0, 100.0, 0.25, 0.3);

    // N = mg, |Fтр| = μN, мёртвая зона μmg/k.
    EXPECT_NEAR(model.normalForce(), 2.0 * 9.8, 1e-12);
    EXPECT_NEAR(model.frictionForce(), 0.25 * 2.0 * 9.8, 1e-12);
    EXPECT_NEAR(model.deadZone(), 0.25 * 2.0 * 9.8 / 100.0, 1e-12);
}

TEST(SpringHorizontal, OutOfRangeParametersAreClampedToPhysical) {
    SpringHorizontalModel zeroMass;
    setSetup(zeroMass, QStringLiteral("m=0; k=100; mu=0.1; x0=0.2"));
    // Нулевая масса недопустима физически: ядро ограничивает её минимальным
    // значением, поэтому деления на ноль в расчёте не возникает.
    EXPECT_GT(zeroMass.mass(), 0.0);
    EXPECT_GT(zeroMass.period(), 0.0);
    EXPECT_TRUE(zeroMass.isValid());

    SpringHorizontalModel negativeFriction;
    setSetup(negativeFriction, QStringLiteral("m=1; k=100; mu=-0.2; x0=0.2"));
    EXPECT_GE(negativeFriction.frictionCoeff(), 0.0);
    EXPECT_NEAR(negativeFriction.frictionForce(), 0.0, 1e-12);

    SpringHorizontalModel zeroOffset;
    setSetup(zeroOffset, QStringLiteral("m=1; k=100; mu=0; x0=0"));
    EXPECT_TRUE(isInvalid(zeroOffset));
}

TEST(SpringHorizontal, RejectsOffsetInsideStaticFrictionZone) {
    SpringHorizontalModel model;
    fill(model, 1.0, 40.0, 0.5, 0.05);
    // μmg/k = 0,1225 м: при x₀ = 0,05 м сила пружины не преодолеет трение покоя.
    EXPECT_GT(model.deadZone(), 0.05);
    EXPECT_TRUE(isInvalid(model));
}

// ------------------------------------------------------------------
// μ = 0: численное решение совпадает с гармоническим законом
// ------------------------------------------------------------------

TEST(SpringHorizontal, FrictionlessMotionMatchesCosineLaw) {
    SpringHorizontalModel model;
    fill(model, 1.0, 100.0, 0.0, 0.2);

    const double omega = model.omega();
    for (int i = 0; i <= 8; ++i) {
        const double t = model.duration() * i / 8.0;
        EXPECT_NEAR(model.displacementAt(t), 0.2 * std::cos(omega * t), 1e-4) << "t = " << t;
        EXPECT_NEAR(model.velocityAt(t), -0.2 * omega * std::sin(omega * t), 1e-3) << "t = " << t;
    }
}

TEST(SpringHorizontal, FrictionlessStartConditionsAreRespected) {
    SpringHorizontalModel model;
    fill(model, 1.5, 60.0, 0.0, -0.15);

    EXPECT_NEAR(model.displacementAt(0.0), -0.15, 1e-12);
    EXPECT_NEAR(model.velocityAt(0.0), 0.0, 1e-12);
    // Отпущенный груз ускоряется к равновесию: a = -kx/m.
    EXPECT_NEAR(model.accelerationAt(0.0), 60.0 * 0.15 / 1.5, 1e-9);
    EXPECT_NEAR(model.elasticForceAt(0.0), 60.0 * 0.15, 1e-9);
}

TEST(SpringHorizontal, FrictionlessMotionNeverStopsAndKeepsAmplitude) {
    SpringHorizontalModel model;
    fill(model, 1.0, 100.0, 0.0, 0.2);

    EXPECT_TRUE(std::isnan(model.restTime()));
    EXPECT_TRUE(std::isnan(model.eventTime()));
    EXPECT_NEAR(model.maxDisplacement(), 0.2, 1e-3);
}

TEST(SpringHorizontal, TurningPointsMatchHarmonicLaw) {
    SpringHorizontalModel model;
    fill(model, 1.0, 100.0, 0.0, 0.2);

    // Через четверть периода груз проходит положение равновесия: x = 0, |v| = Aω.
    const double quarter = model.period() / 4.0;
    EXPECT_NEAR(model.displacementAt(quarter), 0.0, 1e-3);
    EXPECT_NEAR(std::abs(model.velocityAt(quarter)), 0.2 * model.omega(), 1e-2);

    // Через половину периода — в противоположной крайней точке, скорость нулевая.
    const double half = model.period() / 2.0;
    EXPECT_NEAR(model.displacementAt(half), -0.2, 1e-3);
    EXPECT_NEAR(model.velocityAt(half), 0.0, 1e-2);
    EXPECT_NEAR(model.accelerationAt(half), 100.0 * 0.2, 1.0);
}

// ------------------------------------------------------------------
// μ > 0: численное интегрирование, а не подставная гармоническая формула
// ------------------------------------------------------------------

TEST(SpringHorizontal, FrictionDampsAmplitudeFromTurnToTurn) {
    SpringHorizontalModel model;
    fill(model, 1.0, 100.0, 0.12, 0.3);

    // Точки поворота — узлы, в которых скорость стала нулевой (их фиксирует
    // условие остановки). Пока груз движется, |x| в этих точках строго убывает:
    // затухание даёт численное решение, а не подставная гармоническая формула.
    const double stop = model.restTime();
    ASSERT_TRUE(std::isfinite(stop));

    const QVector<QPointF> displacement = model.displacementSeries();
    const QVector<QPointF> velocity = model.velocitySeries();
    QVector<double> amplitudes;
    for (int i = 1; i < velocity.size(); ++i) {
        if (velocity.at(i).x() >= stop)
            break;
        if (std::abs(velocity.at(i).y()) <= 1e-12)
            amplitudes.push_back(std::abs(displacement.at(i).y()));
    }
    ASSERT_GE(amplitudes.size(), 3);
    EXPECT_LT(amplitudes.first(), 0.3);
    for (int i = 1; i < amplitudes.size(); ++i)
        EXPECT_LT(amplitudes.at(i), amplitudes.at(i - 1)) << "поворот " << i;
}

TEST(SpringHorizontal, AmplitudeLossPerHalfPeriodMatchesFormula) {
    SpringHorizontalModel model;
    fill(model, 1.0, 100.0, 0.1, 0.4);

    // За полупериод амплитуда убывает ровно на 2μmg/k — линейно, а не по экспоненте.
    const double expectedLoss = 2.0 * 0.1 * 1.0 * 9.8 / 100.0;
    const double turn = firstTurningTime(model);
    ASSERT_TRUE(std::isfinite(turn));
    const double firstAmplitude = std::abs(model.displacementAt(turn));
    EXPECT_NEAR(0.4 - firstAmplitude, expectedLoss, 2e-3);
}

TEST(SpringHorizontal, BlockEventuallyStopsInsideDeadZone) {
    SpringHorizontalModel model;
    fill(model, 1.0, 100.0, 0.2, 0.3);

    const double stop = model.restTime();
    ASSERT_TRUE(std::isfinite(stop));
    EXPECT_GT(stop, 0.0);
    EXPECT_LE(stop, model.duration());
    // Покоиться груз может только там, где его удерживает трение покоя.
    EXPECT_LE(std::abs(model.restPosition()), model.deadZone() + 1e-6);

    // После остановки состояние не меняется: ни координата, ни скорость.
    EXPECT_NEAR(model.velocityAt(model.duration()), 0.0, 1e-12);
    EXPECT_NEAR(model.displacementAt(model.duration()), model.restPosition(), 1e-9);
}

TEST(SpringHorizontal, StaticFrictionNeitherReversesNorCreatesMotion) {
    SpringHorizontalModel model;
    fill(model, 1.0, 100.0, 0.2, 0.3);

    ASSERT_TRUE(std::isfinite(model.restTime()));
    const double xRest = model.restPosition();
    // Условие покоя: ускорение в точке остановки равно нулю.
    EXPECT_NEAR(model.accelerationOf(xRest, 0.0), 0.0, 1e-12);

    // В покое силы уравновешены, трение не превышает μmg и не «разворачивается».
    const double elastic = model.elasticForceAt(model.duration());
    const double friction = model.frictionForceAt(model.duration());
    EXPECT_NEAR(elastic + friction, 0.0, 1e-9);
    EXPECT_LE(std::abs(friction), model.frictionForce() + 1e-12);
}

TEST(SpringHorizontal, StrongerFrictionStopsMotionEarlier) {
    SpringHorizontalModel soft;
    fill(soft, 1.0, 100.0, 0.08, 0.3);
    SpringHorizontalModel strong;
    fill(strong, 1.0, 100.0, 0.25, 0.3);

    ASSERT_TRUE(std::isfinite(soft.restTime()));
    ASSERT_TRUE(std::isfinite(strong.restTime()));
    EXPECT_GT(strong.frictionForce(), soft.frictionForce());
    EXPECT_LT(strong.restTime(), soft.restTime());
}

TEST(SpringHorizontal, DurationFollowsPeriod) {
    SpringHorizontalModel soft;
    fill(soft, 1.0, 20.0, 0.0, 0.2);
    SpringHorizontalModel stiff;
    fill(stiff, 1.0, 320.0, 0.0, 0.2);

    EXPECT_GT(soft.period(), stiff.period());
    EXPECT_GT(soft.duration(), stiff.duration());
}

TEST(SpringHorizontal, SmallStiffnessLargeMassStillIntegrates) {
    SpringHorizontalModel model;
    fill(model, 5.0, 5.0, 0.01, 0.5);
    ASSERT_TRUE(model.isValid());

    EXPECT_NEAR(model.displacementAt(0.0), 0.5, 1e-12);
    // a(0) = (-kx + μmg)/m: трение направлено против движущей силы.
    EXPECT_NEAR(model.accelerationAt(0.0), (-5.0 * 0.5 + 0.01 * 5.0 * 9.8) / 5.0, 1e-9);
    EXPECT_GT(model.period(), 6.0);
}

// ------------------------------------------------------------------
// Состояние, событие, вычисления, графики
// ------------------------------------------------------------------

TEST(SpringHorizontal, StateExposesBlockKinematics) {
    SpringHorizontalModel model;
    fill(model, 1.0, 100.0, 0.0, 0.2);

    const double t = model.period() / 4.0;
    const lab::SimState state = model.stateAt(t);
    ASSERT_FALSE(state.entities.isEmpty());
    EXPECT_EQ(state.entities.first().id, QStringLiteral("load"));
    EXPECT_NEAR(state.primary, model.displacementAt(t), 1e-12);
    EXPECT_NEAR(state.entities.first().value1, model.velocityAt(t), 1e-12);
    EXPECT_NEAR(state.entities.first().value2, model.accelerationAt(t), 1e-9);
    EXPECT_FALSE(state.resolved);
}

TEST(SpringHorizontal, StateMarksRestAfterStopEvent) {
    SpringHorizontalModel model;
    fill(model, 1.0, 100.0, 0.2, 0.3);

    const double stop = model.restTime();
    ASSERT_TRUE(std::isfinite(stop));
    EXPECT_NEAR(model.eventTime(), stop, 1e-9);

    EXPECT_FALSE(model.stateAt(stop / 2.0).resolved);
    const lab::SimState after = model.stateAt(model.duration());
    EXPECT_TRUE(after.resolved);
    EXPECT_FALSE(after.eventLabel.isEmpty());
}

TEST(SpringHorizontal, DerivationContainsKeyFormulasAndSubstitutions) {
    SpringHorizontalModel model;
    fill(model, 2.0, 200.0, 0.1, 0.25);

    const lab::Derivation d = model.derivation();
    ASSERT_TRUE(d.valid);
    ASSERT_FALSE(d.steps.isEmpty());

    QString all;
    for (const lab::DerivationStep& step : d.steps)
        all += step.content;
    EXPECT_TRUE(all.contains(QStringLiteral("m x''")));
    EXPECT_TRUE(all.contains(QStringLiteral("F_{\\text{упр}} = -k x")));
    EXPECT_TRUE(all.contains(QStringLiteral("N = m g")));
    EXPECT_TRUE(all.contains(QStringLiteral("\\omega = \\sqrt")));
    EXPECT_TRUE(all.contains(QStringLiteral("T = 2\\pi")));
    EXPECT_TRUE(all.contains(QStringLiteral("Рунге")));
    // Подстановка чисел присутствует.
    EXPECT_TRUE(all.contains(QStringLiteral("2")));
}

TEST(SpringHorizontal, ReadoutsFollowModelState) {
    SpringHorizontalModel model;
    fill(model, 1.0, 100.0, 0.15, 0.3);
    model.seekTo(0.35);

    const QVector<lab::Readout> readouts = model.readouts();
    ASSERT_GE(readouts.size(), 6);
    EXPECT_EQ(readouts.first().label, QStringLiteral("Смещение x"));
    EXPECT_TRUE(readouts.first().value.contains(lab::utils::formatNumber(model.displacementAt(0.35), 4)));
    EXPECT_TRUE(readouts.at(2).value.contains(QStringLiteral("м/с²")));
}

TEST(SpringHorizontal, PlotSeriesComeFromComputedTrajectory) {
    SpringHorizontalModel model;
    fill(model, 1.0, 100.0, 0.15, 0.3);

    const QVector<QPointF> x = model.displacementSeries();
    const QVector<QPointF> v = model.velocitySeries();
    const QVector<QPointF> a = model.accelerationSeries();
    ASSERT_GE(x.size(), 200);
    ASSERT_EQ(x.size(), v.size());
    ASSERT_EQ(x.size(), a.size());

    EXPECT_NEAR(x.first().x(), 0.0, 1e-12);
    EXPECT_NEAR(x.first().y(), 0.3, 1e-12);
    EXPECT_NEAR(v.first().y(), 0.0, 1e-12);
    EXPECT_NEAR(x.last().x(), model.duration(), 1e-9);
    // Ряд ускорения строится из уравнения движения, а не из отдельной формулы.
    EXPECT_NEAR(a.first().y(), (-100.0 * 0.3 + 0.15 * 1.0 * 9.8) / 1.0, 1e-9);
}

TEST(SpringHorizontal, BlockHandleChangesModelNotOnlyPicture) {
    SpringHorizontalModel model;
    fill(model, 1.0, 100.0, 0.1, 0.3);

    const QVector<lab::HandleInfo> handles = model.handles();
    ASSERT_FALSE(handles.isEmpty());
    EXPECT_EQ(handles.first().id, QStringLiteral("x0"));

    // 0,18 > μmg/k = 0,098 м — новое смещение физически допустимо.
    model.setHandleValue(QStringLiteral("x0"), -0.18);
    EXPECT_NEAR(model.initialDisplacement(), -0.18, 1e-9);
    EXPECT_NEAR(model.displacementAt(0.0), -0.18, 1e-9);
    EXPECT_NEAR(model.elasticForceAt(0.0), 18.0, 1e-9);

    // Перетаскивание возвращает эксперимент в начало отсчёта времени.
    model.beginInteraction();
    EXPECT_NEAR(model.time(), 0.0, 1e-12);
}

TEST(SpringHorizontal, TrajectoryIsRecomputedAfterParameterChange) {
    SpringHorizontalModel model;
    fill(model, 1.0, 100.0, 0.0, 0.2);
    const double halfPeriod = model.period() / 2.0;
    const double firstHalf = model.displacementAt(halfPeriod);

    setSetup(model, QStringLiteral("m=1; k=100; mu=0; x0=-0.2"));
    const double secondHalf = model.displacementAt(halfPeriod);
    EXPECT_NEAR(firstHalf, -secondHalf, 1e-6);
}

TEST(SpringHorizontal, SeekingBackwardIsReproducible) {
    SpringHorizontalModel model;
    fill(model, 1.0, 100.0, 0.2, 0.3);

    const double t = model.duration() * 0.4;
    const double forward = model.displacementAt(t);
    model.seekTo(model.duration());
    model.seekTo(0.0);
    EXPECT_NEAR(model.displacementAt(t), forward, 1e-12);
}
