#include "scenes/DecayScene.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

#include "core/Trajectory.h"
#include "models/DecayModel.h"
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
constexpr double kCounterHeight = 40.0;
constexpr double kCaptionRow = 28.0;
constexpr double kRowGap = 6.0;

/** Ширина левой колонки (образец) и подписей вертикальной оси хода опыта. */
constexpr double kSampleFraction = 0.29;
constexpr double kNucleiAxisWidth = 56.0;

/**
 * Число ядер в клетке образца. Клеток должно быть достаточно много, чтобы
 * гасли они поодиночке, и достаточно мало, чтобы каждая клетка оставалась
 * различимой: перебор идёт по «удобным» делителям, поэтому подпись
 * «одна клетка = 8 ядер» всегда точна.
 */
constexpr int kPerCellSteps[] = {1, 2, 4, 5, 8, 10, 20, 25, 40, 50, 100, 200, 250, 500, 1000};
/** Границы числа клеток: вне их решётка либо неразличима, либо громоздка. */
constexpr int kMaxCells = 200;
constexpr int kMinCells = 16;

/** Порядок обхода клеток фиксирован: так клетки только гаснут. */
constexpr quint64 kSampleSeed = 0x5AFE0000ULL;

/** Габариты клетки и зазор между клетками. */
constexpr double kCellGap = 1.6;
constexpr double kCellRadius = 1.4;

/** Радиус отметки текущего момента и точек пересечений на шкале опыта. */
constexpr double kMarkRadius = 5.0;

/**
 * Цвета рисунка. Ядро — синий, дочернее ядро — зелёный: сумма тех и других
 * равна N₀, поэтому цвета просто перетекают по мере опыта. Вспышка оранжевым
 * отмечает клетки, гаснущие на текущем шаге наблюдения: по ней видно, что
 * распад идёт целыми ядрами и не непрерывно.
 */
QColor nucleusFill() { return style::bodyA(); }
QColor daughterFill() { return style::bodyC(); }
QColor freshFill() { return style::accentWarm(); }

/** Теория и опыт: кривая и ступенька должны различаться цветом и стилем. */
QColor theoryColor() { return style::bodyB(); }
QColor countedColor() { return style::bodyA(); }
QColor corridorColor() { return QColor(style::inkSoft().red(), style::inkSoft().green(),
                                       style::inkSoft().blue(), 46); }

/** Делитель числа ядер в клетке для N₀ ядер. */
int nucleiPerCellFor(double nuclei) {
    for (int per : kPerCellSteps) {
        if (std::ceil(nuclei / double(per)) <= double(kMaxCells))
            return per;
    }
    return kPerCellSteps[sizeof(kPerCellSteps) / sizeof(kPerCellSteps[0]) - 1];
}

/** Число периодов полураспада в виде доли нераспавшихся ядер: 1, 2, 3 → «1/2». */
QString halfLifeFractionLabel(int periods) {
    if (periods <= 0)
        return QStringLiteral("N₀");
        // Знаменатель — степень двойки: именно так считают распад устно.
    return QStringLiteral("1/%1").arg(int(std::ldexp(1.0, periods)));
}

/** Засечки шкалы времени: целые периоды полураспада, помещающиеся в опыт. */
int halfLivesOnScale(const models::DecayModel* model, double window) {
    const double life = model->halfLife();
    if (life <= 0.0 || !std::isfinite(life))
        return 0;
    return std::clamp(int(std::floor(window / life)), 0, 12);
}

}  // namespace

DecayScene::DecayScene(Simulation* model, QObject* parent) : LabScene(model, parent) {}

// ------------------------------------------------------------------
// Геометрия полосы схемы
// ------------------------------------------------------------------

void DecayScene::reconfigure(const QRectF& area) {
    auto* model = qobject_cast<models::DecayModel*>(m_model);

    // Полоса схемы: слева образец с клетками, справа — ход опыта во времени,
    // под ним лента счётчика и размерная линия T½; внизу — строка подписей.
    m_band = QRectF(area.left() + 18.0, area.top() + 12.0, area.width() - 36.0,
                    std::max(250.0, std::min(336.0, area.height() * 0.47)));

    const double contentTop = m_band.top() + kBandPadding + kTitleRow;
    const double contentBottom = m_band.bottom() - kBandPadding;
    // Столбец строк под графиком опыта: деления времени, размерная линия,
    // лента счётчика и строка подписей.
    const double stack = kAxisRow + kRowGap + kRulerRow + kRowGap + kCounterHeight + kRowGap + kCaptionRow;
    const double plotHeight = std::max(70.0, contentBottom - contentTop - stack);

    const double sampleWidth = std::min(m_band.width() * kSampleFraction, 320.0);
    m_sample = QRectF(m_band.left() + kBandPadding, contentTop, sampleWidth,
                      std::max(80.0, contentBottom - contentTop - kCaptionRow));

    const double stripLeft = m_sample.right() + kNucleiAxisWidth + 14.0;
    m_strip = QRectF(stripLeft, contentTop, std::max(120.0, m_band.right() - kBandPadding - stripLeft),
                     plotHeight);
    m_counterBand = QRectF(stripLeft, m_strip.bottom() + kAxisRow + kRowGap + kRulerRow + kRowGap,
                           m_strip.width(), kCounterHeight);
    m_rulerY = m_strip.bottom() + kAxisRow + kRowGap + kRulerRow * 0.5;

    m_startPx = m_strip.left();
    m_endPx = m_strip.right();
    m_stripTopY = m_strip.top();
    m_stripBottomY = m_strip.bottom();

    const double total = model && model->validate().ok ? model->duration() : 40.0;
    m_pixelsPerHour = (m_endPx - m_startPx) / std::max(1e-6, total);
    m_nucleiTop = model && model->validate().ok ? std::max(1.0, model->initialNuclei()) : 1000.0;
    m_pixelsPerNucleus = (m_stripBottomY - m_stripTopY) / m_nucleiTop;

    // Клетки образца: их число и распределение пересчитываются вместе с N₀.
    const double nuclei = m_nucleiTop;
    m_cellsPerNucleus = nucleiPerCellFor(nuclei);
    m_cellCount = int(std::ceil(nuclei / double(m_cellsPerNucleus)));
    m_cellCount = std::clamp(m_cellCount, kMinCells, kMaxCells);

    // Порядок обхода: детерминированный, поэтому с ростом времени клетки
    // только гаснут и рисунок не «перетасовывается» между кадрами.
    m_permutation.resize(m_cellCount);
    for (int index = 0; index < m_cellCount; ++index)
        m_permutation[index] = index;
    const quint64 seed = kSampleSeed + quint64(std::llround(model ? model->param(QStringLiteral("seed")) : 0.0)) *
                                                    0x1000003ULL;
    SeededRandom random(seed == 0 ? kSampleSeed : seed);
    for (int index = m_cellCount - 1; index > 0; --index) {
        const int swap = random.bounded(index + 1);
        std::swap(m_permutation[index], m_permutation[swap]);
    }

    setRanges(area, 0.0, std::max(1e-6, total), 0.0, 1.0);
}

QRectF DecayScene::schemeBand() const { return m_band; }

DecayScene::SchemeGeometry DecayScene::schemeGeometry() const {
    auto* model = qobject_cast<models::DecayModel*>(m_model);
    SchemeGeometry geometry;
    geometry.sample = m_sample;
    geometry.strip = m_strip;
    geometry.counterBand = m_counterBand;
    geometry.rulerY = m_rulerY;
    geometry.startPx = m_startPx;
    geometry.endPx = m_endPx;
    geometry.pixelsPerHour = m_pixelsPerHour;
    geometry.cellsPerNucleus = m_cellsPerNucleus;
    geometry.nucleiTopPy = sceneYForNuclei(m_nucleiTop);
    if (model && model->validate().ok) {
        geometry.halfLifePx = sceneXForTime(model->halfLife());
        geometry.observationEndPx = sceneXForTime(model->observationTime());
        geometry.targetPx = sceneXForTime(model->targetTime());
        geometry.targetLevelPy = sceneYForNuclei(model->targetFraction() * model->initialNuclei());
        geometry.currentPx = sceneXForTime(model->time());
    }
    return geometry;
}

double DecayScene::sceneXForTime(double t) const { return m_startPx + t * m_pixelsPerHour; }

double DecayScene::timeForSceneX(double sceneX) const {
    return (sceneX - m_startPx) / std::max(1e-9, m_pixelsPerHour);
}

double DecayScene::sceneYForNuclei(double nuclei) const { return m_stripBottomY - nuclei * m_pixelsPerNucleus; }

double DecayScene::nucleiForSceneY(double sceneY) const {
    return (m_stripBottomY - sceneY) / std::max(1e-9, m_pixelsPerNucleus);
}

int DecayScene::nucleiPerCell() const { return m_cellsPerNucleus; }

void DecayScene::gridLayout(int cells, int& columns, int& rows) const {
    // Решётка чуть шире, чем высокая: так колонка клеток повторяет форму
    // рамки образца.
    columns = std::max(1, int(std::ceil(std::sqrt(double(cells) * 1.30))));
    rows = std::max(1, int(std::ceil(double(cells) / double(columns))));
}

// ------------------------------------------------------------------
// Образец: клетки гаснут по одной
// ------------------------------------------------------------------

QVector<DecayScene::NucleusCell> DecayScene::nucleiGridFor(double t) const {
    QVector<NucleusCell> cells;
    auto* model = qobject_cast<models::DecayModel*>(m_model);
    if (!model || !model->validate().ok || m_cellCount <= 0)
        return cells;

    const double per = double(m_cellsPerNucleus);
    // Число целых клеток считается от показаний счётчика, а не от гладкой
    // кривой: ядер в образце ровно столько, сколько их насчитал опыт.
    const double alive = model->countedNucleiAt(t);
    if (!std::isfinite(alive))
        return cells;
    int aliveCells = int(std::ceil(alive / per));
    aliveCells = std::clamp(aliveCells, 0, m_cellCount);

    // Клетки, гаснущие на текущем шаге наблюдения: их столько, сколько
    // распадов показал счётчик за шаг Δt.
    const double step = model->sampleInterval();
    const int node = step > 0.0 ? int(std::floor(t / step + 1e-9)) : 0;
    const double previous = model->countedNucleiAt(t - step);
    int freshCells = 0;
    if (node > 0 && std::isfinite(previous))
        freshCells = std::clamp(int(std::ceil(previous / per)) - aliveCells, 0, m_cellCount - aliveCells);

    int columns = 1;
    int rows = 1;
    gridLayout(m_cellCount, columns, rows);
    const double cellWidth = (m_sample.width() - (columns - 1) * kCellGap) / columns;
    const double cellHeight = (m_sample.height() - (rows - 1) * kCellGap) / rows;

    cells.reserve(m_cellCount);
    for (int rank = 0; rank < m_cellCount; ++rank) {
        const int order = m_permutation.at(rank);
        const int column = order % columns;
        const int row = order / columns;
        NucleusCell cell;
        cell.index = rank;
        cell.rect = QRectF(m_sample.left() + column * (cellWidth + kCellGap),
                           m_sample.top() + row * (cellHeight + kCellGap), cellWidth, cellHeight);
        cell.alive = rank < aliveCells;
        cell.justDecayed = !cell.alive && rank < aliveCells + freshCells;
        cell.nucleiPerCell = per;
        cells.push_back(cell);
    }
    return cells;
}

// ------------------------------------------------------------------
// Векторов в модели распада нет
// ------------------------------------------------------------------

QVector<vectors::Arrow> DecayScene::vectorsFor(double /*t*/) const {
    /*
     * Распад — не движение и не сила: ядра не перемещаются, у них меняется
     * число. Поэтому ни одна категория векторов (скорости, ускорения, силы)
     * здесь не появляется: рисовать стрелку было бы нечем, кроме как
     * придуманной величиной.
     */
    return {};
}

// ------------------------------------------------------------------
// Отрисовка
// ------------------------------------------------------------------

void DecayScene::paintScheme(QPainter* painter) {
    auto* model = qobject_cast<models::DecayModel*>(m_model);
    if (!model)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QVector<NucleusCell> cells = nucleiGridFor(m_model->time());
    drawSample(painter, cells);
    drawStrip(painter);
    drawCounterBand(painter);

    // Все тексты — последним проходом по одному реестру.
    labels::drawAll(painter, annotations());
    drawPlots(painter);
    painter->restore();
}

void DecayScene::drawSample(QPainter* painter, const QVector<NucleusCell>& cells) const {
    auto* model = qobject_cast<models::DecayModel*>(m_model);
    if (!model)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Рамка образца — кювета с препаратом: тонкая стенка и подпись блока.
    painter->setPen(QPen(style::ink(), 1.4));
    painter->setBrush(QColor(255, 255, 255, 225));
    painter->drawRoundedRect(m_sample.adjusted(-6.0, -6.0, 6.0, 6.0), 8.0, 8.0);

    if (!model->validate().ok) {
        painter->setPen(style::inkSoft());
        painter->drawText(m_sample, Qt::AlignCenter, QStringLiteral("постановка недопустима"));
        painter->restore();
        return;
    }

    for (const NucleusCell& cell : cells) {
        const QRectF rect = cell.rect.adjusted(0.4, 0.4, -0.4, -0.4);
        if (cell.alive) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(nucleusFill());
            painter->drawRoundedRect(rect, kCellRadius, kCellRadius);
            continue;
        }
        // Распавшаяся клетка: контур и зелёное пятно дочернего ядра.
        painter->setPen(QPen(style::grid(), 1.0));
        painter->setBrush(QColor(255, 255, 255, 190));
        painter->drawRoundedRect(rect, kCellRadius, kCellRadius);
        const QPointF center = rect.center();
        const double radius = std::min(3.4, std::min(rect.width(), rect.height()) * 0.28);
        painter->setPen(Qt::NoPen);
        painter->setBrush(cell.justDecayed ? freshFill() : daughterFill().darker(110));
        painter->drawEllipse(center, radius, radius);
    }
    painter->restore();
}

void DecayScene::drawStrip(QPainter* painter) const {
    auto* model = qobject_cast<models::DecayModel*>(m_model);
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
    const double n0 = model->initialNuclei();

    // Сетка и вертикальная ось числа ядер: N₀, его половины и четверть.
    painter->setPen(QPen(style::grid(), 0.8));
    for (int power = 0; power <= 4; ++power) {
        const double value = n0 / std::ldexp(1.0, power);
        const double y = sceneYForNuclei(value);
        if (y < plot.top() - 1.0)
            break;
        painter->setPen(QPen(style::grid(), power == 0 ? 1.0 : 0.7, power == 0 ? Qt::SolidLine : Qt::DashLine));
        painter->drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        painter->setPen(style::inkSoft());
        painter->drawText(QRectF(plot.left() - kNucleiAxisWidth, y - 8.0, kNucleiAxisWidth - 6.0, 16.0),
                          Qt::AlignRight | Qt::AlignVCenter, models::DecayModel::nucleiText(value));
        painter->setPen(QPen(style::grid(), 0.7));
    }
    // Ось времени: деления через целый период полураспада; у засечки стоит
    // номер периода и доля нераспавшихся ядер — её читают устно.
    const int marks = halfLivesOnScale(model, total);
    painter->setFont(style::uiFont(8.0));
    for (int period = 0; period <= marks; ++period) {
        const double t = model->halfLife() * period;
        const double x = sceneXForTime(t);
        if (x > plot.right() + 1.0)
            break;
        painter->setPen(QPen(style::grid(), period == 0 ? 1.0 : 0.7, period == 0 ? Qt::SolidLine : Qt::DashLine));
        painter->drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        painter->setPen(QPen(style::ink(), 1.0));
        painter->drawLine(QPointF(x, plot.bottom()), QPointF(x, plot.bottom() + 5.0));
        if (period > 0) {
            painter->setPen(period == 1 ? style::bodyB().darker(150) : style::ink());
            painter->drawText(QRectF(x - 34.0, plot.bottom() + 6.0, 68.0, kAxisRow - 6.0), Qt::AlignHCenter,
                              QStringLiteral("%1·T½ = %2\nосталось %3")
                                  .arg(period)
                                  .arg(models::DecayModel::durationText(t), halfLifeFractionLabel(period)));
        }
        painter->setPen(QPen(style::grid(), 0.7));
    }
    painter->setFont(style::uiFont());

    // Рамка осей.
    painter->setPen(QPen(style::ink(), 1.2));
    painter->setBrush(Qt::NoBrush);
    painter->drawLine(QPointF(plot.left(), plot.bottom()), QPointF(plot.right(), plot.bottom()));
    painter->drawLine(QPointF(plot.left(), plot.top()), QPointF(plot.left(), plot.bottom()));

    // Коридор ±√N: в нём остаётся один опыт, хотя сама теория гладкая.
    QPainterPath corridor;
    const int samples = 160;
    for (int i = 0; i <= samples; ++i) {
        const double t = total * i / double(samples);
        const double y = sceneYForNuclei(model->theoryNucleiAt(t) + model->expectedDeviationAt(t));
        if (i == 0)
            corridor.moveTo(sceneXForTime(t), y);
        else
            corridor.lineTo(sceneXForTime(t), y);
    }
    for (int i = samples; i >= 0; --i) {
        const double t = total * i / double(samples);
        corridor.lineTo(sceneXForTime(t), sceneYForNuclei(std::max(0.0, model->theoryNucleiAt(t) -
                                                                    model->expectedDeviationAt(t))));
    }
    corridor.closeSubpath();
    painter->setPen(Qt::NoPen);
    painter->setBrush(corridorColor());
    painter->drawPath(corridor);

    // Теоретическая кривая N₀·2^(−t/T½).
    {
        QPainterPath curve;
        for (int i = 0; i <= samples; ++i) {
            const double t = total * i / double(samples);
            const double x = sceneXForTime(t);
            const double y = sceneYForNuclei(model->theoryNucleiAt(t));
            if (i == 0)
                curve.moveTo(x, y);
            else
                curve.lineTo(x, y);
        }
        painter->setPen(QPen(theoryColor(), 2.2));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(curve);
    }

    // Ступенька показаний счётчика: целое число ядер, шаг за шагом.
    {
        const QVector<QPointF> step = model->countedSeries();
        if (step.size() >= 2) {
            QPainterPath path;
            path.moveTo(sceneXForTime(step.first().x()), sceneYForNuclei(step.first().y()));
            for (const QPointF& point : step)
                path.lineTo(sceneXForTime(point.x()), sceneYForNuclei(point.y()));
            painter->setPen(QPen(countedColor(), 1.7));
            painter->drawPath(path);
        }
    }

    // Уровень заданной доли d и его пересечение с кривой: это ответ задачи.
    const double moment = model->targetTime();
    const double level = model->targetFraction() * n0;
    if (std::isfinite(moment) && moment <= total && level <= n0) {
        const double y = sceneYForNuclei(level);
        painter->setPen(QPen(style::bodyD(), 1.4, Qt::DashLine));
        painter->drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        const double x = sceneXForTime(moment);
        painter->drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        painter->setPen(QPen(style::bodyD(), 1.6));
        painter->setBrush(QColor(255, 255, 255));
        painter->drawEllipse(QPointF(x, sceneYForNuclei(model->theoryNucleiAt(moment))), kMarkRadius, kMarkRadius);
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
    painter->drawEllipse(QPointF(nowX, sceneYForNuclei(model->countedNucleiAt(currentT))), kMarkRadius, kMarkRadius);

    // Размерная линия T½: по ней период полураспада можно измерить линейкой.
    if (model->halfLife() <= total) {
        labels::drawRulerLine(painter, QPointF(m_startPx, m_rulerY), QPointF(sceneXForTime(model->halfLife()), m_rulerY),
                              style::bodyB(), 0.0);
    }
    // Границы наблюдения — той же размерной линией чуть ниже.
    labels::drawRulerLine(painter, QPointF(m_startPx, m_rulerY + 12.0), QPointF(endX, m_rulerY + 12.0),
                          style::inkSoft(), 0.0);
    painter->restore();
}

void DecayScene::drawCounterBand(QPainter* painter) const {
    auto* model = qobject_cast<models::DecayModel*>(m_model);
    if (!model || !model->validate().ok)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRectF band = m_counterBand;
    const double baseline = band.bottom();
    painter->setPen(QPen(style::ink(), 1.2));
    painter->drawLine(QPointF(band.left(), baseline), QPointF(band.right(), baseline));

    // Распады по интервалам наблюдения: то, что реально видит счётчик.
    const QVector<QPointF> readout = counterReadoutSeries();
    double maxCount = 1.0;
    for (const QPointF& point : readout)
        maxCount = std::max(maxCount, point.y());
    const double pixelsPerDecay = (band.height() - 8.0) / maxCount;

    double left = band.left();
    for (int i = 0; i < readout.size(); ++i) {
        const QPointF& point = readout.at(i);
        const double right = i + 1 < readout.size()
                                 ? sceneXForTime(0.5 * (readout.at(i).x() + readout.at(i + 1).x()))
                                 : sceneXForTime(model->duration());
        const double x = sceneXForTime(point.x());
        const double barLeft = i == 0 ? band.left() : 0.5 * (left + x);
        const double barRight = std::max(barLeft + 0.6, right);
        const double height = point.y() * pixelsPerDecay;
        painter->setPen(Qt::NoPen);
        painter->setBrush(style::bodyD());
        painter->drawRect(QRectF(barLeft, baseline - height, barRight - barLeft, height));
        left = right;
    }

    // Текущий интервал: счётчик щёлкает именно сейчас.
    const double nowX = sceneXForTime(m_model->time());
    painter->setPen(QPen(style::accentWarm(), 1.6));
    painter->drawLine(QPointF(nowX, band.top() - 2.0), QPointF(nowX, baseline + 2.0));
    painter->restore();
}

QVector<QPointF> DecayScene::counterReadoutSeries() const {
    auto* model = qobject_cast<models::DecayModel*>(m_model);
    if (!model || !model->validate().ok)
        return {};
    return model->countsPerStepSeries();
}

// ------------------------------------------------------------------
// Подписи
// ------------------------------------------------------------------

QVector<DecayScene::Annotation> DecayScene::annotations() const {
    QVector<Annotation> list;
    auto* model = qobject_cast<models::DecayModel*>(m_model);
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

    // Верхняя строка: заголовок образца (с расшифровкой решётки) и закон, по
    // которому считают опыт.
    planner.addFirstFree(
        QStringLiteral("sample-title"), {QPointF(m_sample.center().x(), m_band.top() + kBandPadding + 8.0)},
        roomy ? QStringLiteral("образец: посев N₀, 1 клетка — несколько ядер")
              : QStringLiteral("образец: посев N₀"),
        style::ink());
    planner.addFirstFree(
        QStringLiteral("law"), {QPointF(m_strip.center().x(), m_band.top() + kBandPadding + 8.0)},
        roomy ? QStringLiteral("ход опыта: N = N₀·2^(−t/T½), λ = ln2/T½")
              : QStringLiteral("N = N₀·2^(−t/T½)"),
        theoryColor().darker(160));

    if (!model->validate().ok) {
        planner.addFirstFree(QStringLiteral("invalid"), {QPointF(m_strip.center().x(), m_strip.center().y())},
                             QStringLiteral("проверьте параметры"), style::danger(), QColor(255, 245, 245, 235));
        return planner.items();
    }

    Q_UNUSED(model->countedNucleiAt(t));  // числа — на панелях, на рисунке обозначения
    Q_UNUSED(model->theoryNucleiAt(t));
    const double total = model->duration();

    // Под образцом: два счёта рядом — целый опыт и гладкая теория.
    planner.addFirstFree(
        QStringLiteral("counts"), {QPointF(m_sample.center().x(), m_sample.bottom() + kCaptionRow * 0.5)},
        QStringLiteral("счёт N и теория N(t) — два счёта"),
        style::ink(), QColor(255, 255, 255, 220));

    // Искомый момент: доля d и её пересечение с кривой.
    const double moment = model->targetTime();
    if (std::isfinite(moment) && moment <= total) {
        planner.addFirstFree(
            QStringLiteral("target"),
            {QPointF(sceneXForTime(moment) + 116.0,
                     sceneYForNuclei(model->targetFraction() * model->initialNuclei()) - 14.0),
             QPointF(m_strip.center().x(), m_strip.top() + 36.0), QPointF(m_strip.center().x(), m_strip.center().y())},
            QStringLiteral("цель d: время t, число N"),
            style::bodyD().darker(160), QColor(255, 255, 255, 225));
    }

    // Размерная линия T½: период полураспада — ровно половина ядер.
    const double life = model->halfLife();
    if (life <= total) {
        planner.addFirstFree(
            QStringLiteral("half"), {QPointF(m_strip.center().x(), m_rulerY)},
            roomy ? QStringLiteral("T½ — распадается ровно половина ядер")
                  : QStringLiteral("T½ — половина ядер"),
            style::bodyB().darker(150), QColor(255, 255, 255, 220));
    } else {
        // Долгоживущий изотоп: периода полураспада на шкале не видно, поэтому
        // подпись говорит, какая доля ядер распадается за время опыта.
        planner.addFirstFree(
            QStringLiteral("half"), {QPointF(m_strip.center().x(), m_rulerY)},
            roomy ? QStringLiteral("T½ — больше времени опыта: доля распавшихся — в панели")
                  : QStringLiteral("T½ — вне шкалы"),
            style::bodyB().darker(150), QColor(255, 255, 255, 220));
    }

    // Показания счётчика: длительность шага, число распадов и его вероятность.
    planner.addFirstFree(
        QStringLiteral("counter"), {QPointF(m_counterBand.center().x() - 110.0, m_counterBand.bottom() + 14.0)},
        roomy ? QStringLiteral("шаг Δt: распады ΔN · Δp = λΔt")
              : QStringLiteral("Δt: ΔN · Δp = λΔt"),
        style::bodyD().darker(160), QColor(255, 255, 255, 220));

    // Активность и расхождение счёта с теорией.
    planner.addFirstFree(
        QStringLiteral("deviation"), {QPointF(m_counterBand.center().x() + 120.0, m_counterBand.bottom() + 14.0)},
        roomy ? QStringLiteral("A = λN · расхождение счёта с теорией")
              : QStringLiteral("A = λN · расхождение"),
        std::abs(model->relativeDeviationAt(t)) <=
                300.0 * model->expectedDeviationAt(t) / model->initialNuclei()
            ? style::success().darker(130)
            : style::danger().darker(120),
        QColor(255, 255, 255, 220));

    // Ответ задачи после конца наблюдения.
    if (t >= model->observationTime() - 1e-9) {
        planner.addFirstFree(
            QStringLiteral("result"),
            {QPointF(m_strip.center().x(), m_strip.bottom() - 14.0),
             QPointF(m_strip.center().x(), m_strip.top() + 36.0), QPointF(m_strip.center().x(), m_strip.center().y())},
            QStringLiteral("конец опыта: N и ΔN — в панели"),
            style::success().darker(130), QColor(234, 247, 239, 240));
    }

    // Пояснения рисунка: они добавляются только там, где для них есть место.
    planner.tryAdd(QStringLiteral("corridor"), {QPointF(m_strip.right() - 92.0, m_strip.top() + 14.0)},
                   QStringLiteral("коридор ±√N"),
                   style::inkSoft(), QColor(255, 255, 255, 210));
    planner.tryAdd(QStringLiteral("window"), {QPointF(m_strip.right() - 92.0, m_strip.bottom() - 14.0)},
                   QStringLiteral("T — время опыта"),
                   style::ink(), QColor(255, 255, 255, 220));
    planner.tryAdd(QStringLiteral("legend"), {QPointF(m_strip.center().x(), m_strip.top() + 14.0)},
                   QStringLiteral("ступенька — целые ядра опыта, кривая — теория"), style::inkSoft(),
                   QColor(255, 255, 255, 205));

    return planner.items();
}

// ------------------------------------------------------------------
// Панели графиков
// ------------------------------------------------------------------

void DecayScene::drawPlots(QPainter* painter) const {
    auto* model = qobject_cast<models::DecayModel*>(m_model);
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

    // ---- 1. Число ядер: теория, опыт и коридор √N ----
    {
        PlotSeries corridorUpper;
        corridorUpper.points.clear();
        PlotSeries corridorLower;
        const int samples = 120;
        for (int i = 0; i <= samples; ++i) {
            const double t = duration * i / double(samples);
            corridorUpper.points.append(QPointF(t, model->theoryNucleiAt(t) + model->expectedDeviationAt(t)));
            corridorLower.points.append(QPointF(t, std::max(0.0, model->theoryNucleiAt(t) -
                                                                    model->expectedDeviationAt(t))));
        }
        corridorUpper.color = style::inkSoft().lighter(120);
        corridorUpper.width = 1.0;
        corridorUpper.dashed = true;
        corridorLower.color = corridorUpper.color;
        corridorLower.width = 1.0;
        corridorLower.dashed = true;

        PlotSeries theory;
        theory.points = model->theorySeries();
        theory.color = theoryColor();
        theory.label = QStringLiteral("N₀·2^(−t/T½)");
        PlotSeries counted;
        counted.points = model->countedSeries();
        counted.color = countedColor();
        counted.label = QStringLiteral("счёт опыта");

        drawPlot(painter, QRectF(m_band.left(), top, panelWidth, height),
                 QStringLiteral("Ядра: кривая — теория, ступенька — опыт; пунктир — ±√N"), timeUnitLabel(),
                 {corridorUpper, corridorLower, theory, counted}, duration, currentT, eventT);
    }

    // ---- 2. Распавшиеся ядра N₀ − N(t) ----
    {
        PlotSeries theory;
        theory.points = model->decayedTheorySeries();
        theory.color = theoryColor();
        theory.dashed = true;
        theory.label = QStringLiteral("N₀ − N₀·2^(−t/T½)");
        PlotSeries counted;
        counted.points = model->countedDecaysSeries();
        counted.color = style::bodyC();
        counted.label = QStringLiteral("по счётчику");
        drawPlot(painter, QRectF(m_band.left() + panelWidth + kGap, top, panelWidth, height),
                 QStringLiteral("Распавшиеся ядра, штук"), timeUnitLabel(), {theory, counted}, duration, currentT,
                 eventT);
    }

    // ---- 3. Активность A = λN и показания счётчика ----
    {
        PlotSeries theory;
        theory.points = model->activitySeries();
        theory.color = style::accentWarm();
        theory.label = QStringLiteral("A = λN");
        PlotSeries counted;
        counted.points = model->countedActivitySeries();
        counted.color = style::bodyC();
        counted.label = QStringLiteral("распады/ч по опыту");
        drawPlot(painter, QRectF(m_band.left() + 2.0 * (panelWidth + kGap), top, panelWidth, height),
                 QStringLiteral("Активность: кривая — λN, ступенька — счётчик"), timeUnitLabel(), {theory, counted},
                 duration, currentT, eventT);
    }

    // ---- 4. ln N: прямая с наклоном −λ ----
    {
        PlotSeries theory;
        theory.points = model->logarithmSeries();
        theory.color = style::bodyD();
        theory.label = QStringLiteral("ln N = ln N₀ − λt");
        PlotSeries counted;
        counted.points = model->countedLogarithmSeries();
        counted.color = style::bodyA();
        counted.dashed = true;
        counted.label = QStringLiteral("показания счётчика");
        drawPlot(painter, QRectF(m_band.left() + 3.0 * (panelWidth + kGap), top, panelWidth, height),
                 QStringLiteral("ln N: наклон прямой равен −λ"), timeUnitLabel(), {theory, counted}, duration,
                 currentT, eventT);
    }
}

// ------------------------------------------------------------------
// Ручки
// ------------------------------------------------------------------

void DecayScene::positionHandles() {
    auto* model = qobject_cast<models::DecayModel*>(m_model);
    if (!model)
        return;

    const double nowY = m_strip.center().y();
    for (DragHandleItem* handle : m_handles) {
        if (!handle)
            continue;
        const HandleInfo info = handle->info();
        if (info.id == QLatin1String("nuclei")) {
            // Уровень N₀ на вертикальной оси: по нему тянут число ядер.
            handle->setAxis(DragHandleItem::Axis::Vertical);
            handle->setAnchor(QPointF(m_strip.left() - kNucleiAxisWidth * 0.5, handleScenePosition(info)));
        } else if (info.id == QLatin1String("share")) {
            // Уровень d·N₀: ручка двигает заданную долю вверх и вниз.
            handle->setAxis(DragHandleItem::Axis::Vertical);
            handle->setAnchor(QPointF(m_strip.right() + 14.0, handleScenePosition(info)));
        } else if (info.id == QLatin1String("half")) {
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

double DecayScene::handleScenePosition(const HandleInfo& info) const {
    auto* model = qobject_cast<models::DecayModel*>(m_model);
    if (!model)
        return LabScene::handleScenePosition(info);

    const double total = std::max(1e-6, model->duration());
    if (info.id == QLatin1String("nuclei"))
        return std::clamp(sceneYForNuclei(info.value), m_stripTopY - 10.0, m_stripBottomY);
    if (info.id == QLatin1String("share")) {
        const double level = info.value / 100.0 * model->initialNuclei();
        return std::clamp(sceneYForNuclei(level), m_stripTopY - 10.0, m_stripBottomY);
    }
    if (info.id == QLatin1String("half")) {
        // Отметка T½ на размерной линии; если период полураспада больше
        // опыта, ручка становится у конца шкалы и упирается в неё.
        return std::clamp(sceneXForTime(info.value), m_startPx, m_endPx);
    }
    if (info.id == QLatin1String("window")) {
        return std::clamp(sceneXForTime(std::min(info.value, total)), m_startPx, m_endPx);
    }
    return LabScene::handleScenePosition(info);
}

double DecayScene::mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const {
    auto* model = qobject_cast<models::DecayModel*>(m_model);
    if (!model)
        return LabScene::mapHandlePosition(info, scenePos);

    if (info.id == QLatin1String("nuclei")) {
        // Высота над нулевой линией — число ядер N₀.
        const double lift = std::clamp(m_stripBottomY - scenePos.y(), 1.0, m_stripBottomY - m_stripTopY + 10.0);
        return lift / std::max(1e-9, m_pixelsPerNucleus);
    }
    if (info.id == QLatin1String("share")) {
        const double level = std::clamp(nucleiForSceneY(scenePos.y()), 0.0, model->initialNuclei());
        return 100.0 * level / std::max(1.0, model->initialNuclei());
    }
    if (info.id == QLatin1String("half") || info.id == QLatin1String("window"))
        return std::max(0.0, timeForSceneX(scenePos.x()));
    return LabScene::mapHandlePosition(info, scenePos);
}

}  // namespace lab::scenes
