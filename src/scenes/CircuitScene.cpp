#include "CircuitScene.h"

#include "circuit/MnaSolver.h"
#include "models/CircuitModel.h"
#include "scenes/SchemePrimitives.h"
#include "scenes/Style.h"

#include <QFont>
#include <QPainter>
#include <QFontMetricsF>
#include <QGraphicsSceneMouseEvent>
#include <QHash>
#include <QSet>
#include <QPainterPath>
#include <QtMath>

#include "utils/MathText.h"

#include <algorithm>
#include <cmath>

namespace lab::scenes {
namespace {

constexpr double kBoardMargin = 14.0;  // поле вокруг сетки внутри рамки чертежа
constexpr double kCarrierSpeed = 1.6;  // клеток в секунду при токе порядка шкалы

/** Цвета элементов: один и тот же элемент на любой схеме выглядит одинаково. */
QColor partColor(circuit::PartType type) {
    switch (type) {
    case circuit::PartType::Source:    return style::bodyB();
    case circuit::PartType::Resistor:  return style::ink();
    case circuit::PartType::Capacitor: return style::bodyC();
    case circuit::PartType::Inductor:  return style::bodyD();
    case circuit::PartType::Switch:    return style::accentWarm();
    case circuit::PartType::Ammeter:   return style::accent();
    case circuit::PartType::Voltmeter: return style::accent();
    }
    return style::ink();
}

/** Сколько носителев рисовать на участке: зависит от тока, но не от длины. */
int carrierCountFor(double current, double scale) {
    if (scale <= 0.0 || !std::isfinite(current))
        return 0;
    const double ratio = std::abs(current) / scale;
    if (ratio < 0.02)
        return 0;
    return std::clamp(int(2.0 + 4.0 * std::sqrt(ratio)), 2, 6);
}

/** Скорость носителей: медленнее тока в единицу раза, чтобы движение читалось. */
double carrierDrift(double current, double scale, double t) {
    if (scale <= 0.0 || !std::isfinite(current))
        return 0.0;
    const double direction = current >= 0.0 ? 1.0 : -1.0;
    return direction * kCarrierSpeed * t * std::cbrt(std::abs(current) / scale);
}

}  // namespace

// ------------------------------------------------------------------
// Построение и геометрия
// ------------------------------------------------------------------

CircuitScene::CircuitScene(Simulation* model, QObject* parent) : LabScene(model, parent) {
    setSceneRect(0, 0, 1200, 800);
}

models::CircuitModel* CircuitScene::circuitModel() const {
    return qobject_cast<models::CircuitModel*>(m_model);
}

void CircuitScene::reconfigure(const QRectF& area) {
    requestViewArea(area);
    auto* model = circuitModel();
    if (!model)
        return;

    // Горизонтальная шкала — время: по ней тянут ручки t₀ и T и по ней же
    // перематывают опыт нижней осью.
    const double total = std::max(1e-6, model->duration());
    setRanges(area, 0.0, total, 0.0, std::max(1.0, model->sourceEmf()));

    Sheet sheet;
    sheet.columns = std::max(1, model->gridColumns());
    sheet.rows = std::max(1, model->gridRows());

    const double schemeHeight = std::clamp(area.height() * 0.44, 210.0, 340.0);
    sheet.board = QRectF(area.left() + 12.0, area.top() + 10.0, std::max(320.0, area.width() - 24.0),
                         schemeHeight);
    const double plotsTop = sheet.board.bottom() + 14.0;
    sheet.panelsBand = QRectF(sheet.board.left(), plotsTop, sheet.board.width(),
                              std::max(0.0, timeAxisRect().top() - 10.0 - plotsTop));

    // Шаг сетки выбирают так, чтобы лист целиком лёг в рамку: и по ширине,
    // и по высоте. Клетки всегда квадратные — иначе углы проводов перестают
    // быть прямыми, а это главный смысл чертежа на сетке.
    const double availableWidth = sheet.board.width() - 2.0 * kBoardMargin - 210.0;
    const double availableHeight = sheet.board.height() - 2.0 * kBoardMargin;
    sheet.cell = std::max(8.0, std::min(availableWidth / double(sheet.columns),
                                         availableHeight / double(sheet.rows)));
    const QPointF boardCenter(sheet.board.left() + (sheet.board.width() - 210.0) * 0.5,
                              sheet.board.center().y());
    sheet.origin = QPointF(boardCenter.x() - sheet.columns * sheet.cell * 0.5,
                           boardCenter.y() - sheet.rows * sheet.cell * 0.5);
    // Правая колонка показаний — между сеткой и правым краем рамки.
    const double gridRight = sheet.origin.x() + sheet.columns * sheet.cell + sheet.cell;
    sheet.readouts = QRectF(std::max(gridRight + 12.0, sheet.board.right() - 200.0),
                            sheet.board.top() + 12.0, 188.0, sheet.board.height() - 24.0);
    m_sheet = sheet;

    positionHandles();
}

QPointF CircuitScene::cellCenter(const circuit::GridPoint& point) const {
    return QPointF(m_sheet.origin.x() + (point.column + 0.5) * m_sheet.cell,
                   m_sheet.origin.y() + (point.row + 0.5) * m_sheet.cell);
}

circuit::GridPoint CircuitScene::cellAt(const QPointF& pos) const {
    circuit::GridPoint point;
    point.column = int(std::floor((pos.x() - m_sheet.origin.x()) / m_sheet.cell));
    point.row = int(std::floor((pos.y() - m_sheet.origin.y()) / m_sheet.cell));
    return point;
}

QVector<QPointF> CircuitScene::polyline(const QVector<circuit::GridPoint>& path) const {
    QVector<QPointF> points;
    points.reserve(path.size());
    for (const circuit::GridPoint& point : path)
        points.push_back(cellCenter(point));
    return points;
}

QPointF CircuitScene::symbolCenter(const circuit::Part& part) const {
    return cellCenter(circuit::CircuitNet::symbolPoint(part));
}

circuit::GridPoint CircuitScene::netCell(int column, int row) {
    circuit::GridPoint point;
    point.column = column;
    point.row = row;
    return point;
}

QPointF CircuitScene::handleAnchorFor(const HandleInfo& info) const {
    return valueHandleAnchor(info);
}

// ------------------------------------------------------------------
// Отрисовка листа
// ------------------------------------------------------------------

void CircuitScene::drawGrid(QPainter* painter) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(QPen(style::grid(), 1.0));
    for (int column = 0; column <= m_sheet.columns; ++column) {
        const double x = m_sheet.origin.x() + column * m_sheet.cell;
        painter->drawLine(QPointF(x, m_sheet.origin.y()),
                         QPointF(x, m_sheet.origin.y() + m_sheet.rows * m_sheet.cell));
    }
    for (int row = 0; row <= m_sheet.rows; ++row) {
        const double y = m_sheet.origin.y() + row * m_sheet.cell;
        painter->drawLine(QPointF(m_sheet.origin.x(), y),
                         QPointF(m_sheet.origin.x() + m_sheet.columns * m_sheet.cell, y));
    }
    painter->restore();
}

void CircuitScene::drawWires(QPainter* painter, const circuit::Solution& solution) const {
    const auto* model = circuitModel();
    if (!model)
        return;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    QPen wirePen(style::ink(), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPen livePen(style::accentWarm(), 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

    // Участки, по которым течёт ток, подчёркивают провод тем же цветом, что и
    // носители: видно, какая часть схемы рабочая.
    for (const circuit::Wire& wire : model->net().wires()) {
        bool live = false;
        for (const circuit::Solution::Segment& segment : solution.segments) {
            if (segment.wireId == wire.id && segment.determined && std::abs(segment.current) > 1e-9)
                live = true;
        }
        const QVector<QPointF> path = polyline(wire.path);
        if (path.size() < 2)
            continue;
        QPainterPath line;
        line.moveTo(path.first());
        for (int i = 1; i < path.size(); ++i)
            line.lineTo(path.at(i));
        painter->setPen(live ? livePen : wirePen);
        painter->drawPath(line);
    }
    painter->restore();
}

void CircuitScene::drawPartSymbol(QPainter* painter, const circuit::Part& part, const QColor& ink,
                                  bool closed) const {
    const QPointF first = cellCenter(circuit::CircuitNet::terminalsOf(part).first());
    const QPointF second = cellCenter(circuit::CircuitNet::terminalsOf(part).last());
    const QPointF middle = (first + second) * 0.5;
    const bool horizontal = part.horizontal;
    m_leverClosed = closed;
    const double span = m_sheet.cell * 2.0;
    const double body = std::min(m_sheet.cell * 1.2, span * 0.5);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(ink, 2.2));
    painter->setBrush(Qt::NoBrush);

    // Подводки от клемм к телу символа.
    const QPointF leadIn = horizontal ? QPointF(body * 0.5, 0.0) : QPointF(0.0, body * 0.5);
    painter->drawLine(first, middle - leadIn);
    painter->drawLine(middle + leadIn, second);

    auto along = [&](double t) {
        return horizontal ? QPointF(t, 0.0) : QPointF(0.0, t);
    };
    auto across = [&](double t) {
        return horizontal ? QPointF(0.0, t) : QPointF(t, 0.0);
    };

    switch (part.type) {
    case circuit::PartType::Resistor: {
        // Прямоугольник резистора — как на учебных схемах.
        const QRectF bodyRect = horizontal ? QRectF(middle - QPointF(body * 0.5, m_sheet.cell * 0.22),
                                                   QPointF(body, m_sheet.cell * 0.44))
                                          : QRectF(middle - QPointF(m_sheet.cell * 0.22, body * 0.5),
                                                   QPointF(m_sheet.cell * 0.44, body));
        painter->setBrush(QColor(255, 255, 255, 235));
        painter->drawRoundedRect(bodyRect, 2.5, 2.5);
        break;
    }
    case circuit::PartType::Capacitor: {
        const double gap = m_sheet.cell * 0.16;
        const double plate = m_sheet.cell * 0.52;
        painter->drawLine(middle - along(gap) - across(plate * 0.5), middle - along(gap) + across(plate * 0.5));
        painter->drawLine(middle + along(gap) - across(plate * 0.5), middle + along(gap) + across(plate * 0.5));
        break;
    }
    case circuit::PartType::Inductor: {
        // Четыре полувитка по пути элемента.
        const int coils = 4;
        const double start = -body * 0.5;
        const double step = body / coils;
        for (int i = 0; i < coils; ++i) {
            const QPointF center = middle + along(start + step * (i + 0.5));
            const QRectF arcRect = horizontal
                                       ? QRectF(center - QPointF(step * 0.5, m_sheet.cell * 0.26),
                                                QPointF(step, m_sheet.cell * 0.52))
                                       : QRectF(center - QPointF(m_sheet.cell * 0.26, step * 0.5),
                                                QPointF(m_sheet.cell * 0.52, step));
            if (horizontal)
                painter->drawArc(arcRect, 0, 180 * 16);
            else
                painter->drawArc(arcRect, 90 * 16, 180 * 16);
        }
        break;
    }
    case circuit::PartType::Source: {
        // Длинная пластина — «плюс», короткая и толстая — «минус».
        const double plate = m_sheet.cell * 0.56;
        painter->setPen(QPen(ink, 2.2));
        painter->drawLine(middle - along(m_sheet.cell * 0.12) - across(plate * 0.5),
                         middle - along(m_sheet.cell * 0.12) + across(plate * 0.5));
        painter->setPen(QPen(ink, 4.6));
        painter->drawLine(middle + along(m_sheet.cell * 0.12) - across(plate * 0.24),
                         middle + along(m_sheet.cell * 0.12) + across(plate * 0.24));
        break;
    }
    case circuit::PartType::Switch: {
        // Ключ: две точки контактов и рычаг. До t₀ рычаг поднят, после —
        // лежит на контакте: замкнувшийся ключ видно по чертежу.
        const double contact = body * 0.42;
        painter->setBrush(ink);
        for (double sign : {-1.0, 1.0}) {
            const QPointF pin = middle + along(sign * contact);
            painter->drawEllipse(pin, 2.4, 2.4);
        }
        const QPointF leverEnd = m_leverClosed ? middle + along(contact)
                                               : middle + along(contact) + across(-m_sheet.cell * 0.36);
        painter->drawLine(middle - along(contact), leverEnd);
        break;
    }
    case circuit::PartType::Ammeter:
    case circuit::PartType::Voltmeter: {
        const double radius = std::max(9.0, m_sheet.cell * 0.42);
        painter->setBrush(QColor(255, 255, 255, 240));
        painter->drawEllipse(middle, radius, radius);
        break;
    }
    }
    painter->restore();
}

void CircuitScene::drawParts(QPainter* painter, const circuit::Solution& solution, double t) const {
    auto* model = circuitModel();
    if (!model)
        return;
    const int selected = model->selectedPart();
    const QSet<int> switches = [&]() {
        QSet<int> closed;
        for (int id : model->closedSwitchesAt(t))
            closed.insert(id);
        return closed;
    }();

    labels::Planner planner(m_sheet.board);
    for (const circuit::Part& part : model->net().parts()) {
        const bool selectedPart = part.id == selected;
        const circuit::Solution::Value* value = solution.value(part.id);
        const bool conducting = value && value->conducting;
        const bool closed = switches.contains(part.id);
        drawPartSymbol(painter, part, conducting ? partColor(part.type) : style::inkSoft(), closed);

        if (selectedPart) {
            const QPointF center = symbolCenter(part);
            painter->save();
            painter->setPen(QPen(style::accent(), 1.6, Qt::DashLine));
            painter->setBrush(Qt::NoBrush);
            const double half = m_sheet.cell * 0.9;
            painter->drawRect(QRectF(center - QPointF(half, half), QPointF(2 * half, 2 * half)));
            painter->restore();
        }

        // Приборы показывают свои значения прямо у символа: амперметр — ток
        // ветви, вольтметр — напряжение между своими клеммами.
        const QPointF center = symbolCenter(part);
        const QPointF offset = part.horizontal ? QPointF(0.0, -m_sheet.cell * 0.78)
                                               : QPointF(m_sheet.cell * 0.86, 0.0);
        if (part.type == circuit::PartType::Ammeter && conducting)
            planner.add(QStringLiteral("A%1").arg(part.id), center + offset,
                        models::CircuitModel::currentText(value->current), partColor(part.type));
        else if (part.type == circuit::PartType::Voltmeter && conducting)
            planner.add(QStringLiteral("V%1").arg(part.id), center + offset,
                        models::CircuitModel::voltageText(value->voltage), partColor(part.type));
        else if (part.type != circuit::PartType::Switch)
            planner.add(QStringLiteral("p%1").arg(part.id), center + offset,
                        QStringLiteral("%1 = %2").arg(part.name, model->partValueText(part.id)),
                        conducting ? partColor(part.type) : style::inkSoft());

        // Ток ветви подписывают у символа только когда он есть: ноль на каждой
        // ветви превратил бы чертёж в таблицу.
        if (conducting && part.type != circuit::PartType::Ammeter && part.type != circuit::PartType::Voltmeter) {
            const QPointF currentOffset = part.horizontal ? QPointF(0.0, m_sheet.cell * 0.86)
                                                         : QPointF(-m_sheet.cell * 0.92, 0.0);
            planner.addFirstFree(QStringLiteral("i%1").arg(part.id),
                                 {center + currentOffset, center - offset, center - currentOffset},
                                 QStringLiteral("I%1 = %2").arg(
                                     part.name.mid(1), models::CircuitModel::currentText(value->current)),
                                 style::inkSoft());
        }
    }

    // Потенциалы узлов: подписывают точки, где сходятся проводники. Земля —
    // ноль, и её тоже показывают, иначе «отсчёт от нуля» не читается.
    const int ground = model->net().groundNode();
    QHash<int, QPointF> firstPoint;
    for (const circuit::Part& part : model->net().parts()) {
        const auto refs = model->branchTerminals(part.id);
        for (const auto& ref : refs) {
            if (ref.node >= 0 && !firstPoint.contains(ref.node))
                firstPoint.insert(ref.node, cellCenter(ref.point));
        }
    }
    for (int node = 0; node < solution.nodePotentials.size(); ++node) {
        if (!firstPoint.contains(node))
            continue;
        const double potential = solution.nodePotentials.at(size_t(node));
        const QString text = node == ground ? QStringLiteral("φ = 0")
                                           : QStringLiteral("φ = %1")
                                                 .arg(models::CircuitModel::voltageText(potential));
        planner.addFirstFree(QStringLiteral("n%1").arg(node),
                             {firstPoint.value(node) + QPointF(0.0, -m_sheet.cell * 0.9),
                              firstPoint.value(node) + QPointF(m_sheet.cell * 1.1, 0.0),
                              firstPoint.value(node) + QPointF(0.0, m_sheet.cell * 0.95)},
                             text, style::accent());
    }
    labels::drawAll(painter, planner.items());
}

void CircuitScene::drawCarriers(QPainter* painter, const circuit::Solution& solution, double t) const {
    double scale = 0.0;
    for (const circuit::Solution::Segment& segment : solution.segments)
        scale = std::max(scale, std::abs(segment.current));
    if (scale <= 0.0)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    for (const circuit::Solution::Segment& segment : solution.segments) {
        if (!segment.determined || segment.path.size() < 2)
            continue;
        const QVector<QPointF> path = polyline(segment.path);
        // Длина пути в пикселях: по ней раскладывают точки равномерно.
        QVector<double> lengths;
        double total = 0.0;
        for (int i = 1; i < path.size(); ++i) {
            total += QPointF(path.at(i) - path.at(i - 1)).manhattanLength();
            lengths.push_back(total);
        }
        if (total <= 1.0)
            continue;
        const int count = carrierCountFor(segment.current, scale);
        const double drift = carrierDrift(segment.current, scale, t) * m_sheet.cell;
        for (int i = 0; i < count; ++i) {
            double u = (drift + total * i / double(count));
            u = std::fmod(u, total);
            if (u < 0.0)
                u += total;
            int index = int(std::lower_bound(lengths.begin(), lengths.end(), u) - lengths.begin());
            index = std::min(index, int(lengths.size()) - 1);
            const double from = index == 0 ? 0.0 : lengths.at(size_t(index - 1));
            const double segmentLength = lengths.at(size_t(index)) - from;
            const double local = segmentLength > 1e-6 ? (u - from) / segmentLength : 0.0;
            const QPointF position = path.at(size_t(index)) +
                                     (path.at(size_t(index + 1)) - path.at(size_t(index))) * local;
            painter->setPen(Qt::NoPen);
            painter->setBrush(style::accentWarm());
            painter->drawEllipse(position, 3.1, 3.1);
        }
    }
    painter->restore();
}

void CircuitScene::drawTerminals(QPainter* painter) const {
    auto* model = circuitModel();
    if (!model)
        return;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const auto terminals = model->net().terminalPoints();
    for (const circuit::GridPoint& point : terminals) {
        const QPointF center = cellCenter(point);
        painter->setBrush(style::ink());
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(center, 2.6, 2.6);
    }
    // Незамкнутый клик соединения: точку помечают пунктиром — видно, откуда
    // начато провод и чего ждёт схема.
    if (model->hasPendingTerminal()) {
        const QPointF center = cellCenter(model->pendingTerminal());
        painter->setPen(QPen(style::accent(), 1.6, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(center, m_sheet.cell * 0.34, m_sheet.cell * 0.34);
    }
    // Свободные клеммы подсказывают, куда стоит тянуть следующий провод.
    painter->setPen(QPen(style::inkSoft(), 1.2, Qt::DotLine));
    painter->setBrush(Qt::NoBrush);
    for (const circuit::GridPoint& point : model->net().freeTerminals()) {
        const QPointF center = cellCenter(point);
        painter->drawEllipse(center, 5.2, 5.2);
    }
    painter->restore();
}

// ------------------------------------------------------------------
// Правая колонка: показания
// ------------------------------------------------------------------

QVector<QPair<QString, QString>> CircuitScene::readoutLines(double t) const {
    auto* model = circuitModel();
    QVector<QPair<QString, QString>> lines;
    if (!model)
        return lines;

    const auto push = [&lines](const QString& label, const QString& value) {
        lines.push_back({label, value});
    };
    push(QStringLiteral("Ток цепи I"), models::CircuitModel::currentText(model->sourceCurrentAt(t)));
    push(QStringLiteral("U на зажимах"), models::CircuitModel::voltageText(model->terminalVoltageAt(t)));
    const double resistance = model->equivalentResistanceAt(t);
    push(QStringLiteral("R всей цепи"),
         std::isfinite(resistance) ? models::CircuitModel::resistanceText(resistance) : QStringLiteral("—"));
    const int selected = model->selectedPart();
    if (selected > 0) {
        push(QStringLiteral("I выбранной ветви"), models::CircuitModel::currentText(model->currentOf(selected, t)));
        push(QStringLiteral("U выбранной ветви"),
             models::CircuitModel::voltageText(model->voltageOf(selected, t)));
    }
    if (model->hasTransient()) {
        const double tau = model->timeConstantOf(model->net().parts().isEmpty()
                                                     ? 0
                                                     : [&model]() {
                                                           for (const circuit::Part& p : model->net().parts())
                                                               if (p.type == circuit::PartType::Capacitor ||
                                                                   p.type == circuit::PartType::Inductor)
                                                                   return p.id;
                                                           return model->net().parts().first().id;
                                                       }());
        push(QStringLiteral("Постоянная τ"), std::isfinite(tau) ? utils::formatNumber(tau, 3) + QStringLiteral(" с")
                                                                : QStringLiteral("—"));
        const double end = model->transientEndTime();
        push(QStringLiteral("Процесс до"),
             std::isfinite(end) ? utils::formatNumber(end, 2) + QStringLiteral(" с") : QStringLiteral("—"));
    }
    push(QStringLiteral("Мощность источника"),
         utils::formatNumber(model->deliveredPowerAt(t), 2) + QStringLiteral(" Вт"));
    if (model->hasTransient())
        push(QStringLiteral("Энергия поля"),
             utils::formatNumber(model->storedEnergyAt(t), 3) + QStringLiteral(" Дж"));
    return lines;
}

void CircuitScene::drawReadouts(QPainter* painter) const {
    auto* model = circuitModel();
    if (!model)
        return;
    const QRectF box = m_sheet.readouts;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(style::grid(), 1.0));
    painter->setBrush(QColor(255, 255, 255, 210));
    painter->drawRoundedRect(box, 6, 6);

    QFont font = painter->font();
    font.setPointSizeF(std::max(8.0, m_sheet.cell * 0.42));
    painter->setFont(font);
    const QFontMetricsF metrics(font);
    double y = box.top() + metrics.ascent() + 8.0;
    const double valueX = box.right() - 8.0;
    const double labelX = box.left() + 10.0;
    for (const auto& line : readoutLines(model->time())) {
        if (y > box.bottom() - 4.0)
            break;
        painter->setPen(style::inkSoft());
        painter->drawText(QPointF(labelX, y), line.first);
        painter->setPen(style::ink());
        painter->drawText(QRectF(labelX, y - metrics.ascent(), valueX - labelX, metrics.height()),
                          Qt::AlignRight | Qt::AlignVCenter, line.second);
        y += metrics.height() * 1.18;
    }
    painter->restore();
}

// ------------------------------------------------------------------
// Векторы и подписи
// ------------------------------------------------------------------

QVector<vectors::Arrow> CircuitScene::vectorsFor(double t) const {
    QVector<vectors::Arrow> arrows;
    auto* model = circuitModel();
    if (!model || !m_model->showsForces())
        return arrows;
    const circuit::Solution solution = model->solutionAt(t);
    double scale = 0.0;
    for (const circuit::Solution::Value& value : solution.values)
        scale = std::max(scale, std::abs(value.current));
    if (scale <= 0.0)
        return arrows;

    for (const circuit::Part& part : model->net().parts()) {
        const circuit::Solution::Value* value = solution.value(part.id);
        if (!value || !value->conducting)
            continue;
        const QPointF first = cellCenter(circuit::CircuitNet::terminalsOf(part).first());
        const QPointF second = cellCenter(circuit::CircuitNet::terminalsOf(part).last());
        // Ток в ветви идёт от первой клеммы ко второй; стрелку ставят чуть в
        // стороне от символа, чтобы он оставался читаемым.
        const QPointF side = part.horizontal ? QPointF(0.0, m_sheet.cell * 0.62)
                                             : QPointF(m_sheet.cell * 0.62, 0.0);
        vectors::Arrow arrow;
        arrow.from = first + side;
        arrow.to = second + side;
        arrow.kind = vectors::Kind::Current;
        arrow.symbol = QStringLiteral("I");
        arrow.width = 1.8;
        if (value->current < 0.0)
            std::swap(arrow.from, arrow.to);
        arrows.push_back(arrow);
    }
    return arrows;
}

// ------------------------------------------------------------------
// Основной кадр
// ------------------------------------------------------------------

void CircuitScene::paintScheme(QPainter* painter) {
    auto* model = circuitModel();
    if (!model)
        return;
    const double t = m_model->time();
    const circuit::Solution solution = model->solutionAt(t);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(style::ink(), 1.4));
    painter->setBrush(style::paper());
    painter->drawRoundedRect(m_sheet.board, 8, 8);
    painter->restore();

    drawGrid(painter);
    drawWires(painter, solution);
    drawParts(painter, solution, t);
    drawTerminals(painter);
    drawCarriers(painter, solution, t);

    painter->save();
    drawVectors(painter, vectorsFor(t));
    painter->restore();

    drawReadouts(painter);

    // Подпись инструментов: что делает текущий пункт палитры и чего не хватает.
    painter->save();
    QFont font = painter->font();
    font.setPointSizeF(9.0);
    painter->setFont(font);
    painter->setPen(style::inkSoft());
    painter->drawText(QPointF(m_sheet.board.left() + 12.0, m_sheet.board.bottom() - 8.0), model->toolHint());
    painter->restore();

    drawPlots(painter);
}

// ------------------------------------------------------------------
// Четыре панели
// ------------------------------------------------------------------

void CircuitScene::drawPlots(QPainter* painter) const {
    auto* model = circuitModel();
    if (!model)
        return;
    const QRectF band = m_sheet.panelsBand;
    if (band.height() < 90.0 || band.width() < 280.0)
        return;

    constexpr double kPanelGap = 10.0;
    const double panelWidth = (band.width() - 3.0 * kPanelGap) / 4.0;
    const double height = std::min(band.height(), 216.0);
    const double top = band.top() + std::max(0.0, band.height() - height) * 0.5;
    const double duration = std::max(1e-6, m_model->duration());
    const double currentT = m_model->time();
    const double eventT = m_model->eventTime();

    const int selected = model->selectedPart();

    // ---- 1. Токи: источник и выбранная ветвь ----
    {
        PlotSeries total;
        total.points = model->sourceCurrentSeries();
        total.color = style::bodyB();
        total.label = QStringLiteral("I цепи");
        QVector<PlotSeries> series{total};
        if (selected > 0) {
            PlotSeries branch;
            branch.points = model->branchCurrentSeries(selected);
            branch.color = style::bodyC();
            branch.dashed = true;
            branch.label = QStringLiteral("I ветви");
            series.push_back(branch);
        }
        drawPlot(painter, QRectF(band.left(), top, panelWidth, height),
                 QStringLiteral("Токи, А"), timeUnitLabel(), series, duration, currentT, eventT);
    }

    // ---- 2. Напряжения: зажимы источника и ветвь ----
    {
        PlotSeries terminals;
        terminals.points = model->terminalVoltageSeries();
        terminals.color = style::bodyB();
        terminals.label = QStringLiteral("U зажимов");
        QVector<PlotSeries> series{terminals};
        if (selected > 0) {
            PlotSeries branch;
            branch.points = model->branchVoltageSeries(selected);
            branch.color = style::bodyD();
            branch.dashed = true;
            branch.label = QStringLiteral("U ветви");
            series.push_back(branch);
        }
        drawPlot(painter, QRectF(band.left() + panelWidth + kPanelGap, top, panelWidth, height),
                 QStringLiteral("Напряжения, В"), timeUnitLabel(), series, duration, currentT, eventT);
    }

    // ---- 3. Энергия поля и мощности ----
    {
        PlotSeries energy;
        energy.points = model->energySeries();
        energy.color = style::bodyD();
        energy.label = QStringLiteral("W(t)");
        PlotSeries delivered;
        delivered.points = model->sourcePowerSeries();
        delivered.color = style::bodyB();
        delivered.dashed = true;
        delivered.label = QStringLiteral("P источника");
        PlotSeries dissipated;
        dissipated.points = model->dissipatedPowerSeries();
        dissipated.color = style::bodyC();
        dissipated.label = QStringLiteral("P на резисторах");
        drawPlot(painter, QRectF(band.left() + 2.0 * (panelWidth + kPanelGap), top, panelWidth, height),
                 QStringLiteral("Энергия и мощности"), timeUnitLabel(), {energy, delivered, dissipated},
                 duration, currentT, eventT);
    }

    // ---- 4. Вольт-амперная характеристика источника ----
    {
        PlotSeries characteristic;
        characteristic.points = model->terminalVersusCurrent();
        characteristic.color = style::bodyB();
        characteristic.label = QStringLiteral("U(I)");
        const QPointF current(model->sourceCurrentAt(currentT), model->terminalVoltageAt(currentT));
        drawGraph(painter, QRectF(band.left() + 3.0 * (panelWidth + kPanelGap), top, panelWidth, height),
                  QStringLiteral("U от I: наклон даёт −r"), QStringLiteral("I, А"), QStringLiteral("U, В"),
                  {characteristic}, current, false);
    }
}

// ------------------------------------------------------------------
// Мышь: схема собирается кликами
// ------------------------------------------------------------------

int CircuitScene::partAt(const circuit::GridPoint& point) const {
    auto* model = circuitModel();
    if (!model)
        return -1;
    for (const circuit::Part& part : model->net().parts()) {
        const auto terminals = circuit::CircuitNet::terminalsOf(part);
        if (terminals.first() == point || terminals.last() == point ||
            circuit::CircuitNet::symbolPoint(part) == point)
            return part.id;
    }
    return -1;
}

int CircuitScene::terminalAt(const circuit::GridPoint& point) const {
    auto* model = circuitModel();
    if (!model)
        return -1;
    const int id = partAt(point);
    if (id <= 0)
        return -1;
    const circuit::Part* part = model->net().findPart(id);
    if (!part)
        return -1;
    const auto terminals = circuit::CircuitNet::terminalsOf(*part);
    return terminals.first() == point || terminals.last() == point ? id : -1;
}

void CircuitScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    auto* model = circuitModel();
    if (!model || !event) {
        LabScene::mousePressEvent(event);
        return;
    }

    // Ось времени оставаем базовой clase: перемотка работает как везде.
    if (timeAxisRect().contains(event->scenePos())) {
        LabScene::mousePressEvent(event);
        return;
    }
    if (!m_sheet.board.contains(event->scenePos())) {
        LabScene::mousePressEvent(event);
        return;
    }

    const circuit::GridPoint point = cellAt(event->scenePos());
    const bool right = event->button() == Qt::RightButton;
    if (right) {
        // Правая кнопка убирает всё, что стоит в клетке: и элемент, и провод.
        if (!model->removeAt(point))
            model->cancelPendingTerminal();
        m_clickedPart = -1;
        event->accept();
        refresh();
        return;
    }

    const int hit = partAt(point);
    switch (model->tool()) {
    case models::CircuitModel::Tool::Part: {
        if (hit > 0) {
            if (m_clickedPart == hit) {
                model->rotateAt(point);   // второй клик по тому же элементу
                m_clickedPart = -1;
            } else {
                model->setSelectedPart(hit);
                m_clickedPart = hit;
            }
        } else {
            m_clickedPart = model->placeAt(point);
        }
        break;
    }
    case models::CircuitModel::Tool::Wire: {
        model->pickTerminal(point);
        m_clickedPart = -1;
        break;
    }
    case models::CircuitModel::Tool::Delete: {
        if (!model->removeAt(point))
            model->cancelPendingTerminal();
        m_clickedPart = -1;
        break;
    }
    }
    event->accept();
    refresh();
}

void CircuitScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    auto* model = circuitModel();
    if (model && m_sheet.board.contains(event->scenePos())) {
        // Двойной клик — всегда поворот: им удобно править элемент, не глядя
        // на то, какой пункт палитры выбран.
        model->rotateAt(cellAt(event->scenePos()));
        event->accept();
        refresh();
        return;
    }
    LabScene::mouseDoubleClickEvent(event);
}

// ------------------------------------------------------------------
// Ручки
// ------------------------------------------------------------------

QPointF CircuitScene::valueHandleAnchor(const HandleInfo& info) const {
    auto* model = circuitModel();
    if (!model)
        return m_sheet.board.center();

    /*
     * Ручка величин стоит над тем элементом, который она правит: ЭДС — над
     * источником, значение — над выбранным элементом. Иначе при выборе резистора
     * ручка уезжала бы к источнику и менять сопротивление было бы неудобно.
     */
    int target = -1;
    if (info.id == QLatin1String("value"))
        target = model->selectedPart();
    if (target <= 0) {
        for (const circuit::Part& part : model->net().parts()) {
            if (part.type == circuit::PartType::Source) {
                target = part.id;
                break;
            }
        }
    }
    if (target > 0) {
        const circuit::Part* part = model->net().findPart(target);
        if (part)
            return symbolCenter(*part) + QPointF(0.0, -m_sheet.cell * 1.4);
    }
    return QPointF(m_sheet.board.left() + 60.0, m_sheet.board.center().y());
}

void CircuitScene::positionHandles() {
    for (DragHandleItem* handle : m_handles) {
        if (!handle)
            continue;
        const HandleInfo info = handle->info();
        if (info.axis == QLatin1String("x")) {
            handle->setPos(toSceneX(info.value), timeAxisRect().center().y());
            continue;
        }
        const QPointF anchor = valueHandleAnchor(info);
        const double span = std::max(40.0, m_sheet.cell * 2.0);
        const double ratio = info.maxValue > info.minValue
                                 ? (info.value - info.minValue) / (info.maxValue - info.minValue)
                                 : 0.5;
        handle->setPos(anchor.x(), anchor.y() + span * (0.5 - ratio));
    }
}

double CircuitScene::mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const {
    if (info.axis == QLatin1String("x"))
        return std::clamp(toModelValue(scenePos.x()), info.minValue, info.maxValue);
    const QPointF anchor = valueHandleAnchor(info);
    const double span = std::max(40.0, m_sheet.cell * 2.0);
    const double ratio = std::clamp(0.5 - (scenePos.y() - anchor.y()) / span, 0.0, 1.0);
    return info.minValue + ratio * (info.maxValue - info.minValue);
}

void CircuitScene::drawVectors(QPainter* painter, const QVector<vectors::Arrow>& arrows) const {
    for (const vectors::Arrow& arrow : arrows)
        vectors::drawArrow(painter, arrow);
}

}  // namespace lab::scenes
