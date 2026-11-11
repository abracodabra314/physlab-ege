#include "scenes/MixtureScene.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

#include "models/MixtureModel.h"
#include "scenes/SchemePrimitives.h"
#include "scenes/Style.h"
#include "scenes/VectorArrows.h"
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

double MixtureScene::columnHeight() const {
    // Высота максимального столба жидкости. Масштаб выбран так, чтобы
    // смеситель всегда помещался между полкой исходных ёмкостей и полом:
    // тогда трубы идут вниз по всей длине, без «скачков вверх».
    return m_viewArea.height() * 0.32;
}

double MixtureScene::sourceBaseline() const {
    return m_viewArea.top() + m_viewArea.height() * 0.52;
}

double MixtureScene::litersPerPixel() const {
    auto* mix = qobject_cast<models::MixtureModel*>(m_model);
    const double total = mix ? std::max(mix->totalVolume(), 1.0) : 100.0;
    // Диапазон масштаба: максимум из суммарного объёма и паспортных 200 л —
    // поэтому обе ёмкости и смеситель всегда вписываются в свои зоны,
    // а шкалы согласованы между собой.
    return std::max(kMinScaleVolume, total) / columnHeight();
}

QRectF MixtureScene::vesselRect(int index) const {
    auto* mix = qobject_cast<models::MixtureModel*>(m_model);
    const double lPerPx = litersPerPixel();

    if (index < 2) {
        // Исходные ёмкости: левая и правая, стоят на общей линии.
        const double volume = mix ? std::max(0.0, mix->volumeOf(index)) : 50.0;
        const double height = std::max(30.0, volume / lPerPx);
        const double width = std::max(96.0, m_viewArea.width() * 0.15);
        const double x = index == 0 ? m_viewArea.left() + m_viewArea.width() * 0.06
                                    : m_viewArea.right() - m_viewArea.width() * 0.06 - width;
        const double baseline = sourceBaseline();
        return QRectF(x, baseline - height, width, height);
    }
    // Смеситель — по центру ниже исходных ёмкостей; вместимость = суммарный объём.
    const double total = mix ? std::max(1.0, mix->totalVolume()) : 100.0;
    const double height = std::max(80.0, total / lPerPx);
    const double width = std::max(140.0, m_viewArea.width() * 0.20);
    const double x = m_viewArea.center().x() - width / 2.0;
    const double baseline = m_viewArea.bottom() - 70.0;   // под смесителем — линейка концентрации
    return QRectF(x, baseline - height, width, height);
}

void MixtureScene::reconfigure(const QRectF& area) {
    // Вертикальная шкала — литры в текущем масштабе сцены.
    setRanges(area, 0.0, 1.0, 0.0, litersPerPixel() * std::max(1.0, area.height()));
}

double MixtureScene::mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const {
    if (!info.id.startsWith(QLatin1String("v")))
        return info.value;
    const double baseline = sourceBaseline();
    return std::max(0.0, (baseline - scenePos.y()) * litersPerPixel());
}

double MixtureScene::handleScenePosition(const HandleInfo& info) const {
    return sourceBaseline() - info.value / litersPerPixel();
}

void MixtureScene::positionHandles() {
    for (DragHandleItem* handle : m_handles) {
        if (!handle->id().startsWith(QLatin1String("v")))
            continue;
        const int index = handle->id().mid(1).toInt() - 1;
        const QRectF vessel = vesselRect(index);
        HandleInfo info = handle->info();
        info.value = m_model->param(handle->id());
        // Ручка — на поверхности жидкости своей ёмкости: её перетаскивают по высоте.
        handle->setAnchor(QPointF(vessel.center().x(), handleScenePosition(info)));
    }
}

void MixtureScene::paintScheme(QPainter* painter) {
    auto* mix = qobject_cast<models::MixtureModel*>(m_model);
    if (!mix)
        return;
    const SimState state = mix->currentState();
    const double t = m_model->time();

    const QRectF first = vesselRect(0);
    const QRectF second = vesselRect(1);
    const QRectF mixer = vesselRect(2);

    // ---- Полоток, на котором стоят исходные ёмкости, и подставка смесителя ----
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(style::inkSoft(), 1.2));
    painter->setBrush(QColor(234, 234, 228));
    painter->drawRect(QRectF(first.left() - 26.0, sourceBaseline(), first.width() + 52.0, 9.0));
    painter->drawRect(QRectF(second.left() - 26.0, sourceBaseline(), second.width() + 52.0, 9.0));
    painter->drawRect(QRectF(mixer.left() - 18.0, mixer.bottom(), mixer.width() + 36.0, 9.0));
    painter->restore();

    // ---- Трубы от исходных ёмкостей к смесителю (соединяются с кранами) ----
    struct Line {
        QRectF source;
        bool open;      // кран открыт
        bool flowing;   // идёт перекачка прямо сейчас
        QColor color;
    };
    const QVector<Line> lines = {
        {first, mix->tapOpen(0), state.entities[0].active, style::bodyA()},
        {second, mix->tapOpen(1), state.entities[1].active, style::bodyB()},
    };
    for (int i = 0; i < lines.size(); ++i) {
        const Line& line = lines[i];
        const double startX = line.source.center().x();
        const double startY = line.source.bottom();
        // Отвод трубы: строго вниз от ёмкости, затем к смесителю.
        const double targetX = mixer.center().x() + (i == 0 ? -mixer.width() * 0.28 : mixer.width() * 0.28);
        const double elbowY = std::max(startY + 12.0, std::min(startY + 34.0, mixer.top() - 24.0));

        QPainterPath path;
        path.moveTo(startX, startY + 4.0);
        path.lineTo(startX, elbowY);
        path.quadTo(startX, elbowY + 16.0, targetX, elbowY + 16.0);
        path.lineTo(targetX, mixer.top() - 8.0);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(line.open ? line.color.darker(115) : QColor(190, 193, 198),
                             line.open ? 6.5 : 3.0, line.open ? Qt::SolidLine : Qt::DashLine, Qt::RoundCap));
        painter->drawPath(path);
        // Движущиеся капли: только пока идёт перекачка.
        if (line.flowing) {
            QPen drops(liquidColor(state.entities[i].value1).darker(120), 3.0, Qt::SolidLine, Qt::RoundCap);
            QVector<qreal> pattern;
            pattern << 0.02 << 5.0;
            drops.setDashPattern(pattern);
            drops.setDashOffset(std::fmod(t * 42.0, 15.6));
            painter->setPen(drops);
            painter->drawPath(path);
        }
        painter->restore();

        // Кран на линии.
        primitives::drawTap(painter, QPointF(startX, elbowY - 2.0), line.color, line.open);
        LabScene::drawTag(painter, QPointF(startX, elbowY - 20.0),
                          line.open ? QStringLiteral("кран открыт") : QStringLiteral("кран закрыт"),
                          QColor(255, 255, 255, 225), line.open ? line.color.darker(125) : QColor(150, 155, 162));

        // Стрелка направления переливания (без числовых значений).
        if (line.open) {
            vectors::Arrow flow;
            flow.from = QPointF(startX + (targetX - startX) * 0.30, elbowY + 16.0);
            flow.to = QPointF(startX + (targetX - startX) * 0.72, elbowY + 16.0);
            flow.symbol = QStringLiteral("поток");
            flow.kind = vectors::Kind::Flow;
            flow.color = line.flowing ? line.color.darker(120) : QColor(163, 167, 173);
            flow.width = 2.4;
            flow.labelOffset = QPointF(0.0, -16.0);
            vectors::drawArrow(painter, flow);
        }
    }

    // ---- Три ёмкости ----
    for (int i = 0; i < 3; ++i) {
        const QRectF vessel = vesselRect(i);
        const EntityVisual& entity = state.entities[i];
        const double concentration = entity.value1;   // проценты
        const QColor liquid = liquidColor(concentration);
        const double litersInVessel = i < 2 ? entity.value2 * entity.position : entity.value2;
        const double capacityLiters = i < 2 ? std::max(1.0, entity.value2) : std::max(1.0, mix->totalVolume());

        primitives::drawVessel(painter, vessel, entity.position, QColor(96, 104, 116), liquid, 2.4);

        LabScene::drawTag(painter, QPointF(vessel.center().x(), vessel.top() - (i < 2 ? 52.0 : 66.0)),
                          i < 2 ? QStringLiteral("Раствор %1").arg(i + 1) : QStringLiteral("Смеситель"),
                          QColor(255, 255, 255, 235), style::ink());
        LabScene::drawTag(painter, QPointF(vessel.center().x(), vessel.top() - (i < 2 ? 30.0 : 44.0)),
                          QStringLiteral("V = %1 л из %2").arg(utils::formatNumber(litersInVessel, 1),
                                                               utils::formatNumber(capacityLiters, 0)),
                          QColor(255, 255, 255, 225), style::inkSoft());
        if (entity.position > 0.10) {
            LabScene::drawTag(painter, QPointF(vessel.center().x(), vessel.center().y()),
                              QStringLiteral("C = %1 %").arg(utils::formatNumber(concentration, 1)),
                              QColor(255, 255, 255, 205), liquid.darker(220));
        }
        primitives::drawScale(painter, vessel.adjusted(4, 0, 4, 0), 8, QStringLiteral("л"), capacityLiters,
                              style::inkSoft());
    }

    // ---- Полоски массы вещества m = V·C ----
    const double barTop = mixer.top() + 4.0;
    struct SoluteBar {
        QString name;
        double solute;
        QColor color;
    };
    const QVector<SoluteBar> bars = {
        {QStringLiteral("m₁ = V₁·C₁"), mix->tapOpen(0) ? mix->soluteOf(0) : 0.0, style::bodyA()},
        {QStringLiteral("m₂ = V₂·C₂"), mix->tapOpen(1) ? mix->soluteOf(1) : 0.0, style::bodyB()},
        {QStringLiteral("m = m₁ + m₂"), mix->totalSolute(), style::bodyC()},
    };
    const double maxSolute = std::max({bars[0].solute, bars[1].solute, bars[2].solute, 1e-6});
    const double barLeft = second.left() - 10.0;
    const double barWidth = std::max(120.0, second.right() + 10.0 - barLeft);
    for (int i = 0; i < bars.size(); ++i) {
        const SoluteBar& bar = bars[i];
        const double y = barTop + i * 44.0;
        const double w = barWidth * std::clamp(bar.solute / maxSolute, 0.0, 1.0);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(style::inkSoft(), 1.0));
        painter->setBrush(QColor(255, 255, 255, 190));
        painter->drawRoundedRect(QRectF(barLeft, y, barWidth, 17.0), 4, 4);
        painter->setPen(Qt::NoPen);
        painter->setBrush(bar.color);
        painter->drawRoundedRect(QRectF(barLeft, y, std::max(2.0, w), 17.0), 4, 4);
        painter->setPen(style::ink());
        painter->setFont(style::uiFont(9.0, true));
        painter->drawText(QRectF(barLeft, y - 17, barWidth, 15), Qt::AlignLeft,
                          QStringLiteral("%1 = %2 л").arg(bar.name).arg(utils::formatNumber(bar.solute, 2)));
        painter->restore();
    }

    // ---- Линейка концентрации смеси под смесителем ----
    const QRectF gauge(mixer.center().x() - 105.0, mixer.bottom() + 16.0, 210.0, 26.0);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(style::ink(), 1.4));
    painter->setBrush(QColor(255, 255, 255, 225));
    painter->drawRoundedRect(gauge, 6, 6);
    const double cNow = state.primary * 100.0;
    const double cFinal = mix->finalConcentration() * 100.0;
    for (int i = 0; i <= 100; i += 2) {
        const double x = gauge.left() + 4 + (gauge.width() - 8) * i / 100.0;
        painter->setPen(QPen(liquidColor(i), 2.2));
        painter->drawLine(QPointF(x, gauge.top() + 5), QPointF(x, gauge.bottom() - 7));
    }
    const double markerX = gauge.left() + 4 + (gauge.width() - 8) * std::clamp(cNow / 100.0, 0.0, 1.0);
    painter->setPen(QPen(style::danger(), 2.0));
    painter->drawLine(QPointF(markerX, gauge.top() - 4), QPointF(markerX, gauge.bottom() + 4));
    painter->setPen(style::ink());
    painter->setFont(style::uiFont(8.0));
    painter->drawText(QRectF(gauge.left() - 8, gauge.bottom() + 3, 44, 13), Qt::AlignLeft, QStringLiteral("0 %"));
    painter->drawText(QRectF(gauge.right() - 44, gauge.bottom() + 3, 52, 13), Qt::AlignRight,
                      QStringLiteral("100 %"));
    painter->restore();
    LabScene::drawTag(painter, QPointF(mixer.center().x(), gauge.bottom() + 24.0),
                      QStringLiteral("C смеси = %1 % (итог %2 %)")
                          .arg(utils::formatNumber(cNow, 1), utils::formatNumber(cFinal, 2)),
                      QColor(255, 255, 255, 235), style::ink());

    LabScene::drawTag(painter, QPointF(mixer.center().x(), mixer.top() - 22.0),
                      QStringLiteral("V смеси = %1 л").arg(utils::formatNumber(state.secondary, 1)),
                      QColor(255, 255, 255, 235), style::inkSoft());

    // План работы насосов: зависит от состояния кранов и выбранного режима.
    QString schedule;
    if (mix->pumpsSimultaneously()) {
        QStringList parts;
        for (int i = 0; i < 2; ++i) {
            if (!mix->tapOpen(i))
                continue;
            parts << QStringLiteral("раствор %1 — 0…%2 мин").arg(i + 1)
                         .arg(utils::formatNumber(mix->pumpDuration(i), 2));
        }
        schedule = parts.isEmpty() ? QStringLiteral("оба крана закрыты")
                                   : QStringLiteral("оба крана открыты: %1").arg(parts.join(QStringLiteral(", ")));
    } else {
        if (mix->tapOpen(0) && mix->tapOpen(1))
            schedule = QStringLiteral("насос: раствор 1 — 0…%1 мин, раствор 2 — %1…%2 мин")
                           .arg(utils::formatNumber(mix->pumpSwitchTime(), 2),
                                utils::formatNumber(mix->eventTime(), 2));
        else if (mix->tapOpen(0))
            schedule = QStringLiteral("насос: раствор 1 — 0…%1 мин, линия 2 закрыта")
                           .arg(utils::formatNumber(mix->pumpDuration(0), 2));
        else if (mix->tapOpen(1))
            schedule = QStringLiteral("насос: раствор 2 — 0…%1 мин, линия 1 закрыта")
                           .arg(utils::formatNumber(mix->pumpDuration(1), 2));
        else
            schedule = QStringLiteral("оба крана закрыты");
    }
    LabScene::drawTag(painter, QPointF(m_viewArea.center().x(), m_viewArea.top() + 12.0), schedule,
                      QColor(255, 255, 255, 230), style::inkSoft());

    if (!state.eventLabel.isEmpty()) {
        LabScene::drawTag(painter, QPointF(m_viewArea.center().x(), m_viewArea.top() + 12.0), state.eventLabel,
                          QColor(234, 247, 239, 240), style::success().darker(130));
    }
}

}  // namespace lab::scenes
