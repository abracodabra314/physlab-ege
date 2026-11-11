#include "scenes/WorkScene.h"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <functional>

#include "models/WorkModel.h"
#include "scenes/SchemePrimitives.h"
#include "scenes/Style.h"
#include "scenes/VectorArrows.h"
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
    // Производственная площадка: станки стоят вдоль общей линии.
    return QRectF(m_viewArea.left() + 40.0, m_viewArea.top() + 30.0, m_viewArea.width() * 0.52,
                  m_viewArea.height() * 0.62);
}

QRectF WorkScene::conveyorRect() const {
    const QRectF zone = machineZone();
    return QRectF(zone.left() - 14.0, zone.bottom() + 26.0, zone.width() + 28.0, 16.0);
}

double WorkScene::machineX(int index) const {
    const QRectF zone = machineZone();
    return zone.left() + zone.width() * (0.16 + 0.34 * index);
}

void WorkScene::reconfigure(const QRectF& area) {
    setRanges(area, 0.0, 1.0, 0.0, 1.0);
}

double WorkScene::mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const {
    auto* work = qobject_cast<models::WorkModel*>(m_model);
    if (!work)
        return info.value;
    if (info.id.startsWith(QLatin1String("p"))) {
        // Ручка производительности — вертикальная: высота столбика вклада.
        const QRectF zone = machineZone();
        return (zone.bottom() - scenePos.y()) / zone.height();
    }
    return info.value;
}

double WorkScene::handleScenePosition(const HandleInfo& info) const {
    const QRectF zone = machineZone();
    if (info.id.startsWith(QLatin1String("p")))
        return zone.bottom() - info.value * zone.height();
    return 0.0;
}

void WorkScene::positionHandles() {
    auto* work = qobject_cast<models::WorkModel*>(m_model);
    if (!work)
        return;
    for (DragHandleItem* handle : m_handles) {
        HandleInfo info = handle->info();
        info.value = m_model->param(handle->id());
        const int index = handle->id().mid(1).toInt() - 1;
        handle->setAnchor(QPointF(machineX(index), handleScenePosition(info)));
    }
}

void WorkScene::drawMachineBody(QPainter* painter, double x, double baseY, const QColor& color, bool running,
                                double spin) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Станина и стойка.
    painter->setPen(QPen(color.darker(160), 1.6));
    painter->setBrush(QColor(236, 236, 232));
    painter->drawRoundedRect(QRectF(x - 54, baseY - 40.0, 108, 40.0), 6, 6);
    painter->setBrush(color.lighter(160));
    painter->drawRect(QRectF(x - 42, baseY - 148.0, 28, 108.0));
    painter->restore();

    // Рабочая часть: большая шестерня и малая на валу (вращаются при работе).
    primitives::drawGear(painter, QPointF(x - 28, baseY - 84.0), 34.0, spin, color, running, 12);
    primitives::drawGear(painter, QPointF(x + 18, baseY - 118.0), 20.0, -spin * 1.7, color, running, 8);

    // Входной участок (приём заготовки) и выходной (к конвейеру).
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(running ? color.darker(115) : QColor(190, 193, 198), 6.5, Qt::SolidLine, Qt::FlatCap));
    painter->drawLine(QPointF(x - 74.0, baseY - 60.0), QPointF(x - 50.0, baseY - 70.0));
    painter->drawLine(QPointF(x + 4.0, baseY - 52.0), QPointF(x + 58.0, baseY - 52.0));
    painter->restore();
}

void WorkScene::paintScheme(QPainter* painter) {
    auto* work = qobject_cast<models::WorkModel*>(m_model);
    if (!work)
        return;
    const SimState state = work->currentState();
    const QRectF zone = machineZone();
    const double t = m_model->time();
    const double progress = state.primary;

    // ---- Пол площадки ----
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(style::inkSoft(), 1.2));
    painter->setBrush(QColor(238, 238, 233));
    painter->drawRect(QRectF(zone.left() - 20.0, zone.bottom(), zone.width() + 40.0, 12.0));
    painter->restore();

    // ---- Станки ----
    for (const EntityVisual& machine : state.entities) {
        if (!machine.visible)
            continue;
        const int index = machine.id.mid(7).toInt() - 1;   // "machine1" -> 0
        const double x = machineX(index);
        const double baseY = zone.bottom();
        const QColor color = style::variantColor(machine.variant);
        const bool running = machine.active;
        const double spin = running ? t * (2.0 + machine.value1 * 26.0) : 0.0;

        drawMachineBody(painter, x, baseY, color, running, spin);

        // Индикатор состояния на стойке.
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QPointF lamp(x - 28.0, baseY - 136.0);
        painter->setPen(QPen(style::ink(), 1.2));
        painter->setBrush(running ? style::success() : style::danger());
        painter->drawEllipse(lamp, 4.6, 4.6);
        painter->restore();

        // Личная шкала вклада механизма.
        const double contribution = std::clamp(machine.position, 0.0, 1.0);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(QColor(120, 124, 130), 1.2));
        painter->setBrush(QColor(255, 255, 255, 200));
        painter->drawRect(QRectF(x + 34, baseY - 148.0, 16, 104.0));
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawRect(QRectF(x + 35, baseY - 1 - contribution * 103.0, 14, contribution * 103.0));
        painter->restore();

        // Готовая продукция: количество изделий = доля выполненной работы.
        // 10 изделий на всю работу — дискретизация визуальная, привязка к A.
        const int items = int(std::round(contribution * 10.0));
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(color.darker(150), 1.0));
        painter->setBrush(color.lighter(150));
        for (int item = 0; item < items; ++item) {
            const double bx = x + 42.0 + (item % 5) * 11.0;
            const double by = baseY - 24.0 - (item / 5) * 12.0;
            painter->drawRect(QRectF(bx, by, 9.0, 9.0));
        }
        painter->restore();

        // Статус механизма.
        QString status;
        QColor statusColor = style::inkSoft();
        if (!machine.visible) {
            status = QStringLiteral("выключен");
        } else if (t <= machine.value2 + 1e-9) {
            status = QStringLiteral("включится через %1 мин").arg(utils::formatNumber(machine.value2 - t, 2));
            statusColor = style::accentWarm();
        } else if (running) {
            status = QStringLiteral("работает");
            statusColor = style::success().darker(120);
        } else if (progress >= 1.0 - 1e-9) {
            status = QStringLiteral("завершил");
            statusColor = style::inkSoft();
        } else {
            status = QStringLiteral("простой");
            statusColor = style::danger();
        }
        LabScene::drawTag(painter, QPointF(x, baseY - 168.0),
                          QStringLiteral("%1 · %2 %/мин").arg(machine.label)
                              .arg(utils::formatNumber(machine.value1 * 100.0, 1)),
                          QColor(255, 255, 255, 235),
                          machine.visible && machine.active ? color.darker(120) : QColor(150, 155, 162));
        LabScene::drawTag(painter, QPointF(x + 6.0, baseY - 190.0), status, QColor(255, 255, 255, 225), statusColor);
    }

    // ---- Общий конвейер: направление движения заготовки ----
    const QRectF belt = conveyorRect();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(style::ink(), 1.4));
    painter->setBrush(QColor(70, 74, 80));
    painter->drawRoundedRect(belt, 5, 5);
    painter->restore();
    primitives::drawFlowArrow(painter, QPointF(belt.left() + 6.0, belt.center().y()),
                              QPointF(belt.left() + std::min(120.0, belt.width() * 0.24), belt.center().y()),
                              QStringLiteral("конвейер"), vectors::colorOf(vectors::Kind::Flow), true, t);
    LabScene::drawTag(painter, QPointF(belt.right() - 70.0, belt.center().y() - 20.0),
                      QStringLiteral("заготовка →"), QColor(255, 255, 255, 220), style::inkSoft());

    // ---- Общая шкала готовности работы ----
    const double barY = belt.bottom() + 26.0;
    const QRectF bar(zone.left(), barY, zone.width(), 40.0);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(style::ink(), 1.6));
    painter->setBrush(QColor(255, 255, 255, 230));
    painter->drawRoundedRect(bar, 8, 8);
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

    painter->save();
    painter->setFont(style::uiFont(8.5));
    painter->setPen(style::inkSoft());
    painter->drawText(QRectF(bar.left() - 4, bar.bottom() + 2, 60, 14), Qt::AlignLeft, QStringLiteral("0"));
    painter->drawText(QRectF(bar.right() - 56, bar.bottom() + 2, 60, 14), Qt::AlignRight,
                      QStringLiteral("A = 1"));
    painter->restore();

    // ---- Моменты включения механизмов на шкале времени A(t) ----
    const double duration = std::max(0.5, m_model->duration());
    const double plotLeft = zone.right() + 40.0;
    const QRectF plotRect(plotLeft, zone.top() + 20.0, std::max(180.0, m_viewArea.right() - plotLeft),
                          std::min(300.0, zone.height() * 0.9));
    if (plotRect.width() > 170.0) {
        QVector<PlotSeries> series;
        PlotSeries total;
        total.color = style::accent();
        total.points = sampleFunction(duration, 90, [&](double tt) { return work->progressAt(tt); });
        series.push_back(total);
        LabScene::drawPlot(painter, plotRect, QStringLiteral("Выполненная работа A(t), доля"),
                           QStringLiteral("мин"), series, duration, t, m_model->eventTime());
        QString starts = QStringLiteral("включения: ");
        for (int i = 0; i < 3; ++i) {
            if (!work->enabled(i))
                continue;
            starts += QStringLiteral("№%1 — %2 мин; ").arg(i + 1).arg(utils::formatNumber(work->startTimeOf(i), 2));
        }
        LabScene::drawTag(painter, QPointF(plotRect.center().x(), plotRect.bottom() + 22.0), starts,
                          QColor(255, 255, 255, 230), style::inkSoft());
    }

    // ---- Время выполнения ----
    const double total = work->completionTime();
    if (std::isfinite(total)) {
        LabScene::drawTag(painter, QPointF(zone.center().x(), m_viewArea.bottom() - 22.0),
                          QStringLiteral("A = Σ pᵢ·(t − tᵢ) ⇒ t = %1 мин").arg(utils::formatNumber(total, 2)),
                          QColor(255, 255, 255, 235), style::ink());
    }

    if (!state.eventLabel.isEmpty()) {
        LabScene::drawTag(painter, QPointF(zone.center().x(), zone.top() - 12.0), state.eventLabel,
                          QColor(234, 247, 239, 240), style::success().darker(130));
    }
}

}  // namespace lab::scenes
