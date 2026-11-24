#include "scenes/SpringHorizontalScene.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

#include "models/SpringHorizontalModel.h"
#include "scenes/SchemePrimitives.h"
#include "scenes/Style.h"
#include "scenes/VectorArrows.h"
#include "utils/MathText.h"

namespace lab::scenes {
namespace {

constexpr double kMinBlockSize = 46.0;
constexpr double kMaxBlockSize = 92.0;
constexpr int kSpringCoils = 11;

QColor springColorFor(double current, double natural) {
    // Направление деформации видно по цвету: растяжение — тёплый, сжатие — бирюзовый.
    if (current > natural + 0.5)
        return QColor(198, 96, 52);
    if (current < natural - 0.5)
        return QColor(20, 130, 140);
    return style::ink().darker(105);
}

}  // namespace

SpringHorizontalScene::SpringHorizontalScene(Simulation* model, QObject* parent) : LabScene(model, parent) {}

// ------------------------------------------------------------------
// Геометрия: пересчитывается при изменении окна и параметров
// ------------------------------------------------------------------

void SpringHorizontalScene::reconfigure(const QRectF& area) {
    auto* model = qobject_cast<models::SpringHorizontalModel*>(m_model);
    const double amplitude = model ? model->maxDisplacement() : 0.3;

    m_schemeRect = QRectF(area.left() + 24.0, area.top() + 18.0, area.width() - 48.0,
                          std::max(140.0, area.height() * 0.44));

    // Мир сцены — метры смещения относительно положения равновесия.
    const double halfRange = std::max(0.08, amplitude * 1.45);
    setRanges(area, -halfRange, halfRange, -halfRange, halfRange);

    m_wallRect = QRectF(m_schemeRect.left(), m_schemeRect.top() + m_schemeRect.height() * 0.10, 20.0,
                        m_schemeRect.height() * 0.58);
    m_groundY = m_wallRect.bottom();
    m_eqSceneX = m_wallRect.right() + (m_schemeRect.right() - m_wallRect.right()) * 0.52;

    // Масштаб: груз с максимальным смещением не задевает стену и не выходит
    // за правый край схемы.
    const double block = std::clamp(m_schemeRect.width() * 0.085, kMinBlockSize, kMaxBlockSize);
    const double rightRoom = std::max(90.0, m_schemeRect.right() - m_eqSceneX - block * 0.65);
    const double leftRoom = std::max(80.0, m_eqSceneX - m_wallRect.right() - block * 0.65);
    m_pxPerMeter = std::min(rightRoom, leftRoom) / halfRange;

    const double stiffness = model ? model->stiffness() : 120.0;
    const double maxForce = std::max(1.0, stiffness * amplitude + (model ? model->frictionForce() : 0.0));
    m_forceScale = vectors::autoScale(maxForce, 92.0, 26.0);
    const double maxSpeed = model ? model->maxSpeed() : 1.0;
    m_velocityScale = vectors::autoScale(std::max(0.1, maxSpeed), 72.0, 24.0);
}

double SpringHorizontalScene::sceneXOf(double meters) const { return m_eqSceneX + meters * m_pxPerMeter; }

double SpringHorizontalScene::metersOfSceneX(qreal sceneX) const { return (sceneX - m_eqSceneX) / m_pxPerMeter; }

QRectF SpringHorizontalScene::blockRectOf(double meters) const {
    const double size = std::clamp(m_schemeRect.width() * 0.085, kMinBlockSize, kMaxBlockSize);
    const double centerX = sceneXOf(meters);
    return QRectF(centerX - size / 2.0, m_groundY - size, size, size);
}

// ------------------------------------------------------------------
// Перетаскивание груза мышью
// ------------------------------------------------------------------

double SpringHorizontalScene::mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const {
    if (info.id == QLatin1String("x0"))
        return metersOfSceneX(scenePos.x());
    return LabScene::mapHandlePosition(info, scenePos);
}

double SpringHorizontalScene::handleScenePosition(const HandleInfo& info) const {
    if (info.id == QLatin1String("x0"))
        return sceneXOf(info.value);
    return LabScene::handleScenePosition(info);
}

void SpringHorizontalScene::positionHandles() {
    for (DragHandleItem* handle : m_handles) {
        HandleInfo info = handle->info();
        info.value = m_model ? m_model->param(handle->id()) : info.value;
        const QRectF block = blockRectOf(0.0);
        handle->setAnchor(QPointF(handleScenePosition(info), block.top() - 16.0));
    }
}

// ------------------------------------------------------------------
// Схема
// ------------------------------------------------------------------

void SpringHorizontalScene::paintScheme(QPainter* painter) {
    auto* model = qobject_cast<models::SpringHorizontalModel*>(m_model);
    if (!model)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const double x = model->displacementAt(m_model->time());

    // ---- стена: неподвижная опора пружины ----
    primitives::drawFixedSupport(painter, m_wallRect, QPointF(-1.0, 0.0), style::inkSoft());
    painter->setFont(style::uiFont(8.5, true));
    painter->setPen(style::inkSoft());
    painter->drawText(QRectF(m_wallRect.left() - 4.0, m_wallRect.top() - 20.0, 110.0, 16), Qt::AlignLeft,
                      QStringLiteral("неподвижная стена"));

    // ---- горизонтальная поверхность ----
    const QRectF surface(m_wallRect.left(), m_groundY, m_schemeRect.right() - m_wallRect.left(), 12.0);
    painter->setPen(QPen(style::ink(), 1.8));
    painter->setBrush(QColor(226, 224, 216, 190));
    painter->drawRect(surface);
    primitives::drawHatchedSurface(painter, QRectF(surface.left(), surface.bottom(), surface.width(), 16.0),
                                   style::inkSoft());

    // ---- линейка смещений под поверхностью ----
    const double rulerY = surface.bottom() + 30.0;
    const double span = model->maxDisplacement() * 1.35;
    painter->setPen(QPen(style::inkSoft(), 1.1));
    painter->drawLine(QPointF(sceneXOf(-span), rulerY), QPointF(sceneXOf(span), rulerY));
    painter->setFont(style::uiFont(7.5));
    const double tickStep = span > 0.25 ? 0.1 : 0.05;
    for (int i = -10; i <= 10; ++i) {
        const double value = tickStep * i;
        if (std::abs(value) > span + 1e-9)
            continue;
        const double px = sceneXOf(value);
        const bool major = (i % 2) == 0;
        painter->setPen(QPen(major ? style::inkSoft() : style::grid(), major ? 1.1 : 0.8));
        painter->drawLine(QPointF(px, rulerY), QPointF(px, rulerY - (major ? 9.0 : 5.0)));
        Q_UNUSED(value);
        Q_UNUSED(major);  // линейка координат — штрихи без чисел
    }

    // Положение равновесия.
    painter->setPen(QPen(style::accent(), 1.3, Qt::DashLine));
    painter->drawLine(QPointF(m_eqSceneX, m_schemeRect.top() + 4.0), QPointF(m_eqSceneX, rulerY));
    painter->setFont(style::uiFont(8.5, true));
    painter->setPen(style::accent().darker(120));
    painter->drawText(QRectF(m_eqSceneX - 90.0, rulerY + 16.0, 180.0, 15), Qt::AlignCenter,
                      QStringLiteral("x = 0 — положение равновесия"));

    // ---- пружина: длина определяется положением груза ----
    const QRectF block = blockRectOf(x);
    const QPointF springFrom(m_wallRect.right(), block.center().y());
    const QPointF springTo(block.left(), block.center().y());
    const double naturalLength = std::max(24.0, m_eqSceneX - m_wallRect.right() - block.width() / 2.0);
    const double currentLength = std::max(8.0, springTo.x() - springFrom.x());
    primitives::drawSpring(painter, springFrom, springTo, kSpringCoils, 13.0,
                           springColorFor(currentLength, naturalLength), 2.6);
    // Естественная длина пружины — тонкая отметка: видно, растянута она или сжата.
    painter->setPen(QPen(style::inkSoft(), 0.9, Qt::DotLine));
    const double naturalX = m_wallRect.right() + naturalLength;
    painter->drawLine(QPointF(naturalX, block.top() - 12.0), QPointF(naturalX, block.bottom() + 12.0));

    // ---- груз ----
    painter->setPen(QPen(style::bodyA().darker(160), 2.0));
    painter->setBrush(style::bodyA().lighter(150));
    painter->drawRoundedRect(block, 6, 6);
    painter->setPen(Qt::white);
    painter->setFont(style::uiFont(12.0, true));
    painter->drawText(block, Qt::AlignCenter, QStringLiteral("m"));

    // Размер смещения от равновесия.
    if (std::abs(x) > 1e-6) {
        drawDimension(painter, QPointF(m_eqSceneX, m_schemeRect.top() + 10.0),
                      QPointF(block.center().x(), m_schemeRect.top() + 10.0),
                      QStringLiteral("x"), style::accent(), -14.0);
    }

    // Состояние покоя после остановки груза.
    const double stop = model->restTime();
    if (std::isfinite(stop) && m_model->time() >= stop - 1e-9) {
        LabScene::drawTag(painter, QPointF(block.center().x(), block.bottom() + 46.0),
                          QStringLiteral("покой: |Fупр| ≤ μmg"), QColor(234, 247, 239, 235),
                          style::success().darker(120));
    }

    drawForces(painter, vectorsFor(m_model->time()));
    drawPlots(painter);
    painter->restore();
}

QVector<vectors::Arrow> SpringHorizontalScene::vectorsFor(double t) const {
    auto* model = qobject_cast<models::SpringHorizontalModel*>(m_model);
    QVector<vectors::Arrow> arrows;
    if (!model)
        return arrows;

    const double x = model->displacementAt(t);
    const double v = model->velocityAt(t);
    const QRectF block = blockRectOf(x);

    const double weight = model->mass() * model->gravity();
    const double normal = model->normalForce();
    const double springForce = model->elasticForceAt(t);
    const double friction = model->frictionForceAt(t);
    const double acceleration = model->accelerationOf(x, v);

    if (true) {  // силы в динамической модели показываем всегда, в отличие от v и a
        // Сила тяжести — из центра тяжести груза вертикально вниз. Стрелка
        // проходит по заливаемой фигуре груза, поэтому ей нужна светлая
        // обводка: иначе вектор на цветной заливке не читается.
        vectors::Arrow gravityArrow;
        gravityArrow.from = block.center();
        gravityArrow.to = QPointF(block.center().x(), block.center().y() + vectors::visualLength(weight, m_forceScale));
        gravityArrow.symbol = QStringLiteral("mg");
        gravityArrow.kind = vectors::Kind::Gravity;
        gravityArrow.halo = true;
        // Подпись — снаружи тела: иначе «пилюля» закрывает саму стрелку.
        gravityArrow.labelOffset = QPointF(block.width() * 0.5 + 22.0, 4.0);
        arrows.push_back(gravityArrow);

        // Сила упругости — вдоль оси пружины, точка приложения — точка крепления.
        const double springLength = vectors::visualLength(std::abs(springForce), m_forceScale);
        if (springLength > 1.0) {
            vectors::Arrow elasticArrow;
            elasticArrow.from = QPointF(block.left(), block.center().y());
            elasticArrow.to = QPointF(block.left() + std::copysign(springLength, springForce), block.center().y());
            elasticArrow.symbol = QStringLiteral("Fупр");
            elasticArrow.kind = vectors::Kind::Elastic;
            elasticArrow.halo = true;
            elasticArrow.labelOffset = QPointF(0.0, -20.0);
            arrows.push_back(elasticArrow);
        }

        // Сила трения скольжения — из точки контакта вдоль поверхности.
        const double frictionLength = vectors::visualLength(std::abs(friction), m_forceScale);
        if (frictionLength > 1.0) {
            vectors::Arrow frictionArrow;
            frictionArrow.from = QPointF(block.center().x(), block.bottom());
            frictionArrow.to =
                QPointF(block.center().x() + std::copysign(frictionLength, friction), block.bottom());
            frictionArrow.symbol = QStringLiteral("Fтр");
            frictionArrow.kind = vectors::Kind::Friction;
            frictionArrow.labelOffset = QPointF(0.0, 16.0);
            arrows.push_back(frictionArrow);
        }

        // Реакция опоры — из точки контакта вертикально вверх; она тоже
        // проходит по телу, поэтому рисуется с обводкой.
        vectors::Arrow normalArrow;
        normalArrow.from = QPointF(block.center().x(), block.bottom());
        normalArrow.to =
            QPointF(block.center().x(), block.bottom() - vectors::visualLength(normal, m_forceScale));
        normalArrow.symbol = QStringLiteral("N");
        normalArrow.kind = vectors::Kind::Normal;
        normalArrow.halo = true;
        normalArrow.labelOffset = QPointF(-(block.width() * 0.5 + 22.0), -4.0);
        arrows.push_back(normalArrow);
    }

    // Кинематические векторы отложены над грузом, чтобы не перекрывать силы.
    if (m_model->showsVelocities() && std::abs(v) > 1e-9) {
        vectors::Arrow velocityArrow;
        velocityArrow.from = QPointF(block.center().x(), block.top() - 12.0);
        velocityArrow.to =
            QPointF(block.center().x() + std::copysign(vectors::visualLength(std::abs(v), m_velocityScale), v),
                    block.top() - 12.0);
        velocityArrow.symbol = QStringLiteral("v");
        velocityArrow.kind = vectors::Kind::Velocity;
        velocityArrow.labelOffset = QPointF(0.0, -18.0);
        arrows.push_back(velocityArrow);
    }

    if (m_model->showsAccelerations() && std::abs(acceleration) > 1e-9) {
        vectors::Arrow accelerationArrow;
        accelerationArrow.from = QPointF(block.center().x(), block.top() - 46.0);
        accelerationArrow.to = QPointF(
            block.center().x() + std::copysign(vectors::visualLength(std::abs(acceleration), m_forceScale), acceleration),
            block.top() - 46.0);
        accelerationArrow.symbol = QStringLiteral("a");
        accelerationArrow.kind = vectors::Kind::Acceleration;
        accelerationArrow.labelOffset = QPointF(0.0, -18.0);
        arrows.push_back(accelerationArrow);
    }

    return arrows;
}

void SpringHorizontalScene::drawForces(QPainter* painter, const QVector<vectors::Arrow>& arrows) const {
    for (const vectors::Arrow& arrow : arrows)
        vectors::drawArrow(painter, arrow);
}

// ------------------------------------------------------------------
// Графики: строятся по узлам рассчитанной траектории
// ------------------------------------------------------------------

void SpringHorizontalScene::drawPlots(QPainter* painter) const {
    auto* model = qobject_cast<models::SpringHorizontalModel*>(m_model);
    if (!model)
        return;

    const double plotsTop = m_schemeRect.bottom() + 26.0;
    const double available = m_viewArea.bottom() - plotsTop - 4.0;
    if (available < 90.0)
        return;

    constexpr double kGap = 12.0;
    const double plotWidth = (m_viewArea.width() - 2.0 * kGap) / 3.0;
    // Графики занимают всё свободное место под схемой: шкалы остаются
    // крупными, а между схемой, графиками и осью времени не появляется
    // пустой полосы.
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
    const double eventT = m_model->eventTime();
    for (int i = 0; i < specs.size(); ++i) {
        const QRectF rect(m_viewArea.left() + i * (plotWidth + kGap), top, plotWidth, height);
        PlotSeries series;
        series.points = specs.at(i).points;
        series.color = specs.at(i).color;
        LabScene::drawPlot(painter, rect, specs.at(i).title, m_model->timeUnit(), {series}, duration,
                           m_model->time(), eventT);
    }
}

}  // namespace lab::scenes
