#include <gtest/gtest.h>

#include <QApplication>
#include <QImage>
#include <QPainter>

#include <algorithm>
#include <cmath>

#include "TestSupport.h"
#include "models/DecayModel.h"
#include "scenes/DecayScene.h"
#include "scenes/SchemeLabels.h"
#include "scenes/Style.h"
#include "utils/MathText.h"

using lab::models::DecayModel;
using lab::scenes::DecayScene;
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
class DecayGraphicsEnvironment : public ::testing::Environment {
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

const bool kDecayEnvironmentRegistered = [] {
    ::testing::AddGlobalTestEnvironment(new DecayGraphicsEnvironment);
    return true;
}();

/** Учебная постановка: 1000 ядер, T½ = 8 ч, наблюдение 40 ч, доля 25 %. */
void configure(DecayModel& model) {
    setSetup(model, QStringLiteral("isotope=7; half=8; N0=1000; T=40; share=25; seed=7"));
}

/** Пересчитывает геометрию сцены и устанавливает время модели. */
void prepare(DecayScene& scene, DecayModel& model, double t) {
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));
    model.seekTo(t);
}

/** Отрисовывает кадр сцены и возвращает изображение. */
QImage renderFrame(DecayScene& scene) {
    QImage image(kWidth, kHeight, QImage::Format_RGB32);
    image.fill(lab::style::background());
    QPainter painter(&image);
    scene.paintBackgroundInto(&painter, QRectF(0, 0, kWidth, kHeight));
    painter.end();
    return image;
}

/** Доступ к защищённым методам сцены. */
class ProbeScene : public DecayScene {
public:
    using DecayScene::DecayScene;
    using DecayScene::mapHandlePosition;
    using DecayScene::handleScenePosition;
    using DecayScene::positionHandles;
    using DecayScene::timeAxisRect;
};

const DecayScene::NucleusCell* cellByIndex(const QVector<DecayScene::NucleusCell>& cells, int index) {
    for (const DecayScene::NucleusCell& cell : cells) {
        if (cell.index == index)
            return &cell;
    }
    return nullptr;
}

/** Число целых клеток в решётке момента t. */
int aliveCells(const DecayScene& scene, double t) {
    int alive = 0;
    for (const DecayScene::NucleusCell& cell : scene.nucleiGridFor(t))
        alive += cell.alive ? 1 : 0;
    return alive;
}

/** Пары целых клеток с одинаковыми координатами: порядок обхода фиксирован. */
QVector<bool> alivePattern(const DecayScene& scene, double t) {
    QVector<bool> pattern;
    const QVector<DecayScene::NucleusCell> cells = scene.nucleiGridFor(t);
    pattern.reserve(cells.size());
    for (const DecayScene::NucleusCell& cell : cells)
        pattern.push_back(cell.alive);
    return pattern;
}

/** Число пикселей цвета, близкого к заданному, в прямоугольнике [x0, x1) × [y0, y1). */
int countColor(const QImage& image, const QColor& color, int y0, int y1, int x0 = 0, int x1 = -1,
               int tolerance = 24) {
    int found = 0;
    const int right = x1 < 0 ? image.width() : std::min(x1, image.width());
    for (int y = std::max(0, y0); y < std::min(y1, image.height()); ++y) {
        for (int x = std::max(0, x0); x < right; ++x) {
            const QRgb rgb = image.pixel(x, y);
            if (std::abs(qRed(rgb) - color.red()) <= tolerance &&
                std::abs(qGreen(rgb) - color.green()) <= tolerance &&
                std::abs(qBlue(rgb) - color.blue()) <= tolerance)
                ++found;
        }
    }
    return found;
}

/** Строки панелей графиков: между полосой схемы и осью времени. */
QPair<int, int> panelRows(const DecayScene& scene) {
    const ProbeScene* probe = dynamic_cast<const ProbeScene*>(&scene);
    const double top = scene.schemeBand().bottom() + 12.0;
    const double bottom = probe ? probe->timeAxisRect().top() - 8.0 : top + 216.0;
    const double height = std::min(bottom - top, 216.0);
    const double offset = top + std::max(0.0, (bottom - top) - height) * 0.5;
    return {int(offset), int(offset + height)};
}

}  // namespace

// ------------------------------------------------------------------
// Образец: клетки как дискретный счёт
// ------------------------------------------------------------------

TEST(DecayScene, NucleiGridMatchesCounterNotTheory) {
    DecayModel model;
    configure(model);
    DecayScene scene(&model);
    prepare(scene, model, model.halfLife());

    const double t = model.time();
    const double per = double(scene.schemeGeometry().cellsPerNucleus);
    const int alive = aliveCells(scene, t);

    // Клеток ровно столько, сколько ядер показал счётчик, а не гладкая кривая.
    EXPECT_EQ(alive, int(std::ceil(model.countedNucleiAt(t) / per)));
    EXPECT_NE(model.countedNucleiAt(t), model.theoryNucleiAt(t));

    // Клетки покрывают все целые ядра и не добавляют лишней: расшифровка
    // «1 клетка = N ядер» остаётся точной.
    EXPECT_GE(per, 1.0);
    EXPECT_GE(alive * per, model.countedNucleiAt(t));
    EXPECT_LT(alive * per, model.countedNucleiAt(t) + per);
}

TEST(DecayScene, CellsOnlyTurnEmptyAsTimeGrows) {
    DecayModel model;
    configure(model);
    DecayScene scene(&model);
    prepare(scene, model, 0.0);

    const QVector<DecayScene::NucleusCell> first = scene.nucleiGridFor(0.0);
    ASSERT_FALSE(first.isEmpty());
    for (const DecayScene::NucleusCell& cell : first)
        EXPECT_TRUE(cell.alive);

    // Клетки гаснут и никогда не загораются: порядок обхода фиксирован.
    int previousAlive = int(first.size());
    for (double fraction = 0.05; fraction <= 1.001; fraction += 0.05) {
        const double t = model.observationTime() * fraction;
        const int alive = aliveCells(scene, t);
        EXPECT_LE(alive, previousAlive) << "момент " << t;
        previousAlive = alive;
    }
    EXPECT_LE(previousAlive, int(first.size()) / 4);
}

TEST(DecayScene, CellOrderIsFixedForSameSeed) {
    DecayModel first;
    configure(first);
    DecayModel second;
    configure(second);

    DecayScene firstScene(&first);
    DecayScene secondScene(&second);
    prepare(firstScene, first, first.halfLife());
    prepare(secondScene, second, second.halfLife());

    const QVector<bool> left = alivePattern(firstScene, first.time());
    const QVector<bool> right = alivePattern(secondScene, second.time());
    ASSERT_EQ(left.size(), right.size());
    EXPECT_EQ(left, right);

    // Геометрия клеток одинакова при одинаковой постановке.
    const QVector<DecayScene::NucleusCell> cells = firstScene.nucleiGridFor(first.time());
    for (const DecayScene::NucleusCell& cell : cells) {
        const DecayScene::NucleusCell* other = cellByIndex(secondScene.nucleiGridFor(second.time()), cell.index);
        ASSERT_NE(other, nullptr);
        EXPECT_TRUE(cell.rect.isValid());
        EXPECT_NEAR(cell.rect.center().x(), other->rect.center().x(), 1e-9);
    }
}

TEST(DecayScene, CellsStayInsideSampleFrame) {
    DecayModel model;
    configure(model);
    DecayScene scene(&model);
    prepare(scene, model, model.observationTime() * 0.5);

    const DecayScene::SchemeGeometry geometry = scene.schemeGeometry();
    ASSERT_GT(geometry.sample.width(), 40.0);
    const QVector<DecayScene::NucleusCell> cells = scene.nucleiGridFor(model.time());
    ASSERT_FALSE(cells.isEmpty());
    for (const DecayScene::NucleusCell& cell : cells) {
        EXPECT_GE(cell.rect.left(), geometry.sample.left() - 0.5);
        EXPECT_LE(cell.rect.right(), geometry.sample.right() + 0.5);
        EXPECT_GE(cell.rect.top(), geometry.sample.top() - 0.5);
        EXPECT_LE(cell.rect.bottom(), geometry.sample.bottom() + 0.5);
        EXPECT_GT(cell.rect.width(), 1.0);
        EXPECT_GT(cell.rect.height(), 1.0);
    }
}

TEST(DecayScene, SampleKeepsReadableNumberOfCells) {
    for (double nuclei : {50.0, 300.0, 1000.0, 5000.0}) {
        DecayModel model;
        setSetup(model, QStringLiteral("isotope=7; half=8; N0=%1; T=40; share=25; seed=7").arg(nuclei));
        DecayScene scene(&model);
        prepare(scene, model, model.halfLife());

        const int per = scene.schemeGeometry().cellsPerNucleus;
        const QVector<DecayScene::NucleusCell> cells = scene.nucleiGridFor(model.time());
        // Клеток достаточно, чтобы распад шёл поодиночке, и они различимы.
        EXPECT_GE(cells.size(), 16) << "N0 = " << nuclei;
        EXPECT_LE(cells.size(), 200) << "N0 = " << nuclei;
        EXPECT_GE(per, 1) << "N0 = " << nuclei;
        // Число клеток покрывает все ядра образца.
        EXPECT_GE(double(cells.size()) * double(per), nuclei) << "N0 = " << nuclei;
    }
}

TEST(DecayScene, FreshCellsMatchDecaysOfCurrentStep) {
    DecayModel model;
    configure(model);
    DecayScene scene(&model);
    prepare(scene, model, model.halfLife());

    const double t = model.time();
    int fresh = 0;
    for (const DecayScene::NucleusCell& cell : scene.nucleiGridFor(t))
        fresh += cell.justDecayed ? 1 : 0;

    // Вспышка отмечает ровно те клетки, что погасли на текущем шаге наблюдения.
    const double per = double(scene.schemeGeometry().cellsPerNucleus);
    const double before = model.countedNucleiAt(t - model.sampleInterval());
    const double now = model.countedNucleiAt(t);
    EXPECT_EQ(fresh, int(std::ceil(before / per)) - int(std::ceil(now / per)));
    EXPECT_GT(before - now, 0.0);
}

// ------------------------------------------------------------------
// Шкала опыта: отметки T½, доли и уровень заданной доли
// ------------------------------------------------------------------

TEST(DecayScene, HalfLifeMarkMatchesTimeScale) {
    DecayModel model;
    configure(model);
    DecayScene scene(&model);
    prepare(scene, model, 0.0);

    const DecayScene::SchemeGeometry geometry = scene.schemeGeometry();
    EXPECT_GT(geometry.pixelsPerHour, 0.0);
    EXPECT_NEAR(geometry.halfLifePx, geometry.startPx + model.halfLife() * geometry.pixelsPerHour, 1e-6);
    EXPECT_GT(geometry.halfLifePx, geometry.startPx);
    EXPECT_LT(geometry.halfLifePx, geometry.observationEndPx);
    // Конец наблюдения — на шкале раньше конца показа (есть «хвост»).
    EXPECT_LE(geometry.observationEndPx, geometry.endPx);

    // Переводы времени и пикселей согласованы в обе стороны.
    EXPECT_NEAR(scene.sceneXForTime(model.halfLife()), geometry.halfLifePx, 1e-9);
    EXPECT_NEAR(scene.timeForSceneX(geometry.halfLifePx), model.halfLife(), 1e-9);
    EXPECT_NEAR(scene.sceneXForTime(0.0), geometry.startPx, 1e-9);
}

TEST(DecayScene, TargetMarkSitsOnTheoreticalCurve) {
    DecayModel model;
    configure(model);
    DecayScene scene(&model);
    prepare(scene, model, 0.0);

    const DecayScene::SchemeGeometry geometry = scene.schemeGeometry();
    // Отметка доли d стоит на её уровне и на моменте ln(1/d)/λ.
    EXPECT_NEAR(geometry.targetPx, scene.sceneXForTime(model.targetTime()), 1e-6);
    EXPECT_NEAR(geometry.targetLevelPy, scene.sceneYForNuclei(model.targetFraction() * model.initialNuclei()), 1e-6);
    // Уровень d·N₀ лежит между верхом шкалы (N₀) и её нулевой линией.
    EXPECT_GT(geometry.targetLevelPy, geometry.nucleiTopPy - 1.0);
    EXPECT_LT(geometry.targetLevelPy, scene.sceneYForNuclei(0.0) + 1.0);

    // Уровень N₀ — верх шкалы, уровень нуля — её низ.
    EXPECT_NEAR(geometry.nucleiTopPy, scene.sceneYForNuclei(model.initialNuclei()), 1e-9);
    EXPECT_NEAR(scene.nucleiForSceneY(scene.sceneYForNuclei(250.0)), 250.0, 1e-9);
}

TEST(DecayScene, CurrentMarkerFollowsModelTime) {
    DecayModel model;
    configure(model);
    DecayScene scene(&model);

    prepare(scene, model, model.duration() * 0.1);
    const double early = scene.schemeGeometry().currentPx;
    prepare(scene, model, model.duration() * 0.9);
    const double late = scene.schemeGeometry().currentPx;
    EXPECT_GT(late, early + 50.0);
    EXPECT_NEAR(late, scene.sceneXForTime(model.time()), 1e-9);
}

TEST(DecayScene, ScaleMarksHalfFractionsAtWholeHalfLives) {
    DecayModel model;
    configure(model);
    DecayScene scene(&model);
    prepare(scene, model, 0.0);

    // Через n периодов полураспада остаётся 1/2^n ядер: доли и уровни шкалы
    // отвечают друг другу.
    const double n0 = model.initialNuclei();
    for (int periods = 1; periods <= 4; ++periods) {
        const double t = model.halfLife() * periods;
        if (t > model.duration())
            break;
        EXPECT_NEAR(scene.sceneYForNuclei(n0 / std::ldexp(1.0, periods)),
                    scene.sceneYForNuclei(model.theoryNucleiAt(t)), 1e-9)
            << "периодов: " << periods;
    }
}

// ------------------------------------------------------------------
// Векторов в модели распада нет
// ------------------------------------------------------------------

TEST(DecayScene, SchemeHasNoVectorsEvenWithAllCategoriesOn) {
    DecayModel model;
    configure(model);
    model.setShowsVelocities(true);
    model.setShowsAccelerations(true);
    model.setShowsForces(true);

    DecayScene scene(&model);
    prepare(scene, model, model.halfLife());

    // Распад — не движение и не сила: стрелок на этой сцене не бывает.
    EXPECT_TRUE(scene.vectorsFor(model.time()).isEmpty());
    EXPECT_TRUE(scene.vectorsFor(0.0).isEmpty());
    EXPECT_TRUE(scene.vectorsFor(model.duration()).isEmpty());
}

// ------------------------------------------------------------------
// Подписи
// ------------------------------------------------------------------

TEST(DecayScene, AnnotationsDoNotOverlap) {
    DecayModel model;
    configure(model);
    DecayScene scene(&model);

    for (double fraction : {0.0, 0.3, 0.55, 1.0}) {
        prepare(scene, model, model.duration() * fraction);
        const QVector<DecayScene::Annotation> annotations = scene.annotations();
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

TEST(DecayScene, AnnotationsStayInsideSchemeBand) {
    DecayModel model;
    configure(model);
    DecayScene scene(&model);
    prepare(scene, model, model.observationTime() * 0.5);

    const QRectF band = scene.schemeBand();
    for (const DecayScene::Annotation& annotation : scene.annotations()) {
        EXPECT_GE(annotation.rect().left(), band.left() - 1.0) << annotation.id.toUtf8().constData();
        EXPECT_LE(annotation.rect().right(), band.right() + 1.0) << annotation.id.toUtf8().constData();
        EXPECT_GE(annotation.rect().top(), band.top() - 1.0) << annotation.id.toUtf8().constData();
        EXPECT_LE(annotation.rect().bottom(), band.bottom() + 1.0) << annotation.id.toUtf8().constData();
    }
}

TEST(DecayScene, AnnotationsCarryModelNumbers) {
    DecayModel model;
    configure(model);
    DecayScene scene(&model);
    prepare(scene, model, model.observationTime() * 0.5);

    QString all;
    for (const DecayScene::Annotation& annotation : scene.annotations())
        all += annotation.id + QStringLiteral(" ") + annotation.text;

    // Постоянная распада, период полураспада, момент доли и число ядер.
    EXPECT_TRUE(all.contains(model.decayConstantText()));
    EXPECT_TRUE(all.contains(DecayModel::durationText(model.halfLife())));
    EXPECT_TRUE(all.contains(DecayModel::durationText(model.targetTime())));
    EXPECT_TRUE(all.contains(DecayModel::nucleiText(model.countedNucleiAt(model.time()))));
    EXPECT_TRUE(all.contains(DecayModel::nucleiText(model.theoryNucleiAt(model.time()))));
    EXPECT_TRUE(all.contains(DecayModel::durationText(model.sampleInterval())));
    EXPECT_TRUE(all.contains(model.stepProbabilityText()));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.activityAt(model.time()), 1)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.expectedDeviationAt(model.time()), 1)));
}

TEST(DecayScene, AnnotationsUseOnlyModelQuantities) {
    DecayModel model;
    configure(model);
    DecayScene scene(&model);
    prepare(scene, model, model.halfLife());

    // Заданная доля и её момент показаны так же, как их считает модель.
    QString all;
    for (const DecayScene::Annotation& annotation : scene.annotations())
        all += annotation.text;
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.targetFraction() * 100.0, 0) + QStringLiteral(" %")));
    EXPECT_TRUE(all.contains(DecayModel::nucleiText(model.theoryNucleiAt(model.targetTime()))));

    // Счёт и теория показаны раздельно: их числа различаются.
    EXPECT_TRUE(all.contains(DecayModel::nucleiText(std::round(model.countedNucleiAt(model.time())))));
    EXPECT_TRUE(all.contains(DecayModel::nucleiText(std::round(model.theoryNucleiAt(model.time())))));
}

// ------------------------------------------------------------------
// Ручки
// ------------------------------------------------------------------

TEST(DecayScene, HandlesMapBetweenPixelsAndModelValues) {
    DecayModel model;
    configure(model);
    ProbeScene scene(&model);
    prepare(scene, model, 0.0);
    scene.rebuildHandles();
    scene.positionHandles();

    ASSERT_EQ(scene.handles().size(), 4);
    for (const lab::HandleInfo& info : model.handles()) {
        const double pixel = scene.handleScenePosition(info);
        EXPECT_TRUE(std::isfinite(pixel)) << info.id.toUtf8().constData();
        // Обратный перевод позиции ручки даёт то же значение: у ручек числа
        // ядер и доли шкала вертикальная, у времени — горизонтальная.
        const bool vertical = info.id == QLatin1String("nuclei") || info.id == QLatin1String("share");
        const QPointF position = vertical ? QPointF(0.0, pixel) : QPointF(pixel, 0.0);
        EXPECT_NEAR(scene.mapHandlePosition(info, position), info.value, 1.0) << info.id.toUtf8().constData();
    }
}

TEST(DecayScene, HalfLifeHandleSitsOnTheRuler) {
    DecayModel model;
    configure(model);
    ProbeScene scene(&model);
    prepare(scene, model, 0.0);
    scene.rebuildHandles();
    scene.positionHandles();

    const DecayScene::SchemeGeometry geometry = scene.schemeGeometry();
    for (const lab::DragHandleItem* handle : scene.handles()) {
        if (handle->id() == QLatin1String("half")) {
            EXPECT_NEAR(handle->anchor().x(), geometry.halfLifePx, 1e-6);
            EXPECT_NEAR(handle->anchor().y(), geometry.rulerY + 12.0, 1e-6);
        }
        if (handle->id() == QLatin1String("window"))
            EXPECT_NEAR(handle->anchor().x(), geometry.observationEndPx, 1.0);
        if (handle->id() == QLatin1String("nuclei"))
            EXPECT_NEAR(handle->anchor().y(), geometry.nucleiTopPy, 1.0);
        if (handle->id() == QLatin1String("share"))
            EXPECT_NEAR(handle->anchor().y(), geometry.targetLevelPy, 1.0);
    }
}

TEST(DecayScene, DraggingHalfLifeHandleRescalesTheScheme) {
    DecayModel model;
    configure(model);
    ProbeScene scene(&model);
    prepare(scene, model, 0.0);

    const double before = scene.schemeGeometry().halfLifePx;
    model.setHandleValue(QStringLiteral("half"), 16.0);
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));

    // Тот же опыт с вдвое большим периодом полураспада: отметка T½ уезжает
    // вправо, а постоянная распада уменьшается вдвое.
    EXPECT_NEAR(model.halfLife(), 16.0, 1e-9);
    EXPECT_GT(scene.schemeGeometry().halfLifePx, before + 20.0);
    EXPECT_NEAR(model.decayConstant(), 0.6931471805599453 / 16.0, 1e-12);
}

TEST(DecayScene, DraggingNucleiHandleKeepsGridConsistent) {
    DecayModel model;
    configure(model);
    ProbeScene scene(&model);
    prepare(scene, model, model.halfLife());

    model.setHandleValue(QStringLiteral("nuclei"), 3000.0);
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));

    const double per = double(scene.schemeGeometry().cellsPerNucleus);
    const int alive = aliveCells(scene, model.time());
    EXPECT_NEAR(model.initialNuclei(), 3000.0, 1e-9);
    EXPECT_EQ(alive, int(std::ceil(model.countedNucleiAt(model.time()) / per)));
    EXPECT_LE(alive, 200);
}

TEST(DecayScene, FractionHandleMovesTheTargetLevel) {
    DecayModel model;
    configure(model);
    ProbeScene scene(&model);
    prepare(scene, model, 0.0);

    const double before = scene.schemeGeometry().targetLevelPy;
    model.setHandleValue(QStringLiteral("share"), 50.0);
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));

    // Доля 50 % — уровень N₀/2, то есть выше прежнего уровня 25 %.
    EXPECT_NEAR(scene.schemeGeometry().targetLevelPy,
                scene.sceneYForNuclei(0.5 * model.initialNuclei()), 1e-6);
    EXPECT_LT(scene.schemeGeometry().targetLevelPy, before);
    EXPECT_NEAR(model.targetTime(), model.halfLife(), 1e-9);
}

// ------------------------------------------------------------------
// Отрисовка
// ------------------------------------------------------------------

TEST(DecayScene, SampleAndSchemeAreRendered) {
    DecayModel model;
    configure(model);
    DecayScene scene(&model);
    prepare(scene, model, model.observationTime() * 0.5);

    const QImage image = renderFrame(scene);
    const QRectF band = scene.schemeBand();
    const int top = int(band.top());
    const int bottom = int(band.bottom());

    // Ядра синие, дочерние ядра зелёные: обе популяции видны в образце.
    EXPECT_GT(countColor(image, lab::style::bodyA(), top, bottom), 400);
    EXPECT_GT(countColor(image, lab::style::bodyC(), top, bottom), 20);
    // Кривая теории — красный ряд, отметка текущего момента — оранжевая.
    EXPECT_GT(countColor(image, lab::style::bodyB(), top, bottom), 60);
    EXPECT_GT(countColor(image, lab::style::accentWarm(), top, bottom), 20);
}

TEST(DecayScene, FourPanelsAreDrawnBelowScheme) {
    DecayModel model;
    configure(model);
    ProbeScene scene(&model);
    prepare(scene, model, model.duration() * 0.6);

    const QImage image = renderFrame(scene);
    const QPair<int, int> rows = panelRows(scene);
    ASSERT_GT(rows.second - rows.first, 90);

    // Четыре панели стоят рядом по горизонтали: у каждой свой цвет кривой,
    // поэтому проверяют каждую полосу отдельно.
    const QRectF band = scene.schemeBand();
    const double panelWidth = (band.width() - 3.0 * kPanelGap) / 4.0;
    const int top = rows.first;
    const int bottom = rows.second;
    auto panelLeft = [&](int index) { return int(band.left() + index * (panelWidth + kPanelGap)) + 6; };
    auto panelRight = [&](int index) { return int(band.left() + index * (panelWidth + kPanelGap) + panelWidth) - 6; };

    // 1 — ядра (красный теоретический ряд), 2 — распавшиеся (зелёный счёт),
    // 3 — активность (оранжевый), 4 — ln N (фиолетовый наклон).
    EXPECT_GT(countColor(image, lab::style::bodyB(), top, bottom, panelLeft(0), panelRight(0)), 60);
    EXPECT_GT(countColor(image, lab::style::bodyC(), top, bottom, panelLeft(1), panelRight(1)), 60);
    EXPECT_GT(countColor(image, lab::style::accentWarm(), top, bottom, panelLeft(2), panelRight(2)), 60);
    EXPECT_GT(countColor(image, lab::style::bodyD(), top, bottom, panelLeft(3), panelRight(3)), 60);
}

TEST(DecayScene, CounterBandShowsDecayBars) {
    DecayModel model;
    configure(model);
    DecayScene scene(&model);
    prepare(scene, model, model.observationTime());

    const QImage image = renderFrame(scene);
    const QRectF band = scene.schemeGeometry().counterBand;
    ASSERT_GT(band.height(), 10.0);
    // Распады по интервалам наблюдения рисуются фиолетовыми столбцами.
    EXPECT_GT(countColor(image, lab::style::bodyD(), int(band.top()), int(band.bottom()) + 1), 60);
}

TEST(DecayScene, TimeAxisMarkerFollowsModelTime) {
    DecayModel model;
    configure(model);
    DecayScene scene(&model);

    prepare(scene, model, model.duration() * 0.05);
    const int early = int(scene.sceneXForTime(model.time()));
    prepare(scene, model, model.duration() * 0.9);
    const int late = int(scene.sceneXForTime(model.time()));
    EXPECT_GT(late - early, 100);
}

// ------------------------------------------------------------------
// Другие постановки
// ------------------------------------------------------------------

TEST(DecayScene, LongLivingIsotopeKeepsSchemeReadable) {
    DecayModel model;
    // Радон: период полураспада дольше наблюдения — распад почти линейный.
    setSetup(model, QStringLiteral("isotope=2; N0=1000; share=25; seed=7"));
    ASSERT_FALSE(lab::test::isInvalid(model));

    DecayScene scene(&model);
    prepare(scene, model, model.observationTime() * 0.5);

    const QVector<DecayScene::NucleusCell> cells = scene.nucleiGridFor(model.time());
    ASSERT_FALSE(cells.isEmpty());
    // За опыт распадается малая часть ядер: большинство клеток цело.
    EXPECT_GT(aliveCells(scene, model.time()), cells.size() / 2);

    // Отметки T½ на шкале нет — период полураспада за её пределы уходит,
    // но подписи остаются на своих местах и не наползают друг на друга.
    const QVector<DecayScene::Annotation> annotations = scene.annotations();
    ASSERT_FALSE(annotations.isEmpty());
    for (int i = 0; i < annotations.size(); ++i)
        for (int j = i + 1; j < annotations.size(); ++j)
            EXPECT_FALSE(lab::labels::overlaps(annotations.at(i).rect(), annotations.at(j).rect()))
                << annotations.at(i).id.toUtf8().constData() << " и " << annotations.at(j).id.toUtf8().constData();
    EXPECT_FALSE(renderFrame(scene).isNull());
}

TEST(DecayScene, NarrowSchemeKeepsAnnotationsSeparate) {
    DecayModel model;
    configure(model);
    DecayScene scene(&model);
    // Узкая область просмотра: подписи должны ужаться, а не наползать.
    scene.reconfigure(QRectF(20.0, kViewTop, 560.0, 520.0));
    model.seekTo(model.observationTime() * 0.5);

    const QVector<DecayScene::Annotation> annotations = scene.annotations();
    ASSERT_GE(annotations.size(), 5);
    for (int i = 0; i < annotations.size(); ++i) {
        for (int j = i + 1; j < annotations.size(); ++j)
            EXPECT_FALSE(lab::labels::overlaps(annotations.at(i).rect(), annotations.at(j).rect()))
                << annotations.at(i).id.toUtf8().constData() << " и " << annotations.at(j).id.toUtf8().constData();
    }
    EXPECT_FALSE(renderFrame(scene).isNull());
}

TEST(DecayScene, SchemeSurvivesInvalidSetup) {
    DecayModel model;
    // Шаг наблюдения крупнее периода полураспада: расчёт не определён.
    setSetup(model, QStringLiteral("isotope=7; half=0.5; N0=1000; T=240; share=25; seed=7"));
    ASSERT_TRUE(lab::test::isInvalid(model));

    DecayScene scene(&model);
    prepare(scene, model, model.duration() * 0.5);

    EXPECT_TRUE(scene.nucleiGridFor(model.time()).isEmpty());
    EXPECT_TRUE(scene.vectorsFor(model.time()).isEmpty());
    const QVector<DecayScene::Annotation> annotations = scene.annotations();
    ASSERT_FALSE(annotations.isEmpty());
    EXPECT_FALSE(annotations.first().text.isEmpty());
    EXPECT_FALSE(renderFrame(scene).isNull());
}

TEST(DecayScene, DefaultSetupIsTheTeachingCase) {
    // Постановка по умолчанию: именно её видно при первом открытии модели,
    // и в ней должно быть видно несколько периодов полураспада.
    DecayModel model;
    ASSERT_FALSE(lab::test::isInvalid(model));
    EXPECT_GT(model.observationTime(), 3.0 * model.halfLife());
    EXPECT_NEAR(model.halfLivesElapsedAt(model.observationTime()), 5.0, 1e-9);

    DecayScene scene(&model);
    prepare(scene, model, 0.0);
    const DecayScene::SchemeGeometry geometry = scene.schemeGeometry();
    EXPECT_GT(geometry.halfLifePx, geometry.startPx);
    EXPECT_LT(geometry.halfLifePx * 2.0, geometry.endPx + geometry.startPx);
    EXPECT_FALSE(scene.nucleiGridFor(0.0).isEmpty());
}
