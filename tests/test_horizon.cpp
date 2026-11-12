#include <gtest/gtest.h>

#include <cmath>

#include "TestSupport.h"
#include "models/HorizonModel.h"
#include "utils/MathText.h"

using lab::models::HorizonModel;
using lab::test::isInvalid;
using lab::test::setSetup;

namespace {

/** Учебная установка: радиус планеты в километрах, высота подъёма и скорость. */
void fill(HorizonModel& model, double radiusKm, double height, double speed) {
    setSetup(model, QStringLiteral("radius=%1; h=%2; v=%3").arg(radiusKm).arg(height).arg(speed));
}

constexpr double kEarthRadius = 6371.0;   // км

}  // namespace

// ------------------------------------------------------------------
// Расстояние до горизонта по прямой
// ------------------------------------------------------------------

TEST(Horizon, DistanceFollowsPythagoras) {
    HorizonModel model;
    fill(model, kEarthRadius, 2000.0, 25.0);

    // d = √((R + h)² − R²) = √(2Rh + h²).
    const double radius = model.radiusMeters();
    const double expected = std::sqrt(2.0 * radius * 2000.0 + 2000.0 * 2000.0);
    EXPECT_NEAR(model.horizonDistance(2000.0), expected, 1e-9);
    // То же значение из теоремы Пифагора — две независимые записи одной величины.
    EXPECT_NEAR(model.horizonDistance(2000.0), std::sqrt(std::pow(radius + 2000.0, 2.0) - std::pow(radius, 2.0)), 1e-6);
}

TEST(Horizon, DistanceMatchesSchoolNumbers) {
    HorizonModel model;
    fill(model, kEarthRadius, 2.0, 25.0);
    // Классический школьный ответ: с высоты роста человека горизонт в ~5 км.
    EXPECT_NEAR(model.horizonDistance(2.0), std::sqrt(2.0 * 6371000.0 * 2.0), 0.5);
    EXPECT_NEAR(model.horizonDistance(2.0) / 1000.0, 5.04, 0.02);
}

TEST(Horizon, ApproximateFormulaIsExactAtSmallHeights) {
    HorizonModel model;
    fill(model, kEarthRadius, 100.0, 25.0);

    const double exact = model.horizonDistance(100.0);
    const double approx = model.approximateDistance(100.0);
    // h ≪ R: слагаемым h² можно пренебречь, погрешность — доли процента.
    EXPECT_LT(std::abs(exact - approx) / exact, 1e-4);
    EXPECT_GT(exact, approx);   // точная формула всегда больше приближённой
}

TEST(Horizon, DistanceGrowsWithSquareRootOfHeight) {
    HorizonModel model;
    fill(model, kEarthRadius, 100.0, 25.0);

    // d ∼ √h: рост высоты в 4 раза увеличивает дальность вдвое
    // (слагаемое h² вносит поправку около 10⁻⁶).
    EXPECT_NEAR(model.horizonDistance(400.0), 2.0 * model.horizonDistance(100.0), 1.0);
    EXPECT_GT(model.horizonDistance(400.0) / model.horizonDistance(100.0), 1.999);
    EXPECT_LT(model.horizonDistance(400.0) / model.horizonDistance(100.0), 2.001);
}

TEST(Horizon, SmallerPlanetGivesCloserHorizon) {
    HorizonModel earth;
    fill(earth, kEarthRadius, 2000.0, 25.0);
    HorizonModel moon;
    fill(moon, 1737.0, 2000.0, 25.0);

    EXPECT_LT(moon.horizonDistance(2000.0), earth.horizonDistance(2000.0));
}

// ------------------------------------------------------------------
// Центральный угол и угол понижения горизонта
// ------------------------------------------------------------------

TEST(Horizon, CentralAngleFollowsCosine) {
    HorizonModel model;
    fill(model, kEarthRadius, 2000.0, 25.0);

    const double radius = model.radiusMeters();
    const double expected = std::acos(radius / (radius + 2000.0));
    EXPECT_NEAR(model.centralAngle(2000.0), expected, 1e-12);
    EXPECT_NEAR(model.dipAngleDegrees(2000.0), expected * 180.0 / M_PI, 1e-12);
    EXPECT_GT(model.dipAngleDegrees(2000.0), 0.0);
    EXPECT_LT(model.dipAngleDegrees(2000.0), 90.0);
}

TEST(Horizon, CentralAngleGrowsWithHeight) {
    HorizonModel model;
    fill(model, kEarthRadius, 1000.0, 25.0);

    EXPECT_GT(model.centralAngle(5000.0), model.centralAngle(1000.0));
    EXPECT_GT(model.dipAngleDegrees(5000.0), model.dipAngleDegrees(1000.0));
}

TEST(Horizon, AngleTendsToRightAngleAtOrbitHeight) {
    HorizonModel model;
    fill(model, kEarthRadius, 100.0, 25.0);
    // На очень большой высоте горизонт уходит к горизонтальной плоскости:
    // cos α = R/(R + h) → 0, то есть α → 90°.
    EXPECT_LT(model.dipAngleDegrees(1e12), 90.0);
    EXPECT_GT(model.dipAngleDegrees(1e12), 89.9);
}

// ------------------------------------------------------------------
// Расстояние вдоль поверхности
// ------------------------------------------------------------------

TEST(Horizon, ArcLengthEqualsRadiusTimesAngle) {
    HorizonModel model;
    fill(model, kEarthRadius, 2000.0, 25.0);

    const double radius = model.radiusMeters();
    EXPECT_NEAR(model.arcDistance(2000.0), radius * model.centralAngle(2000.0), 1e-9);
}

TEST(Horizon, StraightLineIsLongerThanArc) {
    HorizonModel model;
    fill(model, kEarthRadius, 30000.0, 25.0);

    // Отрезок касательной длиннее дуги: d = R·tanα > R·α = s (tan α > α).
    EXPECT_GT(model.horizonDistance(30000.0), model.arcDistance(30000.0));
    EXPECT_GT(model.horizonDistance(2000.0), model.arcDistance(2000.0));
    const double alpha = model.centralAngle(30000.0);
    const double radius = model.radiusMeters();
    // В прямоугольном треугольнике tan α = AB/OB, поэтому d = R·tan α.
    EXPECT_NEAR(model.horizonDistance(30000.0), radius * std::tan(alpha), 1e-6);
}

TEST(Horizon, ArcAndDistanceAgreeForSmallHeights) {
    HorizonModel model;
    fill(model, kEarthRadius, 2.0, 25.0);

    // При h ≪ R дуга и касательная практически совпадают: их относительная
    // разница — порядка (h/R).
    const double difference = std::abs(model.arcDistance(2.0) - model.horizonDistance(2.0)) /
                              model.horizonDistance(2.0);
    EXPECT_LT(difference, 1e-6);
}

// ------------------------------------------------------------------
// Видимая доля поверхности
// ------------------------------------------------------------------

TEST(Horizon, VisibleFractionMatchesSphericalSegment) {
    HorizonModel model;
    fill(model, kEarthRadius, 2000.0, 25.0);

    const double radius = model.radiusMeters();
    const double cosAlpha = radius / (radius + 2000.0);
    const double area = 2.0 * M_PI * radius * radius * (1.0 - cosAlpha);
    EXPECT_NEAR(model.visibleArea(2000.0), area, 1e-6);
    EXPECT_NEAR(model.visibleFraction(2000.0), area / (4.0 * M_PI * radius * radius), 1e-15);
    // Та же доля из упрощённой формулы h/(2(R + h)).
    EXPECT_NEAR(model.visibleFraction(2000.0), 2000.0 / (2.0 * (radius + 2000.0)), 1e-15);
}

TEST(Horizon, VisibleFractionIsSmallAndGrowsWithHeight) {
    HorizonModel model;
    fill(model, kEarthRadius, 2000.0, 25.0);

    EXPECT_GT(model.visibleFraction(2000.0), 0.0);
    EXPECT_LT(model.visibleFraction(2000.0), 1e-3);
    EXPECT_GT(model.visibleFraction(20000.0), 10.0 * model.visibleFraction(2000.0) * 0.99);
}

// ------------------------------------------------------------------
// Движение: равномерный подъём
// ------------------------------------------------------------------

TEST(Horizon, HeightGrowsLinearlyAndStopsAtTarget) {
    HorizonModel model;
    fill(model, kEarthRadius, 2000.0, 25.0);

    EXPECT_NEAR(model.ascentTime(), 2000.0 / 25.0, 1e-12);
    EXPECT_NEAR(model.heightAt(10.0), 250.0, 1e-12);
    EXPECT_NEAR(model.verticalVelocityAt(10.0), 25.0, 1e-12);

    // Подъём заканчивается на заданной высоте: дальше высоты нет.
    EXPECT_NEAR(model.heightAt(model.ascentTime()), 2000.0, 1e-12);
    EXPECT_NEAR(model.heightAt(model.ascentTime() + 100.0), 2000.0, 1e-12);
    EXPECT_NEAR(model.verticalVelocityAt(model.ascentTime() + 100.0), 0.0, 1e-12);
}

TEST(Horizon, DurationEqualsAscentTime) {
    HorizonModel model;
    fill(model, kEarthRadius, 3000.0, 20.0);

    EXPECT_NEAR(model.duration(), 150.0, 1e-9);
    EXPECT_NEAR(model.eventTime(), 150.0, 1e-9);
    EXPECT_GT(model.timePerSecond(), 0.0);
    EXPECT_GT(model.stepSize(), 0.0);
}

TEST(Horizon, HorizonDistanceFollowsHeightDuringAscent) {
    HorizonModel model;
    fill(model, kEarthRadius, 5000.0, 50.0);

    // Зависимость от времени — та же формула, что и статический расчёт:
    // других источников чисел на графиках нет.
    for (double t = 0.0; t < model.ascentTime(); t += 3.7) {
        EXPECT_NEAR(model.horizonDistanceAt(t), model.horizonDistance(model.heightAt(t)), 1e-9) << "t = " << t;
        EXPECT_NEAR(model.arcDistanceAt(t), model.arcDistance(model.heightAt(t)), 1e-9) << "t = " << t;
        EXPECT_NEAR(model.dipAngleAt(t), model.dipAngleDegrees(model.heightAt(t)), 1e-12) << "t = " << t;
    }
}

TEST(Horizon, DistanceGrowsMonotonicallyDuringAscent) {
    HorizonModel model;
    fill(model, kEarthRadius, 8000.0, 40.0);

    double previous = 0.0;
    for (double t = 0.0; t <= model.duration(); t += model.duration() / 40.0) {
        const double value = model.horizonDistanceAt(t);
        EXPECT_GE(value, previous - 1e-9) << "t = " << t;
        previous = value;
    }
    EXPECT_NEAR(previous, model.horizonDistance(8000.0), 1e-9);
}

// ------------------------------------------------------------------
// Допустимость постановки
// ------------------------------------------------------------------

TEST(Horizon, RejectsHeightComparableToRadius) {
    HorizonModel model;
    setSetup(model, QStringLiteral("radius=100; h=200000; v=25"));
    // Высота 200 км при радиусе 100 км — наблюдатель «выше» планеты.
    EXPECT_TRUE(isInvalid(model));
}

TEST(Horizon, OutOfRangeValuesAreClampedNotRejected) {
    // Панель параметров не пропускает значения вне диапазона: скорость и
    // высота поднимаются до минимума, расчёт остаётся корректным.
    HorizonModel model;
    setSetup(model, QStringLiteral("radius=6371; h=0; v=0"));
    EXPECT_GE(model.ascentSpeed(), 0.5);
    EXPECT_GE(model.maxHeight(), 1.0);
    EXPECT_FALSE(isInvalid(model));
    EXPECT_GT(model.horizonDistance(model.maxHeight()), 0.0);
}

TEST(Horizon, AcceptsOrdinarySchoolSetup) {
    HorizonModel model;
    fill(model, kEarthRadius, 1.7, 1.0);
    EXPECT_FALSE(isInvalid(model));
}

// ------------------------------------------------------------------
// Состояние системы
// ------------------------------------------------------------------

TEST(Horizon, StateCarriesObserverAndPlanet) {
    HorizonModel model;
    fill(model, kEarthRadius, 2000.0, 25.0);
    const double t = model.ascentTime() * 0.4;

    const lab::SimState state = model.stateAt(t);
    ASSERT_GE(state.entities.size(), 2);
    EXPECT_EQ(state.entities.at(0).id, QStringLiteral("observer"));
    EXPECT_EQ(state.entities.at(1).id, QStringLiteral("planet"));

    // Наблюдатель: высота, дальность горизонта и скорость подъёма.
    EXPECT_NEAR(state.entities.at(0).position, model.heightAt(t), 1e-12);
    EXPECT_NEAR(state.entities.at(0).value1, model.horizonDistanceAt(t), 1e-9);
    EXPECT_NEAR(state.entities.at(0).value2, 25.0, 1e-12);
    EXPECT_TRUE(state.entities.at(0).active);

    EXPECT_NEAR(state.primary, model.horizonDistanceAt(t), 1e-9);
    EXPECT_NEAR(state.secondary, model.arcDistanceAt(t), 1e-9);
    EXPECT_FALSE(state.resolved);
}

TEST(Horizon, StateMarksAscentFinished) {
    HorizonModel model;
    fill(model, kEarthRadius, 1000.0, 20.0);

    const lab::SimState after = model.stateAt(model.duration());
    EXPECT_TRUE(after.resolved);
    EXPECT_FALSE(after.entities.at(0).active);
    // После остановки скорость подъёма равна нулю.
    EXPECT_NEAR(after.entities.at(0).value2, 0.0, 1e-12);
    EXPECT_FALSE(after.eventLabel.isEmpty());
}

// ------------------------------------------------------------------
// Запись решения и показания
// ------------------------------------------------------------------

TEST(Horizon, DerivationContainsKeyFormulasAndSubstitutions) {
    HorizonModel model;
    fill(model, kEarthRadius, 2000.0, 25.0);

    const lab::Derivation derivation = model.derivation();
    ASSERT_TRUE(derivation.valid);
    ASSERT_FALSE(derivation.steps.isEmpty());

    QString all;
    for (const lab::DerivationStep& step : derivation.steps)
        all += step.content;
    EXPECT_TRUE(all.contains(QStringLiteral("d = \\sqrt{2Rh + h^{2}}")));
    EXPECT_TRUE(all.contains(QStringLiteral("\\cos\\alpha")));
    EXPECT_TRUE(all.contains(QStringLiteral("s = R\\alpha")));
    EXPECT_TRUE(all.contains(QStringLiteral("S = 2\\pi R^{2}(1 - \\cos\\alpha)")));
    EXPECT_TRUE(all.contains(QStringLiteral("h(t) = vt")));
    EXPECT_TRUE(all.contains(QStringLiteral("OA^{2} = OB^{2} + AB^{2}")));

    // Подстановка чисел: радиус, высота и результат в километрах.
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(6371000.0, 0)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(2000.0, 0)));
    const QString expectedKilometers = lab::utils::formatNumber(model.horizonDistance(2000.0) / 1000.0, 3);
    EXPECT_TRUE(all.contains(expectedKilometers)) << expectedKilometers.toUtf8().constData();
}

TEST(Horizon, DerivationRendersWithoutUnknownCommands) {
    HorizonModel model;
    fill(model, kEarthRadius, 2000.0, 25.0);

    // Каждая строка записи проходит через преобразователь формул: если
    // команда не поддерживается, в тексте остаётся обратный слэш, и
    // пользователь увидит «\frac» вместо дроби.
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

TEST(Horizon, DerivationReportsInvalidSetup) {
    HorizonModel model;
    setSetup(model, QStringLiteral("radius=100; h=200000; v=25"));

    const lab::Derivation derivation = model.derivation();
    EXPECT_FALSE(derivation.valid);
    EXPECT_FALSE(derivation.answerTemplate.isEmpty());
}

TEST(Horizon, AnswerMatchesDirectComputation) {
    HorizonModel model;
    fill(model, kEarthRadius, 2000.0, 25.0);

    const lab::Derivation derivation = model.derivation();
    EXPECT_TRUE(derivation.answerTemplate.contains(lab::utils::formatNumber(model.horizonDistance(2000.0) / 1000.0, 3)));
    EXPECT_TRUE(derivation.answerTemplate.contains(lab::utils::formatNumber(model.arcDistance(2000.0) / 1000.0, 3)));
}

TEST(Horizon, ReadoutsFollowModelState) {
    HorizonModel model;
    fill(model, kEarthRadius, 2000.0, 25.0);
    model.seekTo(model.ascentTime() * 0.5);
    const double t = model.time();

    const QVector<lab::Readout> readouts = model.readouts();
    ASSERT_GE(readouts.size(), 6);
    EXPECT_TRUE(readouts.at(1).value.contains(lab::utils::formatNumber(model.heightAt(t), 1)));
    EXPECT_TRUE(readouts.at(2).value.contains(QStringLiteral("км")));
    EXPECT_TRUE(readouts.at(2).highlight);
    EXPECT_TRUE(readouts.at(5).value.contains(QStringLiteral("%")));
}

// ------------------------------------------------------------------
// Ряды данных
// ------------------------------------------------------------------

TEST(Horizon, TimeSeriesCoverWholeAscent) {
    HorizonModel model;
    fill(model, kEarthRadius, 4000.0, 20.0);

    const QVector<QPointF> height = model.heightSeries();
    const QVector<QPointF> distance = model.distanceSeries();
    const QVector<QPointF> arc = model.arcSeries();
    ASSERT_GE(height.size(), 100);
    EXPECT_EQ(height.size(), distance.size());
    EXPECT_EQ(height.size(), arc.size());

    EXPECT_NEAR(height.first().x(), 0.0, 1e-12);
    EXPECT_NEAR(height.last().x(), model.duration(), 1e-9);
    EXPECT_NEAR(height.first().y(), 0.0, 1e-12);
    EXPECT_NEAR(height.last().y(), 4000.0, 1e-9);
    // Ряды дальности — в километрах.
    EXPECT_NEAR(distance.last().y(), model.horizonDistance(4000.0) / 1000.0, 1e-9);
    EXPECT_NEAR(arc.last().y(), model.arcDistance(4000.0) / 1000.0, 1e-9);
}

TEST(Horizon, HeightVersusDistanceSeriesMatchFormulas) {
    HorizonModel model;
    fill(model, kEarthRadius, 6000.0, 30.0);

    const QVector<QPointF> distance = model.distanceVersusHeight();
    const QVector<QPointF> arc = model.arcVersusHeight();
    ASSERT_GE(distance.size(), 100);
    ASSERT_EQ(distance.size(), arc.size());

    EXPECT_NEAR(distance.first().y(), 0.0, 1e-12);
    EXPECT_NEAR(distance.last().x(), 6000.0, 1e-9);
    EXPECT_NEAR(distance.last().y(), model.horizonDistance(6000.0) / 1000.0, 1e-9);
    for (int i = 1; i < distance.size(); i += 40) {
        EXPECT_NEAR(distance.at(i).y(), model.horizonDistance(distance.at(i).x()) / 1000.0, 1e-9);
        EXPECT_NEAR(arc.at(i).y(), model.arcDistance(arc.at(i).x()) / 1000.0, 1e-9);
        // Прямой отрезок взгляда длиннее пути по поверхности; при h = 0
        // обе величины равны нулю, поэтому первая точка пропускается.
        EXPECT_GT(distance.at(i).y(), arc.at(i).y());
    }
}

// ------------------------------------------------------------------
// Точки взаимодействия
// ------------------------------------------------------------------

TEST(Horizon, HandlesCoverHeightAndRadius) {
    HorizonModel model;
    fill(model, kEarthRadius, 2000.0, 25.0);

    const QVector<lab::HandleInfo> handles = model.handles();
    ASSERT_EQ(handles.size(), 2);
    EXPECT_EQ(handles.at(0).id, QStringLiteral("height"));
    EXPECT_EQ(handles.at(1).id, QStringLiteral("radius"));

    // Перетаскивание меняет модель: пересчитываются все формулы.
    model.setHandleValue(QStringLiteral("height"), 8000.0);
    EXPECT_NEAR(model.maxHeight(), 8000.0, 1e-9);
    EXPECT_GT(model.horizonDistance(8000.0), model.horizonDistance(2000.0));

    model.setHandleValue(QStringLiteral("radius"), 1737.0);
    EXPECT_NEAR(model.radiusKilometers(), 1737.0, 1e-9);
    EXPECT_NEAR(model.radiusMeters(), 1737000.0, 1e-6);
}

TEST(Horizon, ReadbackShowsHorizonDistance) {
    HorizonModel model;
    fill(model, kEarthRadius, 2000.0, 25.0);

    const QVector<lab::HandleInfo> handles = model.handles();
    ASSERT_FALSE(handles.isEmpty());
    EXPECT_TRUE(handles.first().readbackValue.contains(QStringLiteral("км")));
}

TEST(Horizon, DraggingRestartsAscent) {
    HorizonModel model;
    fill(model, kEarthRadius, 2000.0, 25.0);
    model.seekTo(model.duration());
    ASSERT_GT(model.time(), 0.0);

    model.beginInteraction();
    EXPECT_NEAR(model.time(), 0.0, 1e-12);
}

TEST(Horizon, SwitchingPlanetRecomputesEverything) {
    HorizonModel model;
    fill(model, kEarthRadius, 2000.0, 25.0);
    const double onEarth = model.horizonDistance(2000.0);

    model.setHandleValue(QStringLiteral("radius"), 3390.0);
    const double onMars = model.horizonDistance(2000.0);
    EXPECT_LT(onMars, onEarth);
    EXPECT_NEAR(model.horizonDistance(2000.0), std::sqrt(2.0 * 3390000.0 * 2000.0 + 4000000.0), 1e-9);
}
