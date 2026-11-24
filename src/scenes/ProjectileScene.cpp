#include "scenes/ProjectileScene.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

#include "models/ProjectileModel.h"
#include "scenes/SchemePrimitives.h"
#include "scenes/Style.h"
#include "utils/MathText.h"

namespace lab::scenes {
namespace {

// Запас по осям: трасса не должна упираться в край схемы, иначе шкалы
// «слипаются» и точки падения/вершины различить нельзя.
constexpr double kHorizontalPadding = 1.10;
constexpr double kVerticalPadding = 1.20;
// Толщина трассы и пунктир предсказанной части.
constexpr double kTrajectoryWidth = 2.6;

}  // namespace

ProjectileScene::ProjectileScene(Simulation* model, QObject* parent) : LabScene(model, parent) {}

// ------------------------------------------------------------------
// Геометрия: мир задачи — метры, начало координат на поверхности земли
// ------------------------------------------------------------------

void ProjectileScene::reconfigure(const QRectF& area) {
    auto* model = qobject_cast<models::ProjectileModel*>(m_model);

    // Рамка схемы — верхняя часть области просмотра. Внутри неё отдельная
    // прямоугольная зона «мира»: слева и снизу оставлены поля подписей осей.
    m_schemeRect = QRectF(area.left() + 24.0, area.top() + 18.0, area.width() - 48.0,
                          std::max(180.0, area.height() * 0.50));
    m_worldRect = m_schemeRect.adjusted(56.0, 18.0, -18.0, -46.0);

    double travel = 60.0;      // дальность полёта
    double rise = 20.0;        // наибольшая высота
    double startX = 0.0;
    double startY = 0.0;
    if (model && model->validate().ok) {
        travel = std::max(1.0, model->range());
        rise = std::max(1.0, model->maxHeight());
        startX = model->launchPosition();
        startY = model->launchHeight();
    }

    // Ось X: начало координат видно всегда, правая граница — за точкой падения.
    m_worldXMin = -travel * 0.05;
    m_worldXMax = std::max(m_worldXMin + 1.0, startX + travel * kHorizontalPadding);
    m_worldYMin = 0.0;
    m_worldYMax = std::max(std::max(rise, startY) * kVerticalPadding, 2.0);

    setRanges(area, m_worldXMin, m_worldXMax, m_worldYMin, m_worldYMax);
    m_groundY = m_worldRect.bottom();

    const double maxSpeed = model && model->validate().ok ? model->speed() : 30.0;
    m_velocityScale = vectors::autoScale(maxSpeed, 78.0, 26.0);
    const double gravity = model ? model->gravity() : 9.8;
    m_forceScale = vectors::autoScale(gravity, 58.0, 26.0);
}

QPointF ProjectileScene::scenePointOf(double x, double y) const {
    const double px = m_worldRect.left() + (x - m_worldXMin) / std::max(1e-9, m_worldXMax - m_worldXMin) *
                                                m_worldRect.width();
    const double py = m_worldRect.bottom() - (y - m_worldYMin) / std::max(1e-9, m_worldYMax - m_worldYMin) *
                                                m_worldRect.height();
    return QPointF(px, py);
}

QPointF ProjectileScene::muzzleScenePos() const {
    auto* model = qobject_cast<models::ProjectileModel*>(m_model);
    if (!model)
        return QPointF(m_worldRect.left(), m_groundY);
    return scenePointOf(model->launchPosition(), model->launchHeight());
}

// ------------------------------------------------------------------
// Перетаскивание: направление выстрела и точка вылета
// ------------------------------------------------------------------

double ProjectileScene::mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const {
    auto* model = qobject_cast<models::ProjectileModel*>(m_model);
    if (!model)
        return LabScene::mapHandlePosition(info, scenePos);

    if (info.id == QLatin1String("angle")) {
        // Ручка направления выстрела: угол считается по вектору от дульного
        // среза — так можно «навести» ствол мышью.
        const QPointF delta = scenePos - muzzleScenePos();
        if (std::hypot(delta.x(), delta.y()) < 4.0)
            return model->angleDegrees();
        const double angle = std::atan2(-delta.y(), delta.x()) * 180.0 / M_PI;
        return std::clamp(angle, 0.0, 90.0);
    }
    if (info.id == QLatin1String("y0")) {
        const double span = m_worldYMax - m_worldYMin;
        const double meters = m_worldYMin + (m_groundY - scenePos.y()) / std::max(1e-9, m_worldRect.height()) * span;
        return std::max(0.0, meters);
    }
    if (info.id == QLatin1String("x0")) {
        const double span = m_worldXMax - m_worldXMin;
        const double meters =
            m_worldXMin + (scenePos.x() - m_worldRect.left()) / std::max(1e-9, m_worldRect.width()) * span;
        return std::max(0.0, meters);
    }
    return LabScene::mapHandlePosition(info, scenePos);
}

double ProjectileScene::handleScenePosition(const HandleInfo& info) const {
    auto* model = qobject_cast<models::ProjectileModel*>(m_model);
    if (!model)
        return LabScene::handleScenePosition(info);

    if (info.id == QLatin1String("angle")) {
        // Значение угла кодируется положением ручки на луче выстрела;
        // для вертикальной шкалы используется высота конца луча.
        const double radians = info.value * M_PI / 180.0;
        const double radius = aimRadius();
        return muzzleScenePos().y() - std::sin(radians) * radius;
    }
    if (info.id == QLatin1String("y0"))
        return scenePointOf(0.0, info.value).y();
    if (info.id == QLatin1String("x0"))
        return scenePointOf(info.value, 0.0).x();
    return LabScene::handleScenePosition(info);
}

double ProjectileScene::aimRadius() const {
    return std::max(60.0, std::min(m_worldRect.width(), m_worldRect.height()) * 0.26);
}

void ProjectileScene::positionHandles() {
    auto* model = qobject_cast<models::ProjectileModel*>(m_model);
    if (!model)
        return;

    const QPointF muzzle = muzzleScenePos();
    const double radius = aimRadius();

    for (DragHandleItem* handle : m_handles) {
        const HandleInfo info = handle->info();
        if (info.id == QLatin1String("angle")) {
            const double radians = model->angleDegrees() * M_PI / 180.0;
            handle->setAxis(DragHandleItem::Axis::Both);
            handle->setAnchor(muzzle + QPointF(std::cos(radians), -std::sin(radians)) * radius);
        } else if (info.id == QLatin1String("y0")) {
            handle->setAxis(DragHandleItem::Axis::Vertical);
            handle->setAnchor(QPointF(muzzle.x() - 40.0, scenePointOf(0.0, model->launchHeight()).y()));
        } else if (info.id == QLatin1String("x0")) {
            handle->setAxis(DragHandleItem::Axis::Horizontal);
            handle->setAnchor(QPointF(scenePointOf(model->launchPosition(), 0.0).x(), m_groundY + 16.0));
        } else {
            handle->setAnchor(QPointF(muzzle.x(), muzzle.y() - 24.0));
        }
    }
}

// ------------------------------------------------------------------
// Векторы: скорость, её проекции и ускорение
// ------------------------------------------------------------------

QVector<vectors::Arrow> ProjectileScene::vectorsFor(double t) const {
    auto* model = qobject_cast<models::ProjectileModel*>(m_model);
    QVector<vectors::Arrow> arrows;
    if (!model || !model->validate().ok)
        return arrows;

    // После падения снаряда векторов нет: движение завершилось.
    if (t >= model->flightTime() - 1e-9)
        return arrows;

    const QPointF shell = scenePointOf(model->xAt(t), std::max(0.0, model->yAt(t)));
    const double vx = model->vxAt(t);
    const double vy = model->vyAt(t);

    if (m_model->showsVelocities()) {
        // Сами проекции — слагаемые скорости, они рисуются штрихом.
        vectors::Arrow horizontal;
        horizontal.from = shell;
        horizontal.to = shell + QPointF(std::copysign(vectors::visualLength(std::abs(vx), m_velocityScale), vx), 0.0);
        horizontal.symbol = QStringLiteral("vₓ");
        horizontal.kind = vectors::Kind::Velocity;
        horizontal.dashed = true;
        horizontal.width = 2.0;
        horizontal.labelOffset = QPointF(0.0, 18.0);
        arrows.push_back(horizontal);

        vectors::Arrow vertical;
        vertical.from = shell;
        vertical.to = shell + QPointF(0.0, -std::copysign(vectors::visualLength(std::abs(vy), m_velocityScale), vy));
        vertical.symbol = QStringLiteral("vᵧ");
        vertical.kind = vectors::Kind::Velocity;
        vertical.dashed = true;
        vertical.width = 2.0;
        vertical.labelOffset = QPointF(-30.0, 0.0);
        arrows.push_back(vertical);

        // Скорость — векторная сумма проекций: от снаряда по направлению полёта.
        const double speed = std::hypot(vx, vy);
        vectors::Arrow velocity;
        velocity.from = shell;
        velocity.to = shell + QPointF(vx, -vy) / std::max(1e-9, speed) * vectors::visualLength(speed, m_velocityScale);
        velocity.symbol = QStringLiteral("v");
        velocity.kind = vectors::Kind::Velocity;
        velocity.labelOffset = QPointF(18.0, -12.0);
        arrows.push_back(velocity);
    }

    if (true) {  // силы в динамической модели показываем всегда, в отличие от v и a
        // Единственная сила, которая действует на снаряд после вылета, —
        // сила тяжести. Силы сопротивления в модели нет, поэтому других
        // стрелок на схеме не появляется.
        vectors::Arrow weight;
        weight.from = shell;
        weight.to = shell + QPointF(0.0, vectors::visualLength(model->gravity(), m_forceScale));
        weight.symbol = QStringLiteral("mg");
        weight.kind = vectors::Kind::Gravity;
        weight.labelOffset = QPointF(-34.0, 6.0);
        arrows.push_back(weight);
    }

    if (m_model->showsAccelerations()) {
        // Ускорение постоянно: aₓ = 0, aᵧ = −g. Стрелка отложена чуть в
        // стороне от снаряда, чтобы не совпадать со стрелкой силы тяжести.
        const QPointF anchor = shell + QPointF(34.0, 0.0);
        vectors::Arrow acceleration;
        acceleration.from = anchor;
        acceleration.to = anchor + QPointF(0.0, vectors::visualLength(model->gravity(), m_forceScale));
        acceleration.symbol = QStringLiteral("a");
        acceleration.kind = vectors::Kind::Acceleration;
        acceleration.labelOffset = QPointF(26.0, 6.0);
        arrows.push_back(acceleration);
    }

    return arrows;
}

void ProjectileScene::drawForces(QPainter* painter, const QVector<vectors::Arrow>& arrows) const {
    for (const vectors::Arrow& arrow : arrows)
        vectors::drawArrow(painter, arrow);
}

// ------------------------------------------------------------------
// Отрисовка: трасса, оси, проекции движущейся точки
// ------------------------------------------------------------------

void ProjectileScene::drawTrajectory(QPainter* painter) const {
    auto* model = qobject_cast<models::ProjectileModel*>(m_model);
    if (!model || !model->validate().ok)
        return;

    const double duration = std::max(1e-6, m_model->duration());
    const double current = std::min(m_model->time(), duration);
    const QVector<QPointF> full = model->trajectoryPoints();
    if (full.size() < 2)
        return;

    // Предсказанная траектория — тонкий пунктир на весь полёт.
    painter->setPen(QPen(style::inkSoft(), 1.2, Qt::DotLine));
    painter->setBrush(Qt::NoBrush);
    QPainterPath predicted;
    predicted.moveTo(scenePointOf(full.first().x(), full.first().y()));
    for (const QPointF& p : full)
        predicted.lineTo(scenePointOf(p.x(), p.y()));
    painter->drawPath(predicted);

    // Пройденный участок — сплошная линия: трасса прочерчивается постепенно.
    QVector<QPointF> traveled;
    const int steps = 160;
    for (int i = 0; i <= steps; ++i) {
        const double t = current * i / steps;
        traveled.append(QPointF(model->xAt(t), std::max(0.0, model->yAt(t))));
    }
    painter->setPen(QPen(style::bodyC(), kTrajectoryWidth));
    QPainterPath drawn;
    drawn.moveTo(scenePointOf(traveled.first().x(), traveled.first().y()));
    for (const QPointF& p : traveled)
        drawn.lineTo(scenePointOf(p.x(), p.y()));
    painter->drawPath(drawn);
}

void ProjectileScene::drawAngleArc(QPainter* painter) const {
    auto* model = qobject_cast<models::ProjectileModel*>(m_model);
    if (!model || !model->validate().ok)
        return;

    const QPointF muzzle = muzzleScenePos();
    const double angle = model->angleDegrees();
    const double radius = std::max(34.0, std::min(m_worldRect.width(), m_worldRect.height()) * 0.13);

    // Направление выстрела: луч от дульного среза.
    const double radians = angle * M_PI / 180.0;
    const QPointF tip = muzzle + QPointF(std::cos(radians), -std::sin(radians)) * (radius * 1.35);
    painter->setPen(QPen(style::accentWarm(), 2.2, Qt::DashLine));
    painter->drawLine(muzzle, tip);

    // Дуга угла между горизонтом и направлением выстрела.
    painter->setPen(QPen(style::accent(), 1.6));
    painter->setBrush(Qt::NoBrush);
    const QRectF arcBox(muzzle.x() - radius, muzzle.y() - radius, radius * 2.0, radius * 2.0);
    painter->drawArc(arcBox, 0, int(std::round(angle * 16.0)));
    // Горизонт от дульного среза — сторона угла.
    painter->setPen(QPen(style::inkSoft(), 1.2));
    painter->drawLine(muzzle, muzzle + QPointF(radius * 1.2, 0.0));

    drawTag(painter, muzzle + QPointF(std::cos(radians / 2.0), -std::sin(radians / 2.0)) * (radius * 1.55),
            QStringLiteral("θ"), QColor(255, 255, 255, 225),
            style::accent().darker(120));
}

void ProjectileScene::drawProjections(QPainter* painter, const QPointF& shell) const {
    auto* model = qobject_cast<models::ProjectileModel*>(m_model);
    if (!model || !model->validate().ok)
        return;

    const double x = model->xAt(m_model->time());
    const double y = std::max(0.0, model->yAt(m_model->time()));
    const QPointF onXAxis = scenePointOf(x, 0.0);
    const QPointF onYAxis = scenePointOf(0.0, y);

    // Проекция на ось X: вертикальный пунктир от снаряда до горизонта
    // и бегущая точка на оси.
    painter->setPen(QPen(style::bodyA(), 1.4, Qt::DashLine));
    painter->drawLine(shell, onXAxis);
    painter->setPen(QPen(style::bodyA(), 1.4));
    painter->setBrush(style::paper());
    painter->drawEllipse(onXAxis, 5.0, 5.0);
    drawTag(painter, QPointF(onXAxis.x(), m_groundY + 12.0),
            QStringLiteral("x"), QColor(255, 255, 255, 225),
            style::bodyA().darker(160));

    // Проекция на ось Y: горизонтальный пунктир до оси ординат.
    painter->setPen(QPen(style::bodyC(), 1.4, Qt::DashLine));
    painter->drawLine(shell, onYAxis);
    painter->setPen(QPen(style::bodyC(), 1.4));
    painter->setBrush(style::paper());
    painter->drawEllipse(onYAxis, 5.0, 5.0);
    drawTag(painter, QPointF(onYAxis.x() - 4.0, onYAxis.y()),
            QStringLiteral("y"), QColor(255, 255, 255, 225),
            style::bodyC().darker(150));
}

// ------------------------------------------------------------------
// Схема целиком
// ------------------------------------------------------------------

void ProjectileScene::paintScheme(QPainter* painter) {
    auto* model = qobject_cast<models::ProjectileModel*>(m_model);
    if (!model)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // ---- земля ----
    const QRectF ground(m_schemeRect.left(), m_groundY, m_schemeRect.width(),
                        std::max(10.0, m_schemeRect.bottom() - m_groundY));
    painter->setPen(QPen(style::ink(), 1.8));
    painter->setBrush(QColor(226, 224, 216, 190));
    painter->drawRect(ground);
    primitives::drawHatchedSurface(painter, QRectF(ground.left(), ground.bottom(), ground.width(), 14.0),
                                   style::inkSoft());

    // ---- координатные оси ----
    const QPointF origin = scenePointOf(0.0, 0.0);
    painter->setPen(QPen(style::ink(), 1.6));
    painter->drawLine(QPointF(m_worldRect.left(), m_groundY), QPointF(m_worldRect.right(), m_groundY));
    painter->drawLine(origin, QPointF(origin.x(), m_worldRect.top()));
    // Стрелки осей.
    painter->setPen(QPen(style::ink(), 1.6));
    vectors::Arrow xAxisArrow;
    xAxisArrow.from = QPointF(m_worldRect.right() - 22.0, m_groundY);
    xAxisArrow.to = QPointF(m_worldRect.right(), m_groundY);
    xAxisArrow.symbol = QStringLiteral("x, м");
    xAxisArrow.kind = vectors::Kind::Flow;
    xAxisArrow.color = style::ink();
    xAxisArrow.width = 1.6;
    xAxisArrow.labelOffset = QPointF(-6.0, 18.0);
    vectors::drawArrow(painter, xAxisArrow);

    vectors::Arrow yAxisArrow;
    yAxisArrow.from = QPointF(origin.x(), m_worldRect.top() + 22.0);
    yAxisArrow.to = QPointF(origin.x(), m_worldRect.top());
    yAxisArrow.symbol = QStringLiteral("y, м");
    yAxisArrow.kind = vectors::Kind::Flow;
    yAxisArrow.color = style::ink();
    yAxisArrow.width = 1.6;
    yAxisArrow.labelOffset = QPointF(26.0, 4.0);
    vectors::drawArrow(painter, yAxisArrow);

    // Деления по осям: шаг выбирается по диапазону, чтобы подписи не сливались.
    const double xSpan = m_worldXMax - m_worldXMin;
    const double ySpan = m_worldYMax - m_worldYMin;
    const double xStep = niceStep(xSpan / 8.0);
    const double yStep = niceStep(ySpan / 5.0);
    painter->setFont(style::uiFont(7.5));
    for (double value = 0.0; value <= m_worldXMax + 1e-9; value += xStep) {
        const QPointF tick = scenePointOf(value, 0.0);
        painter->setPen(QPen(value == 0.0 ? style::ink() : style::inkSoft(), value == 0.0 ? 1.6 : 1.0));
        painter->drawLine(tick, tick + QPointF(0.0, 7.0));
        if (value > 0.0) {
            painter->setPen(style::inkSoft());
            painter->drawText(QRectF(tick.x() - 30.0, tick.y() + 8.0, 60.0, 13), Qt::AlignHCenter,
                              utils::formatNumber(value, std::abs(xStep) < 1.0 ? 1 : 0));
        }
    }
    for (double value = yStep; value <= m_worldYMax + 1e-9; value += yStep) {
        const QPointF tick = scenePointOf(0.0, value);
        painter->setPen(QPen(style::inkSoft(), 1.0));
        painter->drawLine(tick, tick + QPointF(-7.0, 0.0));
        painter->setPen(style::inkSoft());
        painter->drawText(QRectF(m_schemeRect.left(), tick.y() - 7.0, m_worldRect.left() - m_schemeRect.left() - 10.0, 14),
                          Qt::AlignRight | Qt::AlignVCenter,
                          utils::formatNumber(value, std::abs(yStep) < 1.0 ? 1 : 0));
    }
    // Начало координат.
    painter->setPen(style::ink());
    painter->drawText(QRectF(origin.x() - 22.0, origin.y() + 6.0, 20.0, 14), Qt::AlignRight, QStringLiteral("0"));

    // ---- трасса ----
    drawTrajectory(painter);

    // ---- направление и точка выстрела ----
    drawAngleArc(painter);
    const QPointF muzzle = muzzleScenePos();
    // Ствол: короткий брусок под углом выстрела.
    {
        const double radians = model->angleDegrees() * M_PI / 180.0;
        painter->save();
        painter->translate(muzzle);
        painter->rotate(-model->angleDegrees());
        painter->setPen(QPen(style::ink().darker(120), 1.4));
        painter->setBrush(QColor(120, 116, 108));
        painter->drawRect(QRectF(-26.0, -5.0, 26.0, 10.0));
        painter->restore();
        Q_UNUSED(radians);
    }
    drawTag(painter, QPointF(origin.x() + 60.0, muzzle.y() - 22.0),
            QStringLiteral("y₀"),
            QColor(255, 255, 255, 225), style::ink());

    // ---- точка падения: ключевое событие задачи ----
    const double flight = model->flightTime();
    if (std::isfinite(flight) && model->validate().ok) {
        const QPointF landing = scenePointOf(model->xAt(flight), 0.0);
        painter->setPen(QPen(style::success(), 2.0));
        painter->drawLine(landing + QPointF(-8.0, -8.0), landing + QPointF(8.0, 8.0));
        painter->drawLine(landing + QPointF(-8.0, 8.0), landing + QPointF(8.0, -8.0));
        if (m_model->time() >= flight - 1e-9) {
            drawTag(painter, QPointF(landing.x(), m_groundY - 26.0),
                    QStringLiteral("падение: t, L"),
                    QColor(234, 247, 239, 235), style::success().darker(120));
        }
        // Дальность полёта — размерная линия вдоль горизонта.
        drawDimension(painter, QPointF(muzzle.x(), m_groundY - 8.0), landing,
                      QStringLiteral("L"), style::inkSoft(), 24.0);
    }

    // ---- наибольшая высота ----
    const double apex = model->apexTime();
    if (std::isfinite(apex) && apex > 0.0 && model->validate().ok) {
        const QPointF top = scenePointOf(model->xAt(apex), model->maxHeight());
        painter->setPen(QPen(style::accentWarm(), 1.2, Qt::DashLine));
        painter->drawLine(QPointF(top.x() - 26.0, top.y()), QPointF(top.x() + 26.0, top.y()));
        drawTag(painter, QPointF(top.x(), top.y() - 14.0),
                QStringLiteral("H"),
                QColor(255, 255, 255, 225), style::accentWarm().darker(140));
    }

    // ---- снаряд и его проекции на оси ----
    const QPointF shell = scenePointOf(model->xAt(m_model->time()), std::max(0.0, model->yAt(m_model->time())));
    drawProjections(painter, shell);

    painter->setPen(QPen(style::bodyA().darker(170), 1.6));
    painter->setBrush(style::bodyA());
    painter->drawEllipse(shell, 8.0, 8.0);

    // ---- векторы ----
    drawForces(painter, vectorsFor(m_model->time()));

    drawPlots(painter);
    painter->restore();
}

double ProjectileScene::niceStep(double rough) {
    if (rough <= 0.0 || !std::isfinite(rough))
        return 1.0;
    const double exponent = std::floor(std::log10(rough));
    const double base = std::pow(10.0, exponent);
    const double fraction = rough / base;
    if (fraction >= 5.0)
        return 10.0 * base;
    if (fraction >= 2.0)
        return 5.0 * base;
    if (fraction >= 1.0)
        return 2.0 * base;
    return base;
}

// ------------------------------------------------------------------
// Графики: x(t), y(t), vₓ(t), vᵧ(t) — все по данным модели
// ------------------------------------------------------------------

void ProjectileScene::drawPlots(QPainter* painter) const {
    auto* model = qobject_cast<models::ProjectileModel*>(m_model);
    if (!model || !model->validate().ok)
        return;

    // Графики занимают полосу между схемой и общей осью времени.
    const double plotsTop = m_schemeRect.bottom() + 14.0;
    const double plotsBottom = timeAxisRect().top() - 10.0;
    const double available = plotsBottom - plotsTop;
    if (available < 90.0)
        return;

    constexpr double kGap = 10.0;
    const double plotWidth = (m_schemeRect.width() - 3.0 * kGap) / 4.0;
    const double height = std::min(available, 240.0);
    const double top = plotsTop + std::max(0.0, available - height) * 0.5;

    struct SeriesSpec {
        QString title;
        QVector<QPointF> points;
        QColor color;
    };
    const QVector<SeriesSpec> specs = {
        {QStringLiteral("Координата x(t), м"), model->xSeries(), style::bodyA()},
        {QStringLiteral("Координата y(t), м"), model->ySeries(), style::bodyC()},
        {QStringLiteral("Скорость vₓ(t), м/с"), model->vxSeries(), style::accentWarm()},
        {QStringLiteral("Скорость vᵧ(t), м/с"), model->vySeries(), style::variantColor(3)},
    };

    const double duration = std::max(1e-6, m_model->duration());
    const double eventT = m_model->eventTime();
    for (int i = 0; i < specs.size(); ++i) {
        const QRectF rect(m_schemeRect.left() + i * (plotWidth + kGap), top, plotWidth, height);
        PlotSeries series;
        series.points = specs.at(i).points;
        series.color = specs.at(i).color;
        LabScene::drawPlot(painter, rect, specs.at(i).title, timeUnitLabel(), {series}, duration, m_model->time(),
                           eventT);
    }
}

}  // namespace lab::scenes
