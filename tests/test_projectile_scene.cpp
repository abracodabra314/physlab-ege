#include <gtest/gtest.h>

#include <QApplication>
#include <QImage>
#include <QPainter>

#include <cmath>

#include "TestSupport.h"
#include "models/ProjectileModel.h"
#include "scenes/ProjectileScene.h"
#include "scenes/Style.h"
#include "scenes/VectorArrows.h"

using lab::models::ProjectileModel;
using lab::scenes::ProjectileScene;
using lab::test::setSetup;
using lab::vectors::Arrow;
using lab::vectors::Kind;

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 760;
// Область просмотра как в LabView: снизу остаётся место под ось времени.
constexpr double kViewTop = 12.0;
constexpr double kViewHeight = kHeight - 24.0 - 58.0;
// Полоса строк с маркером текущего момента на общей оси времени.
constexpr int kAxisMarkerTop = 713;
constexpr int kAxisMarkerBottom = 728;

/**
 * Сцена рисует шрифтами и событиями Qt, поэтому QApplication должен
 * существовать до создания первой QGraphicsScene. Приложение создаётся
 * в глобальном окружении тестов и живёт до конца прогона.
 */
class GraphicsEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        static int argc = 1;
        static char arg0[] = "lab_tests";
        static char* argv[] = {arg0, nullptr};
        m_application = new QApplication(argc, argv);
    }

private:
    QApplication* m_application = nullptr;   // принадлежит процессу
};

const bool kGraphicsEnvironmentRegistered = [] {
    ::testing::AddGlobalTestEnvironment(new GraphicsEnvironment);
    return true;
}();

/** Пересчитывает геометрию сцены и устанавливает время модели. */
void prepare(ProjectileScene& scene, ProjectileModel& model, double t) {
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));
    model.seekTo(t);
}

/** Отрисовывает кадр сцены и возвращает изображение. */
QImage renderFrame(ProjectileScene& scene) {
    QImage image(kWidth, kHeight, QImage::Format_RGB32);
    image.fill(lab::style::background());
    QPainter painter(&image);
    scene.paintBackgroundInto(&painter, QRectF(0, 0, kWidth, kHeight));
    painter.end();
    return image;
}

const Arrow* find(const QVector<Arrow>& arrows, Kind kind, const QString& symbol = QString()) {
    for (const Arrow& arrow : arrows) {
        if (arrow.kind != kind)
            continue;
        if (!symbol.isEmpty() && arrow.symbol != symbol)
            continue;
        return &arrow;
    }
    return nullptr;
}

/** Число пикселей цвета, близкого к заданному, в полосе строк [y0, y1). */
int countColor(const QImage& image, const QColor& color, int y0, int y1, int tolerance = 24) {
    int found = 0;
    for (int y = std::max(0, y0); y < std::min(y1, image.height()); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb rgb = image.pixel(x, y);
            if (std::abs(qRed(rgb) - color.red()) <= tolerance &&
                std::abs(qGreen(rgb) - color.green()) <= tolerance &&
                std::abs(qBlue(rgb) - color.blue()) <= tolerance)
                ++found;
        }
    }
    return found;
}

/** Правый край маркера текущего момента на общей оси времени. */
int axisMarkerX(const QImage& image) {
    const QColor marker = lab::style::danger();
    int best = -1;
    for (int y = kAxisMarkerTop; y < kAxisMarkerBottom; ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb rgb = image.pixel(x, y);
            if (std::abs(qRed(rgb) - marker.red()) <= 12 && std::abs(qGreen(rgb) - marker.green()) <= 12 &&
                std::abs(qBlue(rgb) - marker.blue()) <= 12)
                best = std::max(best, x);
        }
    }
    return best;
}

}  // namespace

// ------------------------------------------------------------------
// Векторы: скорость, её проекции, ускорение и сила тяжести
// ------------------------------------------------------------------

TEST(ProjectileVectors, VelocityAndItsProjectionsAreShown) {
    ProjectileModel model;
    setSetup(model, QStringLiteral("v0=30; angle=35; y0=6; x0=0"));
    ProjectileScene scene(&model);
    prepare(scene, model, model.flightTime() * 0.3);

    const QVector<Arrow> arrows = scene.vectorsFor(model.time());
    const Arrow* velocity = find(arrows, Kind::Velocity, QStringLiteral("v"));
    const Arrow* vx = find(arrows, Kind::Velocity, QStringLiteral("vₓ"));
    const Arrow* vy = find(arrows, Kind::Velocity, QStringLiteral("vᵧ"));
    ASSERT_NE(velocity, nullptr);
    ASSERT_NE(vx, nullptr);
    ASSERT_NE(vy, nullptr);

    // Проекция на X горизонтальна, проекция на Y вертикальна.
    EXPECT_NEAR(vx->to.y(), vx->from.y(), 1e-9);
    EXPECT_NEAR(vy->to.x(), vy->from.x(), 1e-9);

    // Знаки соответствуют движению: на подъёме vᵧ > 0, то есть стрелка вверх
    // (в координатах сцены — в сторону уменьшения y).
    EXPECT_GT(model.vyAt(model.time()), 0.0);
    EXPECT_LT(vy->to.y(), vy->from.y());
    EXPECT_GT(vx->to.x(), vx->from.x());

    // Скорость — векторная сумма проекций: её направление совмещено с касательной.
    const double arrowSlope = (velocity->to.y() - velocity->from.y()) / (velocity->to.x() - velocity->from.x());
    const double physicsSlope = -model.vyAt(model.time()) / model.vxAt(model.time());
    EXPECT_NEAR(arrowSlope, physicsSlope, 1e-6);
}

TEST(ProjectileVectors, ProjectionIsTheSumComponentOfVelocity) {
    ProjectileModel model;
    setSetup(model, QStringLiteral("v0=28; angle=50; y0=4; x0=0"));
    ProjectileScene scene(&model);
    prepare(scene, model, model.flightTime() * 0.6);

    const QVector<Arrow> arrows = scene.vectorsFor(model.time());
    const Arrow* velocity = find(arrows, Kind::Velocity, QStringLiteral("v"));
    const Arrow* vx = find(arrows, Kind::Velocity, QStringLiteral("vₓ"));
    const Arrow* vy = find(arrows, Kind::Velocity, QStringLiteral("vᵧ"));
    ASSERT_NE(velocity, nullptr);
    ASSERT_NE(vx, nullptr);
    ASSERT_NE(vy, nullptr);

    // Все три вектора приложены к снаряду.
    EXPECT_NEAR(velocity->from.x(), vx->from.x(), 1e-9);
    EXPECT_NEAR(velocity->from.y(), vy->from.y(), 1e-9);

    // Длина проекции меньше длины скорости: проекция — слагаемое, а не сумма.
    const double velocityLength = std::hypot(velocity->to.x() - velocity->from.x(),
                                             velocity->to.y() - velocity->from.y());
    const double vxLength = std::abs(vx->to.x() - vx->from.x());
    EXPECT_LT(vxLength, velocityLength);
    EXPECT_GT(vxLength, 1.0);
    // Слагаемые рисуются штрихом, сумма — сплошной линией.
    EXPECT_TRUE(vx->dashed);
    EXPECT_TRUE(vy->dashed);
    EXPECT_FALSE(velocity->dashed);
}

TEST(ProjectileVectors, AccelerationPointsDownAndIsConstant) {
    ProjectileModel model;
    setSetup(model, QStringLiteral("v0=34; angle=42; y0=9; x0=0"));
    ProjectileScene scene(&model);

    prepare(scene, model, model.flightTime() * 0.15);
    const QVector<Arrow> earlyArrows = scene.vectorsFor(model.time());
    const Arrow* early = find(earlyArrows, Kind::Acceleration);
    ASSERT_NE(early, nullptr);
    EXPECT_NEAR(early->to.x(), early->from.x(), 1e-9);   // aₓ = 0
    EXPECT_GT(early->to.y(), early->from.y());           // aᵧ = −g, вниз

    prepare(scene, model, model.flightTime() * 0.85);
    const QVector<Arrow> lateArrows = scene.vectorsFor(model.time());
    const Arrow* late = find(lateArrows, Kind::Acceleration);
    ASSERT_NE(late, nullptr);
    // Ускорение постоянно: длина стрелки не меняется.
    EXPECT_NEAR(std::abs(late->to.y() - late->from.y()), std::abs(early->to.y() - early->from.y()), 1e-9);
}

TEST(ProjectileVectors, OnlyGravityIsShownAmongForces) {
    ProjectileModel model;
    setSetup(model, QStringLiteral("v0=30; angle=40; y0=5; x0=0"));
    ProjectileScene scene(&model);
    prepare(scene, model, model.flightTime() * 0.4);

    const QVector<Arrow> arrows = scene.vectorsFor(model.time());
    ASSERT_NE(find(arrows, Kind::Gravity), nullptr);
    // Сопротивления воздуха в модели нет: сила тяжести — единственная сила.
    EXPECT_EQ(find(arrows, Kind::Friction), nullptr);
    EXPECT_EQ(find(arrows, Kind::Normal), nullptr);
    EXPECT_EQ(find(arrows, Kind::Tension), nullptr);
    EXPECT_EQ(find(arrows, Kind::Elastic), nullptr);

    // Стрелка силы тяжести вертикальна и направлена вниз.
    const Arrow* weight = find(arrows, Kind::Gravity);
    EXPECT_NEAR(weight->to.x(), weight->from.x(), 1e-9);
    EXPECT_GT(weight->to.y(), weight->from.y());
}

TEST(ProjectileVectors, NoVectorsAfterLanding) {
    ProjectileModel model;
    setSetup(model, QStringLiteral("v0=30; angle=40; y0=5; x0=0"));
    ProjectileScene scene(&model);
    prepare(scene, model, model.duration());

    // Снаряд упал: движение завершилось, векторов скорости нет.
    EXPECT_TRUE(scene.vectorsFor(model.time()).isEmpty());
}

TEST(ProjectileVectors, CategoriesToggledIndependently) {
    ProjectileModel model;
    setSetup(model, QStringLiteral("v0=32; angle=38; y0=7; x0=0"));
    ProjectileScene scene(&model);
    prepare(scene, model, model.flightTime() * 0.35);
    const double t = model.time();

    ASSERT_NE(find(scene.vectorsFor(t), Kind::Velocity), nullptr);
    ASSERT_NE(find(scene.vectorsFor(t), Kind::Acceleration), nullptr);
    ASSERT_NE(find(scene.vectorsFor(t), Kind::Gravity), nullptr);

    model.setShowsVelocities(false);
    const QVector<Arrow> withoutVelocity = scene.vectorsFor(t);
    EXPECT_EQ(find(withoutVelocity, Kind::Velocity), nullptr);
    EXPECT_NE(find(withoutVelocity, Kind::Gravity), nullptr);
    EXPECT_NE(find(withoutVelocity, Kind::Acceleration), nullptr);

    model.setShowsVelocities(true);
    model.setShowsForces(false);
    const QVector<Arrow> withoutForces = scene.vectorsFor(t);
    // После вылета на снаряд действует только тяжесть — она на чертеже всегда.
    EXPECT_NE(find(withoutForces, Kind::Gravity), nullptr);
    EXPECT_NE(find(withoutForces, Kind::Velocity), nullptr);

    model.setShowsForces(true);
    model.setShowsAccelerations(false);
    const QVector<Arrow> withoutAcceleration = scene.vectorsFor(t);
    EXPECT_EQ(find(withoutAcceleration, Kind::Acceleration), nullptr);
    EXPECT_NE(find(withoutAcceleration, Kind::Gravity), nullptr);
    EXPECT_NE(find(withoutAcceleration, Kind::Velocity), nullptr);
}

TEST(ProjectileVectors, ShellPositionFollowsMotionEquations) {
    ProjectileModel model;
    setSetup(model, QStringLiteral("v0=30; angle=45; y0=0; x0=0"));
    ProjectileScene scene(&model);
    prepare(scene, model, 0.0);
    const QVector<Arrow> startArrows = scene.vectorsFor(model.time());
    const Arrow* start = find(startArrows, Kind::Velocity, QStringLiteral("v"));
    ASSERT_NE(start, nullptr);

    prepare(scene, model, model.flightTime() * 0.5);
    const QVector<Arrow> middleArrows = scene.vectorsFor(model.time());
    const Arrow* middle = find(middleArrows, Kind::Velocity, QStringLiteral("v"));
    ASSERT_NE(middle, nullptr);
    // Точка приложения сместилась вправо и вверх: схема следует за x(t) и y(t).
    EXPECT_GT(middle->from.x(), start->from.x());
}

TEST(ProjectileVectors, ArrowLabelsContainOnlySymbols) {
    ProjectileModel model;
    setSetup(model, QStringLiteral("v0=30; angle=45; y0=8; x0=0"));
    ProjectileScene scene(&model);
    prepare(scene, model, model.flightTime() * 0.3);

    ASSERT_FALSE(scene.vectorsFor(model.time()).isEmpty());
    for (const Arrow& arrow : scene.vectorsFor(model.time())) {
        ASSERT_FALSE(arrow.symbol.isEmpty());
        bool hasDigit = false;
        for (const QChar ch : arrow.symbol)
            hasDigit = hasDigit || ch.isDigit();
        EXPECT_FALSE(hasDigit) << arrow.symbol.toUtf8().constData();
    }
}

// ------------------------------------------------------------------
// Отрисовка: трасса, оси, графики, ось времени
// ------------------------------------------------------------------

TEST(ProjectileScene, TrajectoryGrowsWithTime) {
    ProjectileModel model;
    setSetup(model, QStringLiteral("v0=32; angle=45; y0=12; x0=0"));
    ProjectileScene scene(&model);

    const QColor trace = lab::style::bodyC();
    prepare(scene, model, 0.0);
    const int startPixels = countColor(renderFrame(scene), trace, 60, 380, 10);
    prepare(scene, model, model.flightTime() * 0.5);
    const int middlePixels = countColor(renderFrame(scene), trace, 60, 380, 10);
    prepare(scene, model, model.flightTime());
    const int fullPixels = countColor(renderFrame(scene), trace, 60, 380, 10);

    // Трасса прочерчивается постепенно: к концу она длиннее, чем в начале.
    EXPECT_GT(middlePixels, startPixels);
    EXPECT_GT(fullPixels, middlePixels);
}

TEST(ProjectileScene, ShellMovesAlongSchemeWithTime) {
    ProjectileModel model;
    setSetup(model, QStringLiteral("v0=34; angle=40; y0=10; x0=0"));
    ProjectileScene scene(&model);

    const QColor shell = lab::style::bodyA();
    auto rightmostShellPixel = [&](double t) {
        prepare(scene, model, t);
        const QImage image = renderFrame(scene);
        int best = -1;
        for (int y = 60; y < 380; ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QRgb rgb = image.pixel(x, y);
                if (std::abs(qRed(rgb) - shell.red()) <= 6 && std::abs(qGreen(rgb) - shell.green()) <= 6 &&
                    std::abs(qBlue(rgb) - shell.blue()) <= 6)
                    best = std::max(best, x);
            }
        }
        return best;
    };

    const int early = rightmostShellPixel(model.flightTime() * 0.2);
    const int late = rightmostShellPixel(model.flightTime() * 0.8);
    ASSERT_GT(early, 0);
    ASSERT_GT(late, 0);
    EXPECT_GT(late, early);
}

TEST(ProjectileScene, FourPlotsAreDrawnBelowScheme) {
    ProjectileModel model;
    setSetup(model, QStringLiteral("v0=30; angle=45; y0=10; x0=0"));
    ProjectileScene scene(&model);
    prepare(scene, model, model.flightTime() * 0.5);
    const QImage image = renderFrame(scene);

    const int plotsTop = 420;
    EXPECT_GT(countColor(image, lab::style::bodyA(), plotsTop, kHeight), 150);       // x(t)
    EXPECT_GT(countColor(image, lab::style::bodyC(), plotsTop, kHeight), 150);       // y(t)
    EXPECT_GT(countColor(image, lab::style::accentWarm(), plotsTop, kHeight), 60);   // vₓ(t)
    EXPECT_GT(countColor(image, lab::style::bodyD(), plotsTop, kHeight), 150);       // vᵧ(t)
}

TEST(ProjectileScene, TimeAxisMarkerFollowsModelTime) {
    ProjectileModel model;
    setSetup(model, QStringLiteral("v0=30; angle=45; y0=10; x0=0"));
    ProjectileScene scene(&model);

    prepare(scene, model, model.duration() * 0.05);
    const int early = axisMarkerX(renderFrame(scene));
    prepare(scene, model, model.duration() * 0.9);
    const int late = axisMarkerX(renderFrame(scene));
    ASSERT_GE(early, 0);
    ASSERT_GE(late, 0);
    EXPECT_GT(late - early, 100);
}
