#include <gtest/gtest.h>

#include <QApplication>
#include <QHash>
#include <QImage>
#include <QPainter>

#include <cmath>

#include "TestSupport.h"
#include "models/HorizonModel.h"
#include "scenes/HorizonScene.h"
#include "scenes/Style.h"
#include "scenes/VectorArrows.h"
#include "utils/MathText.h"

using lab::models::HorizonModel;
using lab::scenes::HorizonScene;
using lab::test::setSetup;
using lab::vectors::Arrow;
using lab::vectors::Kind;

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 760;
// Область просмотра как в LabView: снизу остаётся место под ось времени.
constexpr double kViewTop = 12.0;
constexpr double kViewHeight = kHeight - 24.0 - 58.0;
// Полосы: чертёж, графики под ним и строка маркера текущего момента.
constexpr int kSchemeTop = 40;
constexpr int kSchemeBottom = 392;
constexpr int kPlotsTop = 415;
constexpr int kPlotsBottom = 692;
constexpr int kAxisMarkerTop = 713;
constexpr int kAxisMarkerBottom = 728;

/**
 * Сцена рисует шрифтами и событиями Qt, поэтому QApplication должен
 * существовать до создания первой QGraphicsScene.
 */
class HorizonGraphicsEnvironment : public ::testing::Environment {
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

const bool kHorizonEnvironmentRegistered = [] {
    ::testing::AddGlobalTestEnvironment(new HorizonGraphicsEnvironment);
    return true;
}();

/** Учебная постановка: Земля, подъём на 2 км со скоростью 25 м/с. */
void configure(HorizonModel& model) {
    setSetup(model, QStringLiteral("radius=6371; h=2000; v=25"));
}

/** Пересчитывает геометрию сцены и устанавливает время модели. */
void prepare(HorizonScene& scene, HorizonModel& model, double t) {
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));
    model.seekTo(t);
}

/** Отрисовывает кадр сцены и возвращает изображение. */
QImage renderFrame(HorizonScene& scene) {
    QImage image(kWidth, kHeight, QImage::Format_RGB32);
    image.fill(lab::style::background());
    QPainter painter(&image);
    scene.paintBackgroundInto(&painter, QRectF(0, 0, kWidth, kHeight));
    painter.end();
    return image;
}

/**
 * Доступ к защищённым методам сцены: тесты проверяют построение чертежа
 * и обратное преобразование ручек, а не только готовую картинку.
 */
class ProbeScene : public HorizonScene {
public:
    using HorizonScene::HorizonScene;
    using HorizonScene::mapHandlePosition;
    using HorizonScene::positionHandles;
};

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
// Геометрия чертежа: треугольник OAB построен по условиям задачи
// ------------------------------------------------------------------

TEST(HorizonGeometry, ObserverStaysOnVerticalAbovePlanet) {
    HorizonModel model;
    configure(model);
    HorizonScene scene(&model);
    prepare(scene, model, model.ascentTime() * 0.5);

    const HorizonScene::SchemeGeometry geometry = scene.schemeGeometry();
    // Наблюдатель на вертикали OA: его высота отсчитывается от центра.
    EXPECT_NEAR(geometry.observer.x(), geometry.center.x(), 1e-9);
    EXPECT_LT(geometry.observer.y(), geometry.subsatellite.y());
    // Точка поверхности под наблюдателем лежит на окружности планеты.
    EXPECT_NEAR(std::hypot(geometry.subsatellite.x() - geometry.center.x(),
                          geometry.subsatellite.y() - geometry.center.y()),
                geometry.planetRadiusPx, 1e-6);
}

TEST(HorizonGeometry, HorizonPointLiesOnPlanetSurface) {
    HorizonModel model;
    configure(model);
    HorizonScene scene(&model);
    prepare(scene, model, model.ascentTime());

    const HorizonScene::SchemeGeometry geometry = scene.schemeGeometry();
    const double distanceToCenter = std::hypot(geometry.horizon.x() - geometry.center.x(),
                                              geometry.horizon.y() - geometry.center.y());
    EXPECT_NEAR(distanceToCenter, geometry.planetRadiusPx, 1e-6);
    // Точка горизонта смещена в сторону от вертикали: горизонт не «под ногами».
    EXPECT_GT(geometry.horizon.x(), geometry.subsatellite.x() + 1.0);
}

TEST(HorizonGeometry, SightLineIsPerpendicularToRadiusAtHorizon) {
    HorizonModel model;
    configure(model);
    HorizonScene scene(&model);

    // Касательная перпендикулярна радиусу в точке касания — именно это
    // даёт право писать теорему Пифагора. Проверим скалярное произведение.
    for (double fraction : {0.2, 0.55, 1.0}) {
        prepare(scene, model, model.ascentTime() * fraction);
        const HorizonScene::SchemeGeometry geometry = scene.schemeGeometry();
        const QPointF sight = geometry.horizon - geometry.observer;
        const QPointF radius = geometry.horizon - geometry.center;
        const double dot = sight.x() * radius.x() + sight.y() * radius.y();
        const double scales = std::hypot(sight.x(), sight.y()) * std::hypot(radius.x(), radius.y());
        // Косинус угла между линиями взгляда и радиуса равен нулю.
        EXPECT_NEAR(dot / std::max(1e-9, scales), 0.0, 1e-9) << "доля времени: " << fraction;
    }
}

TEST(HorizonGeometry, ObserverHeightMatchesDrawnAngle) {
    HorizonModel model;
    configure(model);
    HorizonScene scene(&model);
    prepare(scene, model, model.ascentTime() * 0.5);

    const HorizonScene::SchemeGeometry geometry = scene.schemeGeometry();
    const double observerDistance = std::hypot(geometry.observer.x() - geometry.center.x(),
                                              geometry.observer.y() - geometry.center.y());
    // Cos α = R/(R + h) для чертежа тоже должно выполняться — иначеheight
    // наблюдателя и угол α нарисованы независимо.
    EXPECT_NEAR(geometry.planetRadiusPx / observerDistance, std::cos(geometry.drawnAlpha), 1e-9);
    EXPECT_GT(geometry.drawnAlpha, 0.0);
}

TEST(HorizonGeometry, DrawnAngleGrowsWithHeight) {
    HorizonModel model;
    configure(model);
    HorizonScene scene(&model);

    prepare(scene, model, 0.0);
    const double start = scene.schemeGeometry().drawnAlpha;
    prepare(scene, model, model.ascentTime() * 0.5);
    const double middle = scene.schemeGeometry().drawnAlpha;
    prepare(scene, model, model.ascentTime());
    const double end = scene.schemeGeometry().drawnAlpha;

    // В начале подъёма горизонта нет: наблюдатель на поверхности.
    EXPECT_NEAR(start, 0.0, 1e-12);
    EXPECT_GT(middle, start);
    EXPECT_GT(end, middle);
    // Чертеж остаётся читаемым: угол не превышает разумного предела.
    EXPECT_LT(end, 1.4);
}

TEST(HorizonGeometry, LiftIsProportionalToInverseCosine) {
    HorizonModel model;
    configure(model);
    HorizonScene scene(&model);
    prepare(scene, model, 0.0);

    const HorizonScene::SchemeGeometry bottom = scene.schemeGeometry();
    prepare(scene, model, model.ascentTime());
    const HorizonScene::SchemeGeometry top = scene.schemeGeometry();

    // Подъём точки A над поверхностью растёт быстрее, чем линейно с α:
    // OA = R/cos α.
    const double liftStart = bottom.subsatellite.y() - bottom.observer.y();
    const double liftEnd = top.subsatellite.y() - top.observer.y();
    EXPECT_NEAR(liftStart, 0.0, 1e-9);
    EXPECT_GT(liftEnd, 1.0);
    EXPECT_NEAR(top.center.y() - top.observer.y(),
                top.planetRadiusPx / std::cos(top.drawnAlpha), 1e-6);
}

TEST(HorizonGeometry, HeightHandleTracksObserver) {
    HorizonModel model;
    configure(model);
    ProbeScene scene(&model);
    prepare(scene, model, model.ascentTime() * 0.3);
    scene.rebuildHandles();
    scene.positionHandles();

    ASSERT_EQ(scene.handles().size(), 2);
    const QPointF observer = scene.schemeGeometry().observer;
    for (lab::DragHandleItem* handle : scene.handles()) {
        if (handle->id() == QLatin1String("height")) {
            EXPECT_NEAR(handle->anchor().y(), observer.y(), 1e-6);
            EXPECT_NEAR(handle->info().value, model.maxHeight(), 1e-9);
        } else if (handle->id() == QLatin1String("radius")) {
            EXPECT_NEAR(handle->info().value, model.radiusKilometers(), 1e-9);
            // Ручка радиуса лежит в нижней части чертежа, на своей линейке.
            EXPECT_GT(handle->anchor().y(), scene.viewArea().top() + 100.0);
        }
    }
}

TEST(HorizonGeometry, HandleMappingIsInvertible) {
    HorizonModel model;
    configure(model);
    ProbeScene scene(&model);
    prepare(scene, model, 0.0);
    scene.rebuildHandles();
    scene.positionHandles();

    // Позиция ручки ↔ значение высоты: перетаскивание возвращает то же число.
    lab::HandleInfo info;
    info.id = QStringLiteral("height");
    info.axis = QStringLiteral("y");
    for (double fraction : {0.2, 0.6, 1.0}) {
        model.seekTo(model.ascentTime() * fraction);
        const double height = model.heightAt(model.time());
        const QPointF anchor = scene.schemeGeometry().observer;
        EXPECT_NEAR(scene.mapHandlePosition(info, anchor), height, 1e-6) << "доля: " << fraction;
    }

    // Линейка радиусов: под позицией ручки лежит тот же радиус, что и в модели.
    lab::HandleInfo radiusInfo;
    radiusInfo.id = QStringLiteral("radius");
    radiusInfo.axis = QStringLiteral("x");
    bool radiusChecked = false;
    for (const lab::DragHandleItem* handle : scene.handles()) {
        if (handle->id() == QLatin1String("radius")) {
            EXPECT_NEAR(scene.mapHandlePosition(radiusInfo, handle->anchor()), model.radiusKilometers(), 1.0);
            radiusChecked = true;
        }
    }
    EXPECT_TRUE(radiusChecked);
}

TEST(HorizonGeometry, LabelsDoNotOverlapEachOther) {
    HorizonModel model;
    configure(model);
    HorizonScene scene(&model);

    // Подписи чертежа читаемы: ни одна не набегает на другую. Проверяем
    // на нескольких моментах подъёма — геометрия всё время меняется.
    for (double fraction : {0.3, 0.6, 1.0}) {
        prepare(scene, model, model.ascentTime() * fraction);
        const QVector<HorizonScene::Annotation> notes = scene.annotations();
        ASSERT_GE(notes.size(), 6) << "доля времени: " << fraction;

        for (int i = 0; i < notes.size(); ++i) {
            for (int j = i + 1; j < notes.size(); ++j) {
                const QRectF first(notes.at(i).anchor - QPointF(notes.at(i).size.width() / 2.0,
                                                               notes.at(i).size.height() / 2.0),
                                   notes.at(i).size);
                const QRectF second(notes.at(j).anchor - QPointF(notes.at(j).size.width() / 2.0,
                                                                notes.at(j).size.height() / 2.0),
                                    notes.at(j).size);
                const QRectF overlap = first.intersected(second);
                EXPECT_FALSE(overlap.isValid() && overlap.width() > 2.0 && overlap.height() > 2.0)
                    << notes.at(i).id.toUtf8().constData() << " vs " << notes.at(j).id.toUtf8().constData()
                    << " при доле времени " << fraction;
            }
        }
    }
}

TEST(HorizonGeometry, LabelsStayInsideScheme) {
    HorizonModel model;
    configure(model);
    ProbeScene scene(&model);
    prepare(scene, model, model.ascentTime());
    scene.rebuildHandles();
    scene.positionHandles();

    const QRectF view = scene.viewArea();
    for (const HorizonScene::Annotation& note : scene.annotations()) {
        const QRectF box(note.anchor - QPointF(note.size.width() / 2.0, note.size.height() / 2.0), note.size);
        EXPECT_GE(box.left(), view.left() - 2.0) << note.id.toUtf8().constData();
        EXPECT_LE(box.right(), view.right() + 2.0) << note.id.toUtf8().constData();
        EXPECT_GE(box.top(), view.top() - 2.0) << note.id.toUtf8().constData();
        EXPECT_LE(box.bottom(), view.bottom() + 2.0) << note.id.toUtf8().constData();
    }
}

TEST(HorizonGeometry, LabelsCarryModelNumbers) {
    HorizonModel model;
    configure(model);
    HorizonScene scene(&model);
    prepare(scene, model, model.ascentTime() * 0.5);

    // Чертеж подписан обозначениями: чисел на рисунке нет, они на панелях.
    const double t = model.time();
    Q_UNUSED(t);
    QHash<QString, QString> expected;
    expected.insert(QStringLiteral("height"), QStringLiteral("h"));
    expected.insert(QStringLiteral("distance"), QStringLiteral("d"));
    expected.insert(QStringLiteral("arc"), QStringLiteral("s"));
    expected.insert(QStringLiteral("alpha"), QStringLiteral("α"));
    expected.insert(QStringLiteral("radius"), QStringLiteral("R"));

    const QVector<HorizonScene::Annotation> notes = scene.annotations();
    for (const HorizonScene::Annotation& note : notes) {
        if (expected.contains(note.id))
            EXPECT_EQ(note.text, expected.value(note.id)) << note.id.toUtf8().constData();
    }
    for (const QString& id : {QStringLiteral("height"), QStringLiteral("distance"), QStringLiteral("arc"),
                             QStringLiteral("alpha"), QStringLiteral("radius")}) {
        bool found = false;
        for (const HorizonScene::Annotation& note : notes)
            found = found || note.id == id;
        EXPECT_TRUE(found) << id.toUtf8().constData();
    }
}

// ------------------------------------------------------------------
// Векторы: только скорость подъёма
// ------------------------------------------------------------------

TEST(HorizonVectors, OnlyAscentVelocityIsShown) {
    HorizonModel model;
    configure(model);
    HorizonScene scene(&model);
    prepare(scene, model, model.ascentTime() * 0.5);

    const QVector<Arrow> arrows = scene.vectorsFor(model.time());
    const Arrow* velocity = find(arrows, Kind::Velocity);
    ASSERT_NE(velocity, nullptr);
    EXPECT_EQ(velocity->symbol, QStringLiteral("v"));

    // Подъём равномерный: на наблюдателя действуют две равные силы —
    // тяжесть mg и подъёмная F. Ускорения нет — скорость постоянна.
    EXPECT_NE(find(arrows, Kind::Gravity), nullptr);
    EXPECT_NE(find(arrows, Kind::Tension), nullptr);
    EXPECT_EQ(find(arrows, Kind::Normal), nullptr);
    EXPECT_EQ(find(arrows, Kind::Friction), nullptr);
    EXPECT_EQ(find(arrows, Kind::Elastic), nullptr);
    EXPECT_EQ(find(arrows, Kind::Acceleration), nullptr);
    // Стрелок скорости не может быть больше одной.
    int velocityCount = 0;
    for (const Arrow& arrow : arrows) {
        if (arrow.kind == Kind::Velocity)
            ++velocityCount;
    }
    EXPECT_EQ(velocityCount, 1);
}

TEST(HorizonVectors, VelocityArrowPointsUpFromObserver) {
    HorizonModel model;
    configure(model);
    HorizonScene scene(&model);
    prepare(scene, model, model.ascentTime() * 0.4);

    const QVector<Arrow> arrows = scene.vectorsFor(model.time());
    const Arrow* velocity = find(arrows, Kind::Velocity);
    ASSERT_NE(velocity, nullptr);

    const QPointF observer = scene.schemeGeometry().observer;
    // Стрелка скорости отложена в сторону от вертикальной пары сил mg и F.
    EXPECT_NEAR(velocity->from.x(), observer.x() + 46.0, 1e-9);
    EXPECT_NEAR(velocity->from.y(), observer.y(), 1e-9);
    // Подъём: стрелка вверх (в координатах сцены — в сторону уменьшения y).
    EXPECT_NEAR(velocity->to.x(), velocity->from.x(), 1e-9);
    EXPECT_LT(velocity->to.y(), velocity->from.y());
    EXPECT_GT(std::hypot(velocity->to.x() - velocity->from.x(), velocity->to.y() - velocity->from.y()), 10.0);
}

TEST(HorizonVectors, NoVelocityAfterAscentFinishes) {
    HorizonModel model;
    configure(model);
    HorizonScene scene(&model);
    prepare(scene, model, model.duration());

    // Наблюдатель остановился: вектора скорости нет, силы остались.
    const QVector<Arrow> arrows = scene.vectorsFor(model.time());
    EXPECT_EQ(find(arrows, Kind::Velocity), nullptr);
    EXPECT_NE(find(arrows, Kind::Gravity), nullptr);
}

TEST(HorizonVectors, VelocityScalesWithAscentSpeed) {
    HorizonModel slow;
    setSetup(slow, QStringLiteral("radius=6371; h=2000; v=5"));
    HorizonScene slowScene(&slow);
    prepare(slowScene, slow, slow.ascentTime() * 0.5);
    const QVector<Arrow> slowArrows = slowScene.vectorsFor(slow.time());
    const Arrow* slowVelocity = find(slowArrows, Kind::Velocity);
    ASSERT_NE(slowVelocity, nullptr);

    HorizonModel fast;
    setSetup(fast, QStringLiteral("radius=6371; h=2000; v=250"));
    HorizonScene fastScene(&fast);
    prepare(fastScene, fast, fast.ascentTime() * 0.5);
    const QVector<Arrow> fastArrows = fastScene.vectorsFor(fast.time());
    const Arrow* fastVelocity = find(fastArrows, Kind::Velocity);
    ASSERT_NE(fastVelocity, nullptr);

    const double slowLength = slowVelocity->from.y() - slowVelocity->to.y();
    const double fastLength = fastVelocity->from.y() - fastVelocity->to.y();
    EXPECT_GT(fastLength, slowLength);
}

TEST(HorizonVectors, VelocitiesToggleIndependently) {
    HorizonModel model;
    configure(model);
    HorizonScene scene(&model);
    prepare(scene, model, model.ascentTime() * 0.5);
    const double t = model.time();

    ASSERT_FALSE(scene.vectorsFor(t).isEmpty());
    model.setShowsVelocities(false);
    const QVector<Arrow> hidden = scene.vectorsFor(t);
    // Скорость скрылась, а уравновешенные силы — часть чертежа, они остались.
    EXPECT_EQ(find(hidden, Kind::Velocity), nullptr);
    EXPECT_NE(find(hidden, Kind::Gravity), nullptr);
    model.setShowsVelocities(true);
    const QVector<Arrow> shown = scene.vectorsFor(t);
    EXPECT_NE(find(shown, Kind::Velocity), nullptr);
}

TEST(HorizonVectors, ArrowLabelsContainOnlySymbols) {
    HorizonModel model;
    configure(model);
    HorizonScene scene(&model);
    prepare(scene, model, model.ascentTime() * 0.5);

    const QVector<Arrow> arrows = scene.vectorsFor(model.time());
    ASSERT_FALSE(arrows.isEmpty());
    for (const Arrow& arrow : arrows) {
        ASSERT_FALSE(arrow.symbol.isEmpty());
        bool hasDigit = false;
        for (const QChar ch : arrow.symbol)
            hasDigit = hasDigit || ch.isDigit();
        EXPECT_FALSE(hasDigit) << arrow.symbol.toUtf8().constData();
    }
}

// ------------------------------------------------------------------
// Отрисовка: чертёж, графики, общая ось времени
// ------------------------------------------------------------------

TEST(HorizonScene, PlanetAndSightLineAreDrawn) {
    HorizonModel model;
    configure(model);
    HorizonScene scene(&model);
    prepare(scene, model, model.ascentTime());

    const QImage image = renderFrame(scene);
    // Линия взгляда (d) — синий, путь по поверхности (s) — фиолетовый.
    EXPECT_GT(countColor(image, lab::style::bodyA(), kSchemeTop, kSchemeBottom), 100);
    EXPECT_GT(countColor(image, lab::style::bodyD(), kSchemeTop, kSchemeBottom), 40);
    // Наблюдатель — красная точка.
    EXPECT_GT(countColor(image, lab::style::bodyB(), kSchemeTop, kSchemeBottom), 60);
}

TEST(HorizonScene, PlanetGrowsWithRadiusParameter) {
    HorizonModel small;
    setSetup(small, QStringLiteral("radius=100; h=200; v=20"));
    HorizonScene smallScene(&small);
    prepare(smallScene, small, 0.0);

    HorizonModel large;
    setSetup(large, QStringLiteral("radius=605100; h=200; v=20"));
    HorizonScene largeScene(&large);
    prepare(largeScene, large, 0.0);

    // Радиус планеты — параметр задачи: чертёж перестраивается, оставаясь
    // в границах области просмотра.
    EXPECT_GT(smallScene.schemeGeometry().planetRadiusPx, 0.0);
    EXPECT_GT(largeScene.schemeGeometry().planetRadiusPx, 0.0);
    EXPECT_LT(largeScene.schemeGeometry().center.y() - largeScene.schemeGeometry().planetRadiusPx,
              largeScene.viewArea().bottom());
    EXPECT_NE(smallScene.schemeGeometry().planetRadiusPx, largeScene.schemeGeometry().planetRadiusPx);
}

TEST(HorizonScene, FourPanelsAreDrawnBelowScheme) {
    HorizonModel model;
    configure(model);
    HorizonScene scene(&model);
    prepare(scene, model, model.ascentTime() * 0.6);

    const QImage image = renderFrame(scene);
    // h(t) — красный, d(t) и d(h) — синий, s(t) и s(h) — фиолетовый.
    EXPECT_GT(countColor(image, lab::style::bodyB(), kPlotsTop, kPlotsBottom), 150);
    EXPECT_GT(countColor(image, lab::style::bodyA(), kPlotsTop, kPlotsBottom), 150);
    EXPECT_GT(countColor(image, lab::style::bodyD(), kPlotsTop, kPlotsBottom), 150);
}

TEST(HorizonScene, TimeAxisMarkerFollowsModelTime) {
    HorizonModel model;
    configure(model);
    HorizonScene scene(&model);

    prepare(scene, model, model.duration() * 0.05);
    const int early = axisMarkerX(renderFrame(scene));
    prepare(scene, model, model.duration() * 0.9);
    const int late = axisMarkerX(renderFrame(scene));
    ASSERT_GE(early, 0);
    ASSERT_GE(late, 0);
    EXPECT_GT(late - early, 100);
}

TEST(HorizonScene, HandlesAndSchemeSurviveInvalidSetup) {
    HorizonModel model;
    setSetup(model, QStringLiteral("radius=100; h=200000; v=25"));
    ProbeScene scene(&model);

    // Недопустимая постановка не должна ронять сцену: предупреждение
    // рисуется поверх, а ручки остаются на своих местах.
    prepare(scene, model, model.duration() * 0.5);
    scene.rebuildHandles();
    scene.positionHandles();
    EXPECT_EQ(scene.handles().size(), 2);
    EXPECT_TRUE(scene.vectorsFor(model.time()).isEmpty());
    const QImage image = renderFrame(scene);
    EXPECT_FALSE(image.isNull());
}
