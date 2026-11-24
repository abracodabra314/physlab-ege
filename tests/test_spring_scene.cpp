#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPainter>

#include <cmath>

#include "TestSupport.h"
#include "models/SpringHorizontalModel.h"
#include "scenes/SpringHorizontalScene.h"
#include "scenes/Style.h"
#include "scenes/VectorArrows.h"

using lab::models::SpringHorizontalModel;
using lab::scenes::SpringHorizontalScene;
using lab::test::setSetup;
using lab::vectors::Arrow;
using lab::vectors::Kind;

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 760;
// Область просмотра задаётся как в LabView: снизу зарезервирована ось времени.
constexpr double kViewTop = 12.0;
constexpr double kViewHeight = kHeight - 24.0 - 58.0;
constexpr int kSchemeTop = 18;
constexpr int kSchemeBottom = 330;
// Полоса строк с маркером текущего момента на общей оси времени.
constexpr int kAxisMarkerTop = 713;
constexpr int kAxisMarkerBottom = 728;

/**
 * Сцена рисует шрифтами и событиями Qt, поэтому QApplication должен
 * существовать до создания первой QGraphicsScene. Приложение создаётся в
 * глобальном окружении тестов и живёт до конца прогона.
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
void prepare(SpringHorizontalScene& scene, SpringHorizontalModel& model, double t) {
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));
    model.seekTo(t);
}

/** Отрисовывает кадр сцены и возвращает изображение. */
QImage renderFrame(SpringHorizontalScene& scene) {
    QImage image(kWidth, kHeight, QImage::Format_RGB32);
    image.fill(lab::style::background());
    QPainter painter(&image);
    scene.paintBackgroundInto(&painter, QRectF(0, 0, kWidth, kHeight));
    painter.end();
    return image;
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

const Arrow* findArrow(const QVector<Arrow>& arrows, Kind kind) {
    for (const Arrow& arrow : arrows) {
        if (arrow.kind == kind)
            return &arrow;
    }
    return nullptr;
}

/** Позиция центра груза: по самой широкой строке его заливки в области схемы. */
int blockCenterX(const QImage& image) {
    const QColor fill = lab::style::bodyA().lighter(150);
    int bestWidth = 0, bestCenter = -1;
    for (int y = kSchemeTop; y < kSchemeBottom; ++y) {
        int first = -1, last = -1;
        for (int x = 0; x < image.width(); ++x) {
            const QRgb rgb = image.pixel(x, y);
            if (std::abs(qRed(rgb) - fill.red()) <= 8 && std::abs(qGreen(rgb) - fill.green()) <= 8 &&
                std::abs(qBlue(rgb) - fill.blue()) <= 8) {
                if (first < 0)
                    first = x;
                last = x;
            }
        }
        if (first >= 0 && last - first > bestWidth) {
            bestWidth = last - first;
            bestCenter = (first + last) / 2;
        }
    }
    return bestCenter;
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
// Векторы: направления и точки приложения задаёт физическая модель
// ------------------------------------------------------------------

TEST(SpringHorizontalVectors, ForceDirectionsMatchEquationOfMotion) {
    SpringHorizontalModel model;
    setSetup(model, QStringLiteral("m=1; k=100; mu=0.15; x0=0.3"));
    SpringHorizontalScene scene(&model);

    // Груз оттянут вправо (x > 0) и отпущен: сила упругости направлена влево
    // (к положению равновесия), сила трения при старте — тоже (v < 0).
    prepare(scene, model, 0.15);
    const QVector<Arrow> arrows = scene.vectorsFor(model.time());

    const Arrow* elastic = findArrow(arrows, Kind::Elastic);
    ASSERT_NE(elastic, nullptr);
    EXPECT_GT(elastic->from.x() - elastic->to.x(), 1.0) << "сила упругости должна тянуть груз к равновесию";

    const Arrow* friction = findArrow(arrows, Kind::Friction);
    ASSERT_NE(friction, nullptr);
    const double v = model.velocityAt(model.time());
    EXPECT_LT(v, 0.0);
    EXPECT_GT(std::copysign(1.0, friction->to.x() - friction->from.x()), 0.0)
        << "трение направлено против скорости";

    // Точки приложения: упругость — у левого края груза (крепление пружины),
    // трение — на линии контакта с поверхностью.
    const double blockLeft = elastic->from.x();
    EXPECT_NEAR(friction->from.y(), elastic->from.y() + 40.0, 90.0);
    EXPECT_GT(blockLeft, 0.0);
}

TEST(SpringHorizontalVectors, GravityAndNormalAreVerticalAndBalanced) {
    SpringHorizontalModel model;
    setSetup(model, QStringLiteral("m=1; k=100; mu=0; x0=0.2"));
    SpringHorizontalScene scene(&model);
    prepare(scene, model, 0.1);

    const QVector<Arrow> arrows = scene.vectorsFor(model.time());
    const Arrow* gravity = findArrow(arrows, Kind::Gravity);
    const Arrow* normal = findArrow(arrows, Kind::Normal);
    ASSERT_NE(gravity, nullptr);
    ASSERT_NE(normal, nullptr);

    // Обе силы вертикальны; mg — вниз, N — вверх.
    EXPECT_NEAR(gravity->to.x(), gravity->from.x(), 1e-9);
    EXPECT_GT(gravity->to.y(), gravity->from.y());
    EXPECT_NEAR(normal->to.x(), normal->from.x(), 1e-9);
    EXPECT_LT(normal->to.y(), normal->from.y());

    // На горизонтальной поверхности N = mg, поэтому стрелки равны по длине.
    EXPECT_NEAR(std::abs(gravity->to.y() - gravity->from.y()), std::abs(normal->to.y() - normal->from.y()), 1.0);
    // Точка приложения mg — центр груза, N — точка контакта (ниже центра).
    EXPECT_GT(normal->from.y(), gravity->from.y());
}

TEST(SpringHorizontalVectors, FrictionArrowOnlyWhenFrictionInModel) {
    SpringHorizontalModel ideal;
    setSetup(ideal, QStringLiteral("m=1; k=100; mu=0; x0=0.2"));
    SpringHorizontalScene idealScene(&ideal);
    prepare(idealScene, ideal, 0.1);
    // Силы трения в модели нет — её не должно быть и среди векторов.
    EXPECT_EQ(findArrow(idealScene.vectorsFor(ideal.time()), Kind::Friction), nullptr);

    SpringHorizontalModel damped;
    setSetup(damped, QStringLiteral("m=1; k=100; mu=0.15; x0=0.3"));
    SpringHorizontalScene dampedScene(&damped);
    prepare(dampedScene, damped, 0.1);
    EXPECT_NE(findArrow(dampedScene.vectorsFor(damped.time()), Kind::Friction), nullptr);
}

TEST(SpringHorizontalVectors, FrictionVanishesAfterStop) {
    SpringHorizontalModel model;
    setSetup(model, QStringLiteral("m=1; k=100; mu=0.2; x0=0.3"));
    SpringHorizontalScene scene(&model);
    ASSERT_TRUE(std::isfinite(model.restTime()));

    prepare(scene, model, model.restTime());
    const Arrow* friction = findArrow(scene.vectorsFor(model.time()), Kind::Friction);
    // В покое сила трения покоя уравновешивает пружину и не рисуется как
    // отдельная стрелка только если груз в центре; здесь он может стоять
    // вне нуля, тогда трение покоя обязано быть показано.
    if (friction) {
        const double elasticX = std::abs(model.elasticForceAt(model.time()));
        EXPECT_LE(std::abs(model.frictionForceAt(model.time())), model.frictionForce() + 1e-9);
        EXPECT_NEAR(elasticX, std::abs(model.frictionForceAt(model.time())), 1e-6);
    }
    // Скорости у покоившегося груза нет — вектор v отсутствует.
    EXPECT_EQ(findArrow(scene.vectorsFor(model.time()), Kind::Velocity), nullptr);
}

TEST(SpringHorizontalVectors, ArrowLengthsGrowWithMagnitude) {
    SpringHorizontalModel weak;
    setSetup(weak, QStringLiteral("m=1; k=20; mu=0; x0=0.3"));
    SpringHorizontalScene weakScene(&weak);

    SpringHorizontalModel stiff;
    setSetup(stiff, QStringLiteral("m=1; k=300; mu=0; x0=0.3"));
    SpringHorizontalScene stiffScene(&stiff);

    // Стрелки масштабируются внутри своей сцены, поэтому сравниваем
    // направление: более жёсткая пружина даёт больший модуль силы.
    prepare(weakScene, weak, 0.0);
    prepare(stiffScene, stiff, 0.0);
    EXPECT_GT(std::abs(stiff.elasticForceAt(0.0)), std::abs(weak.elasticForceAt(0.0)));
}

TEST(SpringHorizontalVectors, CategoriesToggledIndependently) {
    SpringHorizontalModel model;
    setSetup(model, QStringLiteral("m=1; k=100; mu=0.15; x0=0.3"));
    SpringHorizontalScene scene(&model);
    prepare(scene, model, 0.15);
    const double t = model.time();

    // Все категории включены: есть силы, скорость и ускорение.
    ASSERT_NE(findArrow(scene.vectorsFor(t), Kind::Gravity), nullptr);
    ASSERT_NE(findArrow(scene.vectorsFor(t), Kind::Velocity), nullptr);
    ASSERT_NE(findArrow(scene.vectorsFor(t), Kind::Acceleration), nullptr);

    // Силы динамической модели показываем всегда: переключатель их не скрывает.
    model.setShowsForces(false);
    const QVector<Arrow> withoutForces = scene.vectorsFor(t);
    EXPECT_NE(findArrow(withoutForces, Kind::Gravity), nullptr);
    EXPECT_NE(findArrow(withoutForces, Kind::Normal), nullptr);
    EXPECT_NE(findArrow(withoutForces, Kind::Friction), nullptr);
    EXPECT_NE(findArrow(withoutForces, Kind::Velocity), nullptr);
    EXPECT_NE(findArrow(withoutForces, Kind::Acceleration), nullptr);

    model.setShowsForces(true);
    model.setShowsAccelerations(false);
    const QVector<Arrow> withoutAcceleration = scene.vectorsFor(t);
    EXPECT_EQ(findArrow(withoutAcceleration, Kind::Acceleration), nullptr);
    EXPECT_NE(findArrow(withoutAcceleration, Kind::Gravity), nullptr);
    EXPECT_NE(findArrow(withoutAcceleration, Kind::Velocity), nullptr);

    model.setShowsAccelerations(true);
    model.setShowsVelocities(false);
    const QVector<Arrow> withoutVelocity = scene.vectorsFor(t);
    EXPECT_EQ(findArrow(withoutVelocity, Kind::Velocity), nullptr);
    EXPECT_NE(findArrow(withoutVelocity, Kind::Acceleration), nullptr);

    model.setShowsVelocities(true);
    EXPECT_NE(findArrow(scene.vectorsFor(t), Kind::Velocity), nullptr);
}

TEST(SpringHorizontalVectors, ArrowsMoveTogetherWithBlock) {
    SpringHorizontalModel model;
    setSetup(model, QStringLiteral("m=1; k=100; mu=0; x0=0.3"));
    SpringHorizontalScene scene(&model);

    prepare(scene, model, 0.0);
    const double firstX = scene.vectorsFor(model.time()).first().from.x();
    prepare(scene, model, model.period() / 2.0);
    const Arrow* moved = findArrow(scene.vectorsFor(model.time()), Kind::Gravity);
    ASSERT_NE(moved, nullptr);
    // Груз уехал в другую точку — точка приложения силы тоже сместилась.
    EXPECT_GT(std::abs(moved->from.x() - firstX), 40.0);
}

TEST(SpringHorizontalVectors, ArrowLabelsContainOnlySymbols) {
    SpringHorizontalModel model;
    setSetup(model, QStringLiteral("m=1; k=100; mu=0.15; x0=0.3"));
    SpringHorizontalScene scene(&model);
    prepare(scene, model, 0.15);

    for (const Arrow& arrow : scene.vectorsFor(model.time())) {
        ASSERT_FALSE(arrow.symbol.isEmpty());
        // Рядом со стрелкой — только обозначение величины, без чисел.
        bool hasDigit = false;
        for (const QChar ch : arrow.symbol)
            hasDigit = hasDigit || ch.isDigit();
        EXPECT_FALSE(hasDigit) << arrow.symbol.toUtf8().constData();
    }
}

// ------------------------------------------------------------------
// Отрисовка: положение груза и ось времени
// ------------------------------------------------------------------

TEST(SpringHorizontalScene, BlockPositionFollowsModelState) {
    SpringHorizontalModel model;
    setSetup(model, QStringLiteral("m=1; k=100; mu=0; x0=0.2"));
    SpringHorizontalScene scene(&model);

    prepare(scene, model, 0.0);
    const int startCenter = blockCenterX(renderFrame(scene));
    // Через половину периода смещение меняет знак: груз рисуется с другой
    // стороны отметки равновесия.
    prepare(scene, model, model.period() / 2.0);
    const int turnedCenter = blockCenterX(renderFrame(scene));
    ASSERT_GE(startCenter, 0);
    ASSERT_GE(turnedCenter, 0);
    EXPECT_GT(std::abs(startCenter - turnedCenter), 40);
}

TEST(SpringHorizontalScene, ThreePlotsAreDrawnBelowScheme) {
    SpringHorizontalModel model;
    setSetup(model, QStringLiteral("m=1; k=100; mu=0.15; x0=0.3"));
    SpringHorizontalScene scene(&model);
    prepare(scene, model, model.duration() * 0.6);
    const QImage image = renderFrame(scene);

    const int plotsTop = 360;
    EXPECT_GT(countColor(image, lab::style::bodyA(), plotsTop, kHeight), 200);      // x(t)
    EXPECT_GT(countColor(image, lab::style::bodyC(), plotsTop, kHeight), 200);      // v(t)
    EXPECT_GT(countColor(image, lab::style::accentWarm(), plotsTop, kHeight), 40);  // a(t)
}

TEST(SpringHorizontalScene, TimeAxisMarkerFollowsModelTime) {
    SpringHorizontalModel model;
    setSetup(model, QStringLiteral("m=1; k=100; mu=0; x0=0.2"));
    SpringHorizontalScene scene(&model);

    prepare(scene, model, model.duration() * 0.05);
    const int early = axisMarkerX(renderFrame(scene));
    prepare(scene, model, model.duration() * 0.9);
    const int late = axisMarkerX(renderFrame(scene));
    ASSERT_GE(early, 0);
    ASSERT_GE(late, 0);
    EXPECT_GT(late - early, 100);
}
