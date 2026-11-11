#include "scenes/MixtureScene.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

#include "models/MixtureModel.h"
#include "scenes/SchemePrimitives.h"
#include "scenes/Style.h"
#include "utils/MathText.h"

namespace lab::scenes {
namespace {

constexpr double kMinScaleVolume = 200.0;  // минимальная «вместимость» диапазона, л

}  // namespace

MixtureScene::MixtureScene(Simulation* model, QObject* parent) : LabScene(model, parent) {}

QColor MixtureScene::liquidColor(double concentrationPercent) {
    // Раствор: почти бесцветный при 0 % и насыщенный сине-фиолетовый при 100 %.
    const double c = std::clamp(concentrationPercent, 0.0, 100.0) / 100.0;
    return QColor(int(226 - 138 * c), int(231 - 149 * c), int(244 - 58 * c));
}

double MixtureScene::zoneHeight() const {
    return m_viewArea.height() * 0.54;
}

double MixtureScene::litersPerPixel() const {
    auto* mix = qobject_cast<models::MixtureModel*>(m_model);
    const double total = mix ? std::max(mix->totalVolume(), 1.0) : 100.0;
    // Диапазон масштаба: максимум из суммарного объёма и паспортных 200 л,
    // поэтому ёмкости всегда вписываются в зону, а шкалы согласованы между собой.
    return std::max(kMinScaleVolume, total) / zoneHeight();
}

QRectF MixtureScene::vesselRect(int index) const {
    auto* mix = qobject_cast<models::MixtureModel*>(m_model);
    const double lPerPx = litersPerPixel();
    const double bottom = m_viewArea.bottom() - 96.0;

    if (index < 2) {
        const double volume = mix ? mix->volumeOf(index) : 50.0;
        const double height = std::max(24.0, volume / lPerPx);
        const double width = std::max(84.0, m_viewArea.width() * 0.15);
        const double x = m_viewArea.left() + m_viewArea.width() * (0.08 + 0.21 * index);
        return QRectF(x, bottom - height, width, height);
    }
    // Смеситель: вместимость = суммарный объём растворов, шире и выше ёмкостей.
    const double total = mix ? std::max(1.0, mix->totalVolume()) : 100.0;
    const double height = std::max(60.0, total / lPerPx);
    const double width = std::max(110.0, m_viewArea.width() * 0.19);
    const double x = m_viewArea.left() + m_viewArea.width() * 0.62;
    return QRectF(x, bottom - height, width, height);
}

void MixtureScene::reconfigure(const QRectF& area) {
    // Вертикальная шкала — литры в текущем масштабе сцены.
    setRanges(area, 0.0, 1.0, 0.0, litersPerPixel() * std::max(1.0, area.height()));
}

double MixtureScene::mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const {
    if (!info.id.startsWith(QLatin1String("v")))
        return info.value;
    const double bottom = m_viewArea.bottom() - 96.0;
    return (bottom - scenePos.y()) * litersPerPixel();
}

double MixtureScene::handleScenePosition(const HandleInfo& info) const {
    const double bottom = m_viewArea.bottom() - 96.0;
    return bottom - info.value / litersPerPixel();
}

void MixtureScene::positionHandles() {
    for (DragHandleItem* handle : m_handles) {
        if (!handle->id().startsWith(QLatin1String("v")))
            continue;
        const int index = handle->id().mid(1).toInt() - 1;
        const QRectF vessel = vesselRect(index);
        HandleInfo info = handle->info();
        info.value = m_model->param(handle->id());
        // Ручка — на верхушке соответствующей ёмкости: её и перетаскивают по высоте.
        handle->setAnchor(QPointF(vessel.center().x(), handleScenePosition(info)));
    }
}

void MixtureScene::paintScheme(QPainter* painter) {
    auto* mix = qobject_cast<models::MixtureModel*>(m_model);
    if (!mix)
        return;
    const SimState state = mix->currentState();
    const double t = m_model->time();

    // ---- Три ёмкости: два раствора и смеситель ----
    for (int i = 0; i < 3; ++i) {
        const QRectF vessel = vesselRect(i);
        const EntityVisual& entity = state.entities[i];
        const double concentration = entity.value1;              // проценты
        const double capacityLiters = i < 2 ? entity.value2 : std::max(1.0, mix->totalVolume());
        const QColor liquid = liquidColor(concentration);

        primitives::drawVessel(painter, vessel, entity.position, QColor(96, 104, 116), liquid, 2.4);

        const double remaining = i < 2 ? entity.value2 * entity.position : entity.value2;
        LabScene::drawTag(painter, QPointF(vessel.center().x(), vessel.top() - 40.0),
                          i < 2 ? QStringLiteral("Раствор %1").arg(i + 1) : QStringLiteral("Смеситель"),
                          QColor(255, 255, 255, 235), style::ink());
        LabScene::drawTag(painter, QPointF(vessel.center().x(), vessel.top() - 20.0),
                          QStringLiteral("V = %1 л").arg(utils::formatNumber(remaining, 1)),
                          QColor(255, 255, 255, 225), style::inkSoft());
        if (entity.position > 0.10) {
            LabScene::drawTag(painter, QPointF(vessel.center().x(), vessel.center().y()),
                              QStringLiteral("C = %1 %").arg(utils::formatNumber(concentration, 1)),
                              QColor(255, 255, 255, 205), liquid.darker(220));
        }
        primitives::drawScale(painter, vessel.adjusted(4, 0, 4, 0), 8, QStringLiteral("л"),
                              capacityLiters, style::inkSoft());
    }

    // ---- Насосные линии ----
    const QRectF first = vesselRect(0);
    const QRectF second = vesselRect(1);
    const QRectF mixer = vesselRect(2);
    primitives::drawFlowArrow(painter, QPointF(first.right() + 8, first.center().y() - 12),
                              QPointF(mixer.left() - 8, mixer.center().y() - 18),
                              QStringLiteral("насос 60 л/мин"), style::bodyA(), state.entities[0].active);
    primitives::drawFlowArrow(painter, QPointF(second.right() + 8, second.center().y() - 12),
                              QPointF(mixer.left() - 8, mixer.center().y() + 18),
                              QStringLiteral("насос 60 л/мин"), style::bodyB(), state.entities[1].active);

    // ---- Полоски массы вещества m = V·C ----
    const double barTop = m_viewArea.top() + 30.0;
    struct SoluteBar {
        QString name;
        double solute;
        QColor color;
    };
    const QVector<SoluteBar> bars = {
        {QStringLiteral("m₁ = V₁·C₁"), mix->soluteOf(0), style::bodyA()},
        {QStringLiteral("m₂ = V₂·C₂"), mix->soluteOf(1), style::bodyB()},
        {QStringLiteral("m = m₁ + m₂"), mix->totalSolute(), style::bodyC()},
    };
    const double maxSolute = std::max({bars[0].solute, bars[1].solute, bars[2].solute, 1e-6});
    const double barLeft = mixer.right() + 70.0;
    const double barWidth = std::max(120.0, m_viewArea.right() - barLeft - 26.0);
    for (int i = 0; i < bars.size(); ++i) {
        const SoluteBar& bar = bars[i];
        const double y = barTop + i * 46.0;
        const double w = barWidth * std::clamp(bar.solute / maxSolute, 0.0, 1.0);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(style::inkSoft(), 1.0));
        painter->setBrush(QColor(255, 255, 255, 190));
        painter->drawRoundedRect(QRectF(barLeft, y, barWidth, 18.0), 4, 4);
        painter->setPen(Qt::NoPen);
        painter->setBrush(bar.color);
        painter->drawRoundedRect(QRectF(barLeft, y, std::max(2.0, w), 18.0), 4, 4);
        painter->setPen(style::ink());
        painter->setFont(style::uiFont(9.0, true));
        painter->drawText(QRectF(barLeft, y - 18, barWidth, 15), Qt::AlignLeft,
                          QStringLiteral("%1 = %2 л").arg(bar.name).arg(utils::formatNumber(bar.solute, 2)));
        painter->restore();
    }

    // ---- Линейка концентрации смеси ----
    const QRectF gauge(mixer.center().x() - 95.0, m_viewArea.bottom() - 66.0, 190.0, 30.0);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(style::ink(), 1.4));
    painter->setBrush(QColor(255, 255, 255, 225));
    painter->drawRoundedRect(gauge, 6, 6);
    const double cNow = state.primary * 100.0;
    const double cFinal = mix->finalConcentration() * 100.0;
    for (int i = 0; i <= 100; ++i) {
        const double x = gauge.left() + 4 + (gauge.width() - 8) * i / 100.0;
        painter->setPen(QPen(liquidColor(i), 2.2));
        painter->drawLine(QPointF(x, gauge.top() + 6), QPointF(x, gauge.bottom() - 8));
    }
    const double markerX = gauge.left() + 4 + (gauge.width() - 8) * std::clamp(cNow / 100.0, 0.0, 1.0);
    painter->setPen(QPen(style::danger(), 2.0));
    painter->drawLine(QPointF(markerX, gauge.top() - 5), QPointF(markerX, gauge.bottom() + 5));
    painter->setPen(style::ink());
    painter->setFont(style::uiFont(8.5));
    painter->drawText(QRectF(gauge.left() - 8, gauge.bottom() + 4, 44, 14), Qt::AlignLeft, QStringLiteral("0 %"));
    painter->drawText(QRectF(gauge.right() - 44, gauge.bottom() + 4, 52, 14), Qt::AlignRight, QStringLiteral("100 %"));
    painter->restore();
    LabScene::drawTag(painter, QPointF(gauge.center().x(), gauge.top() - 22.0),
                      QStringLiteral("C смеси = %1 % (итог %2 %)")
                          .arg(utils::formatNumber(cNow, 1), utils::formatNumber(cFinal, 2)),
                      QColor(255, 255, 255, 235), style::ink());

    LabScene::drawTag(painter, QPointF(mixer.center().x(), mixer.top() - 62.0),
                      QStringLiteral("V смеси = %1 л").arg(utils::formatNumber(state.secondary, 1)),
                      QColor(255, 255, 255, 235), style::inkSoft());

    if (!state.eventLabel.isEmpty()) {
        LabScene::drawTag(painter, QPointF(m_viewArea.center().x(), m_viewArea.top() + 12.0), state.eventLabel,
                          QColor(234, 247, 239, 240), style::success().darker(130));
    }

    // Подсказка о текущей фазе перекачки.
    QString phase;
    if (state.resolved)
        phase = QStringLiteral("смесь готова");
    else if (t > mix->pumpSwitchTime())
        phase = QStringLiteral("перекачивается второй раствор");
    else
        phase = QStringLiteral("перекачивается первый раствор");
    LabScene::drawTag(painter, QPointF(m_viewArea.center().x(), m_viewArea.bottom() - 16.0), phase,
                      QColor(255, 255, 255, 230), style::inkSoft());
}

}  // namespace lab::scenes
