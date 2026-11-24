#include "scenes/MotionScene.h"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <functional>

#include "models/MotionModel.h"
#include "scenes/Style.h"
#include "scenes/VectorArrows.h"
#include "utils/MathText.h"

namespace lab::scenes {
namespace {

constexpr double kRoadMargin = 70.0;   // отступ дороги от краёв области

/** Строит набор точек функции f на отрезке [0, tMax]. */
QVector<QPointF> sampleFunction(double tMax, int count, const std::function<double(double)>& f) {
    QVector<QPointF> points;
    points.reserve(count + 1);
    for (int i = 0; i <= count; ++i) {
        const double t = tMax * i / count;
        points.append(QPointF(t, f(t)));
    }
    return points;
}

}  // namespace

MotionScene::MotionScene(Simulation* model, QObject* parent) : LabScene(model, parent) {}

double MotionScene::roadY() const {
    return m_viewArea.top() + m_viewArea.height() * 0.30;
}

double MotionScene::roadHalfWidth() const {
    // Толщина дороги — доля области просмотра, а не фиксированные пиксели.
    return std::clamp(m_viewArea.height() * 0.045, 13.0, 26.0);
}

double MotionScene::laneOffset() const {
    // Смещение полос: автомобили едут по разным полосам одной дороги,
    // поэтому в момент встречи не перекрывают друг друга.
    return roadHalfWidth() * 0.42;
}

double MotionScene::carScale() const {
    return std::clamp(m_viewArea.width() / 1350.0, 0.72, 1.25);
}

void MotionScene::reconfigure(const QRectF& area) {
    auto* motion = qobject_cast<models::MotionModel*>(m_model);
    const double x1 = motion ? motion->startOf(0) : 0.0;
    const double x2 = motion ? motion->startOf(1) : 100.0;
    const double distance = std::max(1.0, std::abs(x2 - x1));

    // Физическая ось: начало координат слева, положительное направление вправо.
    // Начало координат сцены совмещено с отметкой x = 0 км.
    const double minPos = 0.0;
    const double maxPos = std::max(x1, x2) + distance * 0.55 + 10.0;

    const QRectF road(area.left() + kRoadMargin, area.top() + 30.0,
                      area.width() - 2 * kRoadMargin, area.height() * 0.60);
    setRanges(road, minPos, maxPos, minPos, maxPos);
}

void MotionScene::positionHandles() {
    auto* motion = qobject_cast<models::MotionModel*>(m_model);
    if (!motion)
        return;
    const SimState state = motion->currentState();
    const double y = roadY();
    for (DragHandleItem* handle : m_handles) {
        const int carIndex = handle->id() == QLatin1String("x1") ? 0 : 1;
        if (carIndex < 0 || carIndex >= state.entities.size())
            continue;
        const double laneY = y + (carIndex == 0 ? -laneOffset() : laneOffset());
        handle->setAnchor(QPointF(toSceneX(state.entities[carIndex].position), laneY));
    }
}

void MotionScene::drawCar(QPainter* painter, const QPointF& center, double dir, const QColor& color,
                          const QString& label, bool moving, bool waiting) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Габариты кузова задаются относительно сцены (масштаб carScale).
    const double s = carScale();
    painter->translate(center);
    painter->scale(s, s);

    const QRectF body(-34, -16, 68, 16);           // кузов
    const QRectF cabin(-14, -27, 30, 13);          // кабина

    QColor fill = color;
    if (waiting)
        fill = QColor(152, 157, 164);

    painter->setPen(QPen(color.darker(160), 1.4));
    painter->setBrush(fill);
    painter->drawRoundedRect(body, 4, 4);
    painter->drawRoundedRect(cabin, 4, 4);

    // Кабина смещена в сторону движения: передок и корма различимы.
    painter->setBrush(QColor(211, 232, 248));
    const double cabinShift = dir >= 0 ? 4.0 : -4.0;
    painter->drawRoundedRect(cabin.adjusted(cabinShift - 3, 3, cabinShift + 3, -2), 3, 3);

    // Два видимых колеса в боковой проекции.
    painter->setBrush(QColor(40, 44, 50));
    painter->setPen(QPen(QColor(20, 22, 26), 1.0));
    painter->drawEllipse(QPointF(-20, 0), 6.2, 6.2);
    painter->drawEllipse(QPointF(20, 0), 6.2, 6.2);
    painter->setBrush(QColor(150, 154, 160));
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(QPointF(-20, 0), 2.2, 2.2);
    painter->drawEllipse(QPointF(20, 0), 2.2, 2.2);

    // Фара (перед) и задний фонарь: сразу видно направление корпуса.
    const double frontX = dir >= 0 ? body.right() : body.left();
    const double rearX = dir >= 0 ? body.left() : body.right();
    painter->setBrush(QColor(250, 214, 110));
    painter->drawEllipse(QPointF(frontX, -10), 2.8, 2.8);
    painter->setBrush(QColor(196, 74, 62));
    painter->drawEllipse(QPointF(rearX, -10), 2.2, 2.2);

    // Коды колёс на дороге под автомобилем.
    if (moving) {
        painter->setPen(QPen(color.darker(110), 1.2, Qt::DashLine));
        painter->drawLine(QPointF(-46.0, 6.2), QPointF(-34.0, 6.2));
        painter->drawLine(QPointF(34.0, 6.2), QPointF(46.0, 6.2));
    }
    painter->restore();

    LabScene::drawTag(painter, QPointF(center.x(), center.y() - 44.0 * carScale()), label,
                      QColor(255, 255, 255, 235), color.darker(120));
}

void MotionScene::paintScheme(QPainter* painter) {
    auto* motion = qobject_cast<models::MotionModel*>(m_model);
    if (!motion)
        return;
    const SimState state = motion->currentState();
    const double y = roadY();
    const double halfWidth = roadHalfWidth();

    // ---- Дорога: полотно, границы, разметка, обочины ----
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRectF road(m_viewArea.left(), y - halfWidth, m_viewArea.width(), 2 * halfWidth);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(86, 90, 97));
    painter->drawRect(road);
    // Границы дорожного полотна.
    painter->setPen(QPen(QColor(238, 238, 232), 2.0));
    painter->drawLine(QPointF(road.left(), road.top() + 1.5), QPointF(road.right(), road.top() + 1.5));
    painter->drawLine(QPointF(road.left(), road.bottom() - 1.5), QPointF(road.right(), road.bottom() - 1.5));
    // Пунктирная осевая разметка.
    QPen dash(QColor(240, 240, 235), 2.0, Qt::DashLine);
    dash.setDashPattern({3.0, 3.0});
    painter->setPen(dash);
    painter->drawLine(QPointF(road.left() + 8, y), QPointF(road.right() - 8, y));
    // Обочины.
    painter->setPen(QPen(QColor(120, 124, 130), 1.0));
    painter->setBrush(QColor(228, 228, 222));
    painter->drawRect(QRectF(road.left(), road.bottom(), road.width(), 8));
    painter->drawRect(QRectF(road.left(), road.top() - 8, road.width(), 8));
    painter->restore();

    // ---- Координатная ось под дорогой: начало слева, «+» вправо ----
    painter->save();
    painter->setPen(QPen(style::inkSoft(), 1.0));
    painter->setFont(style::uiFont(8.0));
    const double axisY = road.bottom() + 22.0;
    painter->drawLine(QPointF(m_viewArea.left(), axisY), QPointF(m_viewArea.right(), axisY));
    // Наконечник «положительного направления».
    painter->setBrush(style::inkSoft());
    painter->setPen(Qt::NoPen);
    QPolygonF axisHead;
    axisHead << QPointF(m_viewArea.right(), axisY) << QPointF(m_viewArea.right() - 9, axisY - 4.5)
             << QPointF(m_viewArea.right() - 9, axisY + 4.5);
    painter->drawPolygon(axisHead);

    const double span = m_worldMax - m_worldMin;
    const double stepKm = span > 400 ? 100.0 : (span > 120 ? 50.0 : (span > 40 ? 20.0 : 5.0));
    const double firstTick = std::ceil(m_worldMin / stepKm) * stepKm;
    for (double km = firstTick; km <= m_worldMax + 1e-6; km += stepKm) {
        const double x = toSceneX(km);
        const bool isOrigin = std::abs(km) < 1e-9;
        painter->setPen(QPen(isOrigin ? style::ink() : style::inkSoft(), isOrigin ? 1.8 : 1.0));
        painter->drawLine(QPointF(x, axisY - (isOrigin ? 7 : 4)), QPointF(x, axisY + (isOrigin ? 7 : 4)));
        // Деления дороги без чисел: расстояния подписаны размерными линиями.
    }
    painter->setPen(style::inkSoft());
    painter->drawText(QRectF(m_viewArea.right() - 60, axisY - 18, 60, 13), Qt::AlignRight,
                      QStringLiteral("x, км"));
    painter->restore();

    // ---- Автомобили (по своим полосам) ----
    for (const EntityVisual& car : state.entities) {
        const int index = car.id == QLatin1String("car1") ? 0 : 1;
        const double cx = toSceneX(car.position);
        const double laneY = y + (index == 0 ? -laneOffset() : laneOffset());
        const double dir = car.value1 >= 0 ? 1.0 : -1.0;
        const bool waiting = m_model->time() <= car.value2 + 1e-9 && !car.active;
        drawCar(painter, QPointF(cx, laneY), dir, style::variantColor(car.variant), car.label,
                car.active, waiting);
    }

    // ---- Расстояние между автомобилями (размерная линия) ----
    const double x1 = toSceneX(state.entities.first().position);
    const double x2 = toSceneX(state.entities.at(1).position);
    if (state.primary > 1e-6) {
        // Размерная линия — только с обозначением: числа живут на панелях.
        LabScene::drawDimension(painter, QPointF(x1, y - 92.0), QPointF(x2, y - 92.0),
                                QStringLiteral("s"), style::ink());
    }

    // ---- Векторы скоростей (общая подсистема) ----
    // Для равномерного движения a = 0 и силы не моделируются —
    // поэтому показываем только скорость.
    if (m_model->showsVelocities()) {
        double maxSpeed = 1.0;
        for (const EntityVisual& car : state.entities)
            maxSpeed = std::max(maxSpeed, std::abs(car.value1));
        const double pxPerUnit = vectors::autoScale(maxSpeed, 100.0 * carScale() * m_model->vectorScale());
        for (const EntityVisual& car : state.entities) {
            const double speed = car.value1;
            if (std::abs(speed) < 1e-9)
                continue;  // автомобиль стоит — вектора скорости нет
            const int index = car.id == QLatin1String("car1") ? 0 : 1;
            const double laneY = y + (index == 0 ? -laneOffset() : laneOffset());
            const double cx = toSceneX(car.position);
            const double length = vectors::visualLength(speed, pxPerUnit, 22.0, 120.0 * carScale());
            vectors::Arrow arrow;
            arrow.from = QPointF(cx, laneY - 52.0 * carScale());
            arrow.to = arrow.from + QPointF(speed >= 0 ? length : -length, 0.0);
            arrow.symbol = index == 0 ? QStringLiteral("v₁") : QStringLiteral("v₂");
            arrow.kind = vectors::Kind::Velocity;
            arrow.color = style::variantColor(car.variant);
            arrow.width = 2.6;
            vectors::drawArrow(painter, arrow);
        }
    }

    // ---- Точка встречи ----
    const double event = motion->meetingTime();
    if (std::isfinite(event)) {
        const double meetX = toSceneX(motion->positionOf(0, event));
        painter->save();
        painter->setPen(QPen(style::success(), 1.2, Qt::DashLine));
        painter->drawLine(QPointF(meetX, m_viewArea.top() + 2.0), QPointF(meetX, y - 20.0));
        painter->setPen(QPen(style::success(), 2.0));
        painter->setBrush(QColor(234, 247, 239, 200));
        painter->drawEllipse(QPointF(meetX, y), 7.0, 7.0);
        painter->restore();
        LabScene::drawTag(painter, QPointF(meetX, m_viewArea.top() + 12.0),
                          QStringLiteral("встреча"), style::success().darker(120).lighter(160),
                          style::success().darker(120));
    }

    // ---- График x(t) и расстояния ----
    const double duration = std::max(0.5, m_model->duration());
    const double plotBottom = m_viewArea.bottom();
    const double plotHeight = std::min(m_viewArea.height() * 0.36, plotBottom - axisPlotTop());
    const QRectF plotRect(m_viewArea.left() + 6.0, plotBottom - std::max(90.0, plotHeight),
                          m_viewArea.width() - 12.0, std::max(90.0, plotHeight));
    if (plotRect.height() > 90.0) {
        QVector<PlotSeries> series;
        PlotSeries s1;
        s1.color = style::bodyA();
        s1.points = sampleFunction(duration, 90, [&](double tt) { return motion->positionOf(0, tt); });
        PlotSeries s2;
        s2.color = style::bodyB();
        s2.points = sampleFunction(duration, 90, [&](double tt) { return motion->positionOf(1, tt); });
        PlotSeries d;
        d.color = style::inkSoft();
        d.dashed = true;
        d.width = 1.6;
        d.points = sampleFunction(duration, 90, [&](double tt) { return motion->distanceAt(tt); });
        series = {s1, s2, d};
        LabScene::drawPlot(painter, plotRect,
                           QStringLiteral("Зависимость координат x(t) и расстояния между автомобилями от времени"),
                           QStringLiteral("ч"), series, duration, m_model->time(), event);
    }

}

double MotionScene::axisPlotTop() const {
    return roadY() + 44.0;
}

}  // namespace lab::scenes
