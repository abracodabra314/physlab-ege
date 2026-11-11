#include "scenes/WellScene.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

#include "models/WellModel.h"
#include "scenes/SchemePrimitives.h"
#include "scenes/Style.h"
#include "scenes/VectorArrows.h"
#include "utils/MathText.h"

namespace lab::scenes {
namespace {

constexpr double kMaxDepth = 60.0;   // паспортный максимум глубины модели

}  // namespace

WellScene::WellScene(Simulation* model, QObject* parent) : LabScene(model, parent) {}

QRectF WellScene::shaftZone() const {
    return QRectF(m_viewArea.left() + m_viewArea.width() * 0.16, m_viewArea.top() + 96.0,
                  m_viewArea.width() * 0.28, m_viewArea.height() - 150.0);
}

QRectF WellScene::shaftRect() const {
    // Зона шахты фиксирована: масштаб пикселей на метр не зависит от
    // текущего значения глубины, поэтому перетаскивание уровня воды
    // не «дёргается».
    const QRectF zone = shaftZone();
    auto* well = qobject_cast<models::WellModel*>(m_model);
    const double depthValue = well ? std::clamp(well->depth(), 0.5, kMaxDepth) : 12.0;
    // Высота шахты строго соответствует глубине в постоянном масштабе.
    const double heightPx = std::max(24.0, depthValue * pxPerMeter());
    return QRectF(zone.center().x() - 46.0, zone.top(), 92.0, heightPx);
}

double WellScene::pxPerMeter() const {
    return shaftZone().height() / kMaxDepth;
}

double WellScene::depthToY(double depthValue) const {
    return shaftZone().top() + std::max(0.0, depthValue) * pxPerMeter();
}

double WellScene::yToDepth(qreal y) const {
    return (y - shaftZone().top()) / pxPerMeter();
}

void WellScene::reconfigure(const QRectF& area) {
    setRanges(area, 0.0, 1.0, 0.0, kMaxDepth);
}

double WellScene::mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const {
    if (info.id == QLatin1String("depth") || info.id == QLatin1String("release"))
        return yToDepth(scenePos.y());
    return info.value;
}

double WellScene::handleScenePosition(const HandleInfo& info) const {
    if (info.id == QLatin1String("depth") || info.id == QLatin1String("release"))
        return depthToY(info.value);
    return 0.0;
}

void WellScene::positionHandles() {
    auto* well = qobject_cast<models::WellModel*>(m_model);
    if (!well)
        return;
    const QRectF shaft = shaftRect();
    for (DragHandleItem* handle : m_handles) {
        if (handle->id() == QLatin1String("depth"))
            handle->setAnchor(QPointF(shaft.right() + 40.0, depthToY(well->depth())));
        else if (handle->id() == QLatin1String("release"))
            handle->setAnchor(QPointF(shaft.center().x(), depthToY(well->releaseDepth())));
    }
}

void WellScene::drawObserver(QPainter* painter, const QPointF& groundPos) {
    // Фиксированный наблюдатель у края шахты: schematicкая фигура.
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(style::ink(), 2.0, Qt::SolidLine, Qt::RoundCap));
    painter->setBrush(Qt::NoBrush);
    painter->drawLine(QPointF(groundPos.x(), groundPos.y() - 44.0), QPointF(groundPos.x(), groundPos.y() - 22.0));
    painter->drawEllipse(QPointF(groundPos.x(), groundPos.y() - 51.0), 7.0, 7.0);
    // Рука, протянутая к устью.
    painter->drawLine(QPointF(groundPos.x(), groundPos.y() - 38.0), QPointF(groundPos.x() + 14.0, groundPos.y() - 30.0));
    // Ноги.
    painter->drawLine(QPointF(groundPos.x(), groundPos.y() - 22.0), QPointF(groundPos.x() - 7.0, groundPos.y()));
    painter->drawLine(QPointF(groundPos.x(), groundPos.y() - 22.0), QPointF(groundPos.x() + 7.0, groundPos.y()));
    painter->restore();
    LabScene::drawTag(painter, QPointF(groundPos.x(), groundPos.y() - 74.0), QStringLiteral("наблюдатель"),
                      QColor(255, 255, 255, 225), style::inkSoft());
}

void WellScene::paintScheme(QPainter* painter) {
    auto* well = qobject_cast<models::WellModel*>(m_model);
    if (!well)
        return;
    const SimState state = well->currentState();
    const QRectF shaft = shaftRect();
    const double groundY = shaft.top();
    const double waterY = shaft.bottom();
    const double depthValue = well->depth();
    const double releaseY = depthToY(well->releaseDepth());

    // ---- Грунт вокруг шахты ----
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(style::sand());
    const double groundBottom = waterY + 34.0;
    painter->drawRect(QRectF(m_viewArea.left(), groundY, shaft.left() - m_viewArea.left(), groundBottom - groundY));
    painter->drawRect(QRectF(shaft.right(), groundY, m_viewArea.right() - shaft.right(), groundBottom - groundY));
    // Дёрн.
    painter->setBrush(QColor(141, 171, 121));
    painter->drawRect(QRectF(m_viewArea.left(), groundY - 7.0, shaft.left() - m_viewArea.left(), 7.0));
    painter->drawRect(QRectF(shaft.right(), groundY - 7.0, m_viewArea.right() - shaft.right(), 7.0));
    // Штриховка пластов грунта.
    painter->setPen(QPen(style::sand().darker(125), 1.0, Qt::DotLine));
    for (int i = 1; i <= 4; ++i) {
        const double y = groundY + (groundBottom - groundY) * i / 5.0;
        painter->drawLine(QPointF(m_viewArea.left() + 30, y), QPointF(shaft.left() - 12, y));
        painter->drawLine(QPointF(shaft.right() + 12, y), QPointF(m_viewArea.right() - 30, y));
    }
    painter->restore();

    // ---- Стенки шахты (разрез) и дно ----
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(QColor(130, 96, 74), 2.0));
    painter->setBrush(QColor(176, 130, 100));
    painter->drawRect(QRectF(shaft.left() - 12, groundY - 16.0, 12, waterY - groundY + 16.0));
    painter->drawRect(QRectF(shaft.right(), groundY - 16.0, 12, waterY - groundY + 16.0));
    // Венец сруба.
    painter->setBrush(QColor(120, 92, 66));
    painter->drawRect(QRectF(shaft.left() - 22, groundY - 24.0, shaft.width() + 44, 10));
    // Кирпичная кладка стенок внутри шахты.
    painter->setPen(QPen(QColor(130, 96, 74, 150), 1.0));
    painter->setBrush(QColor(198, 154, 124));
    constexpr double brickH = 14.0;
    for (double y = groundY; y < waterY; y += brickH) {
        const double row = std::fmod((y - groundY) / brickH, 2.0) < 1.0 ? 0.0 : 9.0;
        for (double x = shaft.left() + row; x < shaft.right() - 3.0; x += 18.0) {
            const double w = std::min(16.5, shaft.right() - x);
            painter->drawRect(QRectF(x, y, w, brickH - 2.5));
        }
    }
    // Обозначение дна: тёмная полоса.
    painter->setPen(QPen(QColor(70, 52, 40), 2.2));
    painter->drawLine(QPointF(shaft.left(), waterY), QPointF(shaft.right(), waterY));
    painter->restore();

    // ---- Вода на дне шахты ----
    primitives::drawVessel(painter, QRectF(shaft.left(), waterY - 22.0, shaft.width(), 22.0), 1.0,
                           QColor(130, 96, 74), style::water(), 2.0);

    // ---- Наблюдатель у левого края ----
    drawObserver(painter, QPointF(shaft.left() - 46.0, groundY - 7.0));

    // ---- Камень ----
    const EntityVisual& stone = state.entities.first();
    const double stoneDepthValue = stone.position;
    const double stoneY = depthToY(stoneDepthValue);
    const bool falling = stone.active;
    const bool impacted = m_model->time() >= well->fallTime() - 1e-9;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(QColor(92, 96, 102));
    painter->setPen(QPen(QColor(58, 61, 66), 1.4));
    QPolygonF stoneShape;
    stoneShape << QPointF(-8, -6) << QPointF(6, -8) << QPointF(9, 4) << QPointF(-2, 8) << QPointF(-9, 2);
    painter->translate(shaft.center().x(), stoneY);
    painter->drawPolygon(stoneShape);
    painter->restore();

    // Точка отпуска камня.
    if (well->releaseDepth() > 0.05) {
        painter->save();
        painter->setPen(QPen(style::inkSoft(), 1.0, Qt::DashLine));
        painter->drawLine(QPointF(shaft.left() + 4.0, releaseY), QPointF(shaft.right() - 4.0, releaseY));
        painter->restore();
        LabScene::drawTag(painter, QPointF(shaft.right() + 96.0, releaseY),
                          QStringLiteral("отпуск: %1 м").arg(utils::formatNumber(well->releaseDepth(), 1)),
                          QColor(255, 255, 255, 215), style::inkSoft());
    }

    // ---- Векторы камня: mg (постоянно), a и v (только во время падения) ----
    const double g = well->gravity();
    const double k = m_model->vectorScale();
    const double vScale = vectors::autoScale(std::max(1.0, g * well->fallTime()), 90.0 * k);
    const double forceScale = vectors::autoScale(std::max(1.0, g), 58.0 * k);  // mg/m = g
    const QPointF stoneCenter(shaft.center().x(), stoneY);

    // Сила тяжести действует и до начала движения: точка приложения — центр камня.
    if (m_model->showsForces()) {
        vectors::Arrow weight;
        weight.from = stoneCenter + QPointF(0.0, 10.0);
        weight.to = weight.from + QPointF(0.0, vectors::visualLength(g, forceScale, 30.0, 90.0));
        weight.symbol = QStringLiteral("mg");
        weight.kind = vectors::Kind::Gravity;
        weight.width = 2.6;
        weight.labelOffset = QPointF(28.0, 6.0);
        vectors::drawArrow(painter, weight);
    }
    if (falling) {
        if (m_model->showsAccelerations()) {
            vectors::Arrow acceleration;
            acceleration.from = stoneCenter + QPointF(-30.0, 6.0);
            acceleration.to = acceleration.from + QPointF(0.0, vectors::visualLength(g, forceScale, 26.0, 80.0));
            acceleration.symbol = QStringLiteral("a = g");
            acceleration.kind = vectors::Kind::Acceleration;
            acceleration.width = 2.2;
            acceleration.labelAtTail = true;
            acceleration.labelOffset = QPointF(0.0, -14.0);
            vectors::drawArrow(painter, acceleration);
        }
        if (m_model->showsVelocities() && stone.value1 > 1e-9) {
            vectors::Arrow velocity;
            velocity.from = stoneCenter + QPointF(30.0, 6.0);
            velocity.to = velocity.from + QPointF(0.0, vectors::visualLength(stone.value1, vScale, 20.0, 110.0));
            velocity.symbol = QStringLiteral("v");
            velocity.kind = vectors::Kind::Velocity;
            velocity.width = 2.4;
            velocity.labelAtTail = true;
            velocity.labelOffset = QPointF(0.0, -14.0);
            vectors::drawArrow(painter, velocity);
        }
    }
    if (impacted) {
        // После удара движение прекращается: вектор скорости не показываем.
        painter->save();
        painter->setPen(QPen(style::waterDeep(), 1.6));
        painter->setBrush(Qt::NoBrush);
        painter->drawArc(QRectF(shaft.center().x() - 20.0, waterY - 14.0, 40.0, 20.0), 200 * 16, 140 * 16);
        painter->restore();
    }
    // Подпись камня — над ним, чтобы не перекрывать стрелки.
    LabScene::drawTag(painter, QPointF(shaft.center().x(), stoneY - 26.0), QStringLiteral("камень"),
                      QColor(255, 255, 255, 220), style::inkSoft());

    // ---- Звуковая волна (после удара, движение вверх к устью) ----
    if (state.entities.at(1).active) {
        const double frontY = depthToY(state.entities.at(1).position);
        primitives::drawSoundWaves(painter, QPointF(shaft.center().x(), waterY - 16.0),
                                   std::max(14.0, waterY - frontY), style::bodyD(), m_model->time() * 1.6, 4);
        // Условное обозначение направления распространения (не вектор тела).
        painter->save();
        painter->setPen(QPen(style::bodyD().darker(120), 1.6, Qt::DashLine));
        const double midY = (frontY + waterY) / 2.0;
        painter->drawLine(QPointF(shaft.center().x() + 30.0, midY + 14.0),
                          QPointF(shaft.center().x() + 30.0, midY - 14.0));
        painter->setBrush(style::bodyD().darker(120));
        painter->setPen(Qt::NoPen);
        QPolygonF head;
        head << QPointF(shaft.center().x() + 30.0, midY - 18.0)
             << QPointF(shaft.center().x() + 25.5, midY - 8.0) << QPointF(shaft.center().x() + 34.5, midY - 8.0);
        painter->drawPolygon(head);
        painter->restore();
        LabScene::drawTag(painter, QPointF(shaft.center().x() + 64.0, (frontY + groundY) / 2.0),
                          QStringLiteral("звук c"), QColor(255, 255, 255, 215), style::bodyD().darker(130));
    }

    // ---- Размерная линия глубины ----
    LabScene::drawDimension(painter, QPointF(shaft.left() - 40.0, groundY), QPointF(shaft.left() - 40.0, waterY),
                            QStringLiteral("h = %1 м").arg(utils::formatNumber(depthValue, 2)), style::ink(), -40.0);

    // ---- Подписки этапов у шахты ----
    const double tf = well->fallTime();
    const double ts = well->soundTime();
    LabScene::drawTag(painter, QPointF(shaft.center().x(), m_viewArea.top() + 26.0),
                      QStringLiteral("t₁ = √(2(h−h₀)/g) = %1 с").arg(utils::formatNumber(tf, 3)),
                      QColor(255, 255, 255, 232), style::bodyC().darker(130));
    LabScene::drawTag(painter, QPointF(shaft.center().x(), groundBottom + 24.0),
                      QStringLiteral("t₂ = h/c = %1 с").arg(utils::formatNumber(ts, 3)),
                      QColor(255, 255, 255, 232), style::bodyD().darker(130));

    // ---- Диаграмма времени T ----
    const double t = m_model->time();
    const double event = well->echoTime();
    const double rightLeft = m_viewArea.left() + m_viewArea.width() * 0.52;
    const double barWidth = std::max(180.0, m_viewArea.right() - rightLeft - 70.0);
    const QRectF barRect(rightLeft + 30.0, m_viewArea.top() + 100.0, barWidth, 38.0);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(style::inkSoft(), 1.2));
    painter->setBrush(QColor(255, 255, 255, 225));
    painter->drawRoundedRect(barRect, 6, 6);

    const double fallWidth = barRect.width() * (tf / std::max(event, 1e-9));
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(style::bodyC().red(), style::bodyC().green(), style::bodyC().blue(), 150));
    painter->drawRect(QRectF(barRect.left() + 2, barRect.top() + 5, fallWidth - 2, barRect.height() - 10));
    painter->setBrush(QColor(style::bodyD().red(), style::bodyD().green(), style::bodyD().blue(), 150));
    painter->drawRect(QRectF(barRect.left() + fallWidth, barRect.top() + 5,
                             barRect.width() - fallWidth - 2, barRect.height() - 10));
    painter->setPen(style::ink());
    painter->setFont(style::uiFont(8.5, true));
    painter->drawText(QRectF(barRect.left(), barRect.top() + 11, fallWidth, 16), Qt::AlignCenter,
                      QStringLiteral("t₁ = %1 с").arg(utils::formatNumber(tf, 3)));
    painter->drawText(QRectF(barRect.left() + fallWidth, barRect.top() + 11, barRect.width() - fallWidth, 16),
                      Qt::AlignCenter, QStringLiteral("t₂ = %1 с").arg(utils::formatNumber(ts, 3)));

    // Маркер текущего момента времени.
    const double progressX = barRect.left() + std::clamp(t / std::max(event, 1e-9), 0.0, 1.0) * barRect.width();
    painter->setPen(QPen(style::danger(), 1.8));
    painter->drawLine(QPointF(progressX, barRect.top() - 10), QPointF(progressX, barRect.bottom() + 10));
    painter->restore();

    LabScene::drawTag(painter, QPointF(barRect.center().x(), barRect.top() - 34.0),
                      QStringLiteral("T = t₁ + t₂ = %1 с").arg(utils::formatNumber(tf + ts, 3)),
                      QColor(255, 255, 255, 235), style::ink());
    LabScene::drawTag(painter, QPointF(barRect.center().x(), barRect.bottom() + 30.0),
                      QStringLiteral("сейчас t = %1 с").arg(utils::formatNumber(t, 3)),
                      QColor(255, 255, 255, 235), style::danger().darker(120));

    if (!state.eventLabel.isEmpty()) {
        LabScene::drawTag(painter, QPointF(barRect.center().x(), m_viewArea.top() + 54.0), state.eventLabel,
                          QColor(234, 247, 239, 240), style::success().darker(130));
    }
}

}  // namespace lab::scenes
