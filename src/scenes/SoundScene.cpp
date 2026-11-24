#include "scenes/SoundScene.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

#include "models/SoundModel.h"
#include "scenes/SchemePrimitives.h"
#include "scenes/SchemeLabels.h"
#include "scenes/Style.h"
#include "utils/MathText.h"

namespace lab::scenes {
namespace {

/**
 * За время наблюдения звук проходит путь в несколько раз больше, чем
 * расстояние между судами, поэтому внешние фронты заведомо больше полосы
 * схемы и обрезаются её краем. Ограничитель говорит, во сколько раз
 * наибольший радиус может быть больше половины высоты полосы: при этом
 * масштаб по вертикали и горизонтали остаётся одним и тем же, то есть
 * фронт на рисунке — настоящая окружность, а не эллипс.
 */
constexpr double kFrontOverflow = 3.0;

/** Отступ подписей и линейки от краёв полосы схемы, пиксели. */
constexpr double kBandPadding = 18.0;
constexpr double kRulerWidth = 250.0;
constexpr double kRulerHeight = 14.0;

/**
 * Опорная скорость стрелки: параметр скорости судна меняется до 60 м/с,
 * поэтому масштаб задаётся этим пределом, а не текущим значением — длина
 * стрелки остаётся пропорциональной скорости при любой постановке.
 */
constexpr double kReferenceShipSpeed = 60.0;
constexpr double kArrowMinPixels = 8.0;
constexpr double kArrowMaxPixels = 78.0;

/** Габарит изображения судна (примитив drawBoat) и его масштабирование. */
constexpr double kBoatWidth = 92.0;
constexpr double kBoatHeight = 42.0;

/** Границы линейки температуры — те же, что у параметра модели. */
constexpr double kTemperatureMin = -30.0;
constexpr double kTemperatureMax = 40.0;

}  // namespace

SoundScene::SoundScene(Simulation* model, QObject* parent) : LabScene(model, parent) {}

// ------------------------------------------------------------------
// Геометрия полосы схемы
// ------------------------------------------------------------------

void SoundScene::reconfigure(const QRectF& area) {
    auto* model = qobject_cast<models::SoundModel*>(m_model);

    // Верх полосы — схема с фронтами, под ней графики, ещё ниже ось времени.
    m_band = QRectF(area.left() + 24.0, area.top() + 18.0, area.width() - 48.0,
                    std::max(230.0, area.height() * 0.58));

    // Модельная ось X совпадает с линией воды: оба судна на одной прямой,
    // поэтому окружности фронтов симметричны относительно этой линии.
    m_waterY = m_band.center().y();
    m_laneOffset = std::clamp(m_band.height() * 0.05, 8.0, 20.0);

    // Горизонтальнее: судно наблюдателя можно перетащить, поэтому запас
    // по краям берётся от начального расстояния.
    const double gap = model && model->validate().ok ? model->initialGap() : 1200.0;
    const double duration = model && model->validate().ok ? model->duration() : 10.0;
    const double sound = model && model->validate().ok ? model->soundSpeed() : 343.0;

    // Максимальный радиус фронта за всё наблюдение — у самого раннего
    // сигнала: он растёт дольше остальных.
    const double maxFrontRadius = std::max(1.0, sound * duration);
    // Ограниченная сверху область: внешнему фронту разрешаем чуть выйти
    // за полосу (он обрезается краем), иначе суда слились бы в точку.
    const double verticalLimit =
        (m_band.height() * 0.5 - kBandPadding) * kFrontOverflow / maxFrontRadius;

    // Горизонтальные границы «интересной» части: пути обоих судов.
    const double sourceEnd = model && model->validate().ok ? model->sourcePositionAt(duration) : 0.0;
    const double observerEnd = model && model->validate().ok ? model->observerPositionAt(duration) : gap;
    const double pathMin = std::min({0.0, sourceEnd, gap, observerEnd});
    const double pathMax = std::max({0.0, sourceEnd, gap, observerEnd});
    const double pathSpan = std::max(1.0, pathMax - pathMin) + gap * 0.35;
    const double horizontalLimit = (m_band.width() - 2.0 * kBandPadding) / pathSpan;

    // Масштаб один для обеих осей: только так фронт остаётся окружностью.
    m_pixelsPerMeter = std::min(verticalLimit, horizontalLimit);
    m_modelCenterX = 0.5 * (pathMin + pathMax);
    m_axisCenterX = m_band.center().x();

    // Изображения судов масштабируются так, чтобы не перекрывать друг
    // друга при данном расстоянии между ними.
    const double gapPixels = gap * m_pixelsPerMeter;
    m_boatScale = std::clamp(gapPixels / (2.4 * kBoatWidth), 0.30, 1.0);

    setRanges(area, 0.0, std::max(1e-6, duration), 0.0, 1.0);
}

double SoundScene::pixelsPerMeter() const { return m_pixelsPerMeter; }

QPointF SoundScene::sourcePointAt(double t) const {
    auto* model = qobject_cast<models::SoundModel*>(m_model);
    const double meters = model ? model->sourcePositionAt(t) : 0.0;
    return QPointF(m_axisCenterX + (meters - m_modelCenterX) * m_pixelsPerMeter, m_waterY);
}

QPointF SoundScene::observerPointAt(double t) const {
    auto* model = qobject_cast<models::SoundModel*>(m_model);
    const double meters = model ? model->observerPositionAt(t) : 0.0;
    return QPointF(m_axisCenterX + (meters - m_modelCenterX) * m_pixelsPerMeter, m_waterY);
}

double SoundScene::waterY() const { return m_waterY; }

double SoundScene::laneOffset() const { return m_laneOffset; }

double SoundScene::boatScale() const { return m_boatScale; }

QRectF SoundScene::riverRect() const {
    // Вода — полоса под линией движения: ось X лежит на линии воды.
    const double depth = std::clamp(m_band.height() * 0.30, 60.0, 150.0);
    return QRectF(m_band.left(), m_waterY, m_band.width(), std::min(depth, m_band.bottom() - m_waterY));
}

QRectF SoundScene::schemeBand() const { return m_band; }

// ------------------------------------------------------------------
// Фронты звуковой волны: центры закреплены в точках излучения
// ------------------------------------------------------------------

QVector<SoundScene::FrontItem> SoundScene::frontsFor(double t) const {
    QVector<FrontItem> items;
    auto* model = qobject_cast<models::SoundModel*>(m_model);
    if (!model || !model->validate().ok)
        return items;

    for (int index = 0; index < models::SoundModel::signalCount(); ++index) {
        FrontItem item;
        item.index = index;
        const double emission = model->emissionTime(index);
        const double centerMeters = model->emissionPoint(index);   // xₖ = v₁·tₖ
        item.centerMeters = centerMeters;
        item.radiusMeters = model->frontRadius(index, t);           // rₖ = c(t − tₖ)
        item.center = QPointF(m_axisCenterX + (centerMeters - m_modelCenterX) * m_pixelsPerMeter, m_waterY);
        item.radiusPx = item.radiusMeters * m_pixelsPerMeter;
        item.emitted = t >= emission - 1e-12;
        item.arrivalTime = model->arrivalTime(index);
        item.received = std::isfinite(item.arrivalTime) && t >= item.arrivalTime - 1e-12;

        // Точка приёма: там, где фронт догнал наблюдателя. Она видна и до
        // приёма (прогноз), и после (состоявшийся факт).
        if (std::isfinite(item.arrivalTime) && item.arrivalTime <= model->duration() + 1e-9) {
            const double arrivalMeters = model->observerPositionAt(item.arrivalTime);
            item.hasReceptionMark = true;
            item.receptionMark = QPointF(m_axisCenterX + (arrivalMeters - m_modelCenterX) * m_pixelsPerMeter,
                                         m_waterY);
        }
        items.push_back(item);
    }
    return items;
}

// ------------------------------------------------------------------
// Векторы: скорости двух судов (сил и ускорений в модели нет)
// ------------------------------------------------------------------

QVector<vectors::Arrow> SoundScene::vectorsFor(double t) const {
    QVector<vectors::Arrow> arrows;
    auto* model = qobject_cast<models::SoundModel*>(m_model);
    if (!model || !model->validate().ok || !m_model->showsVelocities())
        return arrows;

    // Суда движутся равномерно: ускорений нет, а сил в модель задачи не
    // входит ничего (тяга и сопротивление воды не рассматриваются).
    const double scale = kArrowMaxPixels / kReferenceShipSpeed;
    const double boatLift = kBoatHeight * m_boatScale + 14.0;

    struct ShipSpec {
        QPointF anchor;
        double speed;
        QString symbol;
    };
    const QVector<ShipSpec> ships = {
        {sourcePointAt(t) - QPointF(0.0, boatLift), model->sourceSpeed(), QStringLiteral("v₁")},
        {observerPointAt(t) - QPointF(0.0, boatLift), model->observerSpeed(), QStringLiteral("v₂")},
    };

    for (const ShipSpec& ship : ships) {
        const double length = vectors::visualLength(std::abs(ship.speed), scale, kArrowMinPixels, kArrowMaxPixels);
        if (length <= 0.0)
            continue;
        vectors::Arrow arrow;
        arrow.from = ship.anchor;
        // Знак скорости задаёт направление: «навстречу» и «удаляясь» —
        // стрелки смотрят в разные стороны, формула при этом одна.
        arrow.to = ship.anchor + QPointF(ship.speed >= 0.0 ? length : -length, 0.0);
        arrow.symbol = ship.symbol;
        arrow.kind = vectors::Kind::Velocity;
        arrow.labelOffset = QPointF(0.0, -14.0);
        arrows.push_back(arrow);
    }
    return arrows;
}

void SoundScene::drawVectors(QPainter* painter, const QVector<vectors::Arrow>& arrows) const {
    for (const vectors::Arrow& arrow : arrows)
        vectors::drawArrow(painter, arrow);
}

// ------------------------------------------------------------------
// Линейка температуры воздуха
// ------------------------------------------------------------------

QRectF SoundScene::temperatureBarRect() const {
    return QRectF(m_band.left() + kBandPadding, m_band.top() + 26.0, kRulerWidth, kRulerHeight);
}

double SoundScene::temperatureBarX(double celsius) const {
    const QRectF bar = temperatureBarRect();
    const double fraction =
        std::clamp((celsius - kTemperatureMin) / (kTemperatureMax - kTemperatureMin), 0.0, 1.0);
    return bar.left() + fraction * bar.width();
}

double SoundScene::temperatureForBarX(double sceneX) const {
    const QRectF bar = temperatureBarRect();
    const double fraction = std::clamp((sceneX - bar.left()) / std::max(1e-9, bar.width()), 0.0, 1.0);
    return kTemperatureMin + fraction * (kTemperatureMax - kTemperatureMin);
}

// ------------------------------------------------------------------
// Перетаскивание: судно наблюдателя и температура воздуха
// ------------------------------------------------------------------

double SoundScene::mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const {
    auto* model = qobject_cast<models::SoundModel*>(m_model);
    if (!model)
        return LabScene::mapHandlePosition(info, scenePos);

    if (info.id == QLatin1String("gap"))
        return m_modelCenterX + (scenePos.x() - m_axisCenterX) / m_pixelsPerMeter;
    if (info.id == QLatin1String("temp"))
        return temperatureForBarX(scenePos.x());
    return LabScene::mapHandlePosition(info, scenePos);
}

double SoundScene::handleScenePosition(const HandleInfo& info) const {
    auto* model = qobject_cast<models::SoundModel*>(m_model);
    if (!model)
        return LabScene::handleScenePosition(info);

    if (info.id == QLatin1String("gap"))
        return observerPointAt(0.0).x();
    if (info.id == QLatin1String("temp"))
        return temperatureBarX(model->temperatureCelsius());
    return LabScene::handleScenePosition(info);
}

void SoundScene::positionHandles() {
    auto* model = qobject_cast<models::SoundModel*>(m_model);
    if (!model)
        return;

    for (DragHandleItem* handle : m_handles) {
        if (!handle)
            continue;
        const HandleInfo info = handle->info();
        if (info.id == QLatin1String("gap")) {
            // Ручка на судне наблюдателя: перетаскивание вдоль русла меняет
            // начальное расстояние между судами.
            handle->setAxis(DragHandleItem::Axis::Horizontal);
            handle->setAnchor(observerPointAt(0.0) + QPointF(0.0, kBoatHeight * m_boatScale * 0.5));
        } else if (info.id == QLatin1String("temp")) {
            handle->setAxis(DragHandleItem::Axis::Horizontal);
            handle->setAnchor(QPointF(temperatureBarX(model->temperatureCelsius()),
                                      temperatureBarRect().center().y()));
        } else {
            handle->setAnchor(sourcePointAt(m_model->time()));
        }
    }
}

// ------------------------------------------------------------------
// Отрисовка схемы
// ------------------------------------------------------------------

void SoundScene::drawRiver(QPainter* painter) const {
    const QRectF water = riverRect();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Воздух над водой — светлая основа: по нему идут фронты звука.
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(238, 244, 249, 170));
    painter->drawRect(QRectF(m_band.left(), m_band.top(), m_band.width(), m_waterY - m_band.top()));

    // Вода.
    QLinearGradient gradient(water.topLeft(), water.bottomLeft());
    gradient.setColorAt(0.0, style::water().lighter(112));
    gradient.setColorAt(1.0, style::waterDeep().lighter(112));
    painter->setBrush(gradient);
    painter->drawRect(water);

    // Линия воды — она же ось X, по которой движутся суда.
    painter->setPen(QPen(style::waterDeep().darker(120), 1.5));
    painter->drawLine(QPointF(m_band.left(), m_waterY), QPointF(m_band.right(), m_waterY));

    // Береговые кромки полосы.
    painter->setPen(QPen(style::sand().darker(150), 1.2));
    painter->drawLine(QPointF(m_band.left(), m_band.bottom()), QPointF(m_band.right(), m_band.bottom()));
    painter->restore();
}

void SoundScene::drawFronts(QPainter* painter, const QVector<FrontItem>& fronts) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    // Внешний фронт может выйти за полосу: обрезаем, но масштаб не меняем.
    painter->setClipRect(m_band);

    for (const FrontItem& front : fronts) {
        if (!front.emitted || front.radiusPx <= 1.0)
            continue;

        const QRectF circle(front.center.x() - front.radiusPx, front.center.y() - front.radiusPx,
                            front.radiusPx * 2.0, front.radiusPx * 2.0);
        // Принятый фронт показан штрихом: он уже прошёл наблюдателя и
        // больше не «интересный», но своё положение не меняет.
        QPen pen(style::bodyD(), front.received ? 1.3 : 2.0);
        if (front.received)
            pen.setStyle(Qt::DashLine);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(circle);

        // Радиус в сторону наблюдателя: правая граница окружности и есть
        // фронт, который его настигает.
        painter->setPen(QPen(style::bodyD().darker(130), 1.0, Qt::DotLine));
        painter->drawLine(front.center, QPointF(front.center.x() + front.radiusPx, front.center.y()));

        // Точка излучения: где звук родился, там его центр и останется.
        painter->setPen(QPen(style::bodyD().darker(170), 1.4));
        painter->drawLine(QPointF(front.center.x() - 6.0, front.center.y()),
                          QPointF(front.center.x() + 6.0, front.center.y()));
        painter->drawLine(QPointF(front.center.x(), front.center.y() - 6.0),
                          QPointF(front.center.x(), front.center.y() + 6.0));
    }
    painter->restore();
}

void SoundScene::drawReceptionMarks(QPainter* painter, const QVector<FrontItem>& fronts) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    for (const FrontItem& front : fronts) {
        if (!front.hasReceptionMark)
            continue;
        // Состоявшийся приём — залитый кружок, ожидаемый — контурный:
        // схема показывает и предсказание, и факт.
        painter->setPen(QPen(front.received ? style::success().darker(140) : style::inkSoft(), 1.5,
                             front.received ? Qt::SolidLine : Qt::DashLine));
        painter->setBrush(front.received ? style::success() : QColor(255, 255, 255, 200));
        painter->drawEllipse(front.receptionMark, front.received ? 5.5 : 4.5, front.received ? 5.5 : 4.5);

        // Вертикаль от точки приёма к отметке времени вниз полосы.
        painter->setPen(QPen(front.received ? style::success().darker(120) : style::inkSoft(), 1.0, Qt::DashLine));
        painter->drawLine(front.receptionMark, QPointF(front.receptionMark.x(), m_band.bottom() - 6.0));
    }
    painter->restore();
}

void SoundScene::drawShips(QPainter* painter) const {
    auto* model = qobject_cast<models::SoundModel*>(m_model);
    if (!model)
        return;
    const double t = m_model->time();

    struct ShipSpec {
        QPointF at;
        double speed;
        QColor color;
    };
    const QVector<ShipSpec> ships = {
        {sourcePointAt(t), model->sourceSpeed(), style::bodyA()},
        {observerPointAt(t), model->observerSpeed(), style::bodyC()},
    };

    for (const ShipSpec& ship : ships) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        // Изображение судна масштабится относительно линии воды, поэтому
        // ватерлиния остаётся на оси X при любом масштабе.
        painter->translate(ship.at);
        painter->scale(m_boatScale, m_boatScale);
        primitives::drawBoat(painter, QPointF(0.0, 0.0), ship.color, std::abs(ship.speed) > 1e-9,
                             ship.speed >= 0.0);
        painter->restore();
    }

    // Гудок: короткий конус над трубой судна источника — источник звука.
    const QPointF source = sourcePointAt(t);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(style::accentWarm().darker(120), 1.5));
    painter->setBrush(QColor(214, 122, 34, 120));
    const double hornY = source.y() - kBoatHeight * m_boatScale;
    QPolygonF horn;
    horn << QPointF(source.x() - 5.0, hornY) << QPointF(source.x() + 5.0, hornY)
         << QPointF(source.x() + 12.0, hornY - 9.0) << QPointF(source.x() - 12.0, hornY - 9.0);
    painter->drawPolygon(horn);
    painter->restore();
}

void SoundScene::drawGapRuler(QPainter* painter) const {
    auto* model = qobject_cast<models::SoundModel*>(m_model);
    if (!model)
        return;
    const double t = m_model->time();
    const double lift = kBoatHeight * m_boatScale + 46.0;
    // Размерная линия между судами: расстояние между ними меняется, и
    // именно L(t) входит в условие приёма фронта.
    labels::drawRulerLine(painter, sourcePointAt(t) - QPointF(0.0, lift), observerPointAt(t) - QPointF(0.0, lift),
                          style::bodyB(), 0.0);
}

void SoundScene::drawTemperatureBar(QPainter* painter) const {
    auto* model = qobject_cast<models::SoundModel*>(m_model);
    if (!model)
        return;

    const QRectF bar = temperatureBarRect();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(style::inkSoft(), 1.2));
    painter->setBrush(QColor(255, 255, 255, 215));
    painter->drawRoundedRect(bar, 4, 4);

    painter->setFont(style::uiFont(7.5));
    for (const double value : {kTemperatureMin, 0.0, 20.0, kTemperatureMax}) {
        const double x = temperatureBarX(value);
        painter->setPen(QPen(style::inkSoft(), 1.0));
        painter->drawLine(QPointF(x, bar.bottom()), QPointF(x, bar.bottom() + 5.0));
        painter->setPen(style::inkSoft());
        painter->drawText(QRectF(x - 26.0, bar.bottom() + 5.0, 52.0, 13), Qt::AlignHCenter,
                          QStringLiteral("%1°").arg(utils::formatNumber(value, 0)));
    }

    const double currentX = temperatureBarX(model->temperatureCelsius());
    painter->setPen(QPen(style::danger(), 1.6));
    painter->setBrush(style::paper());
    painter->drawEllipse(QPointF(currentX, bar.center().y()), 6.0, 6.0);
    painter->restore();
}

// ------------------------------------------------------------------
// Подписи схемы
// ------------------------------------------------------------------

QVector<SoundScene::Annotation> SoundScene::annotations() const {
    QVector<Annotation> list;
    auto* model = qobject_cast<models::SoundModel*>(m_model);
    if (!model || !model->validate().ok)
        return list;

    const double t = m_model->time();
    labels::Planner planner(m_band);

    // Подписи фронтов: у правой границы окружности, а если там не влезает —
    // у верхней или нижней точки той же окружности. Центр при этом не
    // меняется: подпись остаётся у своего фронта.
    const QVector<FrontItem> fronts = frontsFor(t);
    for (const FrontItem& front : fronts) {
        if (!front.emitted)
            continue;
        const QString subscripts = QStringLiteral("₀₁₂₃₄₅₆₇₈₉");
        const QString text = QStringLiteral("r%1").arg(front.index < 10 ? subscripts.at(front.index + 1)
                                                                       : QString::number(front.index + 1));
        // Большие фронты выходит за рамку рисунка, поэтому подпись ставится
        // у той точки окружности, которая обращена к центру полосы: именно
        // там дуга видна на схеме.
        QPointF toward = m_band.center() - front.center;
        const double distance = std::max(1e-9, std::hypot(toward.x(), toward.y()));
        toward /= distance;
        const QVector<QPointF> candidates = {
            front.center + toward * front.radiusPx,
            front.center + QPointF(front.radiusPx, -26.0),
            front.center - QPointF(front.radiusPx, 26.0),
            front.center + QPointF(0.0, -front.radiusPx - 20.0),
            front.center + QPointF(0.0, front.radiusPx + 20.0),
        };
        planner.addFirstFree(QStringLiteral("front_%1").arg(front.index + 1), candidates, text,
                             style::bodyD().darker(160));
    }

    // Моменты приёма — главный ответ задачи.
    for (int index = 0; index < models::SoundModel::signalCount(); ++index) {
        const double arrival = model->arrivalTime(index);
        if (!std::isfinite(arrival) || arrival > model->duration() + 1e-9)
            continue;
        const bool arrived = t >= arrival - 1e-12;
        const QPointF mark = index < fronts.size() ? fronts.at(index).receptionMark
                                                   : QPointF(m_band.center().x(), m_waterY);
        // Подпись короткая: длинные тексты в узкой полосе схемы неизбежно
        // сталкиваются, а смысл подсказывает цвет (зелёный — уже принят).
        const QString subscripts = QStringLiteral("₀₁₂₃₄₅₆₇₈₉");
        const QString text =
            QStringLiteral("t′%1%2")
                .arg(index < 10 ? subscripts.at(index + 1) : QString::number(index + 1))
                .arg(arrived ? QString() : QStringLiteral(" — ещё в пути"));
        planner.addFirstFree(QStringLiteral("arrival_%1").arg(index + 1),
                             {mark + QPointF(0.0, 34.0), mark + QPointF(0.0, -58.0), mark - QPointF(120.0, 34.0),
                              mark + QPointF(120.0, 34.0)},
                             text, arrived ? style::success().darker(140) : style::inkSoft());
    }

    // Расстояние между судами — размерная линия над судами.
    const double lift = kBoatHeight * m_boatScale + 46.0;
    planner.add(QStringLiteral("gap"),
                labels::rulerLabelPos(sourcePointAt(t) - QPointF(0.0, lift), observerPointAt(t) - QPointF(0.0, lift),
                                      0.0),
                QStringLiteral("L(t)"),
                style::bodyB().darker(160));

    // Кто есть кто: источник звука и наблюдатель (подписи под судами).
    planner.addFirstFree(QStringLiteral("source"),
                         {sourcePointAt(t) + QPointF(0.0, 34.0), sourcePointAt(t) + QPointF(0.0, 60.0),
                          sourcePointAt(t) + QPointF(90.0, 34.0)},
                         QStringLiteral("гудок x₁"),
                         style::bodyA().darker(160));
    planner.addFirstFree(QStringLiteral("observer"),
                         {observerPointAt(t) + QPointF(0.0, 34.0), observerPointAt(t) + QPointF(0.0, 60.0),
                          observerPointAt(t) + QPointF(-90.0, 34.0)},
                         QStringLiteral("наблюдатель x₂"),
                         style::bodyC().darker(160));

    // Скорость звука и её связь с температурой — рядом с линейкой температуры.
    planner.addFirstFree(QStringLiteral("sound"),
                         {QPointF(temperatureBarRect().right() + 150.0, temperatureBarRect().center().y()),
                          QPointF(temperatureBarRect().right() + 150.0, temperatureBarRect().bottom() + 24.0),
                          QPointF(temperatureBarRect().right() + 150.0, temperatureBarRect().top() - 20.0)},
                         QStringLiteral("c"),
                         style::ink());

    // О чертеже: главное, что нужно увидеть на схеме.
    // Одна короткая подсказка о том, как читать рисунок: центр фронта
    // неподвижен, а большие окружности обрезаются краем полосы.
    planner.addFirstFree(QStringLiteral("legend"),
                         {QPointF(m_band.left() + 250.0, m_band.top() + 14.0),
                          QPointF(m_band.left() + 250.0, m_band.top() + 34.0),
                          QPointF(m_band.right() - 210.0, m_band.top() + 14.0)},
                         QStringLiteral("центры фронтов закреплены в точках излучения; внешние окружности "
                                        "обрезаны рамкой"),
                         style::inkSoft(), QColor(255, 255, 255, 205));
    planner.addFirstFree(QStringLiteral("period"),
                         {QPointF(m_band.right() - 200.0, m_band.bottom() - 16.0),
                          QPointF(m_band.right() - 200.0, m_band.bottom() - 36.0)},
                         QStringLiteral("T′"),
                         style::accentWarm().darker(150), QColor(255, 255, 255, 215));

    // Ключевое событие: принят последний сигнал — ответ задачи на схеме.
    if (model->receivedCount(t) >= models::SoundModel::signalCount()) {
        planner.addFirstFree(QStringLiteral("result"),
                             {QPointF(m_band.center().x(), m_band.bottom() - 34.0),
                              QPointF(m_band.center().x(), m_band.bottom() - 56.0),
                              QPointF(m_band.center().x(), m_band.top() + 40.0)},
                             QStringLiteral("приняты все сигналы (t′ и T′ — в панели)"),
                             style::success().darker(130), QColor(234, 247, 239, 240));
    }

    return planner.items();
}

// ------------------------------------------------------------------
// Схема целиком
// ------------------------------------------------------------------

void SoundScene::paintScheme(QPainter* painter) {
    auto* model = qobject_cast<models::SoundModel*>(m_model);
    if (!model)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const double t = m_model->time();
    const QVector<FrontItem> fronts = frontsFor(t);

    drawRiver(painter);
    drawFronts(painter, fronts);
    drawReceptionMarks(painter, fronts);
    drawShips(painter);
    drawGapRuler(painter);
    drawTemperatureBar(painter);
    drawVectors(painter, vectorsFor(t));

    // Все тексты — последними и по одному реестру, поэтому они не
    // набегают друг на друга при любом положении судов.
    labels::drawAll(painter, annotations());

    drawPlots(painter);
    painter->restore();
}

// ------------------------------------------------------------------
// Графики: три от времени и одна не временная зависимость
// ------------------------------------------------------------------

void SoundScene::drawPlots(QPainter* painter) const {
    auto* model = qobject_cast<models::SoundModel*>(m_model);
    if (!model || !model->validate().ok)
        return;

    // Полоса между схемой и общей осью времени.
    const double plotsTop = m_band.bottom() + 14.0;
    const double plotsBottom = timeAxisRect().top() - 10.0;
    const double available = plotsBottom - plotsTop;
    if (available < 90.0)
        return;

    constexpr double kGap = 10.0;
    const double panelWidth = (m_band.width() - 3.0 * kGap) / 4.0;
    const double height = std::min(available, 214.0);
    const double top = plotsTop + std::max(0.0, available - height) * 0.5;
    const double duration = std::max(1e-6, m_model->duration());
    const double currentT = m_model->time();
    const double eventT = m_model->eventTime();

    // ---- 1. Расстояние между судами ----
    {
        PlotSeries gap;
        gap.points = model->gapSeries();
        gap.color = style::bodyB();
        gap.label = QStringLiteral("L(t)");
        drawPlot(painter, QRectF(m_band.left(), top, panelWidth, height),
                 QStringLiteral("Расстояние L(t), м"), timeUnitLabel(), {gap}, duration, currentT, eventT);
    }

    // ---- 2. Радиусы фронтов: все три фронта растут со скоростью звука ----
    {
        QVector<PlotSeries> series;
        const QColor colors[] = {style::bodyD(), style::accentWarm(), style::bodyC()};
        for (int index = 0; index < models::SoundModel::signalCount(); ++index) {
            PlotSeries front;
            front.points = model->frontSeries(index);
            front.color = colors[index % 4];
            front.label = QStringLiteral("r%1").arg(index + 1);
            series.push_back(front);
        }
        drawPlot(painter, QRectF(m_band.left() + panelWidth + kGap, top, panelWidth, height),
                 QStringLiteral("Радиусы фронтов r₁, r₂, r₃, м"), timeUnitLabel(), series, duration, currentT,
                 eventT);
    }

    // ---- 3. Законы движения судов ----
    {
        PlotSeries source;
        source.points = model->sourcePathSeries();
        source.color = style::bodyA();
        source.label = QStringLiteral("x₁(t)");
        PlotSeries observer;
        observer.points = model->observerPathSeries();
        observer.color = style::bodyC();
        observer.dashed = true;
        observer.label = QStringLiteral("x₂(t) − L₀");
        drawPlot(painter, QRectF(m_band.left() + 2.0 * (panelWidth + kGap), top, panelWidth, height),
                 QStringLiteral("Пути судов, м"), timeUnitLabel(), {source, observer}, duration, currentT, eventT);
    }

    // ---- 4. Промежуток между приёмами: не временная зависимость ----
    {
        PlotSeries versusObserver;
        versusObserver.points = model->periodVersusObserverSpeed();
        versusObserver.color = style::bodyB();
        versusObserver.label = QStringLiteral("T′(v₂) при v₁ = %1")
                                   .arg(utils::formatNumber(model->sourceSpeed(), 0));
        PlotSeries versusSource;
        versusSource.points = model->periodVersusSourceSpeed();
        versusSource.color = style::bodyD();
        versusSource.dashed = true;
        versusSource.label = QStringLiteral("T′(v₁) при v₂ = %1")
                                 .arg(utils::formatNumber(model->observerSpeed(), 0));
        const QPointF current(model->observerSpeed(), model->receptionPeriod());
        drawGraph(painter, QRectF(m_band.left() + 3.0 * (panelWidth + kGap), top, panelWidth, height),
                  QStringLiteral("Промежуток приёмов T′(v), с"), QStringLiteral("скорость судна, м/с"),
                  QStringLiteral("T′, с"), {versusObserver, versusSource}, current, true);
    }
}

}  // namespace lab::scenes
