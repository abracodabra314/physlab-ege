#include "scenes/AdiabaticScene.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

#include "models/AdiabaticModel.h"
#include "scenes/SchemeLabels.h"
#include "scenes/Style.h"
#include "scenes/VectorArrows.h"
#include "utils/MathText.h"

namespace lab::scenes {
namespace {

/** Переводы единиц для подписей: объём в литры, давление в килопаскали. */
constexpr double kCubicMetersPerLiter = 1e-3;
constexpr double kPascalsPerKilopascal = 1e3;
constexpr double kSquareMetersPerSquareCentimeter = 1e-4;
[[maybe_unused]] constexpr double kGramsPerKilogram = 1000.0;

/** Отступы полосы схемы и размеры её элементов. */
constexpr double kBandPadding = 20.0;
constexpr double kGaugeWidth = 22.0;
constexpr double kGaugeGap = 26.0;
constexpr double kRulerWidth = 44.0;

/**
 * Опорные длины стрелок: масштаб задаётся наибольшим значением за весь
 * процесс, а не текущим, поэтому стрелки остаются сравнимыми при любой
 * перемотке времени и любой постановке.
 */
constexpr double kForceArrowPixels = 104.0;
constexpr double kSpeedArrowPixels = 64.0;
constexpr double kArrowMinPixels = 10.0;

/** Габарит поршня и его штока в пикселях. */
constexpr double kPistonThickness = 16.0;
constexpr double kRodWidth = 9.0;

/** Ширина и высота штриха у молекулы: по нему видно, насколько она ускорилась. */
constexpr double kMoleculeRadius = 4.2;
constexpr double kMoleculeDashPixels = 26.0;
/** Опорная скорость молекул при данных условиях: при ней штрих полон. */
constexpr double kMoleculeReferenceSpeed = 900.0;

/** Раскраска газа: чем выше температура относительно начальной, тем теплее цвет. */
QColor gasTint(double fraction) {
    // fraction = 0 — начальная температура (прохладный синий), 1 — конечная.
    const double clamped = std::clamp(fraction, 0.0, 1.0);
    return QColor(std::lerp(96.0, 214.0, clamped), std::lerp(165.0, 122.0, clamped),
                  std::lerp(220.0, 34.0, clamped), 96);
}

}  // namespace

AdiabaticScene::AdiabaticScene(Simulation* model, QObject* parent) : LabScene(model, parent) {}

// ------------------------------------------------------------------
// Геометрия чертежа: цилиндр, поршень и шкалы
// ------------------------------------------------------------------

void AdiabaticScene::reconfigure(const QRectF& area) {
    auto* model = qobject_cast<models::AdiabaticModel*>(m_model);

    // Полоса схемы: сосуд, поршень, шкалы давлений и температур.
    m_band = QRectF(area.left() + 18.0, area.top() + 14.0, area.width() - 36.0,
                    std::max(220.0, area.height() * 0.47));

    const double bore = std::min(m_band.width() * 0.24, 230.0);
    // Высота столба газа — физическая величина h = V/S, поэтому вертикальный
    // масштаб линейный и подписан на рисунке. Ширина цилиндра масштаба не
    // несёт: она лишь меняется вместе с площадью поршня.
    double h1 = 0.02;
    double h2 = 0.02;
    double forceMax = 1.0;
    double speedMax = 0.01;
    if (model && model->validate().ok) {
        h1 = model->initialVolume() / model->pistonArea();
        h2 = model->finalVolume() / model->pistonArea();
        const double tau = model->processTime();
        forceMax = std::max({model->gasForceAt(0.0), model->gasForceAt(tau), model->outsideForce(),
                             std::abs(model->externalForceAt(0.0)), std::abs(model->externalForceAt(tau))});
        speedMax = std::abs(model->pistonSpeedAt(0.0));
    }
    m_startHeight = h1;
    m_endHeight = h2;
    m_maxHeight = std::max(1e-4, std::max(h1, h2));

    // Вертикальная область: снизу дно сосуда, сверху — шток и место под
    // подписи; шкалы высот и давлений стоят по бокам сосуда.
    const double usableHeight = m_band.height() - 3.0 * kBandPadding - kPistonThickness;
    m_pixelsPerMeter = usableHeight / m_maxHeight;
    // Ширина цилиндра следует за площадью поршня (линейный размер сечения
    // пропорционален √S), но ограничена, чтобы чертёж оставался читаемым.
    const double referenceSide = std::sqrt(50.0 * kSquareMetersPerSquareCentimeter);
    const double pistonArea = model ? model->pistonArea() : 50.0 * kSquareMetersPerSquareCentimeter;
    m_borePx = std::clamp(bore * std::sqrt(pistonArea) / referenceSide, 54.0, m_band.width() * 0.34);

    const double vesselLeft = vesselLeftX();
    m_bottomY = m_band.bottom() - kBandPadding;
    m_vessel = QRectF(vesselLeft, m_bottomY - m_maxHeight * m_pixelsPerMeter, m_borePx,
                      m_maxHeight * m_pixelsPerMeter);

    m_forceScale = forceMax > 0.0 ? kForceArrowPixels / forceMax : 1.0;
    m_speedScale = speedMax > 0.0 ? kSpeedArrowPixels / speedMax : kSpeedArrowPixels;

    setRanges(area, 0.0, std::max(1e-6, m_model ? m_model->duration() : 1.0), 0.0, 1.0);
}

double AdiabaticScene::vesselLeftX() const {
    // Единая точка отсчёта для чертёжной полосы слева: линейка высот и
    // обе шкалы давлений/температур.
    return m_band.left() + kRulerWidth + kBandPadding * 2.0 + kGaugeWidth * 2.0 + kGaugeGap;
}

double AdiabaticScene::heightForVolume(double volume) const {
    auto* model = qobject_cast<models::AdiabaticModel*>(m_model);
    const double area = model ? model->pistonArea() : 1.0;
    return area > 0.0 ? volume / area : 0.0;
}

double AdiabaticScene::volumeForHeight(double height) const {
    auto* model = qobject_cast<models::AdiabaticModel*>(m_model);
    const double area = model ? model->pistonArea() : 1.0;
    return height * area;
}

double AdiabaticScene::pixelsForHeight(double height) const { return height * m_pixelsPerMeter; }

double AdiabaticScene::heightForPixels(double liftPx) const {
    return m_pixelsPerMeter > 0.0 ? liftPx / m_pixelsPerMeter : 0.0;
}

AdiabaticScene::SchemeGeometry AdiabaticScene::schemeGeometry() const {
    SchemeGeometry geometry;
    geometry.vessel = m_vessel;
    geometry.pixelsPerMeter = m_pixelsPerMeter;
    geometry.gasHeightPx = pixelsForHeight(heightForVolume(
        m_model && m_model->validate().ok
            ? static_cast<models::AdiabaticModel*>(m_model)->volumeAt(m_model->time())
            : 0.0));
    geometry.strokeTopPx = pixelsForHeight(std::max(m_startHeight, m_endHeight));
    geometry.strokeBottomPx = pixelsForHeight(std::min(m_startHeight, m_endHeight));
    geometry.piston = QPointF(m_vessel.center().x(), m_bottomY - geometry.gasHeightPx - kPistonThickness);
    return geometry;
}

QRectF AdiabaticScene::schemeBand() const { return m_band; }

// ------------------------------------------------------------------
// Молекулы: скорость каждой равна √(3RT/M)
// ------------------------------------------------------------------

QVector<AdiabaticScene::MoleculeItem> AdiabaticScene::moleculesFor(double t) const {
    QVector<MoleculeItem> items;
    auto* model = qobject_cast<models::AdiabaticModel*>(m_model);
    if (!model || !model->validate().ok)
        return items;

    const SimState state = model->stateAt(t);
    const double speed = model->rmsSpeedAtTime(t);
    // Газ занимает столб от дна до нижней кромки поршня.
    const double gasHeight = pixelsForHeight(heightForVolume(model->volumeAt(t)));
    const QRectF gas(m_vessel.left(), m_bottomY - gasHeight, m_vessel.width(), gasHeight);

    // Решётка с детерминированным сдвигом: положения молекул не физические,
    // а вот их скорость и направление берутся из состояния модели.
    const int columns = 6;
    int row = 0;
    int column = 0;
    for (const EntityVisual& entity : state.entities) {
        if (!entity.id.startsWith(QLatin1String("molecule_")))
            continue;
        MoleculeItem item;
        item.index = entity.id.mid(9).toInt() - 1;
        const double fx = (column + 0.5) / columns;
        const double fy = (row + 0.5) / 4.0;
        item.position = QPointF(gas.left() + fx * gas.width(), gas.bottom() - fy * gas.height());
        const double length = std::max(1e-9, std::hypot(entity.value1, entity.value2));
        item.direction = QPointF(entity.value1 / length, entity.value2 / length);
        item.speedMeters = speed;
        item.insideGas = gas.contains(item.position);
        items.push_back(item);

        ++column;
        if (column >= columns) {
            column = 0;
            ++row;
        }
    }
    return items;
}

// ------------------------------------------------------------------
// Векторы: силы, действующие на поршень
// ------------------------------------------------------------------

QVector<vectors::Arrow> AdiabaticScene::vectorsFor(double t) const {
    QVector<vectors::Arrow> arrows;
    auto* model = qobject_cast<models::AdiabaticModel*>(m_model);
    if (!model || !model->validate().ok)
        return arrows;

    const double gasHeight = pixelsForHeight(heightForVolume(model->volumeAt(t)));
    const double pistonBottom = m_bottomY - gasHeight;
    const double pistonMid = pistonBottom - kPistonThickness * 0.5;
    const QPointF axis(m_vessel.center().x(), pistonMid);

    if (true) {  // силы в динамической модели показываем всегда, в отличие от v и a
        // Сила давления газа на поршень: F = pS, направлена вверх — газ
        // выталкивает поршень.
        const double gasForce = model->gasForceAt(t);
        vectors::Arrow pressure;
        pressure.from = axis;
        pressure.to = axis + QPointF(0.0, -vectors::visualLength(gasForce, m_forceScale, kArrowMinPixels));
        pressure.symbol = QStringLiteral("F = pS");
        pressure.kind = vectors::Kind::Pressure;
        pressure.labelOffset = QPointF(-58.0, -6.0);
        arrows.push_back(pressure);

        // Сила внешнего (атмосферного) давления p₀S — направлена вниз.
        const double outside = model->outsideForce();
        vectors::Arrow atmosphere;
        atmosphere.from = axis;
        atmosphere.to = axis + QPointF(0.0, vectors::visualLength(outside, m_forceScale, kArrowMinPixels));
        atmosphere.symbol = QStringLiteral("F₀ = p₀S");
        atmosphere.kind = vectors::Kind::Pressure;
        atmosphere.color = style::bodyD();
        atmosphere.labelOffset = QPointF(-70.0, 14.0);
        arrows.push_back(atmosphere);

        // Медленный процесс: поршень уравновешен, поэтому внешняя
        // сила равна разности (p − p₀)S и направлена в сторону,
        // противоположную перевесу газа.
        const double external = model->externalForceAt(t);
        if (std::abs(external) > 1e-9) {
            vectors::Arrow hand;
            hand.from = axis + QPointF(kRodWidth * 2.6, 0.0);
            hand.to = hand.from + QPointF(0.0, external > 0.0 ? vectors::visualLength(external, m_forceScale,
                                                                                     kArrowMinPixels)
                                                              : -vectors::visualLength(-external, m_forceScale,
                                                                                     kArrowMinPixels));
            hand.symbol = QStringLiteral("Fвнеш");
            hand.kind = vectors::Kind::Elastic;
            hand.labelOffset = QPointF(38.0, 0.0);
            arrows.push_back(hand);
        }
    }

    // Скорость поршня: процесс медленный, поршень движется равномерно,
    // ускорения в модели нет.
    if (m_model->showsVelocities()) {
        const double speed = model->pistonSpeedAt(t);
        if (std::abs(speed) > 1e-9) {
            vectors::Arrow velocity;
            const double length = vectors::visualLength(std::abs(speed), m_speedScale, kArrowMinPixels,
                                                       kSpeedArrowPixels);
            velocity.from = QPointF(m_vessel.right() + 16.0, pistonMid);
            velocity.to = velocity.from + QPointF(0.0, speed > 0.0 ? -length : length);
            velocity.symbol = QStringLiteral("v");
            velocity.kind = vectors::Kind::Velocity;
            velocity.labelOffset = QPointF(22.0, 0.0);
            arrows.push_back(velocity);
        }
    }

    return arrows;
}

void AdiabaticScene::drawVectors(QPainter* painter, const QVector<vectors::Arrow>& arrows) const {
    for (const vectors::Arrow& arrow : arrows)
        vectors::drawArrow(painter, arrow);
}

// ------------------------------------------------------------------
// Перетаскивание мышью
// ------------------------------------------------------------------

double AdiabaticScene::mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const {
    if (info.id == QLatin1String("area")) {
        // Площадь поршня: горизонтальное перемещение правой стенки цилиндра.
        // Обратный расчёт повторяет reconfigure(): сторона сечения из
        // ширины сосуда, затем S = a².
        const double left = vesselLeftX();
        const double referenceSide = std::sqrt(50.0 * kSquareMetersPerSquareCentimeter);
        const double bore = std::min(m_band.width() * 0.24, 230.0);
        const double pixels = std::clamp(scenePos.x() - left, 40.0, m_band.width() * 0.34);
        const double side = pixels * referenceSide / bore;
        return side * side / kSquareMetersPerSquareCentimeter;
    }
    if (info.id == QLatin1String("piston") || info.id == QLatin1String("target")) {
        // Высота столба газа -> объём: V = S·h.
        const double lift = std::clamp(m_bottomY - scenePos.y(), 4.0, m_maxHeight * m_pixelsPerMeter);
        return volumeForHeight(heightForPixels(lift)) / kCubicMetersPerLiter;
    }
    return LabScene::mapHandlePosition(info, scenePos);
}

double AdiabaticScene::handleScenePosition(const HandleInfo& info) const {
    auto* model = qobject_cast<models::AdiabaticModel*>(m_model);
    if (!model)
        return LabScene::handleScenePosition(info);

    if (info.id == QLatin1String("piston"))
        return m_bottomY - pixelsForHeight(model->initialVolume() / model->pistonArea());
    if (info.id == QLatin1String("target"))
        return m_bottomY - pixelsForHeight(model->finalVolume() / model->pistonArea());
    if (info.id == QLatin1String("area"))
        return m_vessel.right();
    return LabScene::handleScenePosition(info);
}

void AdiabaticScene::positionHandles() {
    for (DragHandleItem* handle : m_handles) {
        if (!handle)
            continue;
        const HandleInfo info = handle->info();
        if (info.id == QLatin1String("piston")) {
            handle->setAxis(DragHandleItem::Axis::Vertical);
            handle->setAnchor(QPointF(m_vessel.left() - kBandPadding, handleScenePosition(info)));
        } else if (info.id == QLatin1String("target")) {
            handle->setAxis(DragHandleItem::Axis::Vertical);
            handle->setAnchor(QPointF(m_vessel.right() + kBandPadding, handleScenePosition(info)));
        } else if (info.id == QLatin1String("area")) {
            handle->setAxis(DragHandleItem::Axis::Horizontal);
            handle->setAnchor(QPointF(handleScenePosition(info), m_vessel.bottom() + kBandPadding));
        } else {
            handle->setAnchor(QPointF(m_band.center().x(), m_band.center().y()));
        }
    }
}

// ------------------------------------------------------------------
// Отрисовка чертежа
// ------------------------------------------------------------------

void AdiabaticScene::drawVessel(QPainter* painter) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const double wall = 3.0;
    // Наружная оболочка: стенки и дно толще — сосуд теплоизолирован.
    painter->setPen(QPen(style::ink().darker(120), 1.4));
    painter->setBrush(QColor(226, 226, 218, 220));
    QPainterPath shell;
    shell.addRect(QRectF(m_vessel.left() - wall, m_vessel.top() - wall * 2.0, m_vessel.width() + 2.0 * wall,
                         m_vessel.height() + wall * 3.0));
    painter->drawPath(shell);

    // Полость сосуда.
    painter->setBrush(QColor(252, 251, 248));
    painter->setPen(QPen(style::ink(), 1.6));
    painter->drawRect(m_vessel);

    // Штриховка стенок — признак теплоизоляции: Q = 0.
    painter->setPen(QPen(style::inkSoft(), 0.8, Qt::DashLine));
    for (double y = m_vessel.top(); y < m_vessel.bottom(); y += 8.0) {
        painter->drawLine(QPointF(m_vessel.left() - wall + 0.6, y), QPointF(m_vessel.left() - 0.6, y));
        painter->drawLine(QPointF(m_vessel.right() + 0.6, y), QPointF(m_vessel.right() + wall - 0.6, y));
    }
    painter->restore();
}

void AdiabaticScene::drawGas(QPainter* painter) const {
    auto* model = qobject_cast<models::AdiabaticModel*>(m_model);
    if (!model || !model->validate().ok)
        return;

    const double t = m_model->time();
    const double gasHeight = pixelsForHeight(heightForVolume(model->volumeAt(t)));
    const QRectF gas(m_vessel.left(), m_bottomY - gasHeight, m_vessel.width(), gasHeight);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    // Цвет газа отражает температуру: от начальной (холодный синий) к конечной.
    const double span = std::abs(model->finalTemperature() - model->initialTemperature());
    const double fraction = span > 1e-9 ? (model->temperatureAt(t) - model->initialTemperature()) / span : 0.0;
    painter->setPen(Qt::NoPen);
    painter->setBrush(gasTint(model->finalTemperature() >= model->initialTemperature() ? fraction : 1.0 - fraction));
    painter->drawRect(gas);
    painter->restore();
}

void AdiabaticScene::drawMolecules(QPainter* painter, const QVector<MoleculeItem>& molecules) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    for (const MoleculeItem& molecule : molecules) {
        if (!molecule.insideGas)
            continue;
        // Штрих вдоль направления движения: его длина — скорость молекулы.
        const double factor =
            kMoleculeDashPixels * std::clamp(molecule.speedMeters / kMoleculeReferenceSpeed, 0.05, 1.0);
        const QPointF tail = molecule.position - molecule.direction * (factor * 0.5);
        const QPointF head = molecule.position + molecule.direction * (factor * 0.5);
        painter->setPen(QPen(style::bodyD(), 1.1));
        painter->drawLine(tail, head);
        painter->setPen(QPen(style::ink().darker(110), 1.0));
        painter->setBrush(QColor(255, 255, 255, 235));
        painter->drawEllipse(molecule.position, kMoleculeRadius, kMoleculeRadius);
    }
    painter->restore();
}

void AdiabaticScene::drawPiston(QPainter* painter) const {
    auto* model = qobject_cast<models::AdiabaticModel*>(m_model);
    if (!model || !model->validate().ok)
        return;

    const double gasHeight = pixelsForHeight(heightForVolume(model->volumeAt(m_model->time())));
    const double pistonBottom = m_bottomY - gasHeight;
    const QRectF piston(m_vessel.left() + 1.5, pistonBottom - kPistonThickness, m_vessel.width() - 3.0,
                        kPistonThickness);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(style::ink().darker(140), 1.5));
    painter->setBrush(style::bodyB());
    painter->drawRect(piston);
    // Шток вверх: по нему к поршню прикладывают внешнюю силу.
    painter->setBrush(style::inkSoft());
    painter->drawRect(QRectF(m_vessel.center().x() - kRodWidth * 0.5, m_band.top() + kBandPadding, kRodWidth,
                             piston.top() - (m_band.top() + kBandPadding)));
    painter->restore();
}

void AdiabaticScene::drawStrokeScale(QPainter* painter) const {
    auto* model = qobject_cast<models::AdiabaticModel*>(m_model);
    if (!model || !model->validate().ok)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Вертикальная линейка высот столба газа: она же шкала объёма V = S·h.
    const double rulerX = m_band.left() + kRulerWidth;
    painter->setPen(QPen(style::ink(), 1.4));
    painter->drawLine(QPointF(rulerX, m_bottomY), QPointF(rulerX, m_bottomY - m_maxHeight * m_pixelsPerMeter));
    painter->setFont(style::uiFont(7.5));
    for (int i = 0; i <= 4; ++i) {
        const double height = m_maxHeight * i / 4.0;
        const double y = m_bottomY - pixelsForHeight(height);
        Q_UNUSED(height);
        painter->setPen(QPen(style::inkSoft(), 1.0));
        painter->drawLine(QPointF(rulerX - 5.0, y), QPointF(rulerX + 5.0, y));  // штрих без числа: миллиметры — на панели
    }

    // Отметки V₁ и V₂ и путь поршня между ними.
    const double y1 = m_bottomY - pixelsForHeight(m_startHeight);
    const double y2 = m_bottomY - pixelsForHeight(m_endHeight);
    painter->setPen(QPen(style::bodyA(), 1.3, Qt::DashLine));
    painter->drawLine(QPointF(rulerX, y1), QPointF(m_vessel.right() + 4.0, y1));
    painter->setPen(QPen(style::bodyC(), 1.3, Qt::DashLine));
    painter->drawLine(QPointF(rulerX, y2), QPointF(m_vessel.right() + 4.0, y2));
    // Стрелка хода поршня вдоль его оси.
    painter->setPen(QPen(style::inkSoft(), 1.4));
    const double arrowX = m_vessel.center().x();
    painter->drawLine(QPointF(arrowX, y1 - kPistonThickness * 0.5), QPointF(arrowX, y2 - kPistonThickness * 0.5));
    const double head = y2 > y1 ? -6.0 : 6.0;
    painter->drawLine(QPointF(arrowX, y2 - kPistonThickness * 0.5),
                      QPointF(arrowX - 4.0, y2 - kPistonThickness * 0.5 + head));
    painter->drawLine(QPointF(arrowX, y2 - kPistonThickness * 0.5),
                      QPointF(arrowX + 4.0, y2 - kPistonThickness * 0.5 + head));
    painter->restore();
}

void AdiabaticScene::drawPressureGauge(QPainter* painter) const {
    auto* model = qobject_cast<models::AdiabaticModel*>(m_model);
    if (!model || !model->validate().ok)
        return;

    const double t = m_model->time();
    // Шкалы берутся из всего процесса: стрелки-указатели показывают, где на
    // них находится текущее состояние.
    const double pMax = std::max({model->initialPressure(), model->finalPressure(), model->outsidePressure()}) * 1.1;
    const double tMin = std::min(model->initialTemperature(), model->finalTemperature()) * 0.9;
    const double tMax = std::max(model->initialTemperature(), model->finalTemperature()) * 1.05;

    const double gaugeHeight = std::min(m_vessel.height(), m_band.height() - 3.0 * kBandPadding);
    const double top = m_bottomY - gaugeHeight;
    const double pressureX = m_vessel.right() + kGaugeGap;
    const double temperatureX = pressureX + kGaugeWidth + kGaugeGap;

    struct Gauge {
        double x;
        double low;
        double high;
        double current;
        QColor color;
        QVector<double> marks;
    };
    const QVector<Gauge> gauges = {
        {pressureX, 0.0, pMax, model->pressureAt(t), style::bodyB(),
         {model->initialPressure(), model->outsidePressure(), model->finalPressure()}},
        {temperatureX, tMin, tMax, model->temperatureAt(t), style::accentWarm(),
         {model->initialTemperature(), model->finalTemperature()}},
    };

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    for (const Gauge& gauge : gauges) {
        const QRectF bar(gauge.x, top, kGaugeWidth, gaugeHeight);
        painter->setPen(QPen(style::inkSoft(), 1.2));
        painter->setBrush(QColor(255, 255, 255, 215));
        painter->drawRoundedRect(bar, 4, 4);

        const double span = std::max(1e-9, gauge.high - gauge.low);
        const double filled = std::clamp((gauge.current - gauge.low) / span, 0.0, 1.0);
        painter->setPen(Qt::NoPen);
        painter->setBrush(gauge.color);
        painter->drawRect(QRectF(bar.left() + 2.0, bar.bottom() - 2.0 - (bar.height() - 4.0) * filled,
                                 bar.width() - 4.0, (bar.height() - 4.0) * filled));

        // Отметки начального и конечного состояний: видно, насколько изменилась
        // величина к концу процесса.
        for (const double mark : gauge.marks) {
            const double y = bar.bottom() - 2.0 - (bar.height() - 4.0) * std::clamp((mark - gauge.low) / span, 0.0, 1.0);
            painter->setPen(QPen(style::ink().darker(110), 1.2, Qt::DashLine));
            painter->drawLine(QPointF(bar.left() - 3.0, y), QPointF(bar.right() + 3.0, y));
        }
        // Указатель текущего значения.
        const double y = bar.bottom() - 2.0 - (bar.height() - 4.0) * filled;
        painter->setPen(QPen(gauge.color.darker(150), 1.6));
        painter->setBrush(style::paper());
        painter->drawEllipse(QPointF(bar.right() + 7.0, y), 5.0, 5.0);
    }

    // Шкала наружного давления p₀ на шкале давлений: относительно неё судят
    // о том, какую силу приходится прикладывать снаружи.
    const double p0y = top + gaugeHeight - 2.0 -
                       (gaugeHeight - 4.0) *
                           std::clamp((model->outsidePressure() - 0.0) / std::max(1e-9, pMax), 0.0, 1.0);
    painter->setPen(QPen(style::ink(), 1.3));
    painter->drawLine(QPointF(pressureX - 6.0, p0y), QPointF(pressureX + kGaugeWidth + 6.0, p0y));
    painter->restore();
}

// ------------------------------------------------------------------
// Подписи
// ------------------------------------------------------------------

QVector<AdiabaticScene::Annotation> AdiabaticScene::annotations() const {
    QVector<Annotation> list;
    auto* model = qobject_cast<models::AdiabaticModel*>(m_model);
    if (!model || !model->validate().ok)
        return list;

    const double t = m_model->time();
    labels::Planner planner(m_band);

    const double gasHeight = pixelsForHeight(heightForVolume(model->volumeAt(t)));
    const double pistonBottom = m_bottomY - gasHeight;

    // Состояние газа — главный результат: объём, давление, температура.
    planner.addFirstFree(
        QStringLiteral("state"),
        {QPointF(m_vessel.center().x(), m_bottomY - gasHeight * 0.5),
         QPointF(m_vessel.center().x(), m_bottomY - gasHeight * 0.25)},
        QStringLiteral("V, p, T"),
        style::ink());

    // Высота столба газа у линейки высот: она же переводится в объём V = S·h.
    planner.addFirstFree(
        QStringLiteral("height"),
        {QPointF(m_vessel.left() - kBandPadding * 2.2, pistonBottom - kPistonThickness * 0.5),
         QPointF(m_vessel.left() - kBandPadding * 2.2, m_vessel.center().y())},
        QStringLiteral("h"),
        style::bodyB().darker(150));

    // Начальное и конечное положения поршня.
    planner.addFirstFree(
        QStringLiteral("start"),
        {QPointF(m_vessel.left() + kBandPadding * 1.4, m_bottomY - pixelsForHeight(m_startHeight) - 14.0),
         QPointF(m_vessel.left() + kBandPadding * 1.4, m_bottomY - pixelsForHeight(m_startHeight) + 14.0)},
        QStringLiteral("V₁, T₁"),
        style::bodyA().darker(160));
    planner.addFirstFree(
        QStringLiteral("finish"),
        {QPointF(m_vessel.left() + kBandPadding * 1.4, m_bottomY - pixelsForHeight(m_endHeight) + 14.0),
         QPointF(m_vessel.left() + kBandPadding * 1.4, m_bottomY - pixelsForHeight(m_endHeight) - 14.0)},
        QStringLiteral("V₂, T₂"),
        style::bodyC().darker(160));

    // Шкалы давлений и температур.
    const double gaugeTop = m_bottomY - std::min(m_vessel.height(), m_band.height() - 3.0 * kBandPadding);
    const double pressureX = m_vessel.right() + kGaugeGap;
    const double temperatureX = pressureX + kGaugeWidth + kGaugeGap;
    planner.addFirstFree(
        QStringLiteral("pressure"),
        {QPointF(pressureX + kGaugeWidth * 0.5, gaugeTop - 12.0), QPointF(pressureX + kGaugeWidth * 0.5, gaugeTop - 26.0)},
        QStringLiteral("p, кПа"), style::bodyB().darker(160));
    planner.addFirstFree(
        QStringLiteral("temperature"),
        {QPointF(temperatureX + kGaugeWidth * 0.5, gaugeTop - 12.0),
         QPointF(temperatureX + kGaugeWidth * 0.5, gaugeTop - 26.0)},
        QStringLiteral("T, К"), style::accentWarm().darker(160));
    planner.addFirstFree(
        QStringLiteral("outside"),
        {QPointF(pressureX + kGaugeWidth + 22.0, m_band.top() + 14.0), QPointF(pressureX, m_band.top() + 14.0)},
        QStringLiteral("p₀"),
        style::ink());

    // Силы на поршень: их величины берутся из формул модели.
    planner.addFirstFree(
        QStringLiteral("forces"),
        {QPointF(m_vessel.center().x(), m_band.top() + 12.0), QPointF(m_vessel.center().x(), m_band.top() + 32.0)},
        QStringLiteral("F = pS, F₀ = p₀S, Fвнеш"),
        style::inkSoft(), QColor(255, 255, 255, 205));

    // Скорость молекул: та же формула, что и в панели вычислений.
    planner.addFirstFree(
        QStringLiteral("molecules"),
        {QPointF(m_band.left() + 150.0, m_band.bottom() - 12.0), QPointF(m_band.left() + 150.0, m_band.bottom() - 30.0)},
        QStringLiteral("v = √(3RT/M)"),
        style::bodyD().darker(160));

    // Работа и первое начало: знак работы объясняет, кто совершает процесс.
    planner.addFirstFree(
        QStringLiteral("work"),
        {QPointF(m_band.center().x(), m_band.bottom() - 12.0), QPointF(m_band.center().x(), m_band.bottom() - 30.0)},
        QStringLiteral("Q = 0, A = −ΔU"),
        style::inkSoft(), QColor(255, 255, 255, 205));

    // Ответ задачи: состояние после завершения процесса.
    if (t >= model->processTime() - 1e-9) {
        planner.addFirstFree(
            QStringLiteral("result"),
            {QPointF(m_band.right() - 190.0, m_band.bottom() - 12.0),
             QPointF(m_band.right() - 190.0, m_band.bottom() - 32.0)},
            QStringLiteral("конец процесса: p₂, T₂"),
            style::success().darker(130), QColor(234, 247, 239, 240));
    }

    // Как читать чертёж.
    planner.addFirstFree(
        QStringLiteral("legend"),
        {QPointF(m_band.right() - 150.0, m_band.top() + 12.0), QPointF(m_band.right() - 150.0, m_band.top() + 32.0)},
        QStringLiteral("высота столба газа — в масштабе, стенки теплоизолированы (Q = 0)"), style::inkSoft(),
        QColor(255, 255, 255, 205));

    return planner.items();
}

// ------------------------------------------------------------------
// Чертёж целиком
// ------------------------------------------------------------------

void AdiabaticScene::paintScheme(QPainter* painter) {
    auto* model = qobject_cast<models::AdiabaticModel*>(m_model);
    if (!model)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const double t = m_model->time();
    drawVessel(painter);
    drawGas(painter);
    drawMolecules(painter, moleculesFor(t));
    drawPiston(painter);
    drawStrokeScale(painter);
    drawPressureGauge(painter);
    drawVectors(painter, vectorsFor(t));

    // Все тексты — последним проходом по одному реестру.
    labels::drawAll(painter, annotations());

    drawPlots(painter);
    painter->restore();
}

// ------------------------------------------------------------------
// Панели: две не временные зависимости и два графика от времени
// ------------------------------------------------------------------

void AdiabaticScene::drawPlots(QPainter* painter) const {
    auto* model = qobject_cast<models::AdiabaticModel*>(m_model);
    if (!model || !model->validate().ok)
        return;

    const double plotsTop = m_band.bottom() + 14.0;
    const double plotsBottom = timeAxisRect().top() - 10.0;
    const double available = plotsBottom - plotsTop;
    if (available < 90.0)
        return;

    constexpr double kGap = 10.0;
    const double panelWidth = (m_band.width() - 3.0 * kGap) / 4.0;
    const double height = std::min(available, 216.0);
    const double top = plotsTop + std::max(0.0, available - height) * 0.5;
    const double duration = std::max(1e-6, m_model->duration());
    const double currentT = m_model->time();
    const double eventT = m_model->eventTime();

    // ---- 1. Адиабата p(V) рядом с изотермой той же начальной температуры ----
    {
        PlotSeries adiabat;
        adiabat.points = model->pressureVersusVolume();
        adiabat.color = style::bodyB();
        adiabat.label = QStringLiteral("адиабата");
        PlotSeries isotherm;
        isotherm.points = model->isothermVersusVolume();
        isotherm.color = style::bodyA();
        isotherm.dashed = true;
        isotherm.label = QStringLiteral("изотерма T₁");
        const QPointF current(model->volumeAt(currentT) / kCubicMetersPerLiter,
                              model->pressureAt(currentT) / kPascalsPerKilopascal);
        drawGraph(painter, QRectF(m_band.left(), top, panelWidth, height),
                  QStringLiteral("Давление p(V), кПа"), QStringLiteral("V, л"), QStringLiteral("p, кПа"),
                  {adiabat, isotherm}, current, true);
    }

    // ---- 2. Температура T(V) ----
    {
        PlotSeries series;
        series.points = model->temperatureVersusVolume();
        series.color = style::accentWarm();
        series.label = QStringLiteral("T(V)");
        const QPointF current(model->volumeAt(currentT) / kCubicMetersPerLiter, model->temperatureAt(currentT));
        drawGraph(painter, QRectF(m_band.left() + panelWidth + kGap, top, panelWidth, height),
                  QStringLiteral("Температура T(V), К"), QStringLiteral("V, л"), QStringLiteral("T, К"), {series},
                  current, false);
    }

    // ---- 3. Работа газа и изменение внутренней энергии ----
    {
        PlotSeries work;
        work.points = model->workSeries();
        work.color = style::bodyC();
        work.label = QStringLiteral("A(t)");
        PlotSeries energy;
        energy.points = model->energyChangeSeries();
        energy.color = style::bodyD();
        energy.dashed = true;
        energy.label = QStringLiteral("ΔU(t)");
        drawPlot(painter, QRectF(m_band.left() + 2.0 * (panelWidth + kGap), top, panelWidth, height),
                 QStringLiteral("Работа A(t) и ΔU(t), Дж"), timeUnitLabel(), {work, energy}, duration, currentT,
                 eventT);
    }

    // ---- 4. Адиабата в координатах (T, p): показательная зависимость ----
    {
        PlotSeries series;
        series.points = model->pressureVersusTemperature();
        series.color = style::bodyB();
        series.label = QStringLiteral("p(T)");
        const QPointF current(model->temperatureAt(currentT), model->pressureAt(currentT) / kPascalsPerKilopascal);
        drawGraph(painter, QRectF(m_band.left() + 3.0 * (panelWidth + kGap), top, panelWidth, height),
                  QStringLiteral("Давление p(T), кПа"), QStringLiteral("T, К"), QStringLiteral("p, кПа"), {series},
                  current, false);
    }
}

}  // namespace lab::scenes
