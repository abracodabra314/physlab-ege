#include <gtest/gtest.h>

#include <QApplication>
#include <QImage>
#include <QPainter>

#include <algorithm>
#include <cmath>

#include "TestSupport.h"
#include "models/BacteriaModel.h"
#include "scenes/BacteriaScene.h"
#include "scenes/SchemeLabels.h"
#include "scenes/Style.h"
#include "utils/MathText.h"

using lab::models::BacteriaModel;
using lab::scenes::BacteriaScene;
using lab::test::setSetup;

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 760;
// Зазор между панелями графиков — тот же, что в сцене.
constexpr double kPanelGap = 10.0;
// Область просмотра как в LabView: снизу остаётся место под ось времени.
constexpr double kViewTop = 12.0;
constexpr double kViewHeight = kHeight - 24.0 - 58.0;

/**
 * Сцена рисует шрифтами и событиями Qt, поэтому QApplication должен
 * существовать до создания первой QGraphicsScene. Если приложение уже
 * создано другим набором тестов, второе не создаётся.
 */
class BacteriaGraphicsEnvironment : public ::testing::Environment {
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

const bool kBacteriaEnvironmentRegistered = [] {
    ::testing::AddGlobalTestEnvironment(new BacteriaGraphicsEnvironment);
    return true;
}();

/** Учебная постановка: посев 1000, T_d = 2 ч, наблюдение 12 ч, цель 32000. */
void configure(BacteriaModel& model) {
    setSetup(model, QStringLiteral("species=4; doubling=2; N0=1000; T=12; step=1; target=32000"));
}

/** Пересчитывает геометрию сцены и устанавливает время модели. */
void prepare(BacteriaScene& scene, BacteriaModel& model, double t) {
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));
    model.seekTo(t);
}

/** Отрисовывает кадр сцены и возвращает изображение. */
QImage renderFrame(BacteriaScene& scene) {
    QImage image(kWidth, kHeight, QImage::Format_RGB32);
    image.fill(lab::style::background());
    QPainter painter(&image);
    scene.paintBackgroundInto(&painter, QRectF(0, 0, kWidth, kHeight));
    painter.end();
    return image;
}

/** Доступ к защищённым методам сцены. */
class ProbeScene : public BacteriaScene {
public:
    using BacteriaScene::BacteriaScene;
    using BacteriaScene::mapHandlePosition;
    using BacteriaScene::handleScenePosition;
    using BacteriaScene::positionHandles;
    using BacteriaScene::timeAxisRect;
};

/** Клетки первого слоя, упорядоченные по номеру места. */
QVector<BacteriaScene::ColonyCell> firstLayerCells(const QVector<BacteriaScene::ColonyCell>& cells) {
    QVector<BacteriaScene::ColonyCell> result;
    for (const BacteriaScene::ColonyCell& cell : cells) {
        if (cell.layer == 0)
            result.push_back(cell);
    }
    std::sort(result.begin(), result.end(), [](const BacteriaScene::ColonyCell& a, const BacteriaScene::ColonyCell& b) {
        return a.index < b.index;
    });
    return result;
}

/** Число занятых мест рисунка момента t. */
int drawnCellCount(const BacteriaScene& scene, double t) {
    return int(scene.cellsFor(t).size());
}

/** Расстояние клетки рисунка от центра чашки. */
double distanceFromCenter(const BacteriaScene::ColonyCell& cell, const QPointF& center) {
    return std::hypot(cell.center.x() - center.x(), cell.center.y() - center.y());
}

/** Цвета, которыми рисуют кривые и колонии: по ним панели различают. */
QColor paletteColor(int index) {
    switch (index) {
    case 0: return lab::style::bodyA();
    case 1: return lab::style::bodyB();
    case 2: return lab::style::bodyC();
    case 3: return lab::style::bodyD();
    default: return lab::style::accentWarm();
    }
}

/** Пиксель цветной: у серых подписей, сетки и текста каналы близки. */
bool isChromatic(int red, int green, int blue) {
    return std::max({red, green, blue}) - std::min({red, green, blue}) >= 40;
}

/** Номер цвета палитры, ближайшего к пикселю. */
int nearestPaletteIndex(int red, int green, int blue) {
    int best = 0;
    long long bestScore = 0;
    for (int index = 0; index < 5; ++index) {
        const QColor color = paletteColor(index);
        const long long score = (red - color.red()) * 1LL * (red - color.red()) +
                                (green - color.green()) * 1LL * (green - color.green()) +
                                (blue - color.blue()) * 1LL * (blue - color.blue());
        if (index == 0 || score < bestScore) {
            bestScore = score;
            best = index;
        }
    }
    return best;
}

/*
 * Пиксели кривой заданного цвета. Линию рисуют сглаженно, и её края смешаны
 * с белым фоном, поэтому пиксель относят к ближайшему цвету палитры, а не
 * сравнивают с ним напрямую.
 */
int countCurvePixels(const QImage& image, int y0, int y1, int x0, int x1, const QColor& color) {
    const int wanted = nearestPaletteIndex(color.red(), color.green(), color.blue());
    int found = 0;
    for (int y = std::max(0, y0); y < std::min(y1, image.height()); ++y) {
        for (int x = std::max(0, x0); x < std::min(x1, image.width()); ++x) {
            const QRgb rgb = image.pixel(x, y);
            const int red = qRed(rgb), green = qGreen(rgb), blue = qBlue(rgb);
            if (isChromatic(red, green, blue) && nearestPaletteIndex(red, green, blue) == wanted)
                ++found;
        }
    }
    return found;
}

/** Пикселей с содержимым в прямоугольнике панели: кривые, текст, оси. */
int countInk(const QImage& image, int y0, int y1, int x0, int x1) {
    int found = 0;
    for (int y = std::max(0, y0); y < std::min(y1, image.height()); ++y)
        for (int x = std::max(0, x0); x < std::min(x1, image.width()); ++x) {
            const QRgb rgb = image.pixel(x, y);
            if (qRed(rgb) < 160 || qGreen(rgb) < 160 || qBlue(rgb) < 160)
                ++found;
        }
    return found;
}

/** Число пикселей цвета, близкого к заданному, в прямоугольнике [x0, x1) × [y0, y1). */
int countColor(const QImage& image, const QColor& color, int y0, int y1, int x0 = 0, int x1 = -1,
               int tolerance = 24) {
    int found = 0;
    const int right = x1 < 0 ? image.width() : std::min(x1, image.width());
    for (int y = std::max(0, y0); y < std::min(y1, image.height()); ++y) {
        for (int x = std::max(0, x0); x < right; ++x) {
            const QRgb rgb = image.pixel(x, y);
            if (std::abs(qRed(rgb) - color.red()) <= tolerance && std::abs(qGreen(rgb) - color.green()) <= tolerance &&
                std::abs(qBlue(rgb) - color.blue()) <= tolerance)
                ++found;
        }
    }
    return found;
}

/** Строки панелей графиков: между полосой схемы и осью времени. */
QPair<int, int> panelRows(const BacteriaScene& scene) {
    const ProbeScene* probe = dynamic_cast<const ProbeScene*>(&scene);
    const double top = scene.schemeBand().bottom() + 12.0;
    const double bottom = probe ? probe->timeAxisRect().top() - 8.0 : top + 216.0;
    const double height = std::min(bottom - top, 216.0);
    const double offset = top + std::max(0.0, (bottom - top) - height) * 0.5;
    return {int(offset), int(offset + height)};
}

}  // namespace

// ------------------------------------------------------------------
// Чашка: клетки как целый счёт колонии
// ------------------------------------------------------------------

TEST(BacteriaScene, CellCountFollowsWholeCellCountNotTheSmoothCurve) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);
    prepare(scene, model, model.doublingTime() * 1.5);

    const double t = model.time();
    const double per = scene.cellsPerDrawnCell();
    const BacteriaScene::SchemeGeometry geometry = scene.schemeGeometry();

    // Мест рисунка ровно столько, сколько целых клеток насчитал наблюдатель:
    // пока колония не вышла за три слоя кадра, каждое деление добавляет клетки.
    const int needed = int(std::ceil(model.countedCellsAt(t) / per));
    ASSERT_LE(model.overflowFactorAt(t), 3.0);
    EXPECT_EQ(drawnCellCount(scene, t), needed);

    // Число клеток в одной клетке рисунка целое, и оно точно подписано.
    EXPECT_GE(per, 1.0);
    EXPECT_EQ(per, std::round(per));
    EXPECT_NEAR(double(scene.drawnSlots()) * per, geometry.capacityCells, per);
}

TEST(BacteriaScene, CellsOnlyAppearAsTimeGrows) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);
    prepare(scene, model, 0.0);

    const int atStart = drawnCellCount(scene, 0.0);
    EXPECT_GE(atStart, 1);

    int previous = atStart;
    for (double fraction = 0.05; fraction <= 1.001; fraction += 0.05) {
        const double t = model.observationTime() * fraction;
        const QVector<BacteriaScene::ColonyCell> cells = scene.cellsFor(t);
        const int first = int(firstLayerCells(cells).size());
        EXPECT_GE(first, previous) << "момент " << t;
        previous = first;
    }
    EXPECT_GT(previous, atStart);
}

TEST(BacteriaScene, ColonyRadiusGrowsAsSquareRootOfCells) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);
    prepare(scene, model, 0.0);

    const BacteriaScene::SchemeGeometry geometry = scene.schemeGeometry();
    const double capacity = geometry.capacityCells;

    // Пока кадр не заполнен, R = R_кадра·√(N/N_мест).
    for (double fraction = 0.05; fraction <= 0.6; fraction += 0.15) {
        const double t = model.observationTime() * fraction;
        const double overflow = model.theoryCellsAt(t) / capacity;
        ASSERT_LT(overflow, 1.0);
        EXPECT_NEAR(scene.colonyRadiusPx(overflow), (geometry.dishRadius - 6.0) * std::sqrt(overflow), 1e-9)
            << "момент " << t;
    }

    // Тот же закон по самим клеткам: расстояния растут как √N.
    const QVector<BacteriaScene::ColonyCell> early = firstLayerCells(scene.cellsFor(1.0));
    const QVector<BacteriaScene::ColonyCell> late = firstLayerCells(scene.cellsFor(5.0));
    ASSERT_GE(early.size(), 3);
    ASSERT_GE(late.size(), early.size());
    const double scale = std::sqrt(model.theoryCellsAt(5.0) / model.theoryCellsAt(1.0));
    for (int index = 0; index < early.size(); ++index) {
        const double before = distanceFromCenter(early.at(index), geometry.dishCenter);
        const double after = distanceFromCenter(late.at(index), geometry.dishCenter);
        EXPECT_NEAR(after, before * scale, 1e-6) << "место " << index;
    }
}

TEST(BacteriaScene, CellsStayInsideTheDish) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);

    for (double fraction : {0.0, 0.4, 0.8, 1.0}) {
        prepare(scene, model, model.duration() * fraction);
        const BacteriaScene::SchemeGeometry geometry = scene.schemeGeometry();
        const QVector<BacteriaScene::ColonyCell> cells = scene.cellsFor(model.time());
        ASSERT_FALSE(cells.isEmpty());
        for (const BacteriaScene::ColonyCell& cell : cells) {
            EXPECT_GE(cell.center.x(), geometry.dish.left() - 1.0);
            EXPECT_LE(cell.center.x(), geometry.dish.right() + 1.0);
            EXPECT_GE(cell.center.y(), geometry.dish.top() - 1.0);
            EXPECT_LE(cell.center.y(), geometry.dish.bottom() + 1.0);
            EXPECT_GT(cell.length, 1.0);
            EXPECT_GT(cell.width, 0.5);
            EXPECT_LE(cell.length, geometry.dish.width());
        }
    }
}

TEST(BacteriaScene, DishIsACircleInscribedInItsFrame) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);
    prepare(scene, model, 0.0);

    const BacteriaScene::SchemeGeometry geometry = scene.schemeGeometry();
    EXPECT_NEAR(geometry.dish.width(), geometry.dish.height(), 1e-9);
    EXPECT_NEAR(geometry.dishCenter.x(), geometry.dish.center().x(), 1e-9);
    EXPECT_NEAR(geometry.dishCenter.y(), geometry.dish.center().y(), 1e-9);
    EXPECT_GT(geometry.dishRadius, 30.0);
    // Чашка целиком внутри полосы схемы.
    EXPECT_GE(geometry.dish.left(), scene.schemeBand().left() - 0.5);
    EXPECT_LE(geometry.dish.right(), scene.schemeBand().right() + 0.5);
    EXPECT_LE(geometry.dish.bottom(), scene.schemeBand().bottom() + 0.5);
}

TEST(BacteriaScene, FilledDishMatchesTheCapacityRadius) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);
    prepare(scene, model, 0.0);

    const BacteriaScene::SchemeGeometry geometry = scene.schemeGeometry();
    // При N = N_мест кадр заполнен: радиус доходит до края, мест ровно вместимость.
    EXPECT_NEAR(scene.colonyRadiusPx(1.0), geometry.dishRadius - 6.0, 1e-9);
    EXPECT_NEAR(scene.colonyRadiusPx(4.0), scene.colonyRadiusPx(1.0), 1e-9);
    EXPECT_EQ(scene.occupiedSlots(geometry.capacityCells), scene.drawnSlots());
    EXPECT_EQ(scene.occupiedSlots(geometry.capacityCells * 3.0), scene.drawnSlots());
    EXPECT_EQ(scene.occupiedSlots(0.0), 0);

    // Посев занимает лишь часть кадра: радиус колонии меньше края.
    EXPECT_GT(geometry.colonyRadius, 0.0);
    EXPECT_LT(geometry.colonyRadius, scene.colonyRadiusPx(1.0));

    // Момент, когда кадр заполняется, — тот, где N = N_мест.
    const double moment = model.doublingTime() * std::log2(geometry.capacityCells / model.initialCells());
    EXPECT_NEAR(model.overflowFactorAt(moment), 1.0, 1e-9);
    prepare(scene, model, moment);
    EXPECT_NEAR(scene.schemeGeometry().colonyRadius, scene.colonyRadiusPx(1.0), 1e-9);
    EXPECT_EQ(scene.schemeGeometry().occupiedCells, scene.drawnSlots());
}

TEST(BacteriaScene, OverflowAddsLayersWithoutStoppingTheCount) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);
    prepare(scene, model, model.observationTime());

    const double t = model.time();
    ASSERT_GT(model.overflowFactorAt(t), 1.0);

    const QVector<BacteriaScene::ColonyCell> cells = scene.cellsFor(t);
    int layers[4] = {0, 0, 0, 0};
    for (const BacteriaScene::ColonyCell& cell : cells)
        layers[std::min(cell.layer, 3)] += 1;

    // Кадр заполнен — клетки ложатся вторым и третьим слоем.
    EXPECT_GT(layers[0], 0);
    EXPECT_GT(layers[1], 0);
    EXPECT_EQ(layers[0], scene.drawnSlots());
    EXPECT_NEAR(scene.schemeGeometry().layers, model.theoryCellsAt(t) / scene.schemeGeometry().capacityCells, 1e-9);

    // Число клеток модели при этом продолжает удваиваться: теснота рост не
    // останавливает.
    const double earlier = model.observationTime() - model.doublingTime();
    EXPECT_NEAR(model.theoryCellsAt(t) / model.theoryCellsAt(earlier), 2.0, 1e-9);
    EXPECT_GT(model.overflowFactorAt(t), model.overflowFactorAt(earlier));
}

TEST(BacteriaScene, DividingCellsMatchTheLastCompletedIntervalGrowth) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);
    // Середина интервала учёта: помечен последний завершённый подсчёт.
    prepare(scene, model, model.observationTime() * 0.5 + 0.5);

    const double t = model.time();
    const double per = scene.cellsPerDrawnCell();
    double left = 0.0;
    double right = 0.0;
    ASSERT_TRUE(model.countingIntervalBefore(t, left, right));
    ASSERT_LT(right, model.observationTime());
    ASSERT_NEAR(right - left, model.observationInterval(), 1e-9);

    int dividing = 0;
    for (const BacteriaScene::ColonyCell& cell : scene.cellsFor(t))
        dividing += cell.dividing ? 1 : 0;

    /*
     * Помечены ровно те клетки рисунка, что появились между двумя
     * подсчётами: по числу оранжевых палочек видно, какой прирост показал
     * последний интервал.
     */
    const int expected = int(std::ceil(model.countedCellsAt(right) / per)) -
                         int(std::ceil(model.countedCellsAt(left) / per));
    ASSERT_LE(model.overflowFactorAt(t), 3.0);
    EXPECT_GT(expected, 0);
    EXPECT_EQ(dividing, expected);
    EXPECT_NEAR(model.growthInLastStepAt(t), model.countedCellsAt(right) - model.countedCellsAt(left), 1e-9);
}

TEST(BacteriaScene, NoCellIsMarkedDividingBeforeTheFirstCount) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);
    // До первого подсчёта сравнения ещё нет: помечать деления не по чему.
    prepare(scene, model, model.observationInterval() * 0.5);
    double left = 0.0;
    double right = 0.0;
    ASSERT_FALSE(model.countingIntervalBefore(model.time(), left, right));

    for (const BacteriaScene::ColonyCell& cell : scene.cellsFor(model.time()))
        EXPECT_FALSE(cell.dividing) << "место " << cell.index;
}

// ------------------------------------------------------------------
// Шкала опыта: отметки T_d, уровень кадра и уровень цели
// ------------------------------------------------------------------

TEST(BacteriaScene, DoublingMarkMatchesTimeScale) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);
    prepare(scene, model, 0.0);

    const BacteriaScene::SchemeGeometry geometry = scene.schemeGeometry();
    EXPECT_GT(geometry.pixelsPerHour, 0.0);
    EXPECT_NEAR(geometry.doublingPx, geometry.startPx + model.doublingTime() * geometry.pixelsPerHour, 1e-6);
    EXPECT_GT(geometry.doublingPx, geometry.startPx);
    EXPECT_LT(geometry.doublingPx, geometry.observationEndPx);
    EXPECT_LE(geometry.observationEndPx, geometry.endPx);

    // Переводы времени и пикселей согласованы в обе стороны.
    EXPECT_NEAR(scene.sceneXForTime(model.doublingTime()), geometry.doublingPx, 1e-9);
    EXPECT_NEAR(scene.timeForSceneX(geometry.doublingPx), model.doublingTime(), 1e-9);
    EXPECT_NEAR(scene.sceneXForTime(0.0), geometry.startPx, 1e-9);
}

TEST(BacteriaScene, CellAxisLevelsMatchWholeDivisions) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);
    prepare(scene, model, 0.0);

    /*
     * Горизонтальные уровни шкалы стоят на N₀·2^n, а вертикальные — на
     * n·T_d: обе серии соответствуют друг другу, потому что рост идёт
     * делением, а не «на глаз».
     */
    const double n0 = model.initialCells();
    for (int n = 0; n <= 4; ++n) {
        const double t = model.doublingTime() * double(n);
        EXPECT_NEAR(scene.sceneYForCells(n0 * std::exp2(double(n))), scene.sceneYForCells(model.theoryCellsAt(t)), 1e-9)
            << "делений " << n;
        EXPECT_NEAR(scene.sceneXForTime(t), scene.schemeGeometry().startPx + t * scene.schemeGeometry().pixelsPerHour,
                    1e-9);
    }
    // Перевод числа клеток согласован в обе стороны.
    EXPECT_NEAR(scene.cellsForSceneY(scene.sceneYForCells(2000.0)), 2000.0, 1e-9);
    EXPECT_GT(scene.sceneYForCells(n0), scene.sceneYForCells(n0 * 2.0));
}

TEST(BacteriaScene, CapacityLevelSitsWhereTheDishFills) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);
    prepare(scene, model, 0.0);

    const BacteriaScene::SchemeGeometry geometry = scene.schemeGeometry();
    const double capacityY = scene.sceneYForCells(geometry.capacityCells);
    // Уровень заполнения кадра — между верхом шкалы и её нулевой линией.
    EXPECT_GT(capacityY, geometry.cellsTopPy - 1.0);
    EXPECT_LT(capacityY, scene.sceneYForCells(0.0) + 1.0);
    EXPECT_GT(geometry.capacityCells, 0.0);
}

TEST(BacteriaScene, TargetMarkSitsOnTheoreticalCurve) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);
    prepare(scene, model, 0.0);

    const BacteriaScene::SchemeGeometry geometry = scene.schemeGeometry();
    // Отметка цели стоит на её уровне и на моменте T_d·log₂(N/N₀).
    EXPECT_NEAR(geometry.targetPx, scene.sceneXForTime(model.targetTime()), 1e-6);
    EXPECT_NEAR(geometry.targetLevelPy, scene.sceneYForCells(model.targetCells()), 1e-6);
    EXPECT_LT(geometry.targetLevelPy, scene.sceneYForCells(model.initialCells()));

    // Верх шкалы выше всех нанесённых уровней.
    EXPECT_LT(geometry.cellsTopPy, geometry.targetLevelPy);
    EXPECT_LT(geometry.cellsTopPy, scene.sceneYForCells(0.0));
}

TEST(BacteriaScene, CurrentMarkerFollowsModelTime) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);

    prepare(scene, model, model.duration() * 0.1);
    const double early = scene.schemeGeometry().currentPx;
    prepare(scene, model, model.duration() * 0.9);
    const double late = scene.schemeGeometry().currentPx;
    EXPECT_GT(late, early + 50.0);
    EXPECT_NEAR(late, scene.sceneXForTime(model.time()), 1e-9);
}

// ------------------------------------------------------------------
// Векторов в модели деления нет
// ------------------------------------------------------------------

TEST(BacteriaScene, SchemeHasNoVectorsEvenWithAllCategoriesOn) {
    BacteriaModel model;
    configure(model);
    model.setShowsVelocities(true);
    model.setShowsAccelerations(true);
    model.setShowsForces(true);

    BacteriaScene scene(&model);
    prepare(scene, model, model.doublingTime() * 2.0);

    // Колонию не толкают: стрелок на этой сцене не бывает.
    EXPECT_TRUE(scene.vectorsFor(model.time()).isEmpty());
    EXPECT_TRUE(scene.vectorsFor(0.0).isEmpty());
    EXPECT_TRUE(scene.vectorsFor(model.duration()).isEmpty());
}

// ------------------------------------------------------------------
// Подписи
// ------------------------------------------------------------------

TEST(BacteriaScene, AnnotationsDoNotOverlap) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);

    for (double fraction : {0.0, 0.3, 0.55, 1.0}) {
        prepare(scene, model, model.duration() * fraction);
        const QVector<BacteriaScene::Annotation> annotations = scene.annotations();
        ASSERT_GE(annotations.size(), 6);
        for (int i = 0; i < annotations.size(); ++i) {
            for (int j = i + 1; j < annotations.size(); ++j) {
                EXPECT_FALSE(lab::labels::overlaps(annotations.at(i).rect(), annotations.at(j).rect()))
                    << annotations.at(i).id.toUtf8().constData() << " и "
                    << annotations.at(j).id.toUtf8().constData() << " при t = " << model.time();
            }
        }
    }
}

TEST(BacteriaScene, AnnotationsStayInsideSchemeBand) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);
    prepare(scene, model, model.observationTime() * 0.5);

    const QRectF band = scene.schemeBand();
    for (const BacteriaScene::Annotation& annotation : scene.annotations()) {
        EXPECT_GE(annotation.rect().left(), band.left() - 1.0) << annotation.id.toUtf8().constData();
        EXPECT_LE(annotation.rect().right(), band.right() + 1.0) << annotation.id.toUtf8().constData();
        EXPECT_GE(annotation.rect().top(), band.top() - 1.0) << annotation.id.toUtf8().constData();
        EXPECT_LE(annotation.rect().bottom(), band.bottom() + 1.0) << annotation.id.toUtf8().constData();
    }
}

TEST(BacteriaScene, AnnotationsCarryModelNumbers) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);
    prepare(scene, model, model.observationTime() * 0.5);

    QString all;
    for (const BacteriaScene::Annotation& annotation : scene.annotations())
        all += annotation.id + QStringLiteral(" ") + annotation.text;

    // На чертеже — обозначения и формулы деления; чисел-величин нет.
    EXPECT_TRUE(all.contains(QStringLiteral("N = N₀·2")));
    EXPECT_TRUE(all.contains(QStringLiteral("T_d")));
    EXPECT_TRUE(all.contains(QStringLiteral("N_мест")));
    auto carriesValue = [](const QString& text) {
        int run = 0;
        for (const QChar symbol : text) {
            if (symbol.unicode() >= '0' && symbol.unicode() <= '9') {
                ++run;
                if (run >= 2)
                    return true;
            } else if (symbol != QLatin1Char(',')) {
                run = 0;
            }
        }
        return false;
    };
    for (const BacteriaScene::Annotation& annotation : scene.annotations())
        EXPECT_FALSE(carriesValue(annotation.text)) << annotation.id.toUtf8().constData();
}

TEST(BacteriaScene, AnnotationsSayThatOverflowDoesNotStopGrowth) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);
    prepare(scene, model, model.observationTime());
    ASSERT_GT(model.overflowFactorAt(model.time()), 1.0);

    QString all;
    for (const BacteriaScene::Annotation& annotation : scene.annotations())
        all += annotation.text;
    // Переполнение описано словами: рост не останавливается, числа — в панели.
    EXPECT_TRUE(all.contains(QStringLiteral("рост")));
}

TEST(BacteriaScene, NarrowSchemeKeepsAnnotationsSeparate) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);
    // Узкая область просмотра: подписи должны ужаться, а не наползать.
    scene.reconfigure(QRectF(20.0, kViewTop, 560.0, 520.0));
    model.seekTo(model.observationTime() * 0.5);

    const QVector<BacteriaScene::Annotation> annotations = scene.annotations();
    ASSERT_GE(annotations.size(), 5);
    for (int i = 0; i < annotations.size(); ++i) {
        for (int j = i + 1; j < annotations.size(); ++j)
            EXPECT_FALSE(lab::labels::overlaps(annotations.at(i).rect(), annotations.at(j).rect()))
                << annotations.at(i).id.toUtf8().constData() << " и " << annotations.at(j).id.toUtf8().constData();
    }
    // Клетки при этом остаются в чашке своего масштаба.
    const BacteriaScene::SchemeGeometry geometry = scene.schemeGeometry();
    for (const BacteriaScene::ColonyCell& cell : scene.cellsFor(model.time())) {
        EXPECT_GE(cell.center.x(), geometry.dish.left() - 1.0);
        EXPECT_LE(cell.center.x(), geometry.dish.right() + 1.0);
    }
    EXPECT_FALSE(renderFrame(scene).isNull());
}

// ------------------------------------------------------------------
// Ручки
// ------------------------------------------------------------------

TEST(BacteriaScene, HandlesMapBetweenPixelsAndModelValues) {
    BacteriaModel model;
    configure(model);
    ProbeScene scene(&model);
    prepare(scene, model, 0.0);
    scene.rebuildHandles();
    scene.positionHandles();

    ASSERT_EQ(scene.handles().size(), 4);
    for (const lab::HandleInfo& info : model.handles()) {
        const double pixel = scene.handleScenePosition(info);
        EXPECT_TRUE(std::isfinite(pixel)) << info.id.toUtf8().constData();
        // Обратный перевод позиции ручки даёт то же значение: у посева и цели
        // шкала вертикальная, у времени — горизонтальная.
        const bool vertical = info.id == QLatin1String("n0") || info.id == QLatin1String("target");
        const QPointF position = vertical ? QPointF(0.0, pixel) : QPointF(pixel, 0.0);
        EXPECT_NEAR(scene.mapHandlePosition(info, position), info.value, 1.0) << info.id.toUtf8().constData();
    }
}

TEST(BacteriaScene, HandlesSitOnTheirMarks) {
    BacteriaModel model;
    configure(model);
    ProbeScene scene(&model);
    prepare(scene, model, 0.0);
    scene.rebuildHandles();
    scene.positionHandles();

    const BacteriaScene::SchemeGeometry geometry = scene.schemeGeometry();
    for (const lab::DragHandleItem* handle : scene.handles()) {
        if (handle->id() == QLatin1String("doubling")) {
            EXPECT_NEAR(handle->anchor().x(), geometry.doublingPx, 1e-6);
            EXPECT_NEAR(handle->anchor().y(), geometry.rulerY + 12.0, 1e-6);
        }
        if (handle->id() == QLatin1String("window"))
            EXPECT_NEAR(handle->anchor().x(), geometry.observationEndPx, 1.0);
        if (handle->id() == QLatin1String("n0"))
            EXPECT_NEAR(handle->anchor().y(), scene.sceneYForCells(model.initialCells()), 1.0);
        if (handle->id() == QLatin1String("target"))
            EXPECT_NEAR(handle->anchor().y(), geometry.targetLevelPy, 1.0);
    }
}

TEST(BacteriaScene, DraggingDoublingHandleRescalesTheScheme) {
    BacteriaModel model;
    configure(model);
    ProbeScene scene(&model);
    prepare(scene, model, 0.0);

    const double before = scene.schemeGeometry().doublingPx;
    model.setHandleValue(QStringLiteral("doubling"), 4.0);
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));

    // Тот же опыт с вдвое медленнее делящейся культурой: отметка T_d уезжает
    // вправо, а постоянная роста уменьшается вдвое.
    EXPECT_NEAR(model.doublingTime(), 4.0, 1e-9);
    EXPECT_GT(scene.schemeGeometry().doublingPx, before + 20.0);
    EXPECT_NEAR(model.growthRate(), 0.6931471805599453 / 4.0, 1e-12);
    EXPECT_FALSE(scene.cellsFor(model.time()).empty());
}

TEST(BacteriaScene, DraggingTargetHandleMovesTheTargetLevel) {
    BacteriaModel model;
    configure(model);
    ProbeScene scene(&model);
    prepare(scene, model, 0.0);

    const double before = scene.schemeGeometry().targetLevelPy;
    model.setHandleValue(QStringLiteral("target"), 64000.0);
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));

    // Цель выше по шкале: делений нужно больше, момент дальше вперёд.
    EXPECT_NEAR(scene.schemeGeometry().targetLevelPy, scene.sceneYForCells(64000.0), 1e-6);
    EXPECT_LT(scene.schemeGeometry().targetLevelPy, before);
    EXPECT_NEAR(model.targetTime(), model.doublingTime() * 6.0, 1e-9);
}

TEST(BacteriaScene, DraggingSeedHandleKeepsTheColonyConsistent) {
    BacteriaModel model;
    configure(model);
    ProbeScene scene(&model);
    prepare(scene, model, model.doublingTime() * 2.0);

    model.setHandleValue(QStringLiteral("n0"), 8000.0);
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));

    const double per = scene.cellsPerDrawnCell();
    EXPECT_NEAR(model.initialCells(), 8000.0, 1e-9);
    const int needed = int(std::ceil(model.countedCellsAt(model.time()) / per));
    ASSERT_LE(model.overflowFactorAt(model.time()), 3.0);
    EXPECT_EQ(drawnCellCount(scene, model.time()), needed);
    // Кадр при этом заполнен: рисунок доходит до края чашки.
    EXPECT_GE(scene.schemeGeometry().colonyRadius, scene.colonyRadiusPx(1.0) - 1e-9);
}

// ------------------------------------------------------------------
// Отрисовка
// ------------------------------------------------------------------

TEST(BacteriaScene, ColonyAndDishAreRendered) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);
    prepare(scene, model, model.observationTime() * 0.5);

    const QImage image = renderFrame(scene);
    const BacteriaScene::SchemeGeometry geometry = scene.schemeGeometry();
    const int left = int(geometry.dish.left());
    const int right = int(geometry.dish.right()) + 1;
    const int top = int(geometry.dish.top());
    const int bottom = int(geometry.dish.bottom()) + 1;

    // Клетки колонии — зелёные: их видно внутри чашки.
    EXPECT_GT(countColor(image, lab::style::bodyC(), top, bottom, left, right), 400);
    // За пределами чашки зелёного нет: клетки не «высыпаются» на поле.
    EXPECT_LT(countColor(image, lab::style::bodyC(), top, bottom, int(scene.schemeBand().left()), left - 12), 20);
}

TEST(BacteriaScene, OverflowCracksAppearOnlyWhenTheDishIsFull) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);

    // До заполнения кадра трещин нет.
    prepare(scene, model, 0.0);
    ASSERT_LT(model.overflowFactorAt(model.time()), 1.0);
    const BacteriaScene::SchemeGeometry empty = scene.schemeGeometry();
    const QImage before = renderFrame(scene);
    const int left = int(empty.dish.left());
    const int right = int(empty.dish.right()) + 1;
    const int top = int(empty.dish.top());
    const int bottom = int(empty.dish.bottom()) + 1;
    const int dryRed = countColor(before, lab::style::danger(), top, bottom, left, right, 34);

    // После заполнения стекло трескается: красных пикселей заметно больше.
    prepare(scene, model, model.observationTime());
    ASSERT_GT(model.overflowFactorAt(model.time()), 1.0);
    const QImage after = renderFrame(scene);
    const int wetRed = countColor(after, lab::style::danger(), top, bottom, left, right, 34);
    EXPECT_GT(wetRed, dryRed + 20);
}

TEST(BacteriaScene, FourPanelsAreDrawnBelowScheme) {
    BacteriaModel model;
    configure(model);
    ProbeScene scene(&model);
    prepare(scene, model, model.duration() * 0.6);

    const QImage image = renderFrame(scene);
    const QPair<int, int> rows = panelRows(scene);
    ASSERT_GT(rows.second - rows.first, 90);

    /*
     * Четыре панели стоят рядом по горизонтали. Каждую проверяют своим
     * цветом: кривые рисуют сглаженно, и пиксели их краёв смешаны с фоном,
     * поэтому пиксель относят к ближайшему цвету палитры.
     */
    const QRectF band = scene.schemeBand();
    const double panelWidth = (band.width() - 3.0 * kPanelGap) / 4.0;
    auto panelLeft = [&](int index) { return int(band.left() + index * (panelWidth + kPanelGap)) + 6; };
    auto panelRight = [&](int index) { return int(band.left() + index * (panelWidth + kPanelGap) + panelWidth) - 6; };

    for (int index = 0; index < 4; ++index) {
        // В каждой панели есть кривая и подписи: пустых панелей быть не должно.
        EXPECT_GT(countInk(image, rows.first, rows.second, panelLeft(index), panelRight(index)), 900)
            << "панель " << index;
    }

    // 1 — клетки: красная кривая теории и синяя ступенька делений.
    EXPECT_GT(countCurvePixels(image, rows.first, rows.second, panelLeft(0), panelRight(0), lab::style::bodyB()), 60);
    EXPECT_GT(countCurvePixels(image, rows.first, rows.second, panelLeft(0), panelRight(0), lab::style::bodyA()), 100);
    // 2 — прирост по подсчётам: фиолетовые столбцы ΔN.
    EXPECT_GT(countCurvePixels(image, rows.first, rows.second, panelLeft(1), panelRight(1), lab::style::bodyD()), 300);
    // 3 — ln N: зелёная прямая.
    EXPECT_GT(countCurvePixels(image, rows.first, rows.second, panelLeft(2), panelRight(2), lab::style::bodyC()), 300);
    // 4 — N′(N): синяя прямая.
    EXPECT_GT(countCurvePixels(image, rows.first, rows.second, panelLeft(3), panelRight(3), lab::style::bodyA()), 300);
}

TEST(BacteriaScene, GrowthBandShowsBarsPerCountingInterval) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);
    prepare(scene, model, model.observationTime());

    const QImage image = renderFrame(scene);
    const QRectF band = scene.schemeGeometry().growthBand;
    ASSERT_GT(band.height(), 10.0);
    // Прирост по интервалам учёта рисуется фиолетовыми столбцами.
    EXPECT_GT(countColor(image, lab::style::bodyD(), int(band.top()), int(band.bottom()) + 1), 60);
}

TEST(BacteriaScene, SteppedDivisionsAreDrawnOnTheScheme) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);
    prepare(scene, model, model.observationTime() * 0.8);

    const QImage image = renderFrame(scene);
    const BacteriaScene::SchemeGeometry geometry = scene.schemeGeometry();
    const int left = int(geometry.strip.left());
    const int right = int(geometry.strip.right());
    const int top = int(geometry.strip.top());
    const int bottom = int(geometry.strip.bottom()) + 1;

    // Рядом с красной кривой идёт синяя ступенька завершённых делений.
    EXPECT_GT(countColor(image, lab::style::bodyB(), top, bottom, left, right), 60);
    EXPECT_GT(countColor(image, lab::style::bodyA(), top, bottom, left, right), 30);
    // Отметка текущего момента — оранжевая.
    EXPECT_GT(countColor(image, lab::style::accentWarm(), top, bottom, left, right), 20);
}

TEST(BacteriaScene, TimeAxisMarkerFollowsModelTime) {
    BacteriaModel model;
    configure(model);
    BacteriaScene scene(&model);

    prepare(scene, model, model.duration() * 0.05);
    const int early = int(scene.sceneXForTime(model.time()));
    prepare(scene, model, model.duration() * 0.9);
    const int late = int(scene.sceneXForTime(model.time()));
    EXPECT_GT(late - early, 100);
}

// ------------------------------------------------------------------
// Другие постановки
// ------------------------------------------------------------------

TEST(BacteriaScene, SlowSpeciesKeepsSchemeReadable) {
    BacteriaModel model;
    // Туберкулёзная палочка: T_d = 13 ч, наблюдение 78 ч.
    setSetup(model, QStringLiteral("species=3; N0=100; T=78; step=6; target=6400"));
    ASSERT_FALSE(lab::test::isInvalid(model));

    BacteriaScene scene(&model);
    prepare(scene, model, model.observationTime() * 0.5);

    const QVector<BacteriaScene::ColonyCell> cells = scene.cellsFor(model.time());
    ASSERT_FALSE(cells.empty());
    // За половину опыта делений немного: кадр ещё далёк до заполнения.
    EXPECT_LT(model.overflowFactorAt(model.time()), 4.0);

    const QVector<BacteriaScene::Annotation> annotations = scene.annotations();
    ASSERT_FALSE(annotations.isEmpty());
    for (int i = 0; i < annotations.size(); ++i)
        for (int j = i + 1; j < annotations.size(); ++j)
            EXPECT_FALSE(lab::labels::overlaps(annotations.at(i).rect(), annotations.at(j).rect()))
                << annotations.at(i).id.toUtf8().constData() << " и " << annotations.at(j).id.toUtf8().constData();
    EXPECT_FALSE(renderFrame(scene).isNull());
}

TEST(BacteriaScene, SmallSeedStillShowsEveryDivision) {
    BacteriaModel model;
    // Посев из одной клетки: деления видно по клеткам рисунка.
    setSetup(model, QStringLiteral("species=4; doubling=1; N0=1; T=8; step=1; target=128"));
    ASSERT_FALSE(lab::test::isInvalid(model));

    BacteriaScene scene(&model);
    prepare(scene, model, 0.0);
    const int atStart = drawnCellCount(scene, 0.0);
    EXPECT_GE(atStart, 1);

    prepare(scene, model, model.doublingTime() * 4.0);
    const int grown = drawnCellCount(scene, model.time());
    // Клеток рисунка стало больше ровно в 2^4 раз (пока хватает мест).
    const double per = scene.cellsPerDrawnCell();
    EXPECT_NEAR(model.countedCellsAt(model.time()), std::exp2(4.0), 1.0);
    EXPECT_EQ(grown, int(std::ceil(model.countedCellsAt(model.time()) / per)));
    EXPECT_GE(grown, atStart);
}

TEST(BacteriaScene, SchemeSurvivesInvalidSetup) {
    BacteriaModel model;
    // 96 ч при T_d = 15 мин: число клеток не помещается никуда.
    setSetup(model, QStringLiteral("species=4; doubling=0.25; N0=100000; T=96; step=1; target=32000"));
    ASSERT_TRUE(lab::test::isInvalid(model));

    BacteriaScene scene(&model);
    prepare(scene, model, model.duration() * 0.5);

    EXPECT_TRUE(scene.cellsFor(model.time()).empty());
    EXPECT_TRUE(scene.vectorsFor(model.time()).isEmpty());
    // Колонии нет, и радиус не рисуется.
    EXPECT_DOUBLE_EQ(scene.schemeGeometry().colonyRadius, 0.0);
    EXPECT_DOUBLE_EQ(scene.schemeGeometry().occupiedCells, 0);
    const QVector<BacteriaScene::Annotation> annotations = scene.annotations();
    ASSERT_FALSE(annotations.isEmpty());
    EXPECT_FALSE(annotations.first().text.isEmpty());
    EXPECT_FALSE(renderFrame(scene).isNull());
}

TEST(BacteriaScene, DefaultSetupIsTheTeachingCase) {
    // Постановка по умолчанию: именно её видно при первом открытии модели.
    BacteriaModel model;
    ASSERT_FALSE(lab::test::isInvalid(model));
    EXPECT_GT(model.observationTime(), 3.0 * model.doublingTime());

    BacteriaScene scene(&model);
    prepare(scene, model, 0.0);
    const BacteriaScene::SchemeGeometry geometry = scene.schemeGeometry();
    EXPECT_GT(geometry.doublingPx, geometry.startPx);
    EXPECT_LT(geometry.doublingPx * 2.0, geometry.endPx + geometry.startPx);
    EXPECT_FALSE(scene.cellsFor(0.0).empty());
    // За наблюдение кадр успевает переполниться: слои видны на опыте.
    EXPECT_GT(model.overflowFactorAt(model.observationTime()), 1.0);
}
