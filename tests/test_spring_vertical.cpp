#include <gtest/gtest.h>

#include <QApplication>
#include <QImage>
#include <QPainter>

#include <cmath>

#include "TestSupport.h"
#include "models/SpringVerticalModel.h"
#include "scenes/SpringVerticalScene.h"
#include "scenes/Style.h"
#include "scenes/VectorArrows.h"
#include "utils/MathText.h"

using lab::models::SpringVerticalModel;
using lab::scenes::SpringVerticalScene;
using lab::test::isInvalid;
using lab::test::setSetup;
using lab::vectors::Arrow;
using lab::vectors::Kind;

namespace {

/** Создаёт модель с указанными параметрами. */
void fill(SpringVerticalModel& model, double m, double k, double x0) {
    setSetup(model, QStringLiteral("m=%1; k=%2; x0=%3").arg(m).arg(k).arg(x0));
}

}  // namespace

// ------------------------------------------------------------------
// Формулы равновесия и периода
// ------------------------------------------------------------------

TEST(SpringVertical, EquilibriumElongationMatchesFormula) {
    SpringVerticalModel model;
    fill(model, 2.0, 100.0, 0.1);
    // Δl₀ = mg/k = 2·9,8/100 = 0,196 м.
    EXPECT_NEAR(model.equilibriumElongation(), 2.0 * 9.8 / 100.0, 1e-12);
}

TEST(SpringVertical, OmegaAndPeriodFollowFormulas) {
    SpringVerticalModel model;
    fill(model, 2.0, 200.0, 0.1);
    // ω = √(k/m), T = 2π√(m/k).
    EXPECT_NEAR(model.omega(), std::sqrt(100.0), 1e-12);
    EXPECT_NEAR(model.period(), 2.0 * M_PI / model.omega(), 1e-12);
}

TEST(SpringVertical, PeriodDoesNotDependOnAmplitude) {
    SpringVerticalModel small;
    fill(small, 1.0, 100.0, 0.05);
    SpringVerticalModel large;
    fill(large, 1.0, 100.0, 0.2);
    // Линейный осциллятор: период не зависит от амплитуды.
    EXPECT_NEAR(small.period(), large.period(), 1e-12);
}

TEST(SpringVertical, PeriodScalesWithMassAndStiffness) {
    SpringVerticalModel light;
    fill(light, 0.5, 100.0, 0.1);
    SpringVerticalModel heavy;
    fill(heavy, 2.0, 100.0, 0.1);
    SpringVerticalModel stiff;
    fill(stiff, 0.5, 400.0, 0.1);

    EXPECT_GT(heavy.period(), light.period());
    EXPECT_GT(light.period(), stiff.period());
    EXPECT_NEAR(heavy.period(), 2.0 * light.period(), 1e-9);
}

// ------------------------------------------------------------------
// Точное решение уравнения m·x″ = −k·x
// ------------------------------------------------------------------

TEST(SpringVertical, MotionFollowsCosineLaw) {
    SpringVerticalModel model;
    fill(model, 1.0, 100.0, 0.2);

    const double omega = model.omega();
    for (int i = 0; i <= 8; ++i) {
        const double t = model.duration() * i / 8.0;
        EXPECT_NEAR(model.displacementAt(t), 0.2 * std::cos(omega * t), 1e-12) << "t = " << t;
        EXPECT_NEAR(model.velocityAt(t), -0.2 * omega * std::sin(omega * t), 1e-9) << "t = " << t;
    }
}

TEST(SpringVertical, NegativeOffsetStartsWithOppositePhase) {
    SpringVerticalModel model;
    fill(model, 1.0, 100.0, -0.15);

    EXPECT_NEAR(model.displacementAt(0.0), -0.15, 1e-12);
    EXPECT_NEAR(model.velocityAt(0.0), 0.0, 1e-12);
    // x(t) = A cos(ωt + π): ускорение в начальный момент направлено вверх (x < 0 ⇒ a > 0).
    EXPECT_NEAR(model.accelerationAt(0.0), 100.0 * 0.15, 1e-9);
    EXPECT_NEAR(model.initialPhase(), M_PI, 1e-12);
}

TEST(SpringVertical, EquationOfMotionIsLinear) {
    SpringVerticalModel model;
    fill(model, 1.5, 120.0, 0.1);
    // a = −(k/m)x: ускорение пропорционально смещению.
    EXPECT_NEAR(model.accelerationOf(0.1), -120.0 * 0.1 / 1.5, 1e-12);
    EXPECT_NEAR(model.accelerationOf(-0.1), 120.0 * 0.1 / 1.5, 1e-12);
    EXPECT_NEAR(model.accelerationOf(0.0), 0.0, 1e-12);
}

TEST(SpringVertical, EnergyIsConserved) {
    SpringVerticalModel model;
    fill(model, 1.0, 100.0, 0.2);

    auto energy = [&](double t) {
        const double x = model.displacementAt(t);
        const double v = model.velocityAt(t);
        return 0.5 * 100.0 * x * x + 0.5 * 1.0 * v * v;
    };
    const double start = energy(0.0);
    for (double t = 0.0; t <= model.duration(); t += model.period() / 7.0)
        EXPECT_NEAR(energy(t), start, 1e-9) << "t = " << t;
}

// ------------------------------------------------------------------
// Силы: в модели только тяжесть и упругость
// ------------------------------------------------------------------

TEST(SpringVertical, ForcesBalanceAtEquilibrium) {
    SpringVerticalModel model;
    fill(model, 1.5, 120.0, 0.1);

    // В положении равновесия (x = 0) упругость уравновешивает тяжесть.
    EXPECT_NEAR(model.stiffness() * model.equilibriumElongation(), model.weight(), 1e-12);

    // При смещении x вниз растяжение растёт: Fупр − mg = k·x,
    // а равнодействующая mg − Fупр = −k·x даёт ускорение к равновесию.
    const double imbalance = model.elasticForceAt(0.0) - model.weight();
    EXPECT_NEAR(imbalance, model.stiffness() * model.displacementAt(0.0), 1e-12);
    EXPECT_NEAR(model.accelerationAt(0.0), -imbalance / model.mass(), 1e-12);
}

TEST(SpringVertical, ElongationBoundsMatchAmplitude) {
    SpringVerticalModel model;
    fill(model, 1.0, 100.0, 0.2);
    // Максимум и минимум растяжения соответствуют ±A вокруг равновесия.
    EXPECT_NEAR(model.maxElongation(), model.equilibriumElongation() + 0.2, 1e-12);
    EXPECT_NEAR(model.minElongation(), model.equilibriumElongation() - 0.2, 1e-12);
}

TEST(SpringVertical, RejectsInvalidParameters) {
    SpringVerticalModel zeroMass;
    setSetup(zeroMass, QStringLiteral("m=0; k=100; x0=0.1"));
    EXPECT_GT(zeroMass.mass(), 0.0);   // значение ограничено снизу

    SpringVerticalModel zeroOffset;
    setSetup(zeroOffset, QStringLiteral("m=1; k=100; x0=0"));
    EXPECT_TRUE(isInvalid(zeroOffset));

    SpringVerticalModel tooLarge;
    setSetup(tooLarge, QStringLiteral("m=1; k=100; x0=1.5"));
    // Поле ограничено диапазоном: физически недопустимого отклонения не возникает.
    EXPECT_LE(std::abs(tooLarge.initialDisplacement()), 0.25 + 1e-9);
    EXPECT_TRUE(tooLarge.isValid());
}

TEST(SpringVertical, NoStopEventBecauseMotionIsPeriodic) {
    SpringVerticalModel model;
    fill(model, 1.0, 100.0, 0.2);
    EXPECT_TRUE(std::isnan(model.eventTime()));
    EXPECT_FALSE(model.stateAt(model.duration()).resolved);
}

// ------------------------------------------------------------------
// Состояние и панели
// ------------------------------------------------------------------

TEST(SpringVertical, StateCarriesLoadAndSpring) {
    SpringVerticalModel model;
    fill(model, 1.0, 100.0, 0.2);

    const lab::SimState state = model.stateAt(model.period() / 2.0);
    ASSERT_GE(state.entities.size(), 2);
    EXPECT_EQ(state.entities.at(0).id, QStringLiteral("load"));
    EXPECT_EQ(state.entities.at(1).id, QStringLiteral("spring"));
    EXPECT_NEAR(state.entities.at(1).position, model.elongationAt(model.period() / 2.0), 1e-12);
}

TEST(SpringVertical, DerivationContainsKeyFormulas) {
    SpringVerticalModel model;
    fill(model, 2.0, 200.0, 0.15);

    const lab::Derivation d = model.derivation();
    ASSERT_TRUE(d.valid);
    QString all;
    for (const lab::DerivationStep& step : d.steps)
        all += step.content;
    EXPECT_TRUE(all.contains(QStringLiteral("k \\Delta l_{0} = m g")));
    EXPECT_TRUE(all.contains(QStringLiteral("m x'' = -k x")));
    EXPECT_TRUE(all.contains(QStringLiteral("\\omega = \\sqrt")));
    EXPECT_TRUE(all.contains(QStringLiteral("T = 2\\pi")));
    EXPECT_TRUE(all.contains(QStringLiteral("x(t) =")));
}

TEST(SpringVertical, ReadoutsFollowModelState) {
    SpringVerticalModel model;
    fill(model, 1.0, 100.0, 0.2);
    model.seekTo(0.4);

    const QVector<lab::Readout> readouts = model.readouts();
    ASSERT_GE(readouts.size(), 6);
    EXPECT_TRUE(readouts.first().value.contains(lab::utils::formatNumber(model.displacementAt(0.4), 4)));
}

TEST(SpringVertical, SeriesMatchAnalyticalSolution) {
    SpringVerticalModel model;
    fill(model, 1.0, 100.0, 0.2);

    const QVector<QPointF> x = model.displacementSeries();
    const QVector<QPointF> v = model.velocitySeries();
    const QVector<QPointF> a = model.accelerationSeries();
    ASSERT_GE(x.size(), 100);
    ASSERT_EQ(x.size(), v.size());
    ASSERT_EQ(x.size(), a.size());

    EXPECT_NEAR(x.first().y(), 0.2, 1e-12);
    EXPECT_NEAR(v.first().y(), 0.0, 1e-12);
    EXPECT_NEAR(a.first().y(), -100.0 * 0.2, 1e-9);
    EXPECT_NEAR(x.last().x(), model.duration(), 1e-9);
}

TEST(SpringVertical, BlockHandleSetsOffsetAndRestartsTime) {
    SpringVerticalModel model;
    fill(model, 1.0, 100.0, 0.2);

    const QVector<lab::HandleInfo> handles = model.handles();
    ASSERT_FALSE(handles.isEmpty());
    EXPECT_EQ(handles.first().id, QStringLiteral("x0"));
    EXPECT_EQ(handles.first().axis, QStringLiteral("y"));

    model.setHandleValue(QStringLiteral("x0"), -0.1);
    EXPECT_NEAR(model.initialDisplacement(), -0.1, 1e-9);
    EXPECT_NEAR(model.displacementAt(0.0), -0.1, 1e-9);

    model.seekTo(0.5);
    model.beginInteraction();
    EXPECT_NEAR(model.time(), 0.0, 1e-12);
}

// ------------------------------------------------------------------
// Векторы и отрисовка сцены
// ------------------------------------------------------------------

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 760;

const Arrow* find(const QVector<Arrow>& arrows, Kind kind) {
    for (const Arrow& arrow : arrows) {
        if (arrow.kind == kind)
            return &arrow;
    }
    return nullptr;
}

}  // namespace

TEST(SpringVerticalVectors, OnlyGravityAndElasticAreShown) {
    SpringVerticalModel model;
    fill(model, 1.0, 100.0, 0.2);
    SpringVerticalScene scene(&model);
    scene.reconfigure(QRectF(20.0, 12.0, kWidth - 40.0, kHeight - 82.0));
    model.seekTo(0.1);

    const QVector<Arrow> arrows = scene.vectorsFor(model.time());
    ASSERT_NE(find(arrows, Kind::Gravity), nullptr);
    ASSERT_NE(find(arrows, Kind::Elastic), nullptr);
    // Трения в модели нет: сила тяжести и упругость — единственные силы.
    EXPECT_EQ(find(arrows, Kind::Friction), nullptr);
    EXPECT_EQ(find(arrows, Kind::Normal), nullptr);
}

TEST(SpringVerticalVectors, DirectionsArePhysicallyCorrect) {
    SpringVerticalModel model;
    fill(model, 1.0, 100.0, 0.2);
    SpringVerticalScene scene(&model);
    scene.reconfigure(QRectF(20.0, 12.0, kWidth - 40.0, kHeight - 82.0));
    model.seekTo(0.0);

    const QVector<Arrow> arrows = scene.vectorsFor(model.time());
    const Arrow* gravity = find(arrows, Kind::Gravity);
    const Arrow* elastic = find(arrows, Kind::Elastic);
    ASSERT_NE(gravity, nullptr);
    ASSERT_NE(elastic, nullptr);

    // Ось Y направлена вниз: mg — вниз (y растёт), Fупр — вверх (y уменьшается).
    EXPECT_NEAR(gravity->to.x(), gravity->from.x(), 1e-9);
    EXPECT_GT(gravity->to.y(), gravity->from.y());
    EXPECT_NEAR(elastic->to.x(), elastic->from.x(), 1e-9);
    EXPECT_LT(elastic->to.y(), elastic->from.y());
    // Точка приложения упругости — верх груза (крепление пружины).
    EXPECT_LT(elastic->from.y(), gravity->from.y());
}

TEST(SpringVerticalVectors, ElasticForceGrowsWithElongation) {
    SpringVerticalModel stretched;
    fill(stretched, 1.0, 100.0, 0.2);
    SpringVerticalScene stretchedScene(&stretched);
    stretchedScene.reconfigure(QRectF(20.0, 12.0, kWidth - 40.0, kHeight - 82.0));
    stretched.seekTo(0.0);
    const double forceAtMax = stretched.elasticForceAt(0.0);

    SpringVerticalModel compressed;
    fill(compressed, 1.0, 100.0, -0.2);
    SpringVerticalScene compressedScene(&compressed);
    compressedScene.reconfigure(QRectF(20.0, 12.0, kWidth - 40.0, kHeight - 82.0));
    compressed.seekTo(0.0);
    const double forceAtMin = compressed.elasticForceAt(0.0);

    EXPECT_GT(forceAtMax, forceAtMin);
    EXPECT_NEAR(forceAtMax - forceAtMin, 100.0 * 0.4, 1e-9);
}

TEST(SpringVerticalVectors, CategoriesToggledIndependently) {
    SpringVerticalModel model;
    fill(model, 1.0, 100.0, 0.2);
    SpringVerticalScene scene(&model);
    scene.reconfigure(QRectF(20.0, 12.0, kWidth - 40.0, kHeight - 82.0));
    model.seekTo(0.1);
    const double t = model.time();

    ASSERT_NE(find(scene.vectorsFor(t), Kind::Gravity), nullptr);
    ASSERT_NE(find(scene.vectorsFor(t), Kind::Velocity), nullptr);
    ASSERT_NE(find(scene.vectorsFor(t), Kind::Acceleration), nullptr);

    model.setShowsForces(false);
    EXPECT_EQ(find(scene.vectorsFor(t), Kind::Gravity), nullptr);
    EXPECT_EQ(find(scene.vectorsFor(t), Kind::Elastic), nullptr);
    EXPECT_NE(find(scene.vectorsFor(t), Kind::Velocity), nullptr);

    model.setShowsForces(true);
    model.setShowsAccelerations(false);
    EXPECT_EQ(find(scene.vectorsFor(t), Kind::Acceleration), nullptr);
    EXPECT_NE(find(scene.vectorsFor(t), Kind::Gravity), nullptr);
}

TEST(SpringVerticalScene, ThreePlotsAreDrawn) {
    SpringVerticalModel model;
    fill(model, 1.0, 100.0, 0.2);
    SpringVerticalScene scene(&model);
    scene.reconfigure(QRectF(20.0, 12.0, kWidth - 40.0, kHeight - 82.0));
    model.seekTo(model.duration() * 0.4);
    scene.refresh();

    QImage image(kWidth, kHeight, QImage::Format_RGB32);
    image.fill(lab::style::background());
    QPainter painter(&image);
    scene.paintBackgroundInto(&painter, QRectF(0, 0, kWidth, kHeight));
    painter.end();

    auto countColor = [&](const QColor& color, int y0, int y1) {
        int found = 0;
        for (int y = y0; y < std::min(y1, image.height()); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QRgb rgb = image.pixel(x, y);
                if (std::abs(qRed(rgb) - color.red()) <= 24 && std::abs(qGreen(rgb) - color.green()) <= 24 &&
                    std::abs(qBlue(rgb) - color.blue()) <= 24)
                    ++found;
            }
        }
        return found;
    };
    EXPECT_GT(countColor(lab::style::bodyA(), 400, kHeight), 150);      // x(t)
    EXPECT_GT(countColor(lab::style::bodyC(), 400, kHeight), 150);      // v(t)
    EXPECT_GT(countColor(lab::style::accentWarm(), 400, kHeight), 40);  // a(t)
}
