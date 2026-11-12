#include <gtest/gtest.h>

#include <cmath>

#include "TestSupport.h"
#include "models/ProjectileModel.h"
#include "utils/MathText.h"

using lab::models::ProjectileModel;
using lab::test::isInvalid;
using lab::test::setSetup;

namespace {

/** Учебная установка: скорость, угол, высота и положение точки выстрела. */
void fill(ProjectileModel& model, double v0, double angle, double y0 = 0.0, double x0 = 0.0) {
    setSetup(model, QStringLiteral("v0=%1; angle=%2; y0=%3; x0=%4").arg(v0).arg(angle).arg(y0).arg(x0));
}

}  // namespace

// ------------------------------------------------------------------
// Проекции начальной скорости
// ------------------------------------------------------------------

TEST(Projectile, ProjectionsFollowTrigonometry) {
    ProjectileModel model;
    fill(model, 40.0, 30.0);

    // v₀ₓ = v₀ cos θ, v₀ᵧ = v₀ sin θ.
    EXPECT_NEAR(model.initialVx(), 40.0 * std::cos(M_PI / 6.0), 1e-12);
    EXPECT_NEAR(model.initialVy(), 40.0 * std::sin(M_PI / 6.0), 1e-12);
    EXPECT_NEAR(model.angleRadians(), M_PI / 6.0, 1e-12);
}

TEST(Projectile, AngleOfFortyFiveDegreesIsSymmetric) {
    ProjectileModel model;
    fill(model, 25.0, 45.0);
    EXPECT_NEAR(model.initialVx(), model.initialVy(), 1e-12);
}

// ------------------------------------------------------------------
// Закон движения: две независимые проекции
// ------------------------------------------------------------------

TEST(Projectile, MotionFollowsProjectionLaws) {
    ProjectileModel model;
    fill(model, 30.0, 37.0, 5.0, 2.0);

    const double vx = model.initialVx();
    const double vy = model.initialVy();
    for (double t = 0.0; t < model.flightTime(); t += 0.11) {
        EXPECT_NEAR(model.xAt(t), 2.0 + vx * t, 1e-9) << "t = " << t;
        EXPECT_NEAR(model.yAt(t), 5.0 + vy * t - 4.9 * t * t, 1e-9) << "t = " << t;
        EXPECT_NEAR(model.vxAt(t), vx, 1e-12) << "t = " << t;
        EXPECT_NEAR(model.vyAt(t), vy - 9.8 * t, 1e-9) << "t = " << t;
    }
}

TEST(Projectile, HorizontalMotionIsUniformAndVerticalIsAccelerated) {
    ProjectileModel model;
    fill(model, 20.0, 20.0);

    // aₓ = 0: приращения x за равные промежутки времени одинаковы.
    const double h = 0.25;
    EXPECT_NEAR(model.xAt(h) - model.xAt(0.0), model.xAt(2.0 * h) - model.xAt(h), 1e-12);
    // aᵧ = −g: приращения y уменьшаются на g·h².
    const double secondDifference = model.yAt(2.0 * h) - 2.0 * model.yAt(h) + model.yAt(0.0);
    EXPECT_NEAR(secondDifference, -9.8 * h * h, 1e-9);
}

TEST(Projectile, StartConditionsAreRespected) {
    ProjectileModel model;
    fill(model, 18.0, 60.0, 7.5, 3.0);

    EXPECT_NEAR(model.xAt(0.0), 3.0, 1e-12);
    EXPECT_NEAR(model.yAt(0.0), 7.5, 1e-12);
    EXPECT_NEAR(model.speedAt(0.0), 18.0, 1e-9);
}

// ------------------------------------------------------------------
// Время полёта с учётом высоты выстрела
// ------------------------------------------------------------------

TEST(Projectile, FlightTimeFromSurfaceMatchesFormula) {
    ProjectileModel model;
    fill(model, 20.0, 30.0, 0.0);
    // y₀ = 0: T = 2 v₀ sin θ / g.
    EXPECT_NEAR(model.flightTime(), 2.0 * 20.0 * 0.5 / 9.8, 1e-12);
}

TEST(Projectile, FlightTimeAccountsForLaunchHeight) {
    ProjectileModel model;
    fill(model, 20.0, 30.0, 20.0);
    // T = (v₀ᵧ + √(v₀ᵧ² + 2 g y₀)) / g — частный случай y₀ = 0 не единственный.
    const double vy = 20.0 * std::sin(M_PI / 6.0);
    const double expected = (vy + std::sqrt(vy * vy + 2.0 * 9.8 * 20.0)) / 9.8;
    EXPECT_NEAR(model.flightTime(), expected, 1e-12);
    EXPECT_GT(model.flightTime(), 2.0 * vy / 9.8);
    // На земле координата y обнуляется именно в этот момент.
    EXPECT_NEAR(model.yAt(model.flightTime()), 0.0, 1e-9);
}

TEST(Projectile, HorizontalShotFromHeightIsAllowed) {
    ProjectileModel model;
    setSetup(model, QStringLiteral("v0=15; angle=0; y0=20; x0=0"));
    // Горизонтальный выстрел с высоты — тоже парабола: случай допустим.
    ASSERT_FALSE(isInvalid(model));
    // Вверх снаряд не поднимается: время полёта равно времени падения с y₀.
    EXPECT_NEAR(model.flightTime(), std::sqrt(2.0 * 20.0 / 9.8), 1e-12);
    EXPECT_NEAR(model.maxHeight(), 20.0, 1e-12);
}

TEST(Projectile, RejectsDegenerateShots) {
    ProjectileModel flatOnGround;
    setSetup(flatOnGround, QStringLiteral("v0=10; angle=0; y0=0; x0=0"));
    EXPECT_TRUE(isInvalid(flatOnGround));

    ProjectileModel vertical;
    setSetup(vertical, QStringLiteral("v0=10; angle=90; y0=0; x0=0"));
    EXPECT_TRUE(isInvalid(vertical));
}

// ------------------------------------------------------------------
// Дальность и наибольшая высота
// ------------------------------------------------------------------

TEST(Projectile, RangeMatchesFormula) {
    ProjectileModel model;
    fill(model, 30.0, 45.0, 0.0);
    // L = v₀² sin 2θ / g; при θ = 45° это v₀²/g.
    EXPECT_NEAR(model.range(), 30.0 * 30.0 / 9.8, 1e-9);
    EXPECT_NEAR(model.xAt(model.flightTime()), model.range(), 1e-9);
}

TEST(Projectile, RangeAndHeightFollowApexFormulas) {
    ProjectileModel model;
    fill(model, 25.0, 53.130102, 4.0);
    // t_h = v₀ᵧ/g, H = y₀ + v₀ᵧ²/(2g).
    const double vy = model.initialVy();
    EXPECT_NEAR(model.apexTime(), vy / 9.8, 1e-12);
    EXPECT_NEAR(model.maxHeight(), 4.0 + vy * vy / (2.0 * 9.8), 1e-12);
    EXPECT_NEAR(model.yAt(model.apexTime()), model.maxHeight(), 1e-12);
    // В вершине вертикальная проекция скорости обращается в нуль.
    EXPECT_NEAR(model.vyAt(model.apexTime()), 0.0, 1e-9);
}

TEST(Projectile, ApexIsInsideFlight) {
    ProjectileModel model;
    fill(model, 22.0, 35.0, 6.0);
    EXPECT_GT(model.apexTime(), 0.0);
    EXPECT_LT(model.apexTime(), model.flightTime());
}

TEST(Projectile, SpeedAtImpactMatchesProjections) {
    ProjectileModel model;
    fill(model, 24.0, 40.0, 10.0);

    EXPECT_NEAR(model.impactVx(), model.initialVx(), 1e-12);
    EXPECT_NEAR(model.impactVy(), model.initialVy() - 9.8 * model.flightTime(), 1e-12);
    EXPECT_NEAR(model.impactSpeed(), std::hypot(model.impactVx(), model.impactVy()), 1e-12);
    EXPECT_GT(model.impactSpeed(), model.speed());   // выстрел с высоты: удар быстрее вылета
    EXPECT_GT(model.impactAngleDegrees(), 0.0);
    EXPECT_LT(model.impactAngleDegrees(), 90.0);
}

// ------------------------------------------------------------------
// Уравнение траектории y(x)
// ------------------------------------------------------------------

TEST(Projectile, TrajectoryEquationMatchesParametricMotion) {
    ProjectileModel model;
    fill(model, 28.0, 42.0, 3.0, 1.5);

    // y(x) = y₀ + (x − x₀) tan θ − g(x − x₀)²/(2 v₀² cos²θ).
    for (double t = 0.0; t < model.flightTime(); t += 0.13) {
        const double x = model.xAt(t);
        EXPECT_NEAR(model.trajectoryY(x), model.yAt(t), 1e-9) << "x = " << x;
    }
}

TEST(Projectile, TrajectoryPassesThroughLaunchAndLandingPoints) {
    ProjectileModel model;
    fill(model, 26.0, 38.0, 9.0, 2.0);

    EXPECT_NEAR(model.trajectoryY(model.launchPosition()), 9.0, 1e-12);
    EXPECT_NEAR(model.trajectoryY(model.launchPosition() + model.range()), 0.0, 1e-9);
}

// ------------------------------------------------------------------
// Время наблюдения и событие
// ------------------------------------------------------------------

TEST(Projectile, DurationEqualsFlightTimeAndEventIsLanding) {
    ProjectileModel model;
    fill(model, 32.0, 45.0, 12.0);

    EXPECT_NEAR(model.duration(), model.flightTime(), 1e-12);
    EXPECT_NEAR(model.eventTime(), model.flightTime(), 1e-12);
    EXPECT_GT(model.stepSize(), 0.0);
    EXPECT_GT(model.timePerSecond(), 0.0);
}

TEST(Projectile, MotionStopsAtGround) {
    ProjectileModel model;
    fill(model, 30.0, 50.0, 8.0);
    const double flight = model.flightTime();

    // Перемотка за пределы полёта не даёт «движения под землёй».
    EXPECT_NEAR(model.yAt(flight + 5.0), 0.0, 1e-9);
    EXPECT_NEAR(model.xAt(flight + 5.0), model.launchPosition() + model.range(), 1e-9);
    EXPECT_TRUE(model.stateAt(flight).resolved);
    EXPECT_FALSE(model.stateAt(flight / 2.0).resolved);
}

// ------------------------------------------------------------------
// Состояние и проекции движущейся точки
// ------------------------------------------------------------------

TEST(Projectile, StateCarriesShellAndItsProjections) {
    ProjectileModel model;
    fill(model, 27.0, 33.0, 5.0);
    const double t = model.flightTime() * 0.4;

    const lab::SimState state = model.stateAt(t);
    ASSERT_GE(state.entities.size(), 3);
    EXPECT_EQ(state.entities.at(0).id, QStringLiteral("shell"));
    EXPECT_EQ(state.entities.at(1).id, QStringLiteral("proj_x"));
    EXPECT_EQ(state.entities.at(2).id, QStringLiteral("proj_y"));

    // Проекция на ось X — координата x, на ось Y — координата y: именно эти
    // точки бегут по осям на схеме.
    EXPECT_NEAR(state.entities.at(1).position, model.xAt(t), 1e-12);
    EXPECT_NEAR(state.entities.at(2).position, model.yAt(t), 1e-12);
    EXPECT_NEAR(state.entities.at(1).value1, model.vxAt(t), 1e-12);
    EXPECT_NEAR(state.entities.at(2).value1, model.vyAt(t), 1e-12);
}

TEST(Projectile, ShellSpeedIsZeroAfterLanding) {
    ProjectileModel model;
    fill(model, 26.0, 44.0, 6.0);

    const lab::SimState after = model.stateAt(model.duration());
    ASSERT_FALSE(after.entities.isEmpty());
    EXPECT_NEAR(after.entities.first().value2, 0.0, 1e-12);
    EXPECT_FALSE(after.entities.first().active);
}

// ------------------------------------------------------------------
// Панель вычислений и показания
// ------------------------------------------------------------------

TEST(Projectile, DerivationContainsKeyFormulasAndSubstitutions) {
    ProjectileModel model;
    fill(model, 32.0, 45.0, 12.0, 3.0);

    const lab::Derivation d = model.derivation();
    ASSERT_TRUE(d.valid);
    ASSERT_FALSE(d.steps.isEmpty());

    QString all;
    for (const lab::DerivationStep& step : d.steps)
        all += step.content;
    EXPECT_TRUE(all.contains(QStringLiteral("x(t) = x_{0} + v_{0} \\cos\\theta")));
    EXPECT_TRUE(all.contains(QStringLiteral("y(t) = y_{0} + v_{0}")));
    EXPECT_TRUE(all.contains(QStringLiteral("a_{x} = 0")));
    EXPECT_TRUE(all.contains(QStringLiteral("L = v_{0x} T")));
    EXPECT_TRUE(all.contains(QStringLiteral("H = y_{0} +")));
    EXPECT_TRUE(all.contains(QStringLiteral("y(x) = y_{0} +")));
    // Подстановка чисел присутствует (значения formatNumber разделены запятой).
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(32.0, 1)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(12.0, 1)));
}

TEST(Projectile, DerivationReportsInvalidSetup) {
    ProjectileModel model;
    setSetup(model, QStringLiteral("v0=10; angle=0; y0=0; x0=0"));

    const lab::Derivation d = model.derivation();
    EXPECT_FALSE(d.valid);
    EXPECT_FALSE(d.answerTemplate.isEmpty());
}

TEST(Projectile, ReadoutsFollowModelState) {
    ProjectileModel model;
    fill(model, 30.0, 40.0, 10.0);
    model.seekTo(model.flightTime() * 0.5);
    const double t = model.flightTime() * 0.5;

    const QVector<lab::Readout> readouts = model.readouts();
    ASSERT_GE(readouts.size(), 7);
    EXPECT_TRUE(readouts.at(0).value.contains(lab::utils::formatNumber(model.xAt(t), 2)));
    EXPECT_TRUE(readouts.at(2).value.contains(QStringLiteral("м/с")));
    EXPECT_TRUE(readouts.at(5).value.contains(lab::utils::formatNumber(model.range(), 2)));
}

// ------------------------------------------------------------------
// Ряды данных для графиков и трассы
// ------------------------------------------------------------------

TEST(Projectile, SeriesCoverWholeFlight) {
    ProjectileModel model;
    fill(model, 29.0, 41.0, 7.0);

    const QVector<QPointF> x = model.xSeries();
    const QVector<QPointF> y = model.ySeries();
    const QVector<QPointF> vx = model.vxSeries();
    const QVector<QPointF> vy = model.vySeries();
    const QVector<QPointF> trace = model.trajectoryPoints();

    ASSERT_GE(x.size(), 100);
    EXPECT_EQ(x.size(), y.size());
    EXPECT_EQ(x.size(), trace.size());
    EXPECT_NEAR(x.first().x(), 0.0, 1e-12);
    EXPECT_NEAR(x.last().x(), model.flightTime(), 1e-9);
    EXPECT_NEAR(y.last().y(), 0.0, 1e-6);
    EXPECT_NEAR(vx.first().y(), model.initialVx(), 1e-12);
    EXPECT_NEAR(vy.last().y(), model.impactVy(), 1e-6);

    // Трасса хранит координаты (x, y) того же движения, ряды — пары (t, значение).
    for (int i = 0; i < trace.size(); i += 40) {
        EXPECT_NEAR(trace.at(i).x(), x.at(i).y(), 1e-12);
        EXPECT_NEAR(trace.at(i).y(), y.at(i).y(), 1e-12);
    }
}

// ------------------------------------------------------------------
// Точки взаимодействия
// ------------------------------------------------------------------

TEST(Projectile, HandlesCoverAngleAndLaunchPoint) {
    ProjectileModel model;
    fill(model, 30.0, 45.0, 10.0, 2.0);

    const QVector<lab::HandleInfo> handles = model.handles();
    ASSERT_EQ(handles.size(), 3);
    EXPECT_EQ(handles.at(0).id, QStringLiteral("angle"));
    EXPECT_EQ(handles.at(1).id, QStringLiteral("y0"));
    EXPECT_EQ(handles.at(2).id, QStringLiteral("x0"));

    // Перетаскивание направления выстрела меняет модель, а не только картинку.
    model.setHandleValue(QStringLiteral("angle"), 20.0);
    EXPECT_NEAR(model.angleDegrees(), 20.0, 1e-9);
    EXPECT_NEAR(model.initialVy(), 30.0 * std::sin(20.0 * M_PI / 180.0), 1e-9);

    model.setHandleValue(QStringLiteral("y0"), 25.0);
    EXPECT_NEAR(model.launchHeight(), 25.0, 1e-9);
    EXPECT_GT(model.flightTime(), 0.0);

    model.setHandleValue(QStringLiteral("x0"), 12.0);
    EXPECT_NEAR(model.xAt(0.0), 12.0, 1e-9);
}

TEST(Projectile, DraggingRestartsTheFlight) {
    ProjectileModel model;
    fill(model, 30.0, 45.0, 10.0);
    model.seekTo(model.duration());
    ASSERT_GT(model.time(), 0.0);

    model.beginInteraction();
    EXPECT_NEAR(model.time(), 0.0, 1e-12);
}

TEST(Projectile, TrajectoryScalesWithSpeed) {
    ProjectileModel slow;
    fill(slow, 20.0, 45.0);
    ProjectileModel fast;
    fill(fast, 40.0, 45.0);

    // Дальность растет как квадрат скорости: v₀ и 2v₀ дают 4L.
    EXPECT_NEAR(fast.range(), 4.0 * slow.range(), 1e-9);
    EXPECT_NEAR(fast.maxHeight(), 4.0 * slow.maxHeight(), 1e-9);
}

TEST(Projectile, ComplementaryAnglesGiveEqualRangeOnLevelGround) {
    ProjectileModel low;
    fill(low, 30.0, 30.0);
    ProjectileModel high;
    fill(high, 30.0, 60.0);

    EXPECT_NEAR(low.range(), high.range(), 1e-9);
    EXPECT_GT(high.maxHeight(), low.maxHeight());
}
