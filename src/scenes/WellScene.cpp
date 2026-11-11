#include "scenes/WellScene.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

#include "models/WellModel.h"
#include "scenes/SchemePrimitives.h"
#include "scenes/Style.h"
#include "utils/MathText.h"

namespace lab::scenes {
namespace {

constexpr double kMaxDepth = 60.0;   // паспортный максимум глубины модели

}  // namespace

WellScene::WellScene(Simulation* model, QObject* parent) : LabScene(model, parent) {}

QRectF WellScene::shaftRect() const {
    // Зона шахты фиксирована: масштаб пикселей на метр не зависит от
    // текущего значения глубины, поэтому перетаскивание уровня воды
    // не «дёргается».
    const QRectF zone(m_viewArea.left() + m_viewArea.width() * 0.16, m_viewArea.top() + 58.0,
                      m_viewArea.width() * 0.28, m_viewArea.height() - 110.0);
    auto* well = qobject_cast<models::WellModel*>(m_model);
    const double depthValue = well ? std::clamp(well->depth(), 0.5, kMaxDepth) : 12.0;
    // Высота шахты строго соответствует глубине в постоянном масштабе.
    const double heightPx = std::max(24.0, depthValue * (zone.height() / kMaxDepth));
    return QRectF(zone.center().x() - 46.0, zone.top(), 92.0, heightPx);
}

double WellScene::depthToY(double depthValue) const {
    const QRectF zone(m_viewArea.left() + m_viewArea.width() * 0.16, m_viewArea.top() + 58.0,
                      m_viewArea.width() * 0.28, m_viewArea.height() - 110.0);
    const double scale = zone.height() / kMaxDepth;
    return zone.top() + std::max(0.0, depthValue) * scale;
}

double WellScene::yToDepth(qreal y) const {
    const QRectF zone(m_viewArea.left() + m_viewArea.width() * 0.16, m_viewArea.top() + 58.0,
                      m_viewArea.width() * 0.28, m_viewArea.height() - 110.0);
    const double scale = zone.height() / kMaxDepth;
    return (y - zone.top()) / scale;
}

void WellScene::reconfigure(const QRectF& area) {
    setRanges(area, 0.0, 1.0, 0.0, kMaxDepth);
}

double WellScene::mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const {
    if (info.id == QLatin1String("depth"))
        return yToDepth(scenePos.y());
    return info.value;
}

double WellScene::handleScenePosition(const HandleInfo& info) const {
    if (info.id == QLatin1String("depth"))
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
    }
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

    // ---- Стенки шахты (разрез) ----
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
    painter->restore();

    // ---- Вода на дне шахты ----
    primitives::drawVessel(painter, QRectF(shaft.left(), waterY - 30.0, shaft.width(), 30.0), 1.0,
                           QColor(130, 96, 74), style::water(), 2.0);

    // ---- Камень ----
    const double stoneDepth = state.entities.first().position;
    const double stoneY = depthToY(stoneDepth);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(QColor(92, 96, 102));
    painter->setPen(QPen(QColor(58, 61, 66), 1.4));
    QPolygonF stone;
    stone << QPointF(-8, -6) << QPointF(6, -8) << QPointF(9, 4) << QPointF(-2, 8) << QPointF(-9, 2);
    painter->translate(shaft.center().x(), stoneY);
    painter->drawPolygon(stone);
    painter->restore();
    if (state.entities.first().active) {
        LabScene::drawTag(painter, QPointF(shaft.center().x() + 74.0, stoneY),
                          QStringLiteral("v = %1 м/с").arg(utils::formatNumber(state.entities.first().value1, 1)),
                          QColor(255, 255, 255, 225), style::bodyC().darker(130));
    }

    // ---- Звуковая волна ----
    if (state.entities.at(1).active) {
        const double frontY = depthToY(state.entities.at(1).position);
        primitives::drawSoundWaves(painter, QPointF(shaft.center().x(), waterY - 24.0),
                                   std::max(14.0, waterY - frontY), style::bodyD(), m_model->time() * 1.6, 4);
    }

    // ---- Размерная линия глубины ----
    LabScene::drawDimension(painter, QPointF(shaft.left() - 40.0, groundY), QPointF(shaft.left() - 40.0, waterY),
                            QStringLiteral("h = %1 м").arg(utils::formatNumber(depthValue, 2)), style::ink(), -40.0);

    // ---- Подписки этапов у шахты ----
    const double tf = well->fallTime();
    const double ts = well->soundTime();
    LabScene::drawTag(painter, QPointF(shaft.center().x(), m_viewArea.top() + 26.0),
                      QStringLiteral("t₁ = √(2h/g) = %1 с").arg(utils::formatNumber(tf, 3)),
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
                      QStringLiteral("T = t₁ + t₂ = %1 с")
                          .arg(utils::formatNumber(tf + ts, 3)),
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
