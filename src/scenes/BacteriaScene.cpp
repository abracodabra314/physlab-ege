#include "scenes/BacteriaScene.h"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <limits>

#include "models/BacteriaModel.h"
#include "scenes/SchemeLabels.h"
#include "scenes/Style.h"
#include "utils/MathText.h"

namespace lab::scenes {
namespace {

/** Отступы полосы схемы и размеры её строк, пиксели. */
constexpr double kBandPadding = 18.0;
constexpr double kTitleRow = 22.0;
constexpr double kAxisRow = 26.0;
constexpr double kRulerRow = 22.0;
constexpr double kGrowthHeight = 40.0;
constexpr double kCaptionRow = 28.0;
constexpr double kRowGap = 6.0;

/** Ширина колонки подписей вертикальной оси и отступ между блоками. */
constexpr double kCellsAxisWidth = 62.0;
constexpr double kBlockGap = 16.0;

/**
 * Плотность укладки клеток рисунка: сколько квадратных пикселей занимает
 * одна точка с припуском. По этому бюджету подбирают число мест в чашке,
 * а уже по нему — размер точки, поэтому клетки никогда не слипаются.
 */
constexpr double kCellFootprint = 44.0;
/** Границы числа мест: меньше — рост не виден по клеткам, больше — каша. */
constexpr int kMaxDrawnSlots = 900;
constexpr int kMinDrawnSlots = 160;
/** Слоёв колонии, которые показывают при переполнении кадра. */
constexpr int kMaxLayers = 3;

/** Угол золотого сечения: по нему клетки укладываются без щелей и рядов. */
constexpr double kGoldenAngle = 2.399963229728653;

/**
 * Число настоящих клеток, которое показывает одна клетка рисунка. Его берут
 * из «удобного» ряда делителей вместимости кадра, чтобы подпись
 * «1 клетка = 25 клеток» была точной, а число мест в чашке получилось целым.
 */
constexpr int kPerCellSteps[] = {1,    2,    4,    5,    8,    10,   20,   25,   40,   50,   80,   100,
                                 125,  200,  250,  400,  500,  625,  800,  1000, 1250, 2000, 2500, 4000};

/**
 * Цвета рисунка. Колония — одного зелёного цвета: это цвет живых клеток
 * в препаратах. Ни слоёв, ни «свежих» делений цветом не выделяем — деление
 * видно по полосе учёта под чашкой и по ступеньке графика.
 */
QColor cellFill() { return style::bodyC(); }
QColor cellEdge() { return style::bodyC().darker(170); }

/** Теория и ступенька: их различают по цвету и стилю. */
QColor theoryColor() { return style::bodyB(); }
QColor steppedColor() { return style::bodyA(); }
QColor capacityColor() { return style::inkSoft(); }

/** Наименьший «удобный» делитель вместимости, при котором мест хватает. */
int cellsPerDrawnCellFor(double capacity, double budget) {
    const double wanted = capacity / std::max(1.0, budget);
    for (int per : kPerCellSteps) {
        if (double(per) >= wanted)
            return per;
    }
    return kPerCellSteps[sizeof(kPerCellSteps) / sizeof(kPerCellSteps[0]) - 1];
}

/** Засечки шкалы: целые периоды удвоения, помещающиеся в опыт. */
int doublingsOnScale(const models::BacteriaModel* model, double window) {
    const double life = model->doublingTime();
    if (life <= 0.0 || !std::isfinite(life))
        return 0;
    return std::clamp(int(std::floor(window / life)), 0, 12);
}

}  // namespace

BacteriaScene::BacteriaScene(Simulation* model, QObject* parent) : LabScene(model, parent) {}

// ------------------------------------------------------------------
// Геометрия полосы схемы
// ------------------------------------------------------------------

void BacteriaScene::reconfigure(const QRectF& area) {
    auto* model = qobject_cast<models::BacteriaModel*>(m_model);

    // Полоса схемы: слева чашка Петри, справа — ход опыта во времени, под
    // ним лента прироста и размерная линия T_d; внизу — строка подписей.
    m_band = QRectF(area.left() + 18.0, area.top() + 12.0, area.width() - 36.0,
                    std::max(250.0, std::min(336.0, area.height() * 0.47)));

    const double contentTop = m_band.top() + kBandPadding + kTitleRow;
    const double contentBottom = m_band.bottom() - kBandPadding;
    // Столбец строк под графиком опыта: деления времени, размерная линия,
    // лента прироста и строка подписей.
    const double stack = kAxisRow + kRowGap + kRulerRow + kRowGap + kGrowthHeight + kRowGap + kCaptionRow;
    const double plotHeight = std::max(70.0, contentBottom - contentTop - stack);

    // Чашка — круг вписанный квадрат: её диаметр ограничен и высотой чертежа,
    // и шириной левой колонки, чтобы шкала опыта осталась читаемой.
    const double dishSize = std::min({contentBottom - contentTop - kCaptionRow, m_band.width() * 0.30, 264.0});
    m_dish = QRectF(m_band.left() + kBandPadding, contentTop + (contentBottom - contentTop - kCaptionRow - dishSize) * 0.5,
                    dishSize, dishSize);

    const double stripLeft = m_dish.right() + kCellsAxisWidth + kBlockGap;
    m_strip = QRectF(stripLeft, contentTop, std::max(120.0, m_band.right() - kBandPadding - stripLeft), plotHeight);
    m_growthBand = QRectF(stripLeft, m_strip.bottom() + kAxisRow + kRowGap + kRulerRow + kRowGap, m_strip.width(),
                          kGrowthHeight);
    m_rulerY = m_strip.bottom() + kAxisRow + kRowGap + kRulerRow * 0.5;

    m_startPx = m_strip.left();
    m_endPx = m_strip.right();
    m_stripTopY = m_strip.top();
    m_stripBottomY = m_strip.bottom();

    const double total = model && model->validate().ok ? model->duration() : 12.8;
    m_pixelsPerHour = (m_endPx - m_startPx) / std::max(1e-6, total);

    // Вертикальная шкала числа клеток: верх — максимум того, что ростёт, или
    // отметки кадра, либо целевого числа (если оно близко к масштабу).
    double top = 20000.0;
    if (model && model->validate().ok) {
        const double grown = std::max(model->theoryCellsAt(model->observationTime()), model->dishCapacity());
        const double wanted = model->targetCells();
        top = wanted <= 4.0 * grown ? std::max(grown, wanted) : grown;
        m_capacityCells = model->dishCapacity();
    } else {
        m_capacityCells = 20000.0;
    }
    m_cellsTop = std::max(1.0, top) * 1.06;
    m_pixelsPerCell = (m_stripBottomY - m_stripTopY) / m_cellsTop;

    /*
     * Число мест в чашке и размер клетки рисунка. Мест должно быть ровно
     * N_мест / n, где n — число настоящих клеток в одной клетке рисунка:
     * тогда кадр заполняется ровно при N = N_мест, и радиус колонии
     * R = R_кадра·√(N/N_мест) совпадает с числом занятых мест.
     */
    const double dishRadius = dishSize * 0.5;
    const double effectiveRadius = std::max(8.0, dishRadius - 6.0);
    const double budget = M_PI * effectiveRadius * effectiveRadius / kCellFootprint;
    // Пол роста — два слоя: первым слоем чашка заполняется к половине N_мест,
    // вторым — к N_мест, где деления и прекращаются.
    int per = cellsPerDrawnCellFor(m_capacityCells * 0.5, budget);
    int places = int(std::llround(m_capacityCells / double(per)));
    places = std::max(places, 2 * int(std::llround(m_capacityCells * 0.5 / double(per))));
    places = std::clamp(places, kMinDrawnSlots, kMaxDrawnSlots);
    // Размер точки — по шагу укладки: при таком числе мест клетки ровно
    // покрывают круг и не налезают друг на друга.
    const double spacing = 2.0 * effectiveRadius / std::sqrt(double(places));
    m_cellLength = std::clamp(0.95 * spacing, 3.0, 14.0);
    m_cellWidth = std::clamp(0.34 * spacing, 1.3, 4.4);
    m_cellsPerCell = double(per);

    setRanges(area, 0.0, std::max(1e-6, total), 0.0, 1.0);
}

QRectF BacteriaScene::schemeBand() const { return m_band; }

BacteriaScene::SchemeGeometry BacteriaScene::schemeGeometry() const {
    auto* model = qobject_cast<models::BacteriaModel*>(m_model);
    SchemeGeometry geometry;
    geometry.dish = m_dish;
    geometry.dishCenter = m_dish.center();
    geometry.dishRadius = m_dish.width() * 0.5;
    geometry.strip = m_strip;
    geometry.growthBand = m_growthBand;
    geometry.rulerY = m_rulerY;
    geometry.startPx = m_startPx;
    geometry.endPx = m_endPx;
    geometry.pixelsPerHour = m_pixelsPerHour;
    geometry.cellsPerCell = m_cellsPerCell;
    geometry.capacityCells = m_capacityCells;
    geometry.cellsTopPy = sceneYForCells(m_cellsTop);
    geometry.colonyRadius = 0.0;   // при недопустимой постановке колонии нет
    geometry.occupiedCells = 0;
    if (model && model->validate().ok) {
        geometry.doublingPx = sceneXForTime(model->doublingTime());
        geometry.observationEndPx = sceneXForTime(model->observationTime());
        geometry.targetPx = sceneXForTime(model->targetTime());
        geometry.targetLevelPy = sceneYForCells(std::min(model->targetCells(), m_cellsTop));
        geometry.currentPx = sceneXForTime(model->time());
        geometry.layers = model->overflowFactorAt(model->time());
        geometry.occupiedCells = occupiedSlots(model->theoryCellsAt(model->time()));
        geometry.colonyRadius = colonyRadiusPx(model->overflowFactorAt(model->time()));
    }
    return geometry;
}

double BacteriaScene::sceneXForTime(double t) const { return m_startPx + t * m_pixelsPerHour; }

double BacteriaScene::timeForSceneX(double sceneX) const {
    return (sceneX - m_startPx) / std::max(1e-9, m_pixelsPerHour);
}

double BacteriaScene::sceneYForCells(double cells) const { return m_stripBottomY - cells * m_pixelsPerCell; }

double BacteriaScene::cellsForSceneY(double sceneY) const {
    return (m_stripBottomY - sceneY) / std::max(1e-9, m_pixelsPerCell);
}

double BacteriaScene::cellsPerDrawnCell() const { return m_cellsPerCell; }

double BacteriaScene::colonyRadiusPx(double overflow) const {
    const double effective = std::max(4.0, m_dish.width() * 0.5 - 6.0);
    if (!std::isfinite(overflow) || overflow <= 0.0)
        return 0.0;
    // Площадь слоя клеток пропорциональна их числу, поэтому радиус колонии
    // растёт как корень из неё — и доходит до края кадра ровно при N = N_мест.
    return effective * std::min(1.0, std::sqrt(overflow));
}

int BacteriaScene::drawnSlots() const {
    // Мест первого слоя — ровно на половину вместимости: второй слой
    // добирает чашку до N_мест, где рост встаёт.
    const int places = int(std::llround(m_capacityCells * 0.5 / std::max(1.0, m_cellsPerCell)));
    return std::clamp(places, 1, kMaxDrawnSlots * 4);
}

int BacteriaScene::occupiedSlots(double cells) const {
    if (!std::isfinite(cells) || cells <= 0.0)
        return 0;
    return std::clamp(int(std::ceil(cells / std::max(1.0, m_cellsPerCell))), 0, drawnSlots() * kMaxLayers);
}

double BacteriaScene::cellsAxisTop() const { return m_cellsTop; }

QPointF BacteriaScene::slotPosition(int slot, int layer, double fill) const {
    /*
     * Укладка по спирали от центра: i-е место лежит на расстоянии
     * R_колонии·√(i/n), поэтому занятые места всегда образуют круг той же
     * площади, что и колония, и новых клеток добавляется ровно столько,
     * насколько вырос радиус. Слои колонии смещают место на полшага, иначе
     * клетки второго слоя слились бы с клетками первого.
     */
    const int places = std::max(1, drawnSlots());
    const double radius = colonyRadiusPx(fill);
    const double distance = radius * std::sqrt((double(slot) + 0.5) / double(places));
    const double angle = (double(slot) + 0.5 * double(layer)) * kGoldenAngle;
    QPointF offset = QPointF(0.0, 0.0);
    if (layer > 0)
        offset = QPointF(std::cos(angle * 1.7), std::sin(angle * 1.7)) * (m_cellWidth * 0.6 * double(layer));
    return m_dish.center() + QPointF(std::cos(angle), std::sin(angle)) * distance + offset;
}

// ------------------------------------------------------------------
// Чашка: клетки добавляются по одной
// ------------------------------------------------------------------

QVector<BacteriaScene::ColonyCell> BacteriaScene::cellsFor(double t) const {
    QVector<ColonyCell> cells;
    auto* model = qobject_cast<models::BacteriaModel*>(m_model);
    if (!model || !model->validate().ok)
        return cells;

    const double counted = model->countedCellsAt(t);
    if (!std::isfinite(counted) || counted <= 0.0)
        return cells;

    const int places = std::max(1, drawnSlots());
    const double per = std::max(1.0, m_cellsPerCell);
    const double fill = model->overflowFactorAt(t);
    const long double needed = std::ceill(static_cast<long double>(counted) / static_cast<long double>(per));
    const long double capacity = static_cast<long double>(places);

    /*
     * Клетки, разделившиеся на последнем завершённом интервале учёта: их
     * берут как разницу между числом клеток на его конце и на начале. По ним
     * видно, что прирост — это деления, а не непрерывное «размазывание» массы,
     * и они совпадают со столбцом ΔN под схемой.
     */
    double left = 0.0;
    double right = 0.0;
    const bool completed = model->countingIntervalBefore(t, left, right);
    const double before = completed ? model->countedCellsAt(left) : std::numeric_limits<double>::quiet_NaN();
    const long double previous =
        std::isfinite(before) ? std::ceill(static_cast<long double>(before) / static_cast<long double>(per))
                              : static_cast<long double>(0.0);
    const long double layers = std::ceill(needed / capacity);
    const long double total = std::min(layers * capacity, needed);
    /*
     * До первого подсчёта делений не было: посев весь на месте, и помечать
     * его «разделившимся» не за что.
     */
    long double first = total;
    long double marked = total;
    if (completed) {
        const double finished = model->countedCellsAt(right);
        const long double upto =
            std::isfinite(finished)
                ? std::ceill(static_cast<long double>(finished) / static_cast<long double>(per))
                : total;
        first = std::min(total, previous);
        // Правый край метки — конец завершённого подсчёта, а не текущий момент.
        marked = std::min(total, std::max(first, upto));
    }

    cells.reserve(int(std::min<long double>(total, capacity * kMaxLayers)));
    for (int layer = 0; layer < kMaxLayers && static_cast<long double>(layer) < layers; ++layer) {
        const long double from = static_cast<long double>(layer) * capacity;
        const int take = int(std::min<long double>(capacity, needed - from));
        for (int slot = 0; slot < take; ++slot) {
            const long double order = from + static_cast<long double>(slot);
            ColonyCell cell;
            cell.index = layer * places + slot;
            cell.center = slotPosition(slot, layer, fill);
            cell.layer = layer;
            cell.angle = (double(slot) + 0.5 * double(layer)) * kGoldenAngle;
            // Клетки верхних слоёв рисуют чуть меньше: они под первыми.
            const double shrink = 1.0 - 0.16 * double(layer);
            cell.length = m_cellLength * shrink;
            cell.width = m_cellWidth * shrink;
            cell.dividing = order >= first && order < marked;
            cell.cellsPerCell = per;
            cells.push_back(cell);
        }
    }
    return cells;
}

// ------------------------------------------------------------------
// Векторов в модели деления нет
// ------------------------------------------------------------------

QVector<vectors::Arrow> BacteriaScene::vectorsFor(double /*t*/) const {
    /*
     * Колонию не толкают, она не летит и не деформируется: у неё меняется
     * только число клеток. Поэтому ни одна категория векторов (скорости,
     * ускорения, силы) здесь не появляется: стрелку пришлось бы придумать.
     */
    return {};
}

// ------------------------------------------------------------------
// Отрисовка
// ------------------------------------------------------------------

void BacteriaScene::paintScheme(QPainter* painter) {
    auto* model = qobject_cast<models::BacteriaModel*>(m_model);
    if (!model)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QVector<ColonyCell> cells = cellsFor(m_model->time());
    drawDish(painter, cells);
    drawColonyBoundary(painter);
    drawStrip(painter);
    drawGrowthBand(painter);

    // Все тексты — последним проходом по одному реестру.
    labels::drawAll(painter, annotations());
    drawPlots(painter);
    painter->restore();
}

void BacteriaScene::drawDish(QPainter* painter, const QVector<ColonyCell>& cells) const {
    auto* model = qobject_cast<models::BacteriaModel*>(m_model);
    if (!model)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Чашка Петри: стеклянный ободок и мутная среда.
    painter->setPen(QPen(style::ink(), 1.6));
    painter->setBrush(QColor(250, 251, 248, 235));
    painter->drawEllipse(m_dish);
    painter->setPen(QPen(style::grid(), 1.0));
    const double radius = m_dish.width() * 0.5;
    painter->drawEllipse(m_dish.center(), radius - 4.0, radius - 4.0);

    if (!model->validate().ok) {
        painter->setPen(style::inkSoft());
        painter->drawText(m_dish, Qt::AlignCenter, QStringLiteral("постановка недопустима"));
        painter->restore();
        return;
    }

    // Клетки: точки одного цвета, уложенные по спирали (золотой угол).
    // Деление видно по полосе учёта под чашкой, а не по цвету точки.
    for (const ColonyCell& cell : cells) {
        // Диаметр точки — по шагу укладки, чтобы точки ровно покрывали круг.
        const double dotRadius = std::max(0.9, 0.38 * cell.length);
        painter->setPen(QPen(cellEdge(), 0.5));
        painter->setBrush(cellFill());
        painter->drawEllipse(cell.center, dotRadius, dotRadius);
    }
    painter->restore();
}

void BacteriaScene::drawColonyBoundary(QPainter* painter) const {
    auto* model = qobject_cast<models::BacteriaModel*>(m_model);
    if (!model || !model->validate().ok)
        return;

    const double t = m_model->time();
    const double radius = colonyRadiusPx(model->overflowFactorAt(t));
    if (radius <= 1.0)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    // Граница колонии: R = R_кадра·√(N/N_мест), площадь слоя пропорциональна
    // числу клеток.
    painter->setPen(QPen(cellEdge(), 1.2, Qt::DashLine));
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(m_dish.center(), radius, radius);
    // Размерная линия радиуса от центра к краю колонии.
    painter->setPen(QPen(style::bodyC().darker(150), 1.2));
    painter->drawLine(m_dish.center(), QPointF(m_dish.center().x() + radius, m_dish.center().y()));
    painter->setBrush(style::ink());
    painter->drawEllipse(m_dish.center(), 1.8, 1.8);
    painter->restore();
}


void BacteriaScene::drawStrip(QPainter* painter) const {
    auto* model = qobject_cast<models::BacteriaModel*>(m_model);
    if (!model)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRectF plot = m_strip;
    painter->setPen(QPen(style::inkSoft(), 1.0));
    painter->setBrush(QColor(255, 255, 255, 235));
    painter->drawRoundedRect(plot.adjusted(-8.0, -6.0, 8.0, 6.0), 8.0, 8.0);
    if (!model->validate().ok) {
        painter->setPen(style::inkSoft());
        painter->drawText(plot, Qt::AlignCenter, QStringLiteral("постановка недопустима"));
        painter->restore();
        return;
    }

    const double total = model->duration();
    const double currentT = m_model->time();
    const double n0 = model->initialCells();

    // Сетка по удвоениям: горизонтальные линии на уровнях N₀·2^n стоят там же,
    // где вертикальные — на моментах n·T_d, потому что рост идёт делением.
    const int levels = doublingsOnScale(model, total);
    painter->setFont(style::uiFont(8.0));
    for (int n = 0; n <= levels; ++n) {
        const double level = n0 * std::exp2(double(n));
        const double y = sceneYForCells(level);
        if (y < plot.top() - 1.0)
            break;
        painter->setPen(QPen(style::grid(), n == 0 ? 1.0 : 0.7, n == 0 ? Qt::SolidLine : Qt::DashLine));
        painter->drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        painter->setPen(style::inkSoft());
        painter->drawText(QRectF(plot.left() - kCellsAxisWidth, y - 8.0, kCellsAxisWidth - 6.0, 16.0),
                          Qt::AlignRight | Qt::AlignVCenter, models::BacteriaModel::cellsText(level));

        const double t = model->doublingTime() * double(n);
        const double x = sceneXForTime(t);
        if (x <= plot.right() + 1.0) {
            painter->setPen(
                QPen(style::grid(), n == 0 ? 1.0 : 0.7, n == 0 ? Qt::SolidLine : Qt::DashLine));
            painter->drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            painter->setPen(QPen(style::ink(), 1.0));
            painter->drawLine(QPointF(x, plot.bottom()), QPointF(x, plot.bottom() + 5.0));
            if (n > 0) {
                painter->setPen(n == 1 ? style::bodyB().darker(150) : style::ink());
                painter->drawText(QRectF(x - 40.0, plot.bottom() + 6.0, 80.0, kAxisRow - 6.0), Qt::AlignHCenter,
                                  QStringLiteral("%1·T_d = %2\nклеток ×%1")
                                      .arg(QString::number(n), models::BacteriaModel::durationText(t)));
            }
        }
    }
    painter->setFont(style::uiFont());

    // Уровень заполнения кадра: выше него клетки ложатся вторым слоем.
    const double capacityY = sceneYForCells(model->dishCapacity());
    if (capacityY > plot.top() - 1.0 && capacityY < plot.bottom()) {
        painter->setPen(QPen(capacityColor(), 1.2, Qt::DotLine));
        painter->drawLine(QPointF(plot.left(), capacityY), QPointF(plot.right(), capacityY));
    }

    // Рамка осей.
    painter->setPen(QPen(style::ink(), 1.2));
    painter->setBrush(Qt::NoBrush);
    painter->drawLine(QPointF(plot.left(), plot.bottom()), QPointF(plot.right(), plot.bottom()));
    painter->drawLine(QPointF(plot.left(), plot.top()), QPointF(plot.left(), plot.bottom()));

    // Плавная кривая N₀·2^(t/T_d).
    {
        QPainterPath curve;
        const int samples = 200;
        for (int i = 0; i <= samples; ++i) {
            const double t = total * double(i) / double(samples);
            const double x = sceneXForTime(t);
            const double y = sceneYForCells(model->theoryCellsAt(t));
            if (i == 0)
                curve.moveTo(x, y);
            else
                curve.lineTo(x, y);
        }
        painter->setPen(QPen(theoryColor(), 2.2));
        painter->drawPath(curve);
    }

    // Ступенька завершённых делений: до T_d клеток столько же, после — вдвое.
    {
        const QVector<QPointF> step = model->steppedSeries();
        if (step.size() >= 2) {
            QPainterPath path;
            path.moveTo(sceneXForTime(step.first().x()), sceneYForCells(step.first().y()));
            for (const QPointF& point : step)
                path.lineTo(sceneXForTime(point.x()), sceneYForCells(point.y()));
            painter->setPen(QPen(steppedColor(), 1.7, Qt::DashLine));
            painter->drawPath(path);
        }
    }

    // Целевое число клеток и его пересечение с кривой: это ответ задачи.
    const double moment = model->targetTime();
    const double target = model->targetCells();
    if (std::isfinite(moment) && moment <= total && target <= m_cellsTop) {
        const double y = sceneYForCells(target);
        painter->setPen(QPen(style::bodyD(), 1.4, Qt::DashLine));
        painter->drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        const double x = sceneXForTime(moment);
        painter->drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        painter->setPen(QPen(style::bodyD(), 1.6));
        painter->setBrush(QColor(255, 255, 255));
        painter->drawEllipse(QPointF(x, sceneYForCells(model->theoryCellsAt(moment))), 5.0, 5.0);
    }

    // Конец наблюдения и текущий момент.
    const double endX = sceneXForTime(model->observationTime());
    painter->setPen(QPen(style::ink(), 1.4, Qt::DotLine));
    painter->drawLine(QPointF(endX, plot.top()), QPointF(endX, plot.bottom()));

    const double nowX = sceneXForTime(currentT);
    painter->setPen(QPen(style::accentWarm(), 1.6));
    painter->drawLine(QPointF(nowX, plot.top()), QPointF(nowX, plot.bottom()));
    painter->setPen(Qt::NoPen);
    painter->setBrush(style::accentWarm());
    painter->drawEllipse(QPointF(nowX, sceneYForCells(model->countedCellsAt(currentT))), 5.0, 5.0);

    // Размерная линия T_d: период удвоения измеряют линейкой по шкале.
    if (model->doublingTime() <= total) {
        labels::drawRulerLine(painter, QPointF(m_startPx, m_rulerY),
                              QPointF(sceneXForTime(model->doublingTime()), m_rulerY), style::bodyB(), 0.0);
    }
    // Границы наблюдения — той же размерной линией чуть ниже.
    labels::drawRulerLine(painter, QPointF(m_startPx, m_rulerY + 12.0), QPointF(endX, m_rulerY + 12.0),
                          style::inkSoft(), 0.0);
    painter->restore();
}

void BacteriaScene::drawGrowthBand(QPainter* painter) const {
    auto* model = qobject_cast<models::BacteriaModel*>(m_model);
    if (!model || !model->validate().ok)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRectF band = m_growthBand;
    const double baseline = band.bottom();
    painter->setPen(QPen(style::ink(), 1.2));
    painter->drawLine(QPointF(band.left(), baseline), QPointF(band.right(), baseline));

    // Прирост по интервалам учёта: столбец — ΔN между двумя подсчётами.
    const QVector<QPointF> readout = growthReadoutSeries();
    double maxGrowth = 1.0;
    for (const QPointF& point : readout)
        maxGrowth = std::max(maxGrowth, point.y());
    const double pixelsPerCell = (band.height() - 8.0) / maxGrowth;

    double left = band.left();
    for (int i = 0; i < readout.size(); ++i) {
        const QPointF& point = readout.at(i);
        const double right =
            i + 1 < readout.size() ? sceneXForTime(0.5 * (readout.at(i).x() + readout.at(i + 1).x()))
                                   : sceneXForTime(model->observationTime());
        const double x = sceneXForTime(point.x());
        const double barLeft = i == 0 ? band.left() : 0.5 * (left + x);
        const double barRight = std::max(barLeft + 0.6, right);
        const double height = point.y() * pixelsPerCell;
        painter->setPen(Qt::NoPen);
        painter->setBrush(style::bodyD());
        painter->drawRect(QRectF(barLeft, baseline - height, barRight - barLeft, height));
        left = right;
    }

    // Текущий интервал учёта: подсчёт клеток идёт именно сейчас.
    const double nowX = sceneXForTime(m_model->time());
    painter->setPen(QPen(style::accentWarm(), 1.6));
    painter->drawLine(QPointF(nowX, band.top() - 2.0), QPointF(nowX, baseline + 2.0));
    painter->restore();
}

QVector<QPointF> BacteriaScene::growthReadoutSeries() const {
    auto* model = qobject_cast<models::BacteriaModel*>(m_model);
    if (!model || !model->validate().ok)
        return {};
    return model->growthPerStepSeries();
}

// ------------------------------------------------------------------
// Подписи
// ------------------------------------------------------------------

QVector<BacteriaScene::Annotation> BacteriaScene::annotations() const {
    QVector<Annotation> list;
    auto* model = qobject_cast<models::BacteriaModel*>(m_model);
    if (!model)
        return list;

    labels::Planner planner(m_band);
    const double t = m_model->time();
    /*
     * Подписи бывают короткие и полные. Чертёж остаётся читаемым только если
     * каждая подпись умещается, поэтому при узкой полосе схемы берут краткие
     * формулировки, а пояснения добавляет tryAdd — он пропускает то, что не
     * помещается.
     */
    const bool roomy = m_band.width() >= 700.0;

    // Верхняя строка: заголовок чашки (с расшифровкой клетки) и закон роста.
    planner.addFirstFree(
        QStringLiteral("dish-title"), {QPointF(m_dish.center().x(), m_band.top() + kBandPadding + 8.0)},
        roomy ? QStringLiteral("чашка: посев N₀, 1 клетка рисунка — несколько настоящих")
              : QStringLiteral("чашка: посев N₀"),
        style::ink());
    planner.addFirstFree(
        QStringLiteral("law"), {QPointF(m_strip.center().x(), m_band.top() + kBandPadding + 8.0)},
        roomy ? QStringLiteral("ход опыта: N = N₀·2^(t/T_d), k = ln2/T_d")
              : QStringLiteral("N = N₀·2^(t/T_d)"),
        theoryColor().darker(160));

    if (!model->validate().ok) {
        planner.addFirstFree(QStringLiteral("invalid"), {QPointF(m_strip.center().x(), m_strip.center().y())},
                             QStringLiteral("проверьте параметры"), style::danger(), QColor(255, 245, 245, 235));
        return planner.items();
    }


    Q_UNUSED(model->theoryCellsAt(t));   // теория и счёт — на шкалах и панелях
    Q_UNUSED(model->countedCellsAt(t));
    const double total = model->duration();
    const double overflow = model->overflowFactorAt(t);

    // Под чашкой: радиус колонии и сколько клеток в кадре помещается.
    planner.addFirstFree(
        QStringLiteral("colony"), {QPointF(m_dish.center().x(), m_dish.bottom() + kCaptionRow * 0.5)},
        roomy ? QStringLiteral("N клеток\nR = R₀·√(N/N_мест)")
              : QStringLiteral("N · R = R₀·√(N/N_мест)"),
        style::ink(), QColor(255, 255, 255, 220));

    // Деления, прошедшие к моменту t: их берут и из времени, и из числа клеток.
    planner.addFirstFree(
        QStringLiteral("divisions"), {QPointF(m_strip.center().x(), m_strip.bottom() - 12.0)},
        roomy ? QStringLiteral("n = t/T_d · завершено n · клеток N")
              : QStringLiteral("n = t/T_d"),
        style::ink(), QColor(255, 255, 255, 220));

    // Искомый момент: целевое число клеток и его пересечение с кривой.
    const double moment = model->targetTime();
    if (std::isfinite(moment) && moment <= total && model->targetCells() <= m_cellsTop) {
        planner.addFirstFree(
            QStringLiteral("target"),
            {QPointF(sceneXForTime(moment) + 112.0, sceneYForCells(model->targetCells()) - 14.0),
             QPointF(m_strip.center().x(), m_strip.top() + 36.0), QPointF(m_strip.center().x(), m_strip.center().y())},
            QStringLiteral("t = T_d·log₂(N/N₀)"),
            style::bodyD().darker(160), QColor(255, 255, 255, 225));
    }

    // Размерная линия T_d: за неё число клеток удваивается.
    const double life = model->doublingTime();
    if (life <= total) {
        planner.addFirstFree(
            QStringLiteral("doubling"), {QPointF(m_strip.center().x(), m_rulerY)},
            roomy ? QStringLiteral("T_d — клеток становится вдвое больше")
                  : QStringLiteral("T_d — клеток вдвое больше"),
            style::bodyB().darker(150), QColor(255, 255, 255, 220));
    } else {
        planner.addFirstFree(
            QStringLiteral("doubling"), {QPointF(m_strip.center().x(), m_rulerY)},
            QStringLiteral("T_d — больше времени опыта"),
            style::bodyB().darker(150), QColor(255, 255, 255, 220));
    }

    // Скорость прироста: мгновенная и средняя по подсчётам.
    planner.addFirstFree(
        QStringLiteral("growth"), {QPointF(m_growthBand.center().x() - 118.0, m_growthBand.bottom() + 14.0)},
        roomy ? QStringLiteral("Δt: прирост ΔN · N′ = kN")
              : QStringLiteral("ΔN · N′ = kN"),
        style::bodyD().darker(160), QColor(255, 255, 255, 220));

    // Заполнение чашки: после него деления прекращаются.
    planner.addFirstFree(
        QStringLiteral("overflow"), {QPointF(m_growthBand.center().x() + 126.0, m_growthBand.bottom() + 14.0)},
        overflow >= 1.0
            ? QStringLiteral("чашка заполнена — деления прекращаются")
            : roomy ? QStringLiteral("второй слой — к N_мест, там рост встаёт")
                    : QStringLiteral("N_мест"),
        overflow >= 1.0 ? style::success().darker(130) : style::inkSoft(), QColor(255, 255, 255, 220));

    // Ответ задачи после конца наблюдения.
    if (t >= model->observationTime() - 1e-9) {
        planner.addFirstFree(
            QStringLiteral("result"),
            {QPointF(m_strip.center().x(), m_strip.top() + 14.0), QPointF(m_strip.center().x(), m_strip.center().y())},
            QStringLiteral("конец опыта: N, n и ×N/N₀ — в панели"),
            style::success().darker(130), QColor(234, 247, 239, 240));
    }

    // Пояснения рисунка: они добавляются только там, где для них есть место.
    planner.tryAdd(QStringLiteral("window"), {QPointF(m_strip.right() - 80.0, m_strip.bottom() - 14.0)},
                   QStringLiteral("T — время опыта"),
                   style::ink(), QColor(255, 255, 255, 220));
    planner.tryAdd(QStringLiteral("capacity"), {QPointF(m_strip.left() + 92.0, sceneYForCells(model->dishCapacity()) - 14.0)},
                   QStringLiteral("кадр заполнен"), capacityColor().darker(120), QColor(255, 255, 255, 215));
    planner.tryAdd(QStringLiteral("legend"), {QPointF(m_strip.center().x(), m_strip.top() + 14.0)},
                   QStringLiteral("ступенька — целые деления, кривая — N₀·2^(t/T_d)"), style::inkSoft(),
                   QColor(255, 255, 255, 205));

    return planner.items();
}

// ------------------------------------------------------------------
// Панели графиков
// ------------------------------------------------------------------

void BacteriaScene::drawPlots(QPainter* painter) const {
    auto* model = qobject_cast<models::BacteriaModel*>(m_model);
    if (!model || !model->validate().ok)
        return;

    const double plotsTop = m_band.bottom() + 12.0;
    const double plotsBottom = timeAxisRect().top() - 8.0;
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

    // ---- 1. Число клеток: плавная кривая и ступенька делений ----
    {
        PlotSeries theory;
        theory.points = model->theorySeries();
        theory.color = theoryColor();
        theory.label = QStringLiteral("N₀·2^(t/T_d)");
        PlotSeries stepped;
        stepped.points = model->steppedSeries();
        stepped.color = steppedColor();
        stepped.dashed = true;
        stepped.label = QStringLiteral("ступенька делений");
        drawPlot(painter, QRectF(m_band.left(), top, panelWidth, height),
                 QStringLiteral("Клетки: кривая — теория, ступенька — деления"), timeUnitLabel(), {theory, stepped},
                 duration, currentT, eventT);
    }

    // ---- 2. Прирост по интервалам учёта и средняя скорость ----
    {
        PlotSeries growth;
        growth.points = model->growthPerStepSeries();
        growth.color = style::bodyD();
        growth.width = 3.4;
        growth.label = QStringLiteral("ΔN за интервал");
        PlotSeries average;
        average.points = model->averageGrowthSeries();
        average.color = style::accentWarm();
        average.width = 1.2;
        average.dashed = true;
        average.label = QStringLiteral("ΔN/Δt");
        /*
         * При Δt = 1 ч числа ΔN и ΔN/Δt совпадают (делить на единицу — значит
         * ничего не менять), поэтому линии идут одна по другой: толстый
         * фиолетовый столбец прироста и тонкий оранжевый пунктир скорости
         * видно вместе. Меняя Δt, их расхождение видно сразу.
         */
        drawPlot(painter, QRectF(m_band.left() + panelWidth + kGap, top, panelWidth, height),
                 QStringLiteral("Прирост по подсчётам и средняя скорость"), timeUnitLabel(), {growth, average},
                 duration, currentT, eventT);
    }

    // ---- 3. ln N: прямая с наклоном k ----
    {
        PlotSeries series;
        series.points = model->logarithmSeries();
        series.color = style::bodyC();
        series.label = QStringLiteral("ln N = ln N₀ + kt");
        drawPlot(painter, QRectF(m_band.left() + 2.0 * (panelWidth + kGap), top, panelWidth, height),
                 QStringLiteral("ln N: наклон прямой равен k"), timeUnitLabel(), {series}, duration, currentT, eventT);
    }

    // ---- 4. Скорость прироста против числа клеток ----
    {
        PlotSeries series;
        series.points = model->growthVersusCells();
        series.color = style::bodyA();
        series.label = QStringLiteral("N′ = kN");
        const QPointF current(model->theoryCellsAt(currentT), model->growthRateOfCellsAt(currentT));
        drawGraph(painter, QRectF(m_band.left() + 3.0 * (panelWidth + kGap), top, panelWidth, height),
                  QStringLiteral("Скорость прироста N′(N)"), QStringLiteral("N, клеток"),
                  QStringLiteral("N′, клеток/ч"), {series}, current, false);
    }
}

// ------------------------------------------------------------------
// Ручки
// ------------------------------------------------------------------

void BacteriaScene::positionHandles() {
    auto* model = qobject_cast<models::BacteriaModel*>(m_model);
    if (!model)
        return;

    const double nowY = m_strip.center().y();
    for (DragHandleItem* handle : m_handles) {
        if (!handle)
            continue;
        const HandleInfo info = handle->info();
        if (info.id == QLatin1String("n0")) {
            // Уровень посева N₀ на вертикальной оси: по нему тянут посев.
            handle->setAxis(DragHandleItem::Axis::Vertical);
            handle->setAnchor(QPointF(m_strip.left() - kCellsAxisWidth * 0.5, handleScenePosition(info)));
        } else if (info.id == QLatin1String("target")) {
            // Уровень целевого числа клеток: ручка двигает отметку N.
            handle->setAxis(DragHandleItem::Axis::Vertical);
            handle->setAnchor(QPointF(m_strip.right() + 14.0, handleScenePosition(info)));
        } else if (info.id == QLatin1String("doubling")) {
            handle->setAxis(DragHandleItem::Axis::Horizontal);
            handle->setAnchor(QPointF(handleScenePosition(info), m_rulerY + 12.0));
        } else if (info.id == QLatin1String("window")) {
            handle->setAxis(DragHandleItem::Axis::Horizontal);
            handle->setAnchor(QPointF(handleScenePosition(info), nowY));
        } else {
            handle->setAnchor(QPointF(m_band.center().x(), m_band.center().y()));
        }
    }
}

double BacteriaScene::handleScenePosition(const HandleInfo& info) const {
    auto* model = qobject_cast<models::BacteriaModel*>(m_model);
    if (!model)
        return LabScene::handleScenePosition(info);

    const double total = std::max(1e-6, model->duration());
    if (info.id == QLatin1String("n0"))
        return std::clamp(sceneYForCells(info.value), m_stripTopY - 10.0, m_stripBottomY);
    if (info.id == QLatin1String("target"))
        return std::clamp(sceneYForCells(info.value), m_stripTopY - 10.0, m_stripBottomY);
    if (info.id == QLatin1String("doubling")) {
        // Отметка T_d на размерной линии; если период удвоения больше опыта,
        // ручка становится у конца шкалы и упирается в неё.
        return std::clamp(sceneXForTime(info.value), m_startPx, m_endPx);
    }
    if (info.id == QLatin1String("window"))
        return std::clamp(sceneXForTime(std::min(info.value, total)), m_startPx, m_endPx);
    return LabScene::handleScenePosition(info);
}

double BacteriaScene::mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const {
    auto* model = qobject_cast<models::BacteriaModel*>(m_model);
    if (!model)
        return LabScene::mapHandlePosition(info, scenePos);

    if (info.id == QLatin1String("n0") || info.id == QLatin1String("target")) {
        // Высота над нулевой линией — число клеток.
        const double cells = std::clamp(cellsForSceneY(scenePos.y()), 0.0, m_cellsTop);
        return std::max(1.0, cells);
    }
    if (info.id == QLatin1String("doubling") || info.id == QLatin1String("window"))
        return std::max(0.0, timeForSceneX(scenePos.x()));
    return LabScene::mapHandlePosition(info, scenePos);
}

}  // namespace lab::scenes
