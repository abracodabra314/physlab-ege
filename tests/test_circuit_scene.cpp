#include <gtest/gtest.h>

#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <QImage>
#include <QPainter>

#include <algorithm>
#include <cmath>

#include "TestSupport.h"
#include "models/CircuitModel.h"
#include "scenes/CircuitScene.h"
#include "scenes/SchemeLabels.h"
#include "scenes/Style.h"
#include "scenes/VectorArrows.h"
#include "utils/MathText.h"

using lab::circuit::PartType;
using lab::models::CircuitModel;
using lab::scenes::CircuitScene;
using lab::test::setSetup;

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 760;
// Область просмотра как в LabView: снизу остаётся место под ось времени.
constexpr double kViewTop = 12.0;
constexpr double kViewHeight = kHeight - 24.0 - 58.0;

/**
 * Сцена рисует шрифтами и событиями Qt, поэтому QApplication должен
 * существовать до создания первой QGraphicsScene. Если приложение уже
 * создано другим набором тестов, второе не создаётся.
 */
class CircuitGraphicsEnvironment : public ::testing::Environment {
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

const bool kCircuitEnvironmentRegistered = [] {
    ::testing::AddGlobalTestEnvironment(new CircuitGraphicsEnvironment);
    return true;
}();

/** Доступ к защищённой геометрии и мыши сцены. */
class ProbeScene : public CircuitScene {
public:
    using CircuitScene::CircuitScene;
    using CircuitScene::mapHandlePosition;
    using CircuitScene::positionHandles;
    using CircuitScene::timeAxisRect;

    /** Левый клик по точке листа. */
    void leftClick(const QPointF& pos) { dispatch(pos, Qt::LeftButton); }
    void rightClick(const QPointF& pos) { dispatch(pos, Qt::RightButton); }
    void doubleClick(const QPointF& pos) {
        QGraphicsSceneMouseEvent event(QEvent::GraphicsSceneMouseDoubleClick);
        event.setScenePos(pos);
        event.setButton(Qt::LeftButton);
        mouseDoubleClickEvent(&event);
    }

private:
    void dispatch(const QPointF& pos, Qt::MouseButton button) {
        QGraphicsSceneMouseEvent event(QEvent::GraphicsSceneMousePress);
        event.setScenePos(pos);
        event.setButton(button);
        mousePressEvent(&event);
    }
};

/** Схема справочника и сцена, перестроенная под типовое окно. */
void prepare(CircuitModel& model, CircuitScene& scene, int preset, double t = 0.5,
             const QString& assignments = QString()) {
    ASSERT_TRUE(model.applyPreset(preset));
    if (!assignments.isEmpty())
        setSetup(model, assignments);
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));
    model.seekTo(t);
}

/** Отрисовывает кадр сцены и возвращает изображение. */
QImage renderFrame(CircuitScene& scene) {
    QImage image(kWidth, kHeight, QImage::Format_RGB32);
    image.fill(lab::style::background());
    QPainter painter(&image);
    scene.paintBackgroundInto(&painter, QRectF(0, 0, kWidth, kHeight));
    painter.end();
    return image;
}

/** Число пикселей цвета, близкого к заданному, в прямоугольнике. */
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

}  // namespace

// ------------------------------------------------------------------
// Геометрия листа
// ------------------------------------------------------------------

TEST(CircuitSceneGeometry, SheetFitsTheRequestedArea) {
    CircuitModel model;
    CircuitScene scene(&model);
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));

    const CircuitScene::Sheet sheet = scene.sheet();
    EXPECT_GE(sheet.board.left(), 20.0 - 1.0);
    EXPECT_LE(sheet.board.right(), 20.0 + kWidth - 40.0 + 1.0);
    EXPECT_GE(sheet.board.top(), kViewTop - 1.0);
    EXPECT_LE(sheet.board.bottom(), kViewTop + kViewHeight);
    EXPECT_GT(sheet.cell, 4.0);
    EXPECT_EQ(sheet.columns, model.gridColumns());
    EXPECT_EQ(sheet.rows, model.gridRows());

    // Панели умещаются между листом и осью времени.
    EXPECT_GE(sheet.panelsBand.top(), sheet.board.bottom());
    // Полоса панелей доходит до оси времени, которая лежит под областью просмотра.
    EXPECT_LE(sheet.panelsBand.bottom(), kViewHeight + kViewTop + 6.0);
    // Колонка показаний остаётся внутри листа и не налазит на сетку.
    EXPECT_LE(sheet.readouts.right(), sheet.board.right());
    EXPECT_GE(sheet.readouts.left(), sheet.board.left());
}

TEST(CircuitSceneGeometry, GridRoundTripsBetweenCellsAndPoints) {
    CircuitModel model;
    CircuitScene scene(&model);
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));

    for (int column = 0; column <= model.gridColumns(); column += 3) {
        for (int row = 0; row <= model.gridRows(); row += 2) {
            lab::circuit::GridPoint point;
            point.column = column;
            point.row = row;
            const QPointF center = scene.cellCenter(point);
            EXPECT_TRUE(scene.sheet().board.contains(center)) << "клетка " << column << "," << row;
            EXPECT_EQ(scene.cellAt(center), point);
        }
    }
}

TEST(CircuitSceneGeometry, CellsAreSquareAndWiresTurnAtRightAngles) {
    CircuitModel model;
    CircuitScene scene(&model);
    ASSERT_TRUE(model.applyPreset(2));   // параллельно: ветви и шины
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));

    const double cell = scene.sheet().cell;
    const lab::circuit::GridPoint left;
    const lab::circuit::GridPoint right{1, 0};
    // Соседние клетки различаются по координатам одинаково: шаг сетки равный.
    const QPointF a = scene.cellCenter(left);
    const QPointF b = scene.cellCenter(right);
    EXPECT_NEAR(std::abs(b.x() - a.x()), cell, 1e-9);
    EXPECT_NEAR(std::abs(b.y() - a.y()), 0.0, 1e-9);
    const QPointF down = scene.cellCenter(lab::circuit::GridPoint{0, 1});
    EXPECT_NEAR(std::abs(down.y() - a.y()), cell, 1e-9);
    EXPECT_NEAR(std::abs(down.x() - a.x()), 0.0, 1e-9);

    // Провод излома: соседние звенья ломаной перпендикулярны.
    const auto wires = model.net().wires();
    ASSERT_FALSE(wires.isEmpty());
    const QVector<QPointF> path = scene.polyline(wires.first().path);
    ASSERT_GE(path.size(), 2);
    // Каждое звено ломаной идёт по линии сетки: горизонтально или вертикально.
    // Прямой участок и поворот на 90° — единственно возможные здесь изломы.
    for (int i = 1; i < path.size(); ++i) {
        const QPointF link = path.at(size_t(i)) - path.at(size_t(i - 1));
        const bool horizontal = std::abs(link.y()) < 1e-6;
        const bool vertical = std::abs(link.x()) < 1e-6;
        EXPECT_TRUE(horizontal || vertical) << "звено " << i << " идёт наискось";
        EXPECT_NEAR(std::abs(link.x()) + std::abs(link.y()), cell, 1e-6);
    }
}

TEST(CircuitSceneGeometry, SymbolsAndTerminalsLandOnGridPoints) {
    CircuitModel model;
    CircuitScene scene(&model);
    prepare(model, scene, 0);

    for (const lab::circuit::Part& part : model.net().parts()) {
        const QPointF center = scene.symbolCenter(part);
        EXPECT_TRUE(scene.sheet().board.contains(center));
        const auto terminals = lab::circuit::CircuitNet::terminalsOf(part);
        ASSERT_EQ(terminals.size(), 2);
        // Символ стоит ровно посередине между клеммами.
        const QPointF middle = (scene.cellCenter(terminals.first()) + scene.cellCenter(terminals.last())) * 0.5;
        EXPECT_NEAR(center.x(), middle.x(), 1e-6);
        EXPECT_NEAR(center.y(), middle.y(), 1e-6);
        // Клеммы — разные точки сетки.
        EXPECT_NE(terminals.first(), terminals.last());
    }
}

// ------------------------------------------------------------------
// Отрисовка
// ------------------------------------------------------------------

TEST(CircuitScenePaint, DrawsGridWiresAndCarriers) {
    CircuitModel model;
    CircuitScene scene(&model);
    prepare(model, scene, 2, 0.8);
    const QImage frame = renderFrame(scene);
    const CircuitScene::Sheet sheet = scene.sheet();

    // Клетчатая бумага: линии сетки видны.
    const int gridPixels = countColor(frame, lab::style::grid(), int(sheet.board.top()),
                                      int(sheet.board.bottom()), int(sheet.board.left()),
                                      int(sheet.board.right()), 6);
    EXPECT_GT(gridPixels, 400);

    // Провода и символы — чёрные чернила.
    EXPECT_GT(countColor(frame, lab::style::ink(), int(sheet.board.top()), int(sheet.board.bottom()),
                         int(sheet.board.left()), int(sheet.board.right())), 500);

    // Носители заряда и подписи тока — оранжевым по рабочим участкам.
    EXPECT_GT(countColor(frame, lab::style::accentWarm(), int(sheet.board.top()), int(sheet.board.bottom()),
                         int(sheet.board.left()), int(sheet.board.right())), 200);
}

TEST(CircuitScenePaint, PanelsDrawFourGraphs) {
    CircuitModel model;
    CircuitScene scene(&model);
    prepare(model, scene, 4, 0.0);   // заряд конденсатора: процессы на всех рядах
    const QImage frame = renderFrame(scene);
    const CircuitScene::Sheet sheet = scene.sheet();
    ASSERT_GT(sheet.panelsBand.height(), 90.0);

    const int top = int(sheet.panelsBand.top());
    const int bottom = int(sheet.panelsBand.bottom());
    // В полосе панелей есть и кривые (цвет), и подписанные оси (чернила).
    EXPECT_GT(countColor(frame, lab::style::ink(), top, bottom), 400);
    EXPECT_GT(countColor(frame, lab::style::bodyB(), top, bottom), 50);

    // Четыре панели стояят рядом: их кривые попадают в четыре разных четверти
    // полосы, а не в одну.
    const int quarters = 4;
    const double width = sheet.panelsBand.width() / quarters;
    int filled = 0;
    for (int i = 0; i < quarters; ++i) {
        const int left = int(sheet.panelsBand.left() + i * width) + 4;
        const int right = int(sheet.panelsBand.left() + (i + 1) * width) - 4;
        const int drawn = countColor(frame, lab::style::ink(), top, bottom, left, right) +
                          countColor(frame, lab::style::inkSoft(), top, bottom, left, right);
        if (drawn > 150)
            ++filled;
    }
    EXPECT_EQ(filled, quarters);
}

TEST(CircuitScenePaint, ReadoutsListComputedNumbers) {
    CircuitModel model;
    CircuitScene scene(&model);
    prepare(model, scene, 1, 1.0);
    const QImage frame = renderFrame(scene);
    const CircuitScene::Sheet sheet = scene.sheet();

    // Колонка показаний — текст, начерченный чернилами внутри своей рамки.
    EXPECT_GT(countColor(frame, lab::style::inkSoft(), int(sheet.readouts.top()),
                         int(sheet.readouts.bottom()), int(sheet.readouts.left()),
                         int(sheet.readouts.right())), 100);
}

TEST(CircuitScenePaint, InvalidCircuitIsDrawnWithoutCrashing) {
    CircuitModel model;
    CircuitScene scene(&model);
    ASSERT_FALSE(model.applyPreset(CircuitModel::customPresetIndex()));   // пустая сетка
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));
    const QImage frame = renderFrame(scene);
    // Пустой лист остаётся листом: сетка видна, а схема не нарисована.
    const CircuitScene::Sheet sheet = scene.sheet();
    EXPECT_GT(countColor(frame, lab::style::grid(), int(sheet.board.top()), int(sheet.board.bottom()),
                         int(sheet.board.left()), int(sheet.board.right())), 400);
    EXPECT_EQ(model.currentSolution().ok, false);
}

// ------------------------------------------------------------------
// Векторы тока
// ------------------------------------------------------------------

TEST(CircuitSceneVectors, CurrentArrowsAppearOnlyOnConductingBranches) {
    CircuitModel model;
    CircuitScene scene(&model);
    prepare(model, scene, 2, 1.0);
    model.setShowsForces(true);

    const auto arrows = scene.vectorsFor(1.0);
    const int parts = model.net().parts().size();
    EXPECT_GT(arrows.size(), 0);
    EXPECT_LE(arrows.size(), parts);
    for (const lab::vectors::Arrow& arrow : arrows) {
        EXPECT_EQ(arrow.kind, lab::vectors::Kind::Current);
        // Подпись только обозначением величины, без чисел.
        EXPECT_EQ(arrow.symbol, QStringLiteral("I"));
        EXPECT_TRUE(scene.sheet().board.contains(arrow.from));
        EXPECT_TRUE(scene.sheet().board.contains(arrow.to));
        // Стрелка идёт параллельно элементу: по горизонтали или по вертикали.
        const QPointF delta = arrow.to - arrow.from;
        EXPECT_TRUE(std::abs(delta.x()) < 1e-6 || std::abs(delta.y()) < 1e-6);
    }

    // Стрелки тока — часть чертежа: переключатель их не прячет.
    model.setShowsForces(false);
    EXPECT_GT(scene.vectorsFor(1.0).size(), 0);
}

TEST(CircuitSceneVectors, BrokenCircuitHasNoArrows) {
    CircuitModel model;
    CircuitScene scene(&model);
    ASSERT_FALSE(model.applyPreset(CircuitModel::customPresetIndex()));
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));
    model.setShowsForces(true);
    EXPECT_TRUE(scene.vectorsFor(0.5).isEmpty());
}

// ------------------------------------------------------------------
// Мышь: схему делают на листе
// ------------------------------------------------------------------

TEST(CircuitSceneMouse, ClickPlacesThenRotatesThenRemoves) {
    CircuitModel model;
    ProbeScene scene(&model);
    ASSERT_FALSE(model.applyPreset(CircuitModel::customPresetIndex()));
    setSetup(model, QStringLiteral("tool=1"));   // резистор
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));

    const lab::circuit::GridPoint where{6, 3};
    const QPointF position = scene.cellCenter(where);
    scene.leftClick(position);
    ASSERT_EQ(model.net().parts().size(), 1);
    const int id = model.net().partAt(where);
    ASSERT_GT(id, 0);
    EXPECT_EQ(model.selectedPart(), id);
    EXPECT_TRUE(model.net().findPart(id)->horizontal);

    // Второй клик по тому же элементу поворачивает его.
    scene.leftClick(position);
    EXPECT_EQ(model.net().parts().size(), 1);
    EXPECT_FALSE(model.net().findPart(id)->horizontal);

    // Двойной клик тоже поворачивает — обратно.
    scene.doubleClick(position);
    EXPECT_TRUE(model.net().findPart(id)->horizontal);

    // Правый клик убирает элемент со схемы.
    scene.rightClick(position);
    EXPECT_TRUE(model.net().parts().isEmpty());
}

TEST(CircuitSceneMouse, WireToolConnectsTwoTerminals) {
    CircuitModel model;
    ProbeScene scene(&model);
    ASSERT_FALSE(model.applyPreset(CircuitModel::customPresetIndex()));
    setSetup(model, QStringLiteral("tool=1"));
    ASSERT_GT(model.placeAt(lab::circuit::GridPoint{4, 2}), 0);
    ASSERT_GT(model.placeAt(lab::circuit::GridPoint{10, 2}), 0);
    setSetup(model, QStringLiteral("tool=7"));   // провод
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));

    // Первый клик по клемме ещё не соединяет: схема ждёт вторую точку.
    scene.leftClick(scene.cellCenter(lab::circuit::GridPoint{4, 2}));
    EXPECT_TRUE(model.hasPendingTerminal());
    EXPECT_TRUE(model.net().wires().isEmpty());

    scene.leftClick(scene.cellCenter(lab::circuit::GridPoint{10, 2}));
    EXPECT_FALSE(model.hasPendingTerminal());
    EXPECT_FALSE(model.net().wires().isEmpty());
    model.validate();   // расчёт перестраивает узлы топологии
    EXPECT_TRUE(model.net().connected(lab::circuit::GridPoint{4, 2}, lab::circuit::GridPoint{10, 2}));
}

TEST(CircuitSceneMouse, DeleteToolRemovesWhatClickRemoves) {
    CircuitModel model;
    ProbeScene scene(&model);
    prepare(model, scene, 0);
    setSetup(model, QStringLiteral("tool=8"));   // стереть
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));

    const int before = model.net().parts().size();
    ASSERT_GT(before, 1);
    const lab::circuit::GridPoint sourceTerminal{2, 2};
    scene.leftClick(scene.cellCenter(sourceTerminal));
    EXPECT_EQ(model.net().parts().size(), before - 1);
}

TEST(CircuitSceneMouse, TimelineClickRewindsExperiment) {
    CircuitModel model;
    ProbeScene scene(&model);
    prepare(model, scene, 4, 0.0);

    const QRectF axis = scene.timeAxisRect();
    ASSERT_GT(axis.width(), 100.0);
    scene.leftClick(QPointF(axis.center().x(), axis.center().y()));
    EXPECT_NEAR(model.time(), 0.5 * model.duration(), 0.05 * model.duration());

    // Клик вне листа и вне оси остаётся обычной обработкой сцены.
    scene.leftClick(QPointF(scene.sheet().board.left() - 6.0, axis.center().y()));
    EXPECT_GT(model.duration(), 0.0);
}

// ------------------------------------------------------------------
// Ручки
// ------------------------------------------------------------------

TEST(CircuitSceneHandles, ValueHandleTracksTheElementItEdits) {
    CircuitModel model;
    ProbeScene scene(&model);
    prepare(model, scene, 1, 0.5);

    const auto handles = model.handles();
    ASSERT_FALSE(handles.isEmpty());
    scene.positionHandles();
    for (lab::DragHandleItem* item : scene.handles()) {
        const lab::HandleInfo info = item->info();
        if (info.axis == QLatin1String("x"))
            continue;   // временные ручки живут на оси
        EXPECT_TRUE(scene.sheet().board.adjusted(-60, -60, 60, 60).contains(item->pos()))
            << "ручка " << info.id.toUtf8().constData();
    }

    // Ручка «внизу» даёт нижнюю границу диапазона, «вверху» — верхнюю.
    lab::HandleInfo emfHandle;
    for (const lab::HandleInfo& info : handles) {
        if (info.id == QLatin1String("emf"))
            emfHandle = info;
    }
    ASSERT_GT(emfHandle.maxValue, emfHandle.minValue);
    const QPointF anchor = scene.handleAnchorFor(emfHandle);
    EXPECT_NEAR(scene.mapHandlePosition(emfHandle, anchor + QPointF(0.0, -40.0)), emfHandle.maxValue,
                1e-6);
    EXPECT_NEAR(scene.mapHandlePosition(emfHandle, anchor + QPointF(0.0, 40.0)), emfHandle.minValue, 1e-6);
    EXPECT_NEAR(scene.mapHandlePosition(emfHandle, anchor),
                0.5 * (emfHandle.maxValue + emfHandle.minValue), 1e-6);
}

TEST(CircuitSceneHandles, TimeHandlesSitOnTheTimeline) {
    CircuitModel model;
    ProbeScene scene(&model);
    prepare(model, scene, 0, 1.0);
    scene.positionHandles();

    bool sawTimeHandle = false;
    for (const lab::HandleInfo& info : model.handles()) {
        if (info.axis != QLatin1String("x"))
            continue;
        sawTimeHandle = true;
        // Ручку времени ставят на ось, а обратный перевод даёт её же значение.
        const double x = scene.handleScenePosition(info);
        EXPECT_NEAR(x, scene.toSceneX(info.value), 1e-6);
        EXPECT_TRUE(scene.viewArea().adjusted(-2, -2, 2, 2).contains(
            QPointF(x, scene.viewArea().center().y())));
        EXPECT_NEAR(scene.mapHandlePosition(info, QPointF(x, scene.timeAxisRect().center().y())), info.value,
                    1e-6 * std::max(1.0, std::abs(info.value)) + 1e-6);
    }
    EXPECT_TRUE(sawTimeHandle);
}
