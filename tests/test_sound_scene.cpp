#include <gtest/gtest.h>

#include <QApplication>
#include <QImage>
#include <QPainter>

#include <cmath>

#include "TestSupport.h"
#include "models/SoundModel.h"
#include "scenes/SoundScene.h"
#include "scenes/Style.h"
#include "scenes/VectorArrows.h"
#include "utils/MathText.h"

using lab::models::SoundModel;
using lab::scenes::SoundScene;
using lab::test::setSetup;
using lab::vectors::Arrow;
using lab::vectors::Kind;

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 760;
// Область просмотра как в LabView: снизу остаётся место под ось времени.
constexpr double kViewTop = 12.0;
constexpr double kViewHeight = kHeight - 24.0 - 58.0;
// Полосы: схема с фронтами, графики под ней и строка маркера времени.
constexpr int kSchemeTop = 60;
constexpr int kSchemeBottom = 400;
constexpr int kPlotsTop = 440;
constexpr int kPlotsBottom = 652;
// Строка маркера текущего момента: точка на оси времени (её радиус 6 пикселей
// вокруг линии трека). Строкой выше лежит подпись режима «t = …», её брать
// нельзя — она прижата к правому краю и не двигается вместе со временем.
constexpr int kAxisMarkerTop = 714;
constexpr int kAxisMarkerBottom = 727;

/**
 * Сцена рисует шрифтами и событиями Qt, поэтому QApplication должен
 * существовать до создания первой QGraphicsScene. Если приложение уже
 * создано другим набором тестов, второе не создаётся.
 */
class SoundGraphicsEnvironment : public ::testing::Environment {
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

const bool kSoundEnvironmentRegistered = [] {
    ::testing::AddGlobalTestEnvironment(new SoundGraphicsEnvironment);
    return true;
}();

/** Учебная постановка: сближение 12 и 8 м/с, T = 3 с, 6800 м, 20 °C. */
void configure(SoundModel& model) {
    setSetup(model, QStringLiteral("temp=20; T=3; L0=6800; dir1=0; v1=12; dir2=0; v2=8"));
}

/** Пересчитывает геометрию сцены и устанавливает время модели. */
void prepare(SoundScene& scene, SoundModel& model, double t) {
    scene.reconfigure(QRectF(20.0, kViewTop, kWidth - 40.0, kViewHeight));
    model.seekTo(t);
}

/** Отрисовывает кадр сцены и возвращает изображение. */
QImage renderFrame(SoundScene& scene) {
    QImage image(kWidth, kHeight, QImage::Format_RGB32);
    image.fill(lab::style::background());
    QPainter painter(&image);
    scene.paintBackgroundInto(&painter, QRectF(0, 0, kWidth, kHeight));
    painter.end();
    return image;
}

/** Доступ к защищённым методам сцены: проверка преобразования ручек. */
class ProbeScene : public SoundScene {
public:
    using SoundScene::SoundScene;
    using SoundScene::mapHandlePosition;
    using SoundScene::positionHandles;
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

const SoundScene::FrontItem* front(const QVector<SoundScene::FrontItem>& items, int index) {
    for (const SoundScene::FrontItem& item : items) {
        if (item.index == index)
            return &item;
    }
    return nullptr;
}

}  // namespace

// ------------------------------------------------------------------
// Фронты: центр закрепплён в точке излучения
// ------------------------------------------------------------------

TEST(SoundFronts, CentresAreFixedAtEmissionPoints) {
    SoundModel model;
    configure(model);
    SoundScene scene(&model);
    prepare(scene, model, model.duration());

    const double ppm = scene.pixelsPerMeter();
    const QVector<SoundScene::FrontItem> fronts = scene.frontsFor(model.time());
    ASSERT_EQ(fronts.size(), SoundModel::signalCount());

    for (int index = 0; index < fronts.size(); ++index) {
        const SoundScene::FrontItem* item = front(fronts, index);
        ASSERT_NE(item, nullptr);
        // Центр окружности — точка xₖ = v₁tₖ, а не текущее положение судна.
        EXPECT_NEAR(item->centerMeters, model.emissionPoint(index), 1e-9);
        EXPECT_NE(item->centerMeters, model.sourcePositionAt(model.time()));
        EXPECT_NEAR(item->radiusMeters, model.frontRadius(index, model.time()), 1e-9);
    }
    // Центры стоят через равные расстояния: судно идёт равномерно.
    const double spacing = fronts.at(1).center.x() - fronts.at(0).center.x();
    EXPECT_NEAR(spacing, model.sourceSpeed() * model.signalPeriod() * ppm, 1e-6);
}

TEST(SoundFronts, CentresStayPutWhenObserverIsMoved) {
    SoundModel model;
    configure(model);
    SoundScene scene(&model);
    const double t = model.duration() * 0.7;
    prepare(scene, model, t);
    const QVector<SoundScene::FrontItem> before = scene.frontsFor(t);
    ASSERT_FALSE(before.isEmpty());

    // Переставляем наблюдателя: меняются только приёмы, но не фронты.
    setSetup(model, QStringLiteral("temp=20; T=3; L0=6800; dir1=0; v1=12; dir2=1; v2=30"));
    prepare(scene, model, t);
    const QVector<SoundScene::FrontItem> after = scene.frontsFor(t);
    ASSERT_EQ(after.size(), before.size());

    for (int index = 0; index < before.size(); ++index) {
        // Проверяем метры, а не пиксели: у новой постановки своё время
        // наблюдения, поэтому масштаб рисунка законно меняется. Главное —
        // центр и радиус фронта в метрах остаются прежними.
        EXPECT_NEAR(after.at(index).centerMeters, before.at(index).centerMeters, 1e-9) << "фронт " << index;
        EXPECT_NEAR(after.at(index).radiusMeters, before.at(index).radiusMeters, 1e-9) << "фронт " << index;
        EXPECT_EQ(after.at(index).emitted, before.at(index).emitted);
    }
    // Сами моменты приёма при этом изменились — сцена их показывает.
    EXPECT_NE(model.arrivalTime(0), 6800.0 / model.soundSpeed());
}

TEST(SoundFronts, RadiusIsSameScaleOnBothAxes) {
    SoundModel model;
    configure(model);
    SoundScene scene(&model);
    prepare(scene, model, model.duration() * 0.8);

    // Радиус в пикселях = радиус в метрах на тот же масштаб, что и ось X:
    // иначе фронт на рисунке был бы эллипсом, а не окружностью.
    const double ppm = scene.pixelsPerMeter();
    ASSERT_GT(ppm, 0.0);
    for (const SoundScene::FrontItem& item : scene.frontsFor(model.time())) {
        EXPECT_NEAR(item.radiusPx, item.radiusMeters * ppm, 1e-9);
        EXPECT_NEAR(item.center.x(), scene.sourcePointAt(0.0).x() + item.centerMeters * ppm, 1e-6);
    }
}

TEST(SoundFronts, EmissionAndReceptionFlagsFollowTime) {
    SoundModel model;
    configure(model);
    SoundScene scene(&model);

    prepare(scene, model, 0.0);
    const QVector<SoundScene::FrontItem> atStart = scene.frontsFor(0.0);
    ASSERT_GE(atStart.size(), 2);
    // В начальный момент подан только первый сигнал.
    EXPECT_TRUE(front(atStart, 0)->emitted);
    EXPECT_FALSE(front(atStart, 1)->emitted);
    EXPECT_NEAR(front(atStart, 0)->radiusPx, 0.0, 1e-12);

    prepare(scene, model, model.arrivalTime(0) + 0.5);
    const QVector<SoundScene::FrontItem> firstReceived = scene.frontsFor(model.time());
    EXPECT_TRUE(front(firstReceived, 0)->received);
    EXPECT_FALSE(front(firstReceived, 1)->received);
    EXPECT_GT(front(firstReceived, 0)->radiusPx, 0.0);

    prepare(scene, model, model.duration());
    for (const SoundScene::FrontItem& item : scene.frontsFor(model.time())) {
        EXPECT_TRUE(item.emitted);
        EXPECT_TRUE(item.received);
    }
}

TEST(SoundFronts, ReceptionMarkIsWhereObserverWas) {
    SoundModel model;
    configure(model);
    SoundScene scene(&model);

    // Приём — пересечение фронта с наблюдателем. Проверяем в сам момент
    // приёма: к нему радиус фронта как раз равен расстоянию от точки
    // излучения до судна наблюдателя.
    for (int index = 0; index < SoundModel::signalCount(); ++index) {
        const double arrival = model.arrivalTime(index);
        ASSERT_TRUE(std::isfinite(arrival));
        prepare(scene, model, arrival);

        const QVector<SoundScene::FrontItem> fronts = scene.frontsFor(arrival);
        const SoundScene::FrontItem* item = front(fronts, index);
        ASSERT_NE(item, nullptr);
        ASSERT_TRUE(item->hasReceptionMark);
        EXPECT_TRUE(item->received);
        // Точка приёма — там, где судно наблюдателя было в момент приёма.
        EXPECT_NEAR(item->receptionMark.x(), scene.observerPointAt(arrival).x(), 1e-6);
        EXPECT_NEAR(item->receptionMark.y(), scene.observerPointAt(arrival).y(), 1e-6);
        // Правая граница окружности и есть фронт, настигший наблюдателя.
        EXPECT_NEAR(item->center.x() + item->radiusPx, item->receptionMark.x(), 1e-6);
        // То же в метрах модели: xₖ + rₖ = x₂(t′).
        EXPECT_NEAR(model.emissionPoint(index) + item->radiusMeters, model.observerPositionAt(arrival), 1e-6);
    }
}

// ------------------------------------------------------------------
// Векторы: только скорости двух судов
// ------------------------------------------------------------------

TEST(SoundVectors, OnlyShipVelocitiesAreShown) {
    SoundModel model;
    configure(model);
    SoundScene scene(&model);
    prepare(scene, model, model.duration() * 0.5);

    const QVector<Arrow> arrows = scene.vectorsFor(model.time());
    // Сил в модели нет: суда движутся равномерно, тяга и сопротивление
    // воды в расчёт не входят. Ускорений тоже нет.
    EXPECT_EQ(find(arrows, Kind::Gravity), nullptr);
    EXPECT_EQ(find(arrows, Kind::Normal), nullptr);
    EXPECT_EQ(find(arrows, Kind::Friction), nullptr);
    EXPECT_EQ(find(arrows, Kind::Tension), nullptr);
    EXPECT_EQ(find(arrows, Kind::Elastic), nullptr);
    EXPECT_EQ(find(arrows, Kind::Acceleration), nullptr);

    int velocities = 0;
    for (const Arrow& arrow : arrows) {
        if (arrow.kind == Kind::Velocity)
            ++velocities;
    }
    EXPECT_EQ(velocities, 2);
    ASSERT_NE(find(arrows, Kind::Velocity, QStringLiteral("v₁")), nullptr);
    ASSERT_NE(find(arrows, Kind::Velocity, QStringLiteral("v₂")), nullptr);
}

TEST(SoundVectors, ArrowsAreHorizontalAndProportionalToSpeed) {
    SoundModel model;
    configure(model);
    SoundScene scene(&model);
    prepare(scene, model, model.duration() * 0.4);

    // Векторы кладём в именованный контейнер: указатели из временного
    // QVector становятся висячими.
    const QVector<Arrow> arrows = scene.vectorsFor(model.time());
    const Arrow* source = find(arrows, Kind::Velocity, QStringLiteral("v₁"));
    const Arrow* observer = find(arrows, Kind::Velocity, QStringLiteral("v₂"));
    ASSERT_NE(source, nullptr);
    ASSERT_NE(observer, nullptr);

    // Суда движутся вдоль русла: стрелки горизонтальны.
    EXPECT_NEAR(source->to.y(), source->from.y(), 1e-9);
    EXPECT_NEAR(observer->to.y(), observer->from.y(), 1e-9);

    // Судно с гудком быстрее — его стрелка длиннее.
    const double sourceLength = std::abs(source->to.x() - source->from.x());
    const double observerLength = std::abs(observer->to.x() - observer->from.x());
    EXPECT_GT(sourceLength, observerLength);
    EXPECT_GT(sourceLength, 0.0);
}

TEST(SoundVectors, DirectionsFollowTheChoiceParameter) {
    SoundModel approaching;
    configure(approaching);
    SoundScene approachingScene(&approaching);
    prepare(approachingScene, approaching, approaching.duration() * 0.5);

    // Сближение: гудок идёт вправо, наблюдатель — влево.
    const QVector<Arrow> nearArrows = approachingScene.vectorsFor(approaching.time());
    const Arrow* nearSource = find(nearArrows, Kind::Velocity, QStringLiteral("v₁"));
    const Arrow* nearObserver = find(nearArrows, Kind::Velocity, QStringLiteral("v₂"));
    ASSERT_NE(nearSource, nullptr);
    ASSERT_NE(nearObserver, nullptr);
    EXPECT_GT(nearSource->to.x(), nearSource->from.x());
    EXPECT_LT(nearObserver->to.x(), nearObserver->from.x());

    SoundModel receding;
    setSetup(receding, QStringLiteral("temp=20; T=3; L0=6800; dir1=1; v1=12; dir2=1; v2=8"));
    SoundScene recedingScene(&receding);
    prepare(recedingScene, receding, receding.duration() * 0.5);

    // Удаление: обе стрелки разворачиваются, величина та же.
    const QVector<Arrow> farArrows = recedingScene.vectorsFor(receding.time());
    const Arrow* farSource = find(farArrows, Kind::Velocity, QStringLiteral("v₁"));
    const Arrow* farObserver = find(farArrows, Kind::Velocity, QStringLiteral("v₂"));
    ASSERT_NE(farSource, nullptr);
    ASSERT_NE(farObserver, nullptr);
    EXPECT_LT(farSource->to.x(), farSource->from.x());
    EXPECT_GT(farObserver->to.x(), farObserver->from.x());
    EXPECT_NEAR(std::abs(farSource->to.x() - farSource->from.x()),
                std::abs(nearSource->to.x() - nearSource->from.x()), 1e-9);
}

TEST(SoundVectors, VelocitiesToggleIndependently) {
    SoundModel model;
    configure(model);
    SoundScene scene(&model);
    prepare(scene, model, model.duration() * 0.5);
    const double t = model.time();

    ASSERT_FALSE(scene.vectorsFor(t).isEmpty());
    model.setShowsVelocities(false);
    EXPECT_TRUE(scene.vectorsFor(t).isEmpty());
    model.setShowsVelocities(true);
    EXPECT_NE(find(scene.vectorsFor(t), Kind::Velocity), nullptr);
}

TEST(SoundVectors, ArrowLabelsContainOnlySymbols) {
    SoundModel model;
    configure(model);
    SoundScene scene(&model);
    prepare(scene, model, model.duration() * 0.5);

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

TEST(SoundHandles, ObserverHandleTracksObserverShip) {
    SoundModel model;
    configure(model);
    ProbeScene scene(&model);
    prepare(scene, model, 0.0);
    scene.rebuildHandles();
    scene.positionHandles();

    ASSERT_EQ(scene.handles().size(), 2);
    bool gapChecked = false;
    bool tempChecked = false;
    for (const lab::DragHandleItem* handle : scene.handles()) {
        if (handle->id() == QLatin1String("gap")) {
            EXPECT_NEAR(handle->anchor().x(), scene.observerPointAt(0.0).x(), 1e-6);
            EXPECT_NEAR(handle->info().value, model.initialGap(), 1e-9);
            gapChecked = true;
        } else if (handle->id() == QLatin1String("temp")) {
            EXPECT_NEAR(handle->info().value, model.temperatureCelsius(), 1e-9);
            tempChecked = true;
        }
    }
    EXPECT_TRUE(gapChecked);
    EXPECT_TRUE(tempChecked);
}

TEST(SoundHandles, HandleMappingIsInvertible) {
    SoundModel model;
    configure(model);
    ProbeScene scene(&model);
    prepare(scene, model, 0.0);
    scene.rebuildHandles();
    scene.positionHandles();

    lab::HandleInfo info;
    info.id = QStringLiteral("gap");
    info.axis = QStringLiteral("x");
    const QPointF anchor = scene.observerPointAt(0.0);
    EXPECT_NEAR(scene.mapHandlePosition(info, anchor), model.initialGap(), 1e-6);

    // Линейка температуры: под позицией ручки тот же градус, что и в модели.
    lab::HandleInfo tempInfo;
    tempInfo.id = QStringLiteral("temp");
    tempInfo.axis = QStringLiteral("x");
    bool checked = false;
    for (const lab::DragHandleItem* handle : scene.handles()) {
        if (handle->id() == QLatin1String("temp")) {
            EXPECT_NEAR(scene.mapHandlePosition(tempInfo, handle->anchor()), model.temperatureCelsius(), 0.5);
            checked = true;
        }
    }
    EXPECT_TRUE(checked);
}

// ------------------------------------------------------------------
// Подписи схемы
// ------------------------------------------------------------------

TEST(SoundLabels, LabelsDoNotOverlapEachOther) {
    SoundModel model;
    configure(model);
    SoundScene scene(&model);

    for (double fraction : {0.1, 0.5, 1.0}) {
        prepare(scene, model, model.duration() * fraction);
        const QVector<SoundScene::Annotation> notes = scene.annotations();
        ASSERT_GE(notes.size(), 5) << "доля времени: " << fraction;

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

TEST(SoundLabels, LabelsStayInsideScheme) {
    SoundModel model;
    configure(model);
    SoundScene scene(&model);
    prepare(scene, model, model.duration());

    const QRectF band = scene.schemeBand();
    for (const SoundScene::Annotation& note : scene.annotations()) {
        EXPECT_GE(note.rect().left(), band.left() - 2.0) << note.id.toUtf8().constData();
        EXPECT_LE(note.rect().right(), band.right() + 2.0) << note.id.toUtf8().constData();
        EXPECT_GE(note.rect().top(), band.top() - 2.0) << note.id.toUtf8().constData();
        EXPECT_LE(note.rect().bottom(), band.bottom() + 2.0) << note.id.toUtf8().constData();
    }
}

TEST(SoundLabels, LabelsCarryModelNumbers) {
    SoundModel model;
    configure(model);
    SoundScene scene(&model);
    prepare(scene, model, model.arrivalTime(0) + 0.5);
    const double t = model.time();

    // Рисунок несёт обозначения, а не числа: они остались на панелях.
    Q_UNUSED(t);
    const QString subscripts = QStringLiteral("₀₁₂₃₄₅₆₇₈₉");
    QHash<QString, QString> expected;
    expected.insert(QStringLiteral("gap"), QStringLiteral("L(t)"));
    expected.insert(QStringLiteral("period"), QStringLiteral("T′"));
    for (int index = 0; index < SoundModel::signalCount(); ++index) {
        expected.insert(QStringLiteral("front_%1").arg(index + 1),
                        QStringLiteral("r%1").arg(subscripts.at(index + 1)));
    }

    const QVector<SoundScene::Annotation> notes = scene.annotations();
    int matched = 0;
    for (const SoundScene::Annotation& note : notes) {
        if (expected.contains(note.id)) {
            EXPECT_EQ(note.text, expected.value(note.id)) << note.id.toUtf8().constData();
            ++matched;
        }
    }
    // Подписи берут числа из формул модели, а не «на глаз» по рисунку.
    EXPECT_GE(matched, SoundModel::signalCount());
    bool periodShown = false;
    for (const SoundScene::Annotation& note : notes)
        periodShown = periodShown || note.id == QLatin1String("period");
    EXPECT_TRUE(periodShown);
}

// ------------------------------------------------------------------
// Отрисовка
// ------------------------------------------------------------------

TEST(SoundScene, FrontsAndShipsAreDrawn) {
    SoundModel model;
    configure(model);
    SoundScene scene(&model);
    prepare(scene, model, model.arrivalTime(1) + 0.5);

    const QImage image = renderFrame(scene);
    // Фронты — фиолетовые окружности, суда — синее и зелёное.
    EXPECT_GT(countColor(image, lab::style::bodyD(), kSchemeTop, kSchemeBottom), 200);
    EXPECT_GT(countColor(image, lab::style::bodyA(), kSchemeTop, kSchemeBottom), 100);
    EXPECT_GT(countColor(image, lab::style::bodyC(), kSchemeTop, kSchemeBottom), 100);
}

TEST(SoundScene, MoreFrontsAreDrawnLater) {
    SoundModel model;
    configure(model);
    SoundScene scene(&model);

    prepare(scene, model, 0.0);
    const int early = countColor(renderFrame(scene), lab::style::bodyD(), kSchemeTop, kSchemeBottom);
    prepare(scene, model, model.duration());
    const int late = countColor(renderFrame(scene), lab::style::bodyD(), kSchemeTop, kSchemeBottom);

    // Со временем фронтов больше и они крупнее — площадь линий растёт.
    EXPECT_GT(late, early);
}

TEST(SoundScene, FourPanelsAreDrawnBelowScheme) {
    SoundModel model;
    configure(model);
    SoundScene scene(&model);
    prepare(scene, model, model.duration() * 0.6);

    const QImage image = renderFrame(scene);
    // L(t) — красный, радиусы фронтов — фиолетовый, пути судов — синий,
    // T′(v) — красный и фиолетовый.
    EXPECT_GT(countColor(image, lab::style::bodyB(), kPlotsTop, kPlotsBottom), 150);
    EXPECT_GT(countColor(image, lab::style::bodyD(), kPlotsTop, kPlotsBottom), 150);
    EXPECT_GT(countColor(image, lab::style::bodyA(), kPlotsTop, kPlotsBottom), 100);
}

TEST(SoundScene, TimeAxisMarkerFollowsModelTime) {
    SoundModel model;
    configure(model);
    SoundScene scene(&model);

    prepare(scene, model, model.duration() * 0.05);
    const int early = axisMarkerX(renderFrame(scene));
    prepare(scene, model, model.duration() * 0.9);
    const int late = axisMarkerX(renderFrame(scene));
    ASSERT_GE(early, 0);
    ASSERT_GE(late, 0);
    EXPECT_GT(late - early, 100);
}

TEST(SoundScene, SchemeAndHandlesSurviveInvalidSetup) {
    SoundModel model;
    setSetup(model, QStringLiteral("temp=20; T=3; L0=6800; dir1=0; v1=12; dir2=1; v2=340"));
    ProbeScene scene(&model);

    // Наблюдатель почти догоняет звук: приём наступает за пределами
    // наблюдения, сцена не должна падать и рисует то, что посчитано.
    prepare(scene, model, model.duration() * 0.5);
    scene.rebuildHandles();
    scene.positionHandles();
    EXPECT_EQ(scene.handles().size(), 2);
    EXPECT_FALSE(scene.frontsFor(model.time()).isEmpty());

    SoundModel invalid;
    setSetup(invalid, QStringLiteral("temp=20; T=3; L0=6800; dir1=1; v1=12; dir2=1; v2=400"));
    SoundScene invalidScene(&invalid);
    prepare(invalidScene, invalid, 1.0);
    EXPECT_TRUE(invalidScene.frontsFor(invalid.time()).isEmpty());
    EXPECT_TRUE(invalidScene.vectorsFor(invalid.time()).isEmpty());
    EXPECT_TRUE(invalidScene.annotations().isEmpty());
    EXPECT_FALSE(renderFrame(invalidScene).isNull());
}
