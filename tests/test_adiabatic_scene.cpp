#include <gtest/gtest.h>

#include <QApplication>
#include <QHash>
#include <QImage>
#include <QPainter>

#include <algorithm>
#include <cmath>

#include "TestSupport.h"
#include "models/AdiabaticModel.h"
#include "scenes/AdiabaticScene.h"
#include "scenes/Style.h"
#include "scenes/VectorArrows.h"
#include "utils/MathText.h"

using lab::models::AdiabaticModel;
using lab::scenes::AdiabaticScene;
using lab::test::setSetup;
using lab::vectors::Arrow;
using lab::vectors::Kind;

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 760;
// Область просмотра как в LabView: снизу остаётся место под ось времени.
constexpr double kViewTop = 12.0;
constexpr double kViewHeight = kHeight - 24.0 - 58.0;
// Полосы: чертёж с цилиндром, графики под ним и строка маркера времени.
constexpr int kSchemeTop = 26;
constexpr int kSchemeBottom = 345;
// Панели графиков: полоса между схемой и осью времени, высота 216 пикселей
// и вертикальное центрирование — отсюда 417..633.
constexpr int kPlotsTop = 420;
constexpr int kPlotsBottom = 633;
// Строка маркера текущего момента: сама точка на оси (её радиус 6 пикселей),
// строкой выше лежит подпись режима — она к правому краю, её брать нельзя.
/** Толщина корпуса поршня на чертеже — та же, что в сцене. */
constexpr double kPistonThickness = 16.0;

constexpr int kAxisMarkerTop = 714;
constexpr int kAxisMarkerBottom = 727;

/**
 * Сцена рисует шрифтами и событиями Qt, поэтому QApplication должен
 * существовать до создания первой QGraphicsScene. Если приложение уже
 * создано другим набором тестов, второе не создаётся.
 */
class AdiabaticGraphicsEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        if (QApplication::instance() != nullptr)
            return;
        static int argc = 1;
        static char arg0[] = "lab_tests";
        static char* argv[] = {arg0, nullptr};
        m_application = new QApplication(argc, argv);
    }

private:
    QApplication* m_application = nullptr;   // принадлежит процессу
};

const bool kAdiabaticEnvironmentRegistered = [] {
    ::testing::AddGlobalTestEnvironment(new AdiabaticGraphicsEnvironment);
    return true;
}();

/** Учебная постановка: сжатие с 8 л до 2 л, S = 50 см², τ = 6 с. */
void configure(AdiabaticModel& model) {
    setSetup(model, QStringLiteral("gas=0; gamma=1.4; mass=29; nu=1; T1=293; V1=8; V2=2; S=50; p0=101.3; tau=6"));
}

/** Пересчитывает геометрию сцены и устанавливает время модели. */
void prepare(AdiabaticScene& scene, AdiabaticModel& model, double t) {
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));
    model.seekTo(t);
}

/** Отрисовывает кадр сцены и возвращает изображение. */
QImage renderFrame(AdiabaticScene& scene) {
    QImage image(kWidth, kHeight, QImage::Format_RGB32);
    image.fill(lab::style::background());
    QPainter painter(&image);
    scene.paintBackgroundInto(&painter, QRectF(0, 0, kWidth, kHeight));
    painter.end();
    return image;
}

/** Доступ к защищённым методам сцены. */
class ProbeScene : public AdiabaticScene {
public:
    using AdiabaticScene::AdiabaticScene;
    using AdiabaticScene::mapHandlePosition;
    using AdiabaticScene::positionHandles;
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

/** Стрелка силы по её подписи (силы на сцене различаются именно подписями). */
const Arrow* findForce(const QVector<Arrow>& arrows, const QString& symbol) {
    for (const Arrow& arrow : arrows) {
        if (arrow.kind == Kind::Pressure || arrow.kind == Kind::Elastic) {
            if (arrow.symbol == symbol)
                return &arrow;
        }
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

/** Левый и правый край маркера текущего момента на общей оси времени. */
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
// Геометрия чертежа: цилиндр построен по объёмам модели
// ------------------------------------------------------------------

TEST(AdiabaticGeometry, GasHeightMatchesVolumeOverArea) {
    AdiabaticModel model;
    configure(model);
    AdiabaticScene scene(&model);
    prepare(scene, model, model.processTime() * 0.5);

    const AdiabaticScene::SchemeGeometry geometry = scene.schemeGeometry();
    const double t = model.time();
    // h = V/S — высота столба газа в пикселях получена умножением на масштаб.
    const double expectedHeight = model.volumeAt(t) / model.pistonArea();
    EXPECT_GT(geometry.pixelsPerMeter, 0.0);
    EXPECT_NEAR(geometry.gasHeightPx, expectedHeight * geometry.pixelsPerMeter, 1e-9);
    EXPECT_NEAR(geometry.piston.y(), geometry.vessel.bottom() - geometry.gasHeightPx - 16.0, 1e-9);
}

TEST(AdiabaticGeometry, HeightRatioEqualsVolumeRatio) {
    AdiabaticModel model;
    configure(model);
    AdiabaticScene scene(&model);
    prepare(scene, model, 0.0);

    // Площадь поршня одна и та же, поэтому отношение высот столбов равно
    // отношению объёмов — чертёж передаёт масштаб процесса, а не «красиво».
    const AdiabaticScene::SchemeGeometry geometry = scene.schemeGeometry();
    const double ratio = geometry.strokeBottomPx / geometry.strokeTopPx;
    EXPECT_NEAR(ratio, model.finalVolume() / model.initialVolume(), 1e-9);
    EXPECT_GT(geometry.strokeTopPx, geometry.strokeBottomPx);
}

TEST(AdiabaticGeometry, PistonStaysInsideVesselOverWholeProcess) {
    AdiabaticModel model;
    configure(model);
    AdiabaticScene scene(&model);

    for (int i = 0; i <= 40; ++i) {
        prepare(scene, model, model.duration() * i / 40.0);
        const AdiabaticScene::SchemeGeometry geometry = scene.schemeGeometry();
        // Верхняя граница газа — нижняя кромка поршня: она обязана оставаться
        // внутри полости сосуда при любом положении поршня.
        const double gasTop = geometry.vessel.bottom() - geometry.gasHeightPx;
        EXPECT_GE(geometry.gasHeightPx, 0.0) << "момент " << i;
        EXPECT_LE(geometry.gasHeightPx, geometry.vessel.height() + 1e-9) << "момент " << i;
        EXPECT_GE(gasTop, geometry.vessel.top() - 1e-9) << "момент " << i;
        EXPECT_LE(gasTop, geometry.vessel.bottom() + 1e-9) << "момент " << i;
        // Сам корпус поршня выступает вверх через открытый верх сосуда —
        // так и должно быть: в сосуде остаётся только его нижняя кромка.
        EXPECT_NEAR(geometry.piston.y(), gasTop - kPistonThickness, 1e-9) << "момент " << i;
    }
}

TEST(AdiabaticGeometry, VesselHeightShrinksDuringCompression) {
    AdiabaticModel model;
    configure(model);
    AdiabaticScene scene(&model);

    prepare(scene, model, 0.0);
    const double startHeight = scene.schemeGeometry().gasHeightPx;
    prepare(scene, model, model.processTime());
    const double endHeight = scene.schemeGeometry().gasHeightPx;
    EXPECT_GT(startHeight, endHeight);
}

TEST(AdiabaticGeometry, WiderPistonMakesVesselWiderAndGasTaller) {
    AdiabaticModel narrow;
    configure(narrow);
    AdiabaticScene narrowScene(&narrow);
    prepare(narrowScene, narrow, 0.0);

    AdiabaticModel wide;
    configure(wide);
    wide.setParam(QStringLiteral("S"), 200.0);
    AdiabaticScene wideScene(&wide);
    prepare(wideScene, wide, 0.0);

    // Больше площадь сечения — шире цилиндр на чертеже.
    EXPECT_GT(wideScene.schemeGeometry().vessel.width(), narrowScene.schemeGeometry().vessel.width());

    // Чертеж всегда вписан в полосу, поэтому высоты сравнивают метры, а не
    // пиксели: тот же объём при большей площади даёт более низкий столб газа.
    const AdiabaticScene::SchemeGeometry narrowGeo = narrowScene.schemeGeometry();
    const AdiabaticScene::SchemeGeometry wideGeo = wideScene.schemeGeometry();
    const double narrowMeters = narrowGeo.gasHeightPx / narrowGeo.pixelsPerMeter;
    const double wideMeters = wideGeo.gasHeightPx / wideGeo.pixelsPerMeter;
    EXPECT_NEAR(narrowMeters, narrow.initialVolume() / narrow.pistonArea(), 1e-9);
    EXPECT_NEAR(wideMeters, wide.initialVolume() / wide.pistonArea(), 1e-9);
    EXPECT_GT(narrowMeters, wideMeters);
}

// ------------------------------------------------------------------
// Молекулы: их скорость — это √(3RT/M)
// ------------------------------------------------------------------

TEST(AdiabaticMolecules, CountMatchesModelAndPositionsStayInGas) {
    AdiabaticModel model;
    configure(model);
    AdiabaticScene scene(&model);
    prepare(scene, model, model.processTime() * 0.5);

    const double t = model.time();
    const QVector<AdiabaticScene::MoleculeItem> molecules = scene.moleculesFor(t);
    const AdiabaticScene::SchemeGeometry geometry = scene.schemeGeometry();

    int expected = 0;
    for (const lab::EntityVisual& entity : model.stateAt(t).entities) {
        if (entity.id.startsWith(QStringLiteral("molecule_")))
            ++expected;
    }
    ASSERT_EQ(molecules.size(), expected);
    ASSERT_FALSE(molecules.isEmpty());

    const QRectF gas(geometry.vessel.left(), geometry.vessel.bottom() - geometry.gasHeightPx,
                     geometry.vessel.width(), geometry.gasHeightPx);
    for (const AdiabaticScene::MoleculeItem& molecule : molecules) {
        ASSERT_TRUE(molecule.insideGas);
        EXPECT_TRUE(gas.contains(molecule.position)) << "молекула " << molecule.index;
        EXPECT_NEAR(std::hypot(molecule.direction.x(), molecule.direction.y()), 1.0, 1e-9);
    }
}

TEST(AdiabaticMolecules, SpeedEqualsFormulaFromModel) {
    AdiabaticModel model;
    configure(model);
    AdiabaticScene scene(&model);

    for (double fraction : {0.0, 0.5, 1.0}) {
        prepare(scene, model, model.processTime() * fraction);
        const double t = model.time();
        const QVector<AdiabaticScene::MoleculeItem> molecules = scene.moleculesFor(t);
        ASSERT_FALSE(molecules.isEmpty());
        const double expected = std::sqrt(3.0 * 8.314462618 * model.temperatureAt(t) / model.molarMass());
        for (const AdiabaticScene::MoleculeItem& molecule : molecules) {
            EXPECT_NEAR(molecule.speedMeters, expected, 1e-9) << "доля времени " << fraction;
            EXPECT_NEAR(molecule.speedMeters, model.rmsSpeedAtTime(t), 1e-9);
        }
    }
}

TEST(AdiabaticMolecules, SpeedGrowsAsGasIsCompressed) {
    AdiabaticModel model;
    configure(model);
    AdiabaticScene scene(&model);

    prepare(scene, model, 0.0);
    const QVector<AdiabaticScene::MoleculeItem> start = scene.moleculesFor(model.time());
    prepare(scene, model, model.processTime());
    const QVector<AdiabaticScene::MoleculeItem> end = scene.moleculesFor(model.time());
    ASSERT_FALSE(start.isEmpty());
    ASSERT_EQ(start.size(), end.size());

    // Сжатие разогревает газ: скорость каждой молекулы растёт.
    for (int i = 0; i < start.size(); ++i)
        EXPECT_GT(end.at(i).speedMeters, start.at(i).speedMeters) << "молекула " << i;
}

TEST(AdiabaticMolecules, ExpansionSlowsMoleculesDown) {
    AdiabaticModel model;
    setSetup(model, QStringLiteral("gas=0; nu=1; T1=293; V1=2; V2=8; S=50; p0=101.3; tau=6"));
    AdiabaticScene scene(&model);

    prepare(scene, model, 0.0);
    const QVector<AdiabaticScene::MoleculeItem> start = scene.moleculesFor(model.time());
    prepare(scene, model, model.processTime());
    const QVector<AdiabaticScene::MoleculeItem> end = scene.moleculesFor(model.time());
    ASSERT_FALSE(start.isEmpty());
    EXPECT_LT(end.first().speedMeters, start.first().speedMeters);
}

// ------------------------------------------------------------------
// Векторы: силы на поршень и скорость поршня
// ------------------------------------------------------------------

TEST(AdiabaticVectors, OnlyPressureForcesAndPistonSpeedAreShown) {
    AdiabaticModel model;
    configure(model);
    AdiabaticScene scene(&model);
    prepare(scene, model, model.processTime() * 0.5);

    // Сил тяжести, реакции, трения и натяжения в модели нет: поршень
    // движется заданно, его масса в условие задачи не входит.
    const QVector<Arrow> arrows = scene.vectorsFor(model.time());
    EXPECT_EQ(find(arrows, Kind::Gravity), nullptr);
    EXPECT_EQ(find(arrows, Kind::Normal), nullptr);
    EXPECT_EQ(find(arrows, Kind::Friction), nullptr);
    EXPECT_EQ(find(arrows, Kind::Tension), nullptr);
    EXPECT_EQ(find(arrows, Kind::Acceleration), nullptr);
    EXPECT_EQ(find(arrows, Kind::Flow), nullptr);
    EXPECT_EQ(find(arrows, Kind::Current), nullptr);

    ASSERT_NE(findForce(arrows, QStringLiteral("F = pS")), nullptr);
    ASSERT_NE(findForce(arrows, QStringLiteral("F₀ = p₀S")), nullptr);
    ASSERT_NE(findForce(arrows, QStringLiteral("Fвнеш")), nullptr);
    ASSERT_NE(find(arrows, Kind::Velocity), nullptr);
}

TEST(AdiabaticVectors, GasForceArrowPointsUpAndOutsideArrowDown) {
    AdiabaticModel model;
    configure(model);
    AdiabaticScene scene(&model);
    prepare(scene, model, model.processTime() * 0.5);

    const QVector<Arrow> arrows = scene.vectorsFor(model.time());
    const Arrow* gas = findForce(arrows, QStringLiteral("F = pS"));
    const Arrow* outside = findForce(arrows, QStringLiteral("F₀ = p₀S"));
    ASSERT_NE(gas, nullptr);
    ASSERT_NE(outside, nullptr);

    // Газ выталкивает поршень вверх, атмосфера давит вниз.
    EXPECT_LT(gas->to.y(), gas->from.y());
    EXPECT_GT(outside->to.y(), outside->from.y());
    // Давление газа выше атмосферного — его стрелка длиннее.
    EXPECT_GT(std::abs(gas->to.y() - gas->from.y()), std::abs(outside->to.y() - outside->from.y()));
}

TEST(AdiabaticVectors, ArrowLengthsAreProportionalToForces) {
    AdiabaticModel model;
    configure(model);
    AdiabaticScene scene(&model);
    prepare(scene, model, model.processTime() * 0.5);
    const double t = model.time();

    const QVector<Arrow> arrows = scene.vectorsFor(t);
    const Arrow* gas = findForce(arrows, QStringLiteral("F = pS"));
    const Arrow* outside = findForce(arrows, QStringLiteral("F₀ = p₀S"));
    const Arrow* hand = findForce(arrows, QStringLiteral("Fвнеш"));
    ASSERT_NE(gas, nullptr);
    ASSERT_NE(outside, nullptr);
    ASSERT_NE(hand, nullptr);

    const double gasLength = std::abs(gas->to.y() - gas->from.y());
    const double outsideLength = std::abs(outside->to.y() - outside->from.y());
    const double handLength = std::abs(hand->to.y() - hand->from.y());

    // Все три стрелки drawn по одному масштабу: отношение длин равно
    // отношению сил (для стрелок длиннее минимального штриха).
    EXPECT_NEAR(gasLength / handLength, model.gasForceAt(t) / std::abs(model.externalForceAt(t)), 1e-6);
    // Порядок длин повторяет порядок сил, даже малые силы остаются видимыми.
    EXPECT_GT(gasLength, handLength);
    EXPECT_GT(handLength, outsideLength);
    EXPECT_GT(model.gasForceAt(t), std::abs(model.externalForceAt(t)));
    EXPECT_GT(std::abs(model.externalForceAt(t)), model.outsideForce());

    // Равновесие поршня: F = F₀ + Fвнеш — в числах модели, из которых растут
    // длины стрелок (мелкая сила не масштабируется линейно, её штрих
    // ограничен снизу, чтобы стрелку было видно).
    EXPECT_NEAR(model.gasForceAt(t), model.outsideForce() + model.externalForceAt(t), 1e-9);
}

TEST(AdiabaticVectors, ExternalArrowFlipsWhenOutsidePressureWins) {
    AdiabaticModel gas;
    configure(gas);
    AdiabaticScene gasScene(&gas);
    prepare(gasScene, gas, 0.0);

    AdiabaticModel squeezed;
    configure(squeezed);
    // Внешнее давление выше начального давления газа (V₁ = 20 л даёт
    // p₁ ≈ 122 кПа): удерживать поршень приходится вверх, и стрелка
    // разворачивается.
    squeezed.setParam(QStringLiteral("V1"), 20.0);
    squeezed.setParam(QStringLiteral("p0"), 300.0);
    AdiabaticScene squeezedScene(&squeezed);
    prepare(squeezedScene, squeezed, 0.0);

    const QVector<Arrow> gasArrows = gasScene.vectorsFor(0.0);
    const QVector<Arrow> squeezedArrows = squeezedScene.vectorsFor(0.0);
    const Arrow* gasHand = findForce(gasArrows, QStringLiteral("Fвнеш"));
    const Arrow* squeezedHand = findForce(squeezedArrows, QStringLiteral("Fвнеш"));
    ASSERT_NE(gasHand, nullptr);
    ASSERT_NE(squeezedHand, nullptr);
    EXPECT_GT(gasHand->to.y(), gasHand->from.y());
    EXPECT_LT(squeezedHand->to.y(), squeezedHand->from.y());
}

TEST(AdiabaticVectors, PistonSpeedArrowFollowsProcessDirection) {
    AdiabaticModel compression;
    configure(compression);
    AdiabaticScene compressionScene(&compression);
    prepare(compressionScene, compression, compression.processTime() * 0.5);

    // Сжатие: поршень идёт вниз, стрелка скорости направлена вниз.
    const QVector<Arrow> down = compressionScene.vectorsFor(compression.time());
    const Arrow* descending = find(down, Kind::Velocity);
    ASSERT_NE(descending, nullptr);
    EXPECT_GT(descending->to.y(), descending->from.y());

    AdiabaticModel expansion;
    setSetup(expansion, QStringLiteral("gas=0; nu=1; T1=293; V1=2; V2=8; S=50; p0=101.3; tau=6"));
    AdiabaticScene expansionScene(&expansion);
    prepare(expansionScene, expansion, expansion.processTime() * 0.5);

    // Расширение: поршень поднимается.
    const QVector<Arrow> up = expansionScene.vectorsFor(expansion.time());
    const Arrow* ascending = find(up, Kind::Velocity);
    ASSERT_NE(ascending, nullptr);
    EXPECT_LT(ascending->to.y(), ascending->from.y());
}

TEST(AdiabaticVectors, NoSpeedArrowAfterProcessEnds) {
    AdiabaticModel model;
    configure(model);
    AdiabaticScene scene(&model);
    prepare(scene, model, model.duration());

    const QVector<Arrow> arrows = scene.vectorsFor(model.time());
    EXPECT_EQ(find(arrows, Kind::Velocity), nullptr);
    // Силы при этом остаются: поршень держат и в покое.
    EXPECT_NE(findForce(arrows, QStringLiteral("F = pS")), nullptr);
}

TEST(AdiabaticVectors, ForcesAndSpeedToggleIndependently) {
    AdiabaticModel model;
    configure(model);
    AdiabaticScene scene(&model);
    prepare(scene, model, model.processTime() * 0.5);
    const double t = model.time();

    ASSERT_NE(findForce(scene.vectorsFor(t), QStringLiteral("F = pS")), nullptr);
    model.setShowsForces(false);
    const QVector<Arrow> withoutForces = scene.vectorsFor(t);
    EXPECT_EQ(find(withoutForces, Kind::Pressure), nullptr);
    EXPECT_EQ(find(withoutForces, Kind::Elastic), nullptr);
    // Скорость поршня — отдельная категория, она осталась.
    EXPECT_NE(find(withoutForces, Kind::Velocity), nullptr);

    model.setShowsForces(true);
    model.setShowsVelocities(false);
    const QVector<Arrow> withoutSpeed = scene.vectorsFor(t);
    EXPECT_EQ(find(withoutSpeed, Kind::Velocity), nullptr);
    EXPECT_NE(findForce(withoutSpeed, QStringLiteral("F = pS")), nullptr);
}

TEST(AdiabaticVectors, ArrowLabelsContainOnlySymbols) {
    AdiabaticModel model;
    configure(model);
    AdiabaticScene scene(&model);
    prepare(scene, model, model.processTime() * 0.5);

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
// Перетаскивание мышью
// ------------------------------------------------------------------

TEST(AdiabaticHandles, HandlesTrackVolumesAndArea) {
    AdiabaticModel model;
    configure(model);
    ProbeScene scene(&model);
    prepare(scene, model, 0.0);
    scene.rebuildHandles();
    scene.positionHandles();

    ASSERT_EQ(scene.handles().size(), 3);
    const AdiabaticScene::SchemeGeometry geometry = scene.schemeGeometry();
    int checked = 0;
    for (const lab::DragHandleItem* handle : scene.handles()) {
        if (handle->id() == QLatin1String("piston")) {
            // Ручка начального объёма стоит на уровне поршня при V₁.
            EXPECT_NEAR(handle->anchor().y(), geometry.vessel.bottom() - geometry.strokeTopPx, 1e-6);
            EXPECT_NEAR(handle->info().value, model.initialVolume() * 1e3, 1e-6);
            ++checked;
        } else if (handle->id() == QLatin1String("target")) {
            EXPECT_NEAR(handle->anchor().y(), geometry.vessel.bottom() - geometry.strokeBottomPx, 1e-6);
            EXPECT_NEAR(handle->info().value, model.finalVolume() * 1e3, 1e-6);
            ++checked;
        } else if (handle->id() == QLatin1String("area")) {
            EXPECT_NEAR(handle->anchor().x(), geometry.vessel.right(), 1e-6);
            EXPECT_NEAR(handle->info().value, model.pistonArea() * 1e4, 1e-6);
            ++checked;
        }
    }
    EXPECT_EQ(checked, 3);
}

TEST(AdiabaticHandles, MappingIsInvertible) {
    AdiabaticModel model;
    configure(model);
    ProbeScene scene(&model);
    prepare(scene, model, 0.0);
    scene.rebuildHandles();
    scene.positionHandles();
    const AdiabaticScene::SchemeGeometry geometry = scene.schemeGeometry();

    lab::HandleInfo volume;
    volume.id = QStringLiteral("piston");
    volume.axis = QStringLiteral("y");
    const QPointF startLevel(geometry.vessel.center().x(), geometry.vessel.bottom() - geometry.strokeTopPx);
    EXPECT_NEAR(scene.mapHandlePosition(volume, startLevel), model.initialVolume() * 1e3, 1e-6);

    lab::HandleInfo area;
    area.id = QStringLiteral("area");
    area.axis = QStringLiteral("both");
    EXPECT_NEAR(scene.mapHandlePosition(area, QPointF(geometry.vessel.right(), geometry.vessel.bottom())),
                model.pistonArea() * 1e4, 1e-6);
}

// ------------------------------------------------------------------
// Подписи
// ------------------------------------------------------------------

TEST(AdiabaticLabels, LabelsDoNotOverlapEachOther) {
    AdiabaticModel model;
    configure(model);
    AdiabaticScene scene(&model);

    for (double fraction : {0.0, 0.5, 1.0}) {
        prepare(scene, model, model.duration() * fraction);
        const QVector<AdiabaticScene::Annotation> notes = scene.annotations();
        ASSERT_GE(notes.size(), 8) << "доля времени " << fraction;

        for (int i = 0; i < notes.size(); ++i) {
            for (int j = i + 1; j < notes.size(); ++j) {
                const QRectF overlap = notes.at(i).rect().intersected(notes.at(j).rect());
                EXPECT_FALSE(overlap.isValid() && overlap.width() > 2.0 && overlap.height() > 2.0)
                    << notes.at(i).id.toUtf8().constData() << " vs " << notes.at(j).id.toUtf8().constData()
                    << " при доле времени " << fraction;
            }
        }
    }
}

TEST(AdiabaticLabels, LabelsStayInsideScheme) {
    AdiabaticModel model;
    configure(model);
    AdiabaticScene scene(&model);
    prepare(scene, model, model.processTime());

    const QRectF band = scene.schemeBand();
    for (const AdiabaticScene::Annotation& note : scene.annotations()) {
        EXPECT_GE(note.rect().left(), band.left() - 2.0) << note.id.toUtf8().constData();
        EXPECT_LE(note.rect().right(), band.right() + 2.0) << note.id.toUtf8().constData();
        EXPECT_GE(note.rect().top(), band.top() - 2.0) << note.id.toUtf8().constData();
        EXPECT_LE(note.rect().bottom(), band.bottom() + 2.0) << note.id.toUtf8().constData();
    }
}

TEST(AdiabaticLabels, LabelsCarryModelNumbers) {
    AdiabaticModel model;
    configure(model);
    AdiabaticScene scene(&model);
    prepare(scene, model, model.processTime() * 0.5);
    const double t = model.time();

    QHash<QString, QString> expected;
    expected.insert(QStringLiteral("state"),
                    QStringLiteral("V = %1 л, p = %2 кПа, T = %3 К")
                        .arg(lab::utils::formatNumber(model.volumeAt(t) * 1e3, 2),
                             lab::utils::formatNumber(model.pressureAt(t) / 1e3, 1),
                             lab::utils::formatNumber(model.temperatureAt(t), 1)));
    expected.insert(QStringLiteral("finish"),
                    QStringLiteral("V₂ = %1 л, T₂ = %2 К")
                        .arg(lab::utils::formatNumber(model.finalVolume() * 1e3, 2),
                             lab::utils::formatNumber(model.finalTemperature(), 1)));
    expected.insert(QStringLiteral("forces"),
                    QStringLiteral("F = pS = %1 Н, F₀ = p₀S = %2 Н, Fвнеш = %3 Н")
                        .arg(lab::utils::formatNumber(model.gasForceAt(t), 0),
                             lab::utils::formatNumber(model.outsideForce(), 0),
                             lab::utils::formatNumber(std::abs(model.externalForceAt(t)), 0)));
    expected.insert(QStringLiteral("molecules"),
                    QStringLiteral("v = √(3RT/M) = %1 м/с при M = %2 г/моль")
                        .arg(lab::utils::formatNumber(model.rmsSpeedAtTime(t), 0),
                             lab::utils::formatNumber(model.molarMass() * 1e3, 1)));

    const QVector<AdiabaticScene::Annotation> notes = scene.annotations();
    int matched = 0;
    for (const AdiabaticScene::Annotation& note : notes) {
        if (expected.contains(note.id)) {
            EXPECT_EQ(note.text, expected.value(note.id)) << note.id.toUtf8().constData();
            ++matched;
        }
    }
    // Подписи берут числа из формул модели, а не «на глаз» по рисунку.
    EXPECT_GE(matched, 4);
}

TEST(AdiabaticLabels, ResultAppearsOnlyAfterProcessEnds) {
    AdiabaticModel model;
    configure(model);
    AdiabaticScene scene(&model);

    prepare(scene, model, model.processTime() * 0.5);
    bool shownDuring = false;
    for (const AdiabaticScene::Annotation& note : scene.annotations())
        shownDuring = shownDuring || note.id == QLatin1String("result");
    EXPECT_FALSE(shownDuring);

    prepare(scene, model, model.processTime());
    bool shownAfter = false;
    for (const AdiabaticScene::Annotation& note : scene.annotations()) {
        if (note.id != QLatin1String("result"))
            continue;
        shownAfter = true;
        EXPECT_TRUE(note.text.contains(lab::utils::formatNumber(model.finalPressure() / 1e3, 1)));
        EXPECT_TRUE(note.text.contains(lab::utils::formatNumber(model.finalTemperature(), 1)));
    }
    EXPECT_TRUE(shownAfter);
}

// ------------------------------------------------------------------
// Отрисовка
// ------------------------------------------------------------------

TEST(AdiabaticScene, VesselPistonAndMoleculesAreDrawn) {
    AdiabaticModel model;
    configure(model);
    AdiabaticScene scene(&model);
    prepare(scene, model, model.processTime() * 0.5);

    const QImage image = renderFrame(scene);
    // Поршень — красный, молекулы — фиолетовые штрихи, шкала температур —
    // оранжевая, сила газа — малиновая.
    EXPECT_GT(countColor(image, lab::style::bodyB(), kSchemeTop, kSchemeBottom), 300);
    EXPECT_GT(countColor(image, lab::style::bodyD(), kSchemeTop, kSchemeBottom), 60);
    EXPECT_GT(countColor(image, lab::style::accentWarm(), kSchemeTop, kSchemeBottom), 100);
    EXPECT_GT(countColor(image, QColor(178, 60, 120), kSchemeTop, kSchemeBottom), 30);
}

TEST(AdiabaticScene, VesselIsWiderWithLargerPistonArea) {
    AdiabaticModel narrow;
    configure(narrow);
    AdiabaticScene narrowScene(&narrow);
    prepare(narrowScene, narrow, 0.0);

    AdiabaticModel wide;
    configure(wide);
    wide.setParam(QStringLiteral("S"), 200.0);
    AdiabaticScene wideScene(&wide);
    prepare(wideScene, wide, 0.0);

    // Ширина цилиндра на чертеже следует за площадью поршня.
    EXPECT_GT(wideScene.schemeGeometry().vessel.width(), narrowScene.schemeGeometry().vessel.width() + 1.0);
}

TEST(AdiabaticScene, FourPanelsAreDrawnBelowScheme) {
    AdiabaticModel model;
    configure(model);
    AdiabaticScene scene(&model);
    prepare(scene, model, model.duration() * 0.6);

    const QImage image = renderFrame(scene);
    // p(V) — красный, T(V) — оранжевый, A(t) — зелёный, p(T) — красный.
    EXPECT_GT(countColor(image, lab::style::bodyB(), kPlotsTop, kPlotsBottom), 150);
    EXPECT_GT(countColor(image, lab::style::accentWarm(), kPlotsTop, kPlotsBottom), 150);
    EXPECT_GT(countColor(image, lab::style::bodyC(), kPlotsTop, kPlotsBottom), 100);
}

TEST(AdiabaticScene, TimeAxisMarkerFollowsModelTime) {
    AdiabaticModel model;
    configure(model);
    AdiabaticScene scene(&model);

    prepare(scene, model, model.duration() * 0.05);
    const int early = axisMarkerX(renderFrame(scene));
    prepare(scene, model, model.duration() * 0.9);
    const int late = axisMarkerX(renderFrame(scene));
    ASSERT_GE(early, 0);
    ASSERT_GE(late, 0);
    EXPECT_GT(late - early, 100);
}

TEST(AdiabaticScene, SchemeAndHandlesSurviveInvalidSetup) {
    AdiabaticModel model;
    // Процесс ещё допустим, но объём конца близок к началу: чертёж должен
    // пережить малый ход поршня.
    setSetup(model, QStringLiteral("gas=0; nu=1; T1=293; V1=8; V2=7.9; S=50; p0=101.3; tau=6"));
    ProbeScene scene(&model);
    prepare(scene, model, model.duration() * 0.5);
    scene.rebuildHandles();
    scene.positionHandles();
    EXPECT_EQ(scene.handles().size(), 3);
    EXPECT_FALSE(scene.moleculesFor(model.time()).isEmpty());
    EXPECT_FALSE(scene.vectorsFor(model.time()).isEmpty());

    // Постановка без процесса: сцена не падает и не рисует ничего лишнего.
    AdiabaticModel invalid;
    setSetup(invalid, QStringLiteral("gas=0; nu=1; T1=293; V1=5; V2=5; S=50; p0=101.3; tau=6"));
    AdiabaticScene invalidScene(&invalid);
    prepare(invalidScene, invalid, 1.0);
    EXPECT_TRUE(invalidScene.moleculesFor(invalid.time()).isEmpty());
    EXPECT_TRUE(invalidScene.vectorsFor(invalid.time()).isEmpty());
    EXPECT_TRUE(invalidScene.annotations().isEmpty());
    EXPECT_FALSE(renderFrame(invalidScene).isNull());
}
