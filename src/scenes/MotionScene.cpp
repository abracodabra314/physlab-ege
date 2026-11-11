#include "scenes/MotionScene.h"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <functional>

#include "models/MotionModel.h"
#include "scenes/Style.h"
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

void MotionScene::reconfigure(const QRectF& area) {
    auto* motion = qobject_cast<models::MotionModel*>(m_model);
    const double x1 = motion ? motion->startOf(0) : 0.0;
    const double x2 = motion ? motion->startOf(1) : 100.0;
    const double distance = std::max(1.0, std::abs(x2 - x1));
    // Мир сцены: обе машины и запас вперёд, чтобы было видно их движение.
    const double minPos = std::min(x1, x2) - distance * 0.22 - 5.0;
    const double maxPos = std::max(x1, x2) + distance * 0.55 + 5.0;

    const QRectF road(area.left() + kRoadMargin, area.top() + 24.0,
                      area.width() - 2 * kRoadMargin, area.height() * 0.60);
    setRanges(road, minPos, maxPos, minPos, maxPos);
}

void MotionScene::positionHandles() {
    auto* motion = qobject_cast<models::MotionModel*>(m_model);
    if (!motion)
        return;
    const SimState state = motion->currentState();
    for (DragHandleItem* handle : m_handles) {
        const int carIndex = handle->id() == QLatin1String("x1") ? 0 : 1;
        if (carIndex < 0 || carIndex >= state.entities.size())
            continue;
        handle->setAnchor(QPointF(toSceneX(state.entities[carIndex].position), roadY() - 30.0));
    }
}

void MotionScene::drawCar(QPainter* painter, const QPointF& center, double dir, const QColor& color,
                          const QString& label, bool moving, bool waiting) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Габариты кузова и кабины (маштабированы под «учебную» схему).
    const QRectF body(-34, -18, 68, 18);
    const QRectF cabin(-16, -29, 32, 13);

    QColor fill = color;
    if (waiting)
        fill = QColor(152, 157, 164);

    painter->setPen(QPen(color.darker(160), 1.4));
    painter->setBrush(fill);
    painter->drawRoundedRect(body, 5, 5);
    painter->drawRoundedRect(cabin, 4, 4);

    // Стекно кабины.
    painter->setBrush(QColor(211, 232, 248));
    painter->drawRoundedRect(cabin.adjusted(4, 3, -4, -2), 3, 3);

    // Колёса.
    painter->setBrush(QColor(40, 44, 50));
    painter->setPen(QPen(QColor(20, 22, 26), 1.0));
    painter->drawEllipse(QPointF(-20, 0), 6.5, 6.5);
    painter->drawEllipse(QPointF(20, 0), 6.5, 6.5);

    // Фара показывает направление движения.
    painter->setBrush(QColor(250, 214, 110));
    painter->setPen(Qt::NoPen);
    const double headX = dir >= 0 ? body.right() : body.left();
    painter->drawEllipse(QPointF(headX, -12), 3.0, 3.0);
    painter->restore();

    LabScene::drawTag(painter, QPointF(center.x(), center.y() - 40.0), label,
                      QColor(255, 255, 255, 235), color.darker(120));
    Q_UNUSED(moving);
}

void MotionScene::paintScheme(QPainter* painter) {
    auto* motion = qobject_cast<models::MotionModel*>(m_model);
    if (!motion)
        return;
    const SimState state = motion->currentState();
    const double y = roadY();

    // ---- Дорога ----
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(86, 90, 97));
    const QRectF road(m_viewArea.left(), y - 15.0, m_viewArea.width(), 32.0);
    painter->drawRect(road);
    // Осевая прерывистая разметка.
    QPen dash(QColor(240, 240, 235), 2.0, Qt::DashLine);
    dash.setDashPattern({3.0, 3.0});
    painter->setPen(dash);
    painter->drawLine(QPointF(road.left() + 8, y + 1.0), QPointF(road.right() - 8, y + 1.0));
    // Обочина-тротуар.
    painter->setPen(QPen(QColor(120, 124, 130), 1.0));
    painter->setBrush(QColor(228, 228, 222));
    painter->drawRect(QRectF(road.left(), road.bottom(), road.width(), 8));
    painter->restore();

    // ---- Координатная шкала под дорогой ----
    painter->save();
    painter->setPen(QPen(style::inkSoft(), 1.0));
    painter->setFont(style::uiFont(8.0));
    const double scaleTop = road.bottom() + 6.0;
    painter->drawLine(QPointF(m_viewArea.left(), scaleTop), QPointF(m_viewArea.right(), scaleTop));
    const double span = m_worldMax - m_worldMin;
    const double stepKm = span > 400 ? 100.0 : (span > 120 ? 50.0 : (span > 40 ? 20.0 : 5.0));
    const double firstTick = std::ceil(m_worldMin / stepKm) * stepKm;
    for (double km = firstTick; km <= m_worldMax + 1e-6; km += stepKm) {
        const double x = toSceneX(km);
        painter->drawLine(QPointF(x, scaleTop - 4), QPointF(x, scaleTop + 4));
        painter->drawText(QRectF(x - 30, scaleTop + 6, 60, 13), Qt::AlignHCenter,
                          utils::formatNumber(km, 0));
    }
    painter->drawText(QRectF(m_viewArea.right() - 60, scaleTop + 6, 60, 13), Qt::AlignRight | Qt::AlignVCenter,
                      QStringLiteral("x, км"));
    painter->restore();

    // ---- Автомобили ----
    for (const EntityVisual& car : state.entities) {
        const double cx = toSceneX(car.position);
        const double dir = car.value1 >= 0 ? 1.0 : -1.0;
        const bool waiting = m_model->time() <= car.value2 + 1e-9;
        painter->save();
        painter->translate(cx, y - 2.0);
        drawCar(painter, QPointF(0, 0), dir, style::variantColor(car.variant), car.label, car.active, waiting);
        painter->restore();
    }

    // ---- Расстояние между автомобилями ----
    const double x1 = toSceneX(state.entities.first().position);
    const double x2 = toSceneX(state.entities.at(1).position);
    LabScene::drawDimension(painter, QPointF(x1, y - 74.0), QPointF(x2, y - 74.0),
                            QStringLiteral("s = %1 км").arg(utils::formatNumber(state.primary, 1)),
                            style::ink());

    // ---- Векторы скоростей ----
    for (const EntityVisual& car : state.entities) {
        const double speed = car.value1;
        if (std::abs(speed) < 1e-9)
            continue;
        const double cx = toSceneX(car.position);
        const double length = std::clamp(std::abs(speed) * 0.8, 24.0, 110.0) * (speed >= 0 ? 1.0 : -1.0);
        const QPointF origin(cx, y - 52.0);
        const QColor color = style::variantColor(car.variant);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(color, 2.6));
        painter->drawLine(origin, origin + QPointF(length, 0));
        const double s = length > 0 ? 1.0 : -1.0;
        const QPointF tip = origin + QPointF(length, 0);
        painter->setBrush(color);
        painter->setPen(Qt::NoPen);
        QPolygonF head;
        head << tip << tip - QPointF(10.0 * s, -5.0) << tip - QPointF(10.0 * s, 5.0);
        painter->drawPolygon(head);
        painter->restore();
        LabScene::drawTag(painter, origin + QPointF(length / 2.0, -15.0),
                          QStringLiteral("v = %1 км/ч").arg(utils::formatNumber(speed, 0)),
                          QColor(255, 255, 255, 230), color.darker(120));
    }

    // ---- Отметка момента события ----
    const double event = motion->meetingTime();
    if (std::isfinite(event)) {
        const double meetX = toSceneX(motion->positionOf(0, event));
        painter->save();
        painter->setPen(QPen(style::success(), 1.2, Qt::DashLine));
        painter->drawLine(QPointF(meetX, m_viewArea.top() + 2.0), QPointF(meetX, y - 22.0));
        painter->restore();
        LabScene::drawTag(painter, QPointF(meetX, m_viewArea.top() + 12.0),
                          QStringLiteral("t = %1 ч").arg(utils::formatNumber(event, 2)),
                          QColor(234, 247, 239, 235), style::success().darker(120));
    }

    // ---- График x(t) и расстояния ----
    const double duration = std::max(0.5, m_model->duration());
    const QRectF plotRect(m_viewArea.left() + 6.0, m_viewArea.bottom() - std::min(m_viewArea.height() * 0.36, 230.0),
                          m_viewArea.width() - 12.0, std::min(m_viewArea.height() * 0.36, 230.0));
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

}  // namespace lab::scenes
