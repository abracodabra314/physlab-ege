#include "scenes/BoatScene.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

#include "models/BoatModel.h"
#include "scenes/SchemePrimitives.h"
#include "scenes/Style.h"
#include "utils/MathText.h"

namespace lab::scenes {
namespace {

constexpr double kMargin = 60.0;

}  // namespace

BoatScene::BoatScene(Simulation* model, QObject* parent) : LabScene(model, parent) {}

double BoatScene::waterY() const {
    return m_viewArea.top() + m_viewArea.height() * 0.42;
}

void BoatScene::reconfigure(const QRectF& area) {
    auto* boat = qobject_cast<models::BoatModel*>(m_model);
    const double L = boat ? boat->distanceBetweenPorts() : 120.0;
    // Небольшой запас по краям, чтобы пристани не прижимались к границе.
    const double pad = L * 0.10 + 8.0;
    const QRectF world(area.left() + kMargin, area.top() + 30.0, area.width() - 2 * kMargin,
                       area.height() * 0.62);
    setRanges(world, -pad, L + pad, 0.0, std::max(10.0, L));
}

void BoatScene::positionHandles() {
    auto* boat = qobject_cast<models::BoatModel*>(m_model);
    if (!boat)
        return;
    const double L = boat->distanceBetweenPorts();
    const SimState state = boat->currentState();

    for (DragHandleItem* handle : m_handles) {
        if (handle->id() == QLatin1String("L")) {
            handle->setAnchor(QPointF(toSceneX(L), waterY() - 86.0));
            continue;
        }
        const int index = handle->id() == QLatin1String("x1") ? 0 : 1;
        if (index < 0 || index >= state.entities.size())
            continue;
        handle->setAnchor(QPointF(toSceneX(state.entities[index].position), waterY() - 34.0));
    }
}

void BoatScene::paintScheme(QPainter* painter) {
    auto* boat = qobject_cast<models::BoatModel*>(m_model);
    if (!boat)
        return;
    const SimState state = boat->currentState();
    const double L = boat->distanceBetweenPorts();
    const double water = waterY();
    const double xA = toSceneX(0.0);
    const double xB = toSceneX(L);
    const double riverTop = water - 26.0;
    const double riverBottom = water + 120.0;
    const QRectF river(m_viewArea.left(), riverTop, m_viewArea.width(), riverBottom - riverTop);

    // ---- Берега и вода ----
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(style::sand().lighter(106));
    painter->drawRect(QRectF(river.left(), m_viewArea.top(), river.width(), riverTop - m_viewArea.top()));
    painter->setBrush(style::water());
    painter->drawRect(river);
    // Верхняя кромка воды.
    QPen waterLine(style::waterDeep().darker(110), 1.4);
    painter->setPen(waterLine);
    painter->drawLine(QPointF(river.left(), riverTop), QPointF(river.right(), riverTop));
    painter->restore();

    // Стрелки течения внутри воды.
    primitives::drawCurrentArrow(painter,
                                 QRectF(river.left() + 40, riverTop + 40, river.width() - 80, 60),
                                 style::waterDeep(),
                                 QStringLiteral("течение → vт = %1 км/ч").arg(utils::formatNumber(boat->currentSpeed(), 1)));

    // ---- Пристани ----
    primitives::drawPier(painter, xA, riverTop, 40.0, QColor(120, 92, 66));
    primitives::drawPier(painter, xB, riverTop, 40.0, QColor(120, 92, 66));
    LabScene::drawTag(painter, QPointF(xA, riverTop - 58.0), QStringLiteral("Пристань А"),
                      QColor(255, 255, 255, 235), QColor(90, 70, 50));
    LabScene::drawTag(painter, QPointF(xB, riverTop - 58.0), QStringLiteral("Пристань Б"),
                      QColor(255, 255, 255, 235), QColor(90, 70, 50));

    // ---- Размерная линия L ----
    LabScene::drawDimension(painter, QPointF(xA, m_viewArea.top() + 12.0), QPointF(xB, m_viewArea.top() + 12.0),
                            QStringLiteral("L = %1 км").arg(utils::formatNumber(L, 0)), style::ink());

    // ---- Теплоходы ----
    for (const EntityVisual& entity : state.entities) {
        const double x = toSceneX(entity.position);
        const bool headingRight = entity.value1 >= 0;
        painter->save();
        painter->translate(x, riverTop + 2.0);
        primitives::drawBoat(painter, QPointF(0, 0), style::variantColor(entity.variant),
                             entity.active, headingRight);
        painter->restore();
        // Подпись теплохода — над причальными метками, чтобы не перекрывать корпус.
        LabScene::drawTag(painter, QPointF(x, riverTop - 84.0), entity.label,
                          QColor(255, 255, 255, 235), style::variantColor(entity.variant).darker(120));

        // Вектор скорости относительно берега.
        const double speed = entity.value1;
        if (std::abs(speed) > 1e-9 && entity.active) {
            const double len = std::clamp(std::abs(speed) * 3.2, 22.0, 110.0) * (speed > 0 ? 1.0 : -1.0);
            const QPointF origin(x, riverTop + 66.0);
            const QColor color = style::variantColor(entity.variant);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(QPen(color, 2.6));
            painter->drawLine(origin, origin + QPointF(len, 0));
            const double s = len > 0 ? 1.0 : -1.0;
            const QPointF tip = origin + QPointF(len, 0);
            painter->setBrush(color);
            painter->setPen(Qt::NoPen);
            QPolygonF head;
            head << tip << tip - QPointF(10.0 * s, -5.0) << tip - QPointF(10.0 * s, 5.0);
            painter->drawPolygon(head);
            painter->restore();
            LabScene::drawTag(painter, origin + QPointF(len / 2.0, 18.0),
                              QStringLiteral("v = %1 км/ч").arg(utils::formatNumber(speed, 1)),
                              QColor(255, 255, 255, 230), color.darker(120));
        } else if (!entity.active && m_model->time() <= entity.value2 + 1e-9) {
            LabScene::drawTag(painter, QPointF(x, riverTop + 66.0),
                              QStringLiteral("отправление через %1 ч").arg(utils::formatNumber(entity.value2 - m_model->time(), 2)),
                              QColor(255, 250, 235, 235), style::accentWarm());
        }
    }

    // ---- Расстояние между теплоходами ----
    const double x1 = toSceneX(state.entities.first().position);
    const double x2 = toSceneX(state.entities.at(1).position);
    LabScene::drawDimension(painter, QPointF(x1, riverBottom + 18.0), QPointF(x2, riverBottom + 18.0),
                            QStringLiteral("s = %1 км").arg(utils::formatNumber(state.primary, 1)),
                            style::inkSoft());

    // ---- Отметки времени прибытия ----
    struct ArrivalMark {
        double time;
        QColor color;
    };
    const QVector<ArrivalMark> marks = {
        {boat->arrivalTimeOf(0), style::bodyA()},
        {boat->arrivalTimeOf(1), style::bodyB()},
    };
    const double duration = std::max(0.5, m_model->duration());
    const QRectF timeline(m_viewArea.left() + 8.0, m_viewArea.bottom() - 44.0, m_viewArea.width() - 16.0, 22.0);
    painter->save();
    painter->setPen(QPen(style::inkSoft(), 1.2));
    painter->setBrush(QColor(255, 255, 255, 200));
    painter->drawRoundedRect(timeline, 6, 6);
    painter->setPen(style::ink());
    painter->setFont(style::uiFont(8.0));
    painter->drawText(QRectF(timeline.left(), timeline.top() - 16, 90, 14), Qt::AlignLeft,
                      QStringLiteral("t = 0"));
    for (const ArrivalMark& mark : marks) {
        if (!std::isfinite(mark.time) || mark.time > duration)
            continue;
        const double x = timeline.left() + (mark.time / duration) * timeline.width();
        painter->setPen(QPen(mark.color, 2.0));
        painter->drawLine(QPointF(x, timeline.top() + 2), QPointF(x, timeline.bottom() - 2));
        painter->drawText(QRectF(x - 60, timeline.bottom() + 1, 120, 14), Qt::AlignHCenter,
                          QStringLiteral("%1 ч").arg(utils::formatNumber(mark.time, 2)));
    }
    // Текущий момент.
    const double currentX = timeline.left() + std::clamp(m_model->time() / duration, 0.0, 1.0) * timeline.width();
    painter->setPen(QPen(style::danger(), 1.6));
    painter->drawLine(QPointF(currentX, timeline.top() - 3), QPointF(currentX, timeline.bottom() + 3));
    painter->restore();
}

}  // namespace lab::scenes
