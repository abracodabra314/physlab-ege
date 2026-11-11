#include "scenes/WorkScene.h"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <functional>

#include "models/WorkModel.h"
#include "scenes/SchemePrimitives.h"
#include "scenes/Style.h"
#include "utils/MathText.h"

namespace lab::scenes {
namespace {

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

WorkScene::WorkScene(Simulation* model, QObject* parent) : LabScene(model, parent) {}

QRectF WorkScene::machineZone() const {
    return QRectF(m_viewArea.left() + 40.0, m_viewArea.top() + 30.0, m_viewArea.width() * 0.52,
                  m_viewArea.height() * 0.66);
}

double WorkScene::machineX(int index) const {
    const QRectF zone = machineZone();
    return zone.left() + zone.width() * (0.18 + 0.32 * index);
}

void WorkScene::reconfigure(const QRectF& area) {
    setRanges(area, 0.0, 1.0, 0.0, 1.0);
}

double WorkScene::mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const {
    auto* work = qobject_cast<models::WorkModel*>(m_model);
    if (!work)
        return info.value;
    if (info.id.startsWith(QLatin1String("p"))) {
        const QRectF zone = machineZone();
        const double value = (zone.bottom() - scenePos.y()) / zone.height();
        return value;
    }
    if (info.id == QLatin1String("soloWork")) {
        const QRectF zone = machineZone();
        const double fraction = (scenePos.x() - zone.left()) / zone.width();
        return fraction;
    }
    return info.value;
}

double WorkScene::handleScenePosition(const HandleInfo& info) const {
    const QRectF zone = machineZone();
    if (info.id.startsWith(QLatin1String("p")))
        return zone.bottom() - info.value * zone.height();
    if (info.id == QLatin1String("soloWork"))
        return zone.left() + info.value * zone.width();
    return 0.0;
}

void WorkScene::positionHandles() {
    auto* work = qobject_cast<models::WorkModel*>(m_model);
    if (!work)
        return;
    for (DragHandleItem* handle : m_handles) {
        HandleInfo info = handle->info();
        info.value = m_model->param(handle->id());
        const double along = handleScenePosition(info);
        if (info.axis == QLatin1String("y")) {
            const int index = handle->id().mid(1).toInt() - 1;
            handle->setAnchor(QPointF(machineX(index), along));
        } else {
            const QRectF zone = machineZone();
            handle->setAnchor(QPointF(along, zone.bottom() + 54.0));
        }
    }
}

void WorkScene::paintScheme(QPainter* painter) {
    auto* work = qobject_cast<models::WorkModel*>(m_model);
    if (!work)
        return;
    const SimState state = work->currentState();
    const QRectF zone = machineZone();
    const double t = m_model->time();
    const double progress = state.primary;

    // ---- Станки ----
    for (const EntityVisual& machine : state.entities) {
        if (!machine.visible)
            continue;
        const int index = machine.id.mid(7).toInt() - 1;   // "machine1" -> 0
        const double x = machineX(index);
        const double baseY = zone.bottom();
        const QColor color = style::variantColor(machine.variant);
        const bool running = machine.active && progress < 1.0 - 1e-9;

        // Станина.
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(color.darker(160), 1.6));
        painter->setBrush(QColor(236, 236, 232));
        painter->drawRoundedRect(QRectF(x - 52, baseY - 44.0, 104, 44.0), 6, 6);
        painter->setBrush(color.lighter(160));
        painter->drawRect(QRectF(x - 40, baseY - 150.0, 26, 106.0));
        painter->restore();

        // Большая шестерня (вращается при работе) и малая на валу.
        const double spin = running ? t * (2.0 + machine.value1 * 26.0) : 0.0;
        primitives::drawGear(painter, QPointF(x - 26, baseY - 84.0), 34.0, spin, color, running, 12);
        primitives::drawGear(painter, QPointF(x + 20, baseY - 118.0), 20.0, -spin * 1.7, color, running, 8);

        // Индикатор личной выполненной части: вертикальный столбик.
        const double contribution = std::clamp(machine.position, 0.0, 1.0);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(QColor(120, 124, 130), 1.2));
        painter->setBrush(QColor(255, 255, 255, 200));
        painter->drawRect(QRectF(x + 34, baseY - 150.0, 16, 106.0));
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawRect(QRectF(x + 35, baseY - 1 - contribution * 105.0, 14, contribution * 105.0));
        painter->restore();

        LabScene::drawTag(painter, QPointF(x, baseY - 168.0),
                          QStringLiteral("%1: %2 %/мин")
                              .arg(machine.label)
                              .arg(utils::formatNumber(machine.value1 * 100.0, 1)),
                          QColor(255, 255, 255, 235),
                          machine.visible && machine.active ? color.darker(120) : QColor(150, 155, 162));
        LabScene::drawTag(painter, QPointF(x + 42, baseY - 172.0),
                          QStringLiteral("вклад %1 %").arg(utils::formatNumber(contribution * 100.0, 0)),
                          QColor(255, 255, 255, 225), style::inkSoft());
    }

    // ---- Полоса выполненной работы ----
    const double barY = zone.bottom() + 34.0;
    const QRectF bar(zone.left(), barY, zone.width(), 40.0);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(style::ink(), 1.6));
    painter->setBrush(QColor(255, 255, 255, 230));
    painter->drawRoundedRect(bar, 8, 8);
    // Стек contributions — доли механизмов в выполненной части.
    double xCursor = bar.left() + 3.0;
    const double innerWidth = bar.width() - 6.0;
    const double fillWidth = innerWidth * std::clamp(progress, 0.0, 1.0);
    for (const EntityVisual& machine : state.entities) {
        if (!machine.visible || progress < 1e-9)
            continue;
        const double share = machine.position / std::max(progress, 1e-9);
        const double w = fillWidth * std::clamp(share, 0.0, 1.0);
        if (w > 0.5) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(style::variantColor(machine.variant));
            painter->drawRect(QRectF(xCursor, bar.top() + 4, w, bar.height() - 8));
            xCursor += w;
        }
    }
    // Деления процентов.
    painter->setPen(QPen(style::inkSoft(), 0.8, Qt::DotLine));
    for (int i = 1; i < 10; ++i) {
        const double x = bar.left() + bar.width() * i / 10.0;
        painter->drawLine(QPointF(x, bar.top() + 2), QPointF(x, bar.bottom() - 2));
    }
    painter->setPen(style::ink());
    painter->setFont(style::uiFont(10.0, true));
    painter->drawText(bar, Qt::AlignCenter,
                      QStringLiteral("Выполнено %1 % работы").arg(utils::formatNumber(progress * 100.0, 1)));
    painter->restore();

    // Разметка «вся работа = 1».
    painter->save();
    painter->setFont(style::uiFont(8.5));
    painter->setPen(style::inkSoft());
    painter->drawText(QRectF(bar.left() - 4, bar.bottom() + 2, 60, 14), Qt::AlignLeft, QStringLiteral("0"));
    painter->drawText(QRectF(bar.right() - 56, bar.bottom() + 2, 60, 14), Qt::AlignRight,
                      QStringLiteral("A = 1"));
    painter->restore();

    // ---- Отметка конца одиночного этапа (соло-режим) ----
    auto* workModel = static_cast<models::WorkModel*>(m_model);
    const bool sequential = workModel->mode() == models::WorkModel::Mode::Sequential;
    if (sequential) {
        const double soloWork = m_model->param(QStringLiteral("soloWork"));
        const double x = bar.left() + bar.width() * std::clamp(soloWork, 0.0, 1.0);
        painter->save();
        painter->setPen(QPen(style::accentWarm(), 1.8, Qt::DashLine));
        painter->drawLine(QPointF(x, bar.top() - 14.0), QPointF(x, bar.bottom() + 6.0));
        painter->restore();
        LabScene::drawTag(painter, QPointF(x, bar.top() - 26.0),
                          QStringLiteral("конец одиночного этапа"),
                          QColor(255, 250, 235, 235), style::accentWarm().darker(120));
    }

    // ---- График A(t) ----
    const double duration = std::max(0.5, m_model->duration());
    const double plotLeft = zone.right() + 40.0;
    const QRectF plotRect(plotLeft, zone.top() + 20.0,
                          std::max(180.0, m_viewArea.right() - plotLeft),
                          std::min(300.0, zone.height() * 0.9));
    if (plotRect.width() > 170.0) {
        QVector<PlotSeries> series;
        PlotSeries total;
        total.color = style::accent();
        total.points = sampleFunction(duration, 80, [&](double tt) { return workModel->progressAt(tt); });
        series.push_back(total);
        LabScene::drawPlot(painter, plotRect, QStringLiteral("Выполненная работа A(t), доля"),
                           QStringLiteral("мин"), series, duration, t, m_model->eventTime());
    }

    // ---- Время выполнения ----
    const double total = workModel->completionTime();
    if (std::isfinite(total)) {
        LabScene::drawTag(painter, QPointF(zone.center().x(), m_viewArea.bottom() - 18.0),
                          QStringLiteral("A = (p₁ + p₂)·t ⇒ t = %1 мин").arg(utils::formatNumber(total, 2)),
                          QColor(255, 255, 255, 235), style::ink());
    }

    if (!state.eventLabel.isEmpty()) {
        LabScene::drawTag(painter, QPointF(zone.center().x(), zone.top() - 12.0), state.eventLabel,
                          QColor(234, 247, 239, 240), style::success().darker(130));
    }
}

}  // namespace lab::scenes
