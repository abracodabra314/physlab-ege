#include "scenes/BoatScene.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

#include "models/BoatModel.h"
#include "scenes/SchemePrimitives.h"
#include "scenes/Style.h"
#include "scenes/VectorArrows.h"
#include "utils/MathText.h"

namespace lab::scenes {
namespace {

constexpr double kMargin = 70.0;     // отступ русла от краёв области

}  // namespace

BoatScene::BoatScene(Simulation* model, QObject* parent) : LabScene(model, parent) {}

double BoatScene::waterY() const {
    return m_viewArea.top() + m_viewArea.height() * 0.42;
}

double BoatScene::riverDepth() const {
    return std::clamp(m_viewArea.height() * 0.30, 90.0, 170.0);
}

double BoatScene::laneOffset() const {
    return std::clamp(riverDepth() * 0.16, 14.0, 30.0);
}

double BoatScene::boatScale() const {
    return std::clamp(m_viewArea.width() / 1350.0, 0.72, 1.2);
}

QRectF BoatScene::riverRect() const {
    const double water = waterY();
    return QRectF(m_viewArea.left(), water - 14.0, m_viewArea.width(), riverDepth() + 14.0);
}

void BoatScene::reconfigure(const QRectF& area) {
    auto* boat = qobject_cast<models::BoatModel*>(m_model);
    const double L = boat ? boat->distanceBetweenPorts() : 120.0;
    // Ось вдоль русла: координата 0 км — пристань A (слева), «+» — вправо,
    // по направлению течения. Небольшой запас по краям, чтобы пристани
    // не прижимались к границе области.
    const double pad = L * 0.08 + 6.0;
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
    const double water = waterY();

    for (DragHandleItem* handle : m_handles) {
        if (handle->id() == QLatin1String("L")) {
            // Ручка пристани Б — на берегу, перемещается только по горизонтали.
            handle->setAnchor(QPointF(toSceneX(L), water - 96.0));
            continue;
        }
        const int index = handle->id() == QLatin1String("x1") ? 0 : 1;
        if (index < 0 || index >= state.entities.size())
            continue;
        // Ручка теплохода — на его корпусе; движение вдоль русла.
        const double laneY = water + 6.0 + (index == 0 ? -laneOffset() : laneOffset());
        handle->setAnchor(QPointF(toSceneX(state.entities[index].position), laneY - 30.0 * boatScale()));
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
    const QRectF river = riverRect();
    const double riverBottom = river.bottom();
    const double s = boatScale();

    // ---- Берега и вода ----
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    // Верхний берег.
    painter->setBrush(style::sand().lighter(106));
    painter->drawRect(QRectF(river.left(), m_viewArea.top(), river.width(), river.top() - m_viewArea.top()));
    // Нижний берег.
    painter->drawRect(QRectF(river.left(), riverBottom, river.width(), m_viewArea.bottom() - riverBottom));
    // Русло.
    QLinearGradient gradient(river.topLeft(), river.bottomLeft());
    gradient.setColorAt(0.0, style::water().lighter(112));
    gradient.setColorAt(1.0, style::waterDeep());
    painter->setBrush(gradient);
    painter->drawRect(river);
    // Кромка воды у верхней бровки.
    painter->setPen(QPen(style::waterDeep().darker(110), 1.4));
    painter->drawLine(QPointF(river.left(), river.top()), QPointF(river.right(), river.top()));
    // Бровки берегов.
    painter->setPen(QPen(style::sand().darker(140), 1.2));
    painter->drawLine(QPointF(river.left(), riverBottom), QPointF(river.right(), riverBottom));
    painter->restore();

    // Ориентиры на берегах (условные знаки, без декоративной анимации).
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    for (int i = 1; i <= 3; ++i) {
        const double x = river.left() + river.width() * i / 4.0;
        painter->setPen(QPen(QColor(104, 132, 92), 1.4));
        painter->setBrush(QColor(126, 158, 110));
        painter->drawEllipse(QPointF(x, river.top() - 34.0), 6.5, 6.5);
        painter->setPen(QPen(QColor(120, 92, 66), 1.6));
        painter->setBrush(QColor(120, 92, 66));
        painter->drawRect(QRectF(x - 1.4, river.top() - 28.0, 2.8, 8.0));
        painter->setPen(QPen(QColor(104, 132, 92), 1.2));
        painter->setBrush(QColor(126, 158, 110));
        painter->drawEllipse(QPointF(x + 26.0, riverBottom + 22.0), 5.0, 5.0);
    }
    painter->restore();

    // ---- Стрелки течения на поверхности воды ----
    primitives::drawCurrentArrow(painter,
                                 QRectF(river.left() + 40, river.top() + riverDepth() * 0.55,
                                        river.width() - 80, 46.0),
                                 style::waterDeep(), QStringLiteral("течение"));

    // ---- Пристани ----
    const bool firstArrived = std::isfinite(boat->arrivalTimeOf(0)) && m_model->time() >= boat->arrivalTimeOf(0) - 1e-9;
    const bool secondArrived = std::isfinite(boat->arrivalTimeOf(1)) && m_model->time() >= boat->arrivalTimeOf(1) - 1e-9;
    primitives::drawPier(painter, xA, river.top(), 40.0, QColor(120, 92, 66));
    primitives::drawPier(painter, xB, river.top(), 40.0, QColor(120, 92, 66));
    // Причалы назначения подсвечиваются в момент прибытия.
    painter->save();
    painter->setPen(QPen(style::success(), 2.2, Qt::DashLine));
    painter->setBrush(Qt::NoBrush);
    if (firstArrived)
        painter->drawRoundedRect(QRectF(xA - 30, river.top() - 22, 60, 30), 6, 6);
    if (secondArrived)
        painter->drawRoundedRect(QRectF(xB - 30, river.top() - 22, 60, 30), 6, 6);
    painter->restore();

    LabScene::drawTag(painter, QPointF(xA, river.top() - 58.0), QStringLiteral("Пристань А (x = 0)"),
                      QColor(255, 255, 255, 235), QColor(90, 70, 50));
    LabScene::drawTag(painter, QPointF(xB, river.top() - 58.0),
                      QStringLiteral("Пристань Б (x = %1 км)").arg(utils::formatNumber(L, 0)),
                      QColor(255, 255, 255, 235), QColor(90, 70, 50));

    // ---- Размерная линия расстояния между пристанями ----
    LabScene::drawDimension(painter, QPointF(xA, m_viewArea.top() + 12.0), QPointF(xB, m_viewArea.top() + 12.0),
                            QStringLiteral("S = %1 км").arg(utils::formatNumber(L, 0)), style::ink());

    // ---- Векторы: собственная скорость + скорость течения = скорость относительно берега ----
    const double u = boat->currentSpeed();
    double maxVectorMagnitude = std::max(1.0, u);
    for (const EntityVisual& entity : state.entities) {
        maxVectorMagnitude = std::max(maxVectorMagnitude, std::abs(entity.value1));
        maxVectorMagnitude = std::max(maxVectorMagnitude, std::abs(boat->ownSpeedOf(entity.variant)));
    }
    const double pxPerUnit = vectors::autoScale(maxVectorMagnitude, 96.0 * s * m_model->vectorScale());

    // ---- Теплоходы ----
    for (const EntityVisual& entity : state.entities) {
        const int index = entity.variant;
        const double x = toSceneX(entity.position);
        const double laneY = water + 6.0 + (index == 0 ? -laneOffset() : laneOffset());
        const bool headingRight = entity.value1 >= 0.0;

        painter->save();
        painter->translate(x, laneY);
        painter->scale(s, s);
        primitives::drawBoat(painter, QPointF(0, 0), style::variantColor(entity.variant),
                             entity.active, headingRight);
        painter->restore();

        LabScene::drawTag(painter, QPointF(x, laneY - 74.0 * s), entity.label,
                          QColor(255, 255, 255, 235), style::variantColor(entity.variant).darker(120));

        const double own = boat->ownSpeedOf(index);
        const double shore = entity.value1;   // скорость относительно берега (знакованная)
        const bool departed = m_model->time() > entity.value2 + 1e-9;

        if (m_model->showsVelocities() && departed) {
            // Три вектора в одном масштабе: собственная (по воде), течение,
            // результирующая относительно берега. Чисел у стрелок нет.
            const double ownLength = vectors::visualLength(own, pxPerUnit, 22.0, 120.0 * s);
            const double currentLength = vectors::visualLength(u, pxPerUnit, 14.0, 120.0 * s);
            const double shoreLength = vectors::visualLength(shore, pxPerUnit, 14.0, 120.0 * s);
            const double arrowY = laneY + 20.0 * s;

            // Слагаемые: собственная скорость — по направлению рейса.
            vectors::Arrow ownArrow;
            ownArrow.from = QPointF(x, arrowY - 15.0);
            ownArrow.to = ownArrow.from + QPointF(own >= 0 ? ownLength : -ownLength, 0.0);
            ownArrow.symbol = index == 0 ? QStringLiteral("v₁ по воде") : QStringLiteral("v₂ по воде");
            ownArrow.kind = vectors::Kind::Velocity;
            ownArrow.color = style::variantColor(entity.variant);
            ownArrow.width = 2.2;
            ownArrow.dashed = true;
            ownArrow.labelOffset = QPointF(0.0, -14.0);
            vectors::drawArrow(painter, ownArrow);

            // Скорость течения — всегда в сторону пристани Б.
            vectors::Arrow flowArrow;
            flowArrow.from = QPointF(x, arrowY + 2.0);
            flowArrow.to = flowArrow.from + QPointF(currentLength, 0.0);
            flowArrow.symbol = QStringLiteral("vт");
            flowArrow.kind = vectors::Kind::Velocity;
            flowArrow.color = style::waterDeep().darker(115);
            flowArrow.width = 2.0;
            flowArrow.dashed = true;
            flowArrow.labelOffset = QPointF(0.0, 13.0);
            vectors::drawArrow(painter, flowArrow);

            // Результирующая — фактическое движение относительно берега.
            if (std::abs(shore) > 1e-9) {
                vectors::Arrow shoreArrow;
                shoreArrow.from = QPointF(x, arrowY + 19.0);
                shoreArrow.to = shoreArrow.from + QPointF(shore >= 0 ? shoreLength : -shoreLength, 0.0);
                shoreArrow.symbol = index == 0 ? QStringLiteral("v₁ по берегу")
                                               : QStringLiteral("v₂ по берегу");
                shoreArrow.kind = vectors::Kind::Velocity;
                shoreArrow.color = style::variantColor(entity.variant).darker(125);
                shoreArrow.width = 3.0;
                shoreArrow.labelOffset = QPointF(0.0, 15.0);
                vectors::drawArrow(painter, shoreArrow);
            }
        }

        if (!departed) {
            LabScene::drawTag(painter, QPointF(x, laneY + 22.0 * s),
                              QStringLiteral("отправление через %1 ч")
                                  .arg(utils::formatNumber(entity.value2 - m_model->time(), 2)),
                              QColor(255, 250, 235, 235), style::accentWarm());
        } else if (entity.active && std::abs(shore) < 1e-9) {
            LabScene::drawTag(painter, QPointF(x, laneY + 22.0 * s), QStringLiteral("стоит на месте"),
                              QColor(255, 246, 245, 235), style::danger());
        }
    }

    // ---- Расстояние между теплоходами ----
    const double x1 = toSceneX(state.entities.first().position);
    const double x2 = toSceneX(state.entities.at(1).position);
    if (state.primary > 1e-6) {
        LabScene::drawDimension(painter, QPointF(x1, riverBottom + 30.0), QPointF(x2, riverBottom + 30.0),
                                QStringLiteral("s = %1 км").arg(utils::formatNumber(state.primary, 1)),
                                style::inkSoft());
    }

    // ---- Масштабный ряд отметок пути на нижней бровке ----
    painter->save();
    painter->setPen(QPen(style::inkSoft(), 1.0));
    painter->setFont(style::uiFont(7.5));
    const double kmStep = L > 300 ? 50.0 : (L > 120 ? 25.0 : 10.0);
    for (double km = kmStep; km < L; km += kmStep) {
        const double x = toSceneX(km);
        painter->drawLine(QPointF(x, riverBottom), QPointF(x, riverBottom + 6.0));
    }
    painter->restore();

    if (!state.eventLabel.isEmpty()) {
        LabScene::drawTag(painter, QPointF(m_viewArea.center().x(), riverBottom + 52.0), state.eventLabel,
                          QColor(234, 247, 239, 240), style::success().darker(130));
    }
}

}  // namespace lab::scenes
