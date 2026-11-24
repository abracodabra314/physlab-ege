#include "scenes/SpringVerticalScene.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

#include "models/SpringVerticalModel.h"
#include "scenes/SchemePrimitives.h"
#include "scenes/Style.h"
#include "utils/MathText.h"

namespace lab::scenes {
namespace {

constexpr double kMinBlockSize = 40.0;
constexpr double kMaxBlockSize = 78.0;
constexpr int kSpringCoils = 10;
/**
 * Натуральная длина пружины в метрах — соглашение отрисовки: на период
 * колебаний она не влияет (его задают m и k), но без неё длина пружины
 * на рисунке была бы равна одному лишь растяжению.
 */
constexpr double kNaturalLength = 0.16;

QColor springColorFor(double elongation, double equilibrium) {
    // Растянута сильнее равновесной — тёплый цвет, сжата — бирюзовый.
    if (elongation > equilibrium + 2e-3)
        return QColor(198, 96, 52);
    if (elongation < equilibrium - 2e-3)
        return QColor(20, 130, 140);
    return style::ink().darker(105);
}

}  // namespace

SpringVerticalScene::SpringVerticalScene(Simulation* model, QObject* parent) : LabScene(model, parent) {}

// ------------------------------------------------------------------
// Геометрия
// ------------------------------------------------------------------

void SpringVerticalScene::reconfigure(const QRectF& area) {
    auto* model = qobject_cast<models::SpringVerticalModel*>(m_model);
    const double elongMin = model ? std::max(0.0, model->minElongation()) : 0.02;
    const double elongMax = model ? model->maxElongation() : 0.25;

    m_schemeRect = QRectF(area.left() + 24.0, area.top() + 18.0, area.width() - 48.0,
                          std::max(170.0, area.height() * 0.52));

    // Шкала положения: от минимального до максимального растяжения пружины.
    m_elongationMin = elongMin;
    m_elongationMax = std::max(elongMin + 0.05, elongMax);

    const double blockSize = std::clamp(m_schemeRect.width() * 0.06, kMinBlockSize, kMaxBlockSize);
    m_anchorX = m_schemeRect.left() + m_schemeRect.width() * 0.34;
    m_anchorY = m_schemeRect.top() + 34.0;
    m_supportRect = QRectF(m_anchorX - 74.0, m_schemeRect.top() + 6.0, 148.0, 16.0);

    // Пружина с грузом не должны выйти за нижнюю границу схемы.
    const double travel = (kNaturalLength + m_elongationMax - m_elongationMin) * 1.0;
    const double available = std::max(60.0, m_schemeRect.bottom() - m_anchorY - blockSize - 26.0);
    m_pxPerMeter = available / std::max(0.05, travel);

    const double maxForce = std::max(1.0, model ? model->stiffness() * (model->maxElongation()) : 40.0);
    m_forceScale = vectors::autoScale(maxForce, 84.0, 26.0);
    const double maxSpeed = model ? model->amplitude() * model->omega() : 1.0;
    m_velocityScale = vectors::autoScale(std::max(0.1, maxSpeed), 66.0, 24.0);

    setRanges(area, -1.0, 1.0, m_elongationMin, m_elongationMax);
}

double SpringVerticalScene::sceneYOfElongation(double elongation) const {
    return m_anchorY + (kNaturalLength + (elongation - m_elongationMin)) * m_pxPerMeter;
}

double SpringVerticalScene::elongationOfSceneY(qreal sceneY) const {
    if (m_pxPerMeter <= 0.0)
        return m_elongationMin;
    return m_elongationMin + (sceneY - m_anchorY) / m_pxPerMeter - kNaturalLength;
}

double SpringVerticalScene::blockSize() const {
    return std::clamp(m_schemeRect.width() * 0.06, kMinBlockSize, kMaxBlockSize);
}

QRectF SpringVerticalScene::blockRectOfElongation(double elongation) const {
    const double size = blockSize();
    const double top = sceneYOfElongation(elongation);
    return QRectF(m_anchorX - size / 2.0, top, size, size);
}

// ------------------------------------------------------------------
// Перетаскивание груза мышью
// ------------------------------------------------------------------

double SpringVerticalScene::mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const {
    if (info.id == QLatin1String("x0")) {
        auto* model = qobject_cast<models::SpringVerticalModel*>(m_model);
        const double equilibrium = model ? model->equilibriumElongation() : 0.0;
        // Ручка держится за центр груза: чтобы получить растяжение пружины,
        // нужно сдвинуть позицию курсора на половину высоты блока.
        const double elongation = elongationOfSceneY(scenePos.y() - blockSize() / 2.0);
        return elongation - equilibrium;
    }
    return LabScene::mapHandlePosition(info, scenePos);
}

double SpringVerticalScene::handleScenePosition(const HandleInfo& info) const {
    if (info.id == QLatin1String("x0")) {
        auto* model = qobject_cast<models::SpringVerticalModel*>(m_model);
        const double equilibrium = model ? model->equilibriumElongation() : 0.0;
        return blockRectOfElongation(equilibrium + info.value).center().y();
    }
    return LabScene::handleScenePosition(info);
}

void SpringVerticalScene::positionHandles() {
    for (DragHandleItem* handle : m_handles) {
        HandleInfo info = handle->info();
        info.value = m_model ? m_model->param(handle->id()) : info.value;
        // Ручка — сбоку от груза, на уровне его центра.
        handle->setAnchor(QPointF(m_anchorX + blockSize() * 0.5 + 26.0, handleScenePosition(info)));
    }
}

// ------------------------------------------------------------------
// Векторы: в модели только две силы
// ------------------------------------------------------------------

QVector<vectors::Arrow> SpringVerticalScene::vectorsFor(double t) const {
    auto* model = qobject_cast<models::SpringVerticalModel*>(m_model);
    QVector<vectors::Arrow> arrows;
    if (!model)
        return arrows;

    const double x = model->displacementAt(t);
    const double v = model->velocityAt(t);
    const QRectF block = blockRectOfElongation(model->elongationAt(t));

    if (true) {  // силы в динамической модели показываем всегда, в отличие от v и a
        // Сила тяжести — из центра груза вниз.
        vectors::Arrow gravityArrow;
        gravityArrow.from = block.center();
        gravityArrow.to =
            QPointF(block.center().x(), block.center().y() + vectors::visualLength(model->weight(), m_forceScale));
        gravityArrow.symbol = QStringLiteral("mg");
        gravityArrow.kind = vectors::Kind::Gravity;
        gravityArrow.halo = true;
        gravityArrow.labelOffset = QPointF(block.width() * 0.5 + 24.0, 4.0);
        arrows.push_back(gravityArrow);

        // Сила упругости — из точки крепления пружины вертикально вверх.
        vectors::Arrow elasticArrow;
        elasticArrow.from = QPointF(block.center().x(), block.top());
        elasticArrow.to = QPointF(block.center().x(),
                                 block.top() - vectors::visualLength(model->elasticForceAt(t), m_forceScale));
        elasticArrow.symbol = QStringLiteral("Fупр");
        elasticArrow.kind = vectors::Kind::Elastic;
        elasticArrow.halo = true;
        elasticArrow.labelOffset = QPointF(-(block.width() * 0.5 + 26.0), -4.0);
        arrows.push_back(elasticArrow);
    }

    if (m_model->showsVelocities() && std::abs(v) > 1e-9) {
        // Ось Y направлена вниз: v > 0 — движение вниз.
        vectors::Arrow velocityArrow;
        velocityArrow.from = QPointF(block.right() + 30.0, block.center().y());
        velocityArrow.to = QPointF(block.right() + 30.0,
                                  block.center().y() + std::copysign(vectors::visualLength(std::abs(v), m_velocityScale), v));
        velocityArrow.symbol = QStringLiteral("v");
        velocityArrow.kind = vectors::Kind::Velocity;
        velocityArrow.labelOffset = QPointF(24.0, 0.0);
        arrows.push_back(velocityArrow);
    }

    if (m_model->showsAccelerations() && std::abs(model->accelerationOf(x)) > 1e-9) {
        const double acceleration = model->accelerationOf(x);
        vectors::Arrow accelerationArrow;
        accelerationArrow.from = QPointF(block.left() - 30.0, block.center().y());
        accelerationArrow.to = QPointF(
            block.left() - 30.0,
            block.center().y() + std::copysign(vectors::visualLength(std::abs(acceleration), m_forceScale), acceleration));
        accelerationArrow.symbol = QStringLiteral("a");
        accelerationArrow.kind = vectors::Kind::Acceleration;
        accelerationArrow.labelOffset = QPointF(-26.0, 0.0);
        arrows.push_back(accelerationArrow);
    }

    return arrows;
}

void SpringVerticalScene::drawForces(QPainter* painter, const QVector<vectors::Arrow>& arrows) const {
    for (const vectors::Arrow& arrow : arrows)
        vectors::drawArrow(painter, arrow);
}

// ------------------------------------------------------------------
// Схема
// ------------------------------------------------------------------

void SpringVerticalScene::paintScheme(QPainter* painter) {
    auto* model = qobject_cast<models::SpringVerticalModel*>(m_model);
    if (!model)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const double x = model->displacementAt(m_model->time());
    const double elongation = model->elongationAt(m_model->time());
    const double equilibrium = model->equilibriumElongation();

    // ---- крепление сверху (неподвижная опора) ----
    primitives::drawFixedSupport(painter, m_supportRect, QPointF(0.0, -1.0), style::inkSoft());
    painter->setFont(style::uiFont(8.5, true));
    painter->setPen(style::inkSoft());
    painter->drawText(QRectF(m_supportRect.left() - 10.0, m_supportRect.bottom() + 2.0, 200.0, 15), Qt::AlignLeft,
                      QStringLiteral("неподвижное крепление"));

    // ---- пружина: длина = натуральная длина + растяжение ----
    const QRectF block = blockRectOfElongation(elongation);
    const QPointF springFrom(m_anchorX, m_supportRect.bottom());
    const QPointF springTo(m_anchorX, block.top());
    primitives::drawSpring(painter, springFrom, springTo, kSpringCoils, 12.0,
                           springColorFor(elongation, equilibrium), 2.6);

    // ---- груз ----
    painter->setPen(QPen(style::bodyA().darker(160), 2.0));
    painter->setBrush(style::bodyA().lighter(150));
    painter->drawRoundedRect(block, 6, 6);
    painter->setPen(Qt::white);
    painter->setFont(style::uiFont(11.5, true));
    painter->drawText(block, Qt::AlignCenter, QStringLiteral("m"));

    // ---- вертикальная шкала положения справа ----
    const double rulerX = m_schemeRect.right() - 52.0;
    painter->setPen(QPen(style::inkSoft(), 1.2));
    painter->drawLine(QPointF(rulerX, m_anchorY + 6.0), QPointF(rulerX, m_schemeRect.bottom() - 6.0));
    painter->setFont(style::uiFont(7.5));
    const double step = (m_elongationMax - m_elongationMin) > 0.3 ? 0.1 : 0.05;
    for (int i = -8; i <= 12; ++i) {
        const double value = std::round((m_elongationMin + step * i) / step) * step;
        if (value < m_elongationMin - 1e-9 || value > m_elongationMax + 1e-9)
            continue;
        const double py = sceneYOfElongation(value);
        Q_UNUSED(value);
        painter->setPen(QPen(style::inkSoft(), 1.0));
        painter->drawLine(QPointF(rulerX, py), QPointF(rulerX + 9.0, py));  // штрих Δl без числа
    }

    // ---- положение равновесия ----
    const double equilibriumY = sceneYOfElongation(equilibrium);
    painter->setPen(QPen(style::accent(), 1.4, Qt::DashLine));
    painter->drawLine(QPointF(m_schemeRect.left() + 20.0, equilibriumY), QPointF(rulerX, equilibriumY));
    painter->setFont(style::uiFont(8.5, true));
    painter->setPen(style::accent().darker(120));
    painter->drawText(QRectF(m_schemeRect.left() + 22.0, equilibriumY - 17.0, 320.0, 15), Qt::AlignLeft,
                      QStringLiteral("x = 0 — равновесие: k·Δl₀ = mg"));

    // ---- растяжение пружины в текущий момент ----
    if (elongation > 1e-6) {
        drawDimension(painter, QPointF(m_anchorX + 40.0, m_supportRect.bottom()),
                      QPointF(m_anchorX + 40.0, block.top()),
                      QStringLiteral("Δl"), style::inkSoft(), 0.0);
    }
    // Смещение относительно равновесия.
    if (std::abs(x) > 1e-6) {
        drawDimension(painter, QPointF(m_anchorX - 46.0, equilibriumY), QPointF(m_anchorX - 46.0, block.center().y()),
                      QStringLiteral("x"), style::accent(), 0.0);
    }

    drawForces(painter, vectorsFor(m_model->time()));
    drawPlots(painter);
    painter->restore();
}

// ------------------------------------------------------------------
// Графики
// ------------------------------------------------------------------

void SpringVerticalScene::drawPlots(QPainter* painter) const {
    auto* model = qobject_cast<models::SpringVerticalModel*>(m_model);
    if (!model)
        return;

    const double plotsTop = m_schemeRect.bottom() + 20.0;
    const double available = m_viewArea.bottom() - plotsTop - 4.0;
    if (available < 90.0)
        return;

    constexpr double kGap = 12.0;
    const double plotWidth = (m_viewArea.width() - 2.0 * kGap) / 3.0;
    const double height = std::min(available, 300.0);
    const double top = plotsTop + std::max(0.0, available - height) * 0.5;

    struct SeriesSpec {
        QString title;
        QVector<QPointF> points;
        QColor color;
    };
    const QVector<SeriesSpec> specs = {
        {QStringLiteral("Координата x(t), м"), model->displacementSeries(), style::bodyA()},
        {QStringLiteral("Скорость v(t), м/с"), model->velocitySeries(), style::bodyC()},
        {QStringLiteral("Ускорение a(t), м/с²"), model->accelerationSeries(), style::accentWarm()},
    };

    const double duration = std::max(1e-6, m_model->duration());
    for (int i = 0; i < specs.size(); ++i) {
        const QRectF rect(m_viewArea.left() + i * (plotWidth + kGap), top, plotWidth, height);
        PlotSeries series;
        series.points = specs.at(i).points;
        series.color = specs.at(i).color;
        LabScene::drawPlot(painter, rect, specs.at(i).title, m_model->timeUnit(), {series}, duration,
                           m_model->time(), std::numeric_limits<double>::quiet_NaN());
    }
}

}  // namespace lab::scenes
