#include "scenes/TankScene.h"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <functional>

#include "models/TankModel.h"
#include "scenes/SchemePrimitives.h"
#include "scenes/SchemeLabels.h"
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
    // Зона бака — левая часть сцены; фиксирована, чтобы масштаб не «прыгал»
    // при изменении уровня или высоты.
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
    if (info.id == QLatin1String("level") || info.id == QLatin1String("height")) {
        // Экранная Y направлена вниз, физическая высота — вверх.
        return (zone.bottom() - scenePos.y()) / scale;
    }
    return info.value;
}

double TankScene::handleScenePosition(const HandleInfo& info) const {
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
            // Ручка уровня — на поверхности воды внутри бака.
            const double level = handle->info().value;
            const double fraction = std::clamp(level / std::max(0.01, tank->height()), 0.0, 1.0);
            const double waterTop = tankArea.bottom() - fraction * tankArea.height();
            handle->setAnchor(QPointF(tankArea.center().x(), waterTop));
        } else if (handle->id() == QLatin1String("height")) {
            // Ручка высоты — на крышке бака (правый край).
            handle->setAnchor(QPointF(tankArea.right() + 58.0, tankArea.top()));
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
    const double t = m_model->time();

    // ---- Бак: вертикальные стенки, дно, открытый верх ----
    primitives::drawVessel(painter, tankArea, levelFraction, QColor(96, 104, 116), style::water(), 2.6);

    // Максимальный уровень — отметка у кромки открытого верха.
    painter->save();
    painter->setPen(QPen(style::danger(), 1.4, Qt::DashLine));
    painter->drawLine(QPointF(tankArea.left() - 14.0, tankArea.top()),
                      QPointF(tankArea.right() + 14.0, tankArea.top()));
    painter->restore();
    LabScene::drawTag(painter, QPointF(tankArea.right() + 66.0, tankArea.top() - 14.0),
                      QStringLiteral("Vmax"),
                      QColor(255, 246, 245, 235), style::danger().darker(115));

    // Подпись объёма в воде.
    if (levelFraction > 0.06) {
        const double waterMiddle = (waterTop + tankArea.bottom()) / 2.0;
        LabScene::drawTag(painter, QPointF(tankArea.center().x(), waterMiddle),
                          QStringLiteral("V"),
                          QColor(255, 255, 255, 215), style::waterDeep().darker(140));
    }

    // Шкала вместимости справа от бака.
    primitives::drawScale(painter, tankArea.adjusted(4, 0, 4, 0), 10, QStringLiteral("л"),
                          tank->capacity(), style::inkSoft());

    // ---- Краны: трубы наполнения сверху, слива — у дна ----
    struct TapSpec {
        QString id;
        QString label;
        bool isInflow;
        int variant;
        int lane;   // 0 или 1 — номер точки крепления
    };
    const QVector<TapSpec> taps = {
        {QStringLiteral("in1"),  QStringLiteral("Кран 1"),  true,  0, 0},
        {QStringLiteral("in2"),  QStringLiteral("Кран 2"),  true,  2, 1},
        {QStringLiteral("out1"), QStringLiteral("Слив 1"), false, 1, 0},
        {QStringLiteral("out2"), QStringLiteral("Слив 2"), false, 3, 1},
    };

    for (const TapSpec& tap : taps) {
        const double rate = tank->tapRate(tap.id);
        const bool open = tank->tapOpen(tap.id) && rate > 1e-9;
        const QColor color = style::variantColor(tap.variant);

        if (tap.isInflow) {
            // Труба входит в бак сверху; струя — от крана до поверхности воды.
            const double x = tankArea.left() + tankArea.width() * (0.25 + 0.5 * tap.lane);
            primitives::drawTap(painter, QPointF(x, tankArea.top() - 66.0), color, open);
            // Участок трубы над баком.
            painter->save();
            painter->setPen(QPen(open ? color.darker(115) : QColor(190, 193, 198), 7.0, Qt::SolidLine, Qt::FlatCap));
            painter->drawLine(QPointF(x, tankArea.top() - 52.0), QPointF(x, tankArea.top() - 30.0));
            painter->restore();
            // Поток внутри бака: от кромки верха до уровня воды.
            const double streamBottom = levelFraction > 0.02 ? waterTop - 2.0 : tankArea.bottom() - 4.0;
            primitives::drawFlowArrow(painter, QPointF(x, tankArea.top() - 22.0),
                                      QPointF(x, std::max(tankArea.top() + 6.0, streamBottom)),
                                      QString(), color, open, t);
            LabScene::drawTag(painter, QPointF(x, tankArea.top() - 96.0),
                              QStringLiteral("%1 · q%2").arg(tap.label).arg(tap.lane + 1),
                              QColor(255, 255, 255, 230), open ? color.darker(120) : QColor(150, 155, 162));
        } else {
            // Сливной патрубок у дна справа; поток — наружу.
            const double y = tankArea.bottom() - 18.0 - 34.0 * tap.lane;
            painter->save();
            painter->setPen(QPen(open ? color.darker(115) : QColor(190, 193, 198), 7.0, Qt::SolidLine, Qt::FlatCap));
            painter->drawLine(QPointF(tankArea.right() - 2.0, y), QPointF(tankArea.right() + 96.0, y));
            painter->restore();
            primitives::drawTap(painter, QPointF(tankArea.right() + 52.0, y), color, open);
            primitives::drawFlowArrow(painter, QPointF(tankArea.right() + 72.0, y),
                                      QPointF(tankArea.right() + 150.0, y),
                                      QStringLiteral("q"), color, open, t);
            LabScene::drawTag(painter, QPointF(tankArea.right() + 110.0, y - 30.0), tap.label,
                              QColor(255, 255, 255, 230), open ? color.darker(120) : QColor(150, 155, 162));
        }
    }

    // ---- Размерные линии: уровень, высота, сторона основания ----
    LabScene::drawDimension(painter, QPointF(tankArea.left(), waterTop), QPointF(tankArea.right(), waterTop),
                            QStringLiteral("h"),
                            style::waterDeep(), -18.0);
    LabScene::drawDimension(painter, QPointF(tankArea.left(), tankArea.top()),
                            QPointF(tankArea.left(), tankArea.bottom()),
                            QStringLiteral("H"),
                            style::inkSoft(), -34.0);
    LabScene::drawDimension(painter, QPointF(tankArea.left(), tankArea.bottom()),
                            QPointF(tankArea.right(), tankArea.bottom()),
                            QStringLiteral("a"),
                            style::inkSoft(), 22.0);

    // ---- Итоговая производительность потоков ----
    const double net = tank->netRate();
    const QString netText = QStringLiteral("Q = Σqᵢ");
    Q_UNUSED(net);
    LabScene::drawTag(painter, QPointF(tankArea.center().x(), tankArea.bottom() + 62.0), netText,
                      QColor(255, 255, 255, 235), net > 0.0 ? style::bodyC().darker(120)
                                                            : (net < 0.0 ? style::danger().darker(115)
                                                                         : style::inkSoft()));

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
        volume.points = sampleFunction(duration, 90, [&](double tt) { return tank->volumeAt(tt); });
        series.push_back(volume);
        LabScene::drawPlot(painter, plotRect,
                           QStringLiteral("Объём воды V(t), л"), QStringLiteral("мин"), series, duration,
                           m_model->time(), m_model->eventTime());
        LabScene::drawTag(painter, QPointF(plotRect.center().x(), plotRect.bottom() + 20.0),
                          QStringLiteral("V(t) = V₀ + Q·t"),
                          QColor(255, 255, 255, 235), style::ink());
    }

    if (!state.eventLabel.isEmpty()) {
        LabScene::drawTag(painter, QPointF(tankArea.center().x(), m_viewArea.top() + 14.0),
                          lab::labels::stripValues(state.eventLabel),
                          QColor(234, 247, 239, 240), style::success().darker(130));
    }
}

}  // namespace lab::scenes
