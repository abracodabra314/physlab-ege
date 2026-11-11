#include "scenes/TankScene.h"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <functional>

#include "models/TankModel.h"
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

constexpr double kMaxDimension = 5.0;   // максимальный размер бака по паспорту модели

}  // namespace

TankScene::TankScene(Simulation* model, QObject* parent) : LabScene(model, parent) {}

QRectF TankScene::zoneRect() const {
    // Зона бака — левая часть сцены; фиксирована, чтобы масштаб не «прыгал».
    return QRectF(m_viewArea.left() + 110.0, m_viewArea.top() + 40.0,
                  m_viewArea.width() * 0.36, m_viewArea.height() * 0.80);
}

double TankScene::scalePx() const {
    const QRectF zone = zoneRect();
    return std::min(zone.width(), zone.height()) / kMaxDimension;
}

QRectF TankScene::tankRect() const {
    auto* tank = qobject_cast<models::TankModel*>(m_model);
    const double widthValue = tank ? std::clamp(tank->width(), 0.2, kMaxDimension) : 2.0;
    const double heightValue = tank ? std::clamp(tank->height(), 0.2, kMaxDimension) : 2.5;
    const double scale = scalePx();
    const double w = widthValue * scale;
    const double h = heightValue * scale;
    const QRectF zone = zoneRect();
    return QRectF(zone.center().x() - w / 2.0, zone.bottom() - h, w, h);
}

void TankScene::reconfigure(const QRectF& area) {
    // Диапазон величин по вертикали — метры уровня (0..макс. высота).
    setRanges(area, 0.0, 1.0, 0.0, kMaxDimension);
}

double TankScene::mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const {
    auto* tank = qobject_cast<models::TankModel*>(m_model);
    if (!tank)
        return info.value;
    const double scale = scalePx();
    const QRectF zone = zoneRect();
    if (info.id == QLatin1String("level")) {
        const double level = (zone.bottom() - scenePos.y()) / scale;
        return level;
    }
    if (info.id == QLatin1String("height")) {
        const double heightValue = (zone.bottom() - scenePos.y()) / scale;
        return heightValue;
    }
    return info.value;
}

double TankScene::handleScenePosition(const HandleInfo& info) const {
    auto* tank = qobject_cast<models::TankModel*>(m_model);
    if (!tank)
        return 0.0;
    const double scale = scalePx();
    const QRectF zone = zoneRect();
    return zone.bottom() - info.value * scale;
}

void TankScene::positionHandles() {
    auto* tank = qobject_cast<models::TankModel*>(m_model);
    if (!tank)
        return;
    const QRectF tankArea = tankRect();
    for (DragHandleItem* handle : m_handles) {
        if (handle->id() == QLatin1String("level")) {
            const double y = handleScenePosition(handle->info());
            handle->setAnchor(QPointF(tankArea.center().x(), y));
        } else if (handle->id() == QLatin1String("height")) {
            const double y = handleScenePosition(handle->info());
            handle->setAnchor(QPointF(tankArea.right() + 64.0, y));
        }
    }
}

void TankScene::paintScheme(QPainter* painter) {
    auto* tank = qobject_cast<models::TankModel*>(m_model);
    if (!tank)
        return;
    const SimState state = tank->currentState();
    const QRectF tankArea = tankRect();
    const double levelFraction = std::clamp(state.primary / std::max(0.01, tank->height()), 0.0, 1.0);
    const double waterTop = tankArea.bottom() - levelFraction * tankArea.height();

    // ---- Бак и вода ----
    primitives::drawVessel(painter, tankArea, levelFraction, QColor(96, 104, 116), style::water(), 2.6);

    const double waterMiddle = (waterTop + tankArea.bottom()) / 2.0;
    LabScene::drawTag(painter, QPointF(tankArea.center().x(), waterMiddle),
                      QStringLiteral("V = %1 л").arg(utils::formatNumber(state.secondary, 0)),
                      QColor(255, 255, 255, 215), style::waterDeep().darker(140));

    // Шкала вместимости справа от бака.
    primitives::drawScale(painter, tankArea.adjusted(4, 0, 4, 0), 10, QStringLiteral("л"),
                          tank->capacity(), style::inkSoft());

    // ---- Краны: наполнение сверху слева, слив справа у дна ----
    struct TapSpec {
        QString id;
        QString label;
        bool isInflow;
        int variant;
        double lane;   // 0 — верхний/левый, 1 — нижний/правый дубль
    };
    const QVector<TapSpec> taps = {
        {QStringLiteral("in1"), QStringLiteral("Кран 1"), true, 0, 0.0},
        {QStringLiteral("in2"), QStringLiteral("Кран 2"), true, 2, 1.0},
        {QStringLiteral("out1"), QStringLiteral("Слив 1"), false, 1, 0.0},
        {QStringLiteral("out2"), QStringLiteral("Слив 2"), false, 3, 1.0},
    };

    for (const TapSpec& tap : taps) {
        const double rate = tank->tapRate(tap.id);
        const bool open = tank->tapOpen(tap.id) && rate > 1e-9;
        const QColor color = style::variantColor(tap.variant);

        if (tap.isInflow) {
            // Струя входит в бак сверху.
            const double x = tankArea.left() + tankArea.width() * (0.25 + 0.5 * tap.lane);
            const double outerX = x;
            primitives::drawTap(painter, QPointF(outerX, tankArea.top() - 54.0), color, open);
            primitives::drawFlowArrow(
                painter, QPointF(outerX, tankArea.top() - 38.0), QPointF(x, tankArea.top() - 6.0),
                QStringLiteral("%1 л/мин").arg(utils::formatNumber(rate, 0)), color, open);
            LabScene::drawTag(painter, QPointF(outerX, tankArea.top() - 78.0), tap.label,
                              QColor(255, 255, 255, 230), open ? color.darker(120) : QColor(150, 155, 162));
        } else {
            // Слив — у дна справа; линия начинается за шкалой вместимости.
            const double y = tankArea.bottom() - 16.0 - 40.0 * tap.lane;
            primitives::drawTap(painter, QPointF(tankArea.right() + 130.0, y), color, open);
            primitives::drawFlowArrow(
                painter, QPointF(tankArea.right() + 108.0, y), QPointF(tankArea.right() + 240.0, y),
                QStringLiteral("%1 л/мин").arg(utils::formatNumber(rate, 0)), color, open);
            LabScene::drawTag(painter, QPointF(tankArea.right() + 130.0, y - 28.0), tap.label,
                              QColor(255, 255, 255, 230), open ? color.darker(120) : QColor(150, 155, 162));
        }
    }

    // ---- Размерные линии ----
    LabScene::drawDimension(painter, QPointF(tankArea.left(), waterTop), QPointF(tankArea.right(), waterTop),
                            QStringLiteral("h = %1 м").arg(utils::formatNumber(state.primary, 2)),
                            style::waterDeep(), -20.0);
    LabScene::drawDimension(painter, QPointF(tankArea.left(), tankArea.top()),
                            QPointF(tankArea.left(), tankArea.bottom()),
                            QStringLiteral("H = %1 м").arg(utils::formatNumber(tank->height(), 2)),
                            style::inkSoft(), -36.0);
    LabScene::drawDimension(painter, QPointF(tankArea.left(), tankArea.bottom()),
                            QPointF(tankArea.right(), tankArea.bottom()),
                            QStringLiteral("a = %1 м").arg(utils::formatNumber(tank->width(), 2)),
                            style::inkSoft(), 24.0);

    // ---- График объёма V(t) ----
    const double duration = std::max(0.5, m_model->duration());
    const double plotLeft = m_viewArea.left() + m_viewArea.width() * 0.55;
    const QRectF plotRect(plotLeft, m_viewArea.top() + 6.0,
                          std::max(240.0, m_viewArea.right() - plotLeft),
                          std::min(320.0, m_viewArea.height() * 0.66));
    if (plotRect.width() > 190.0) {
        QVector<PlotSeries> series;
        PlotSeries volume;
        volume.color = style::waterDeep();
        volume.points = sampleFunction(duration, 90, [&](double t) { return tank->volumeAt(t); });
        series.push_back(volume);
        LabScene::drawPlot(painter, plotRect,
                           QStringLiteral("Объём воды V(t), л"), QStringLiteral("мин"), series, duration,
                           m_model->time(), m_model->eventTime());
    }

    // ---- Формула изменения объёма ----
    const double net = tank->netRate();
    const QString netText = net >= 0.0
                                ? QStringLiteral("V(t) = %1 + %2·t").arg(utils::formatNumber(tank->initialVolume(), 0),
                                                                         utils::formatNumber(net, 1))
                                : QStringLiteral("V(t) = %1 − %2·t").arg(utils::formatNumber(tank->initialVolume(), 0),
                                                                         utils::formatNumber(std::abs(net), 1));
    LabScene::drawTag(painter, QPointF(plotRect.isValid() && plotRect.width() > 190.0
                                           ? plotRect.center().x()
                                           : m_viewArea.center().x(),
                                       m_viewArea.bottom() - 26.0),
                      netText, QColor(255, 255, 255, 235), style::ink());

    if (!state.eventLabel.isEmpty()) {
        LabScene::drawTag(painter, QPointF(tankArea.center().x(), m_viewArea.top() + 14.0), state.eventLabel,
                          QColor(234, 247, 239, 240), style::success().darker(130));
    }
}

}  // namespace lab::scenes
