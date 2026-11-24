#include "circuit/CircuitNet.h"

#include <algorithm>
#include <limits>

namespace lab::circuit {
namespace {

/** Шаг от точки провода к соседней точке того же провода. */
struct Step {
    int column = 0;
    int row = 0;

    bool operator==(const Step& other) const {
        return column == other.column && row == other.row;
    }
};

Step stepBetween(const GridPoint& from, const GridPoint& to) {
    Step step;
    step.column = to.column == from.column ? 0 : (to.column > from.column ? 1 : -1);
    step.row = to.row == from.row ? 0 : (to.row > from.row ? 1 : -1);
    return step;
}

/** Шаги к соседним точкам провода; у конца провода сосед один. */
QVector<Step> neighboursAt(const Wire& wire, int index) {
    QVector<Step> result;
    if (index > 0)
        result.push_back(stepBetween(wire.path.at(index), wire.path.at(index - 1)));
    if (index + 1 < wire.path.size())
        result.push_back(stepBetween(wire.path.at(index), wire.path.at(index + 1)));
    return result;
}

/**
 * Провод идёт через точку насквозь: соседи лежат по разные стороны от неё, на
 * одной прямой. Шаг к одному соседу тогда ровно противоположен шагу к другому;
 * равенство шагов означало бы, что провод повернул.
 */
bool passesStraightAt(const Wire& wire, int index, bool& horizontal) {
    const QVector<Step> neighbours = neighboursAt(wire, index);
    if (neighbours.size() != 2)
        return false;
    const Step& before = neighbours.first();
    const Step& after = neighbours.last();
    if (before.column != -after.column || before.row != -after.row)
        return false;
    horizontal = before.row == 0;
    return true;
}

/**
 * Обрывается ли провод в точке: конец или поворот. Такой провод соединяется
 * со всем, что стоит в этой же точке: его линия здесь кончается или меняет
 * направление, и «случайное» пересечение прочесть нельзя.
 */
bool hasStubAt(const Wire& wire, int index) {
    bool horizontal = false;
    return !passesStraightAt(wire, index, horizontal);
}

/*
 * Правило соединения проводов в общей точке — учебное соглашение чертежа:
 *  - конец провода или его излом — это вывод: он соединяется со всем, что
 *    попало в ту же точку;
 *  - два провода, идущие через точку каждый насквозь, соединены только
 *    тогда, когда идут по одной прямой; перпендикулярное пересечение «без
 *    пайки» соединения не даёт.
 * Поэтому узел на схеме — всегда конец, излом или Т-стык, а не любой пиксель
 * пересечения линий.
 */
bool wiresJoinAt(const Wire& left, int leftIndex, const Wire& right, int rightIndex) {
    if (hasStubAt(left, leftIndex) || hasStubAt(right, rightIndex))
        return true;
    bool leftHorizontal = false;
    bool rightHorizontal = false;
    if (!passesStraightAt(left, leftIndex, leftHorizontal) || !passesStraightAt(right, rightIndex, rightHorizontal))
        return false;
    return leftHorizontal == rightHorizontal;
}

/**
 * Разворачивает концы ломаной в путь по шагам сетки. Возвращает false, если
 * соседние концы не лежат на одной горизонтальной или вертикальной линии.
 */
bool expandPath(const QVector<GridPoint>& ends, QVector<GridPoint>& out) {
    out.clear();
    if (ends.size() < 2)
        return false;
    out.push_back(ends.first());
    for (int i = 1; i < ends.size(); ++i) {
        const GridPoint from = ends.at(i - 1);
        const GridPoint to = ends.at(i);
        const int dx = to.column - from.column;
        const int dy = to.row - from.row;
        if (dx != 0 && dy != 0)
            return false;
        if (dx == 0 && dy == 0)
            return false;
        const int sx = dx == 0 ? 0 : (dx > 0 ? 1 : -1);
        const int sy = dy == 0 ? 0 : (dy > 0 ? 1 : -1);
        const int steps = std::abs(dx) + std::abs(dy);
        for (int s = 0; s < steps; ++s) {
            GridPoint next = out.back();
            next.column += sx;
            next.row += sy;
            out.push_back(next);
        }
    }
    return true;
}

/** Одна клетка объединения: точка провода или клемма элемента. */
struct Cell {
    int wire = -1;    // индекс провода, -1 — клемма элемента
    int index = 0;    // индекс точки в пути провода
    quint32 key = 0;  // точка сетки
};

}  // namespace

// ------------------------------------------------------------------
// Обозначения элементов
// ------------------------------------------------------------------

QString shortName(PartType type) {
    switch (type) {
    case PartType::Source:    return QStringLiteral("E");
    case PartType::Resistor:  return QStringLiteral("R");
    case PartType::Capacitor: return QStringLiteral("C");
    case PartType::Inductor:  return QStringLiteral("L");
    case PartType::Switch:    return QStringLiteral("K");
    case PartType::Ammeter:   return QStringLiteral("A");
    case PartType::Voltmeter: return QStringLiteral("V");
    }
    return QStringLiteral("R");
}

QString valueUnit(PartType type) {
    switch (type) {
    case PartType::Source:    return QStringLiteral("В");
    case PartType::Resistor:  return QStringLiteral("Ом");
    case PartType::Capacitor: return QStringLiteral("мкФ");
    case PartType::Inductor:  return QStringLiteral("Гн");
    case PartType::Switch:    return QStringLiteral("мкс");
    case PartType::Ammeter:   return QStringLiteral("мкОм");
    case PartType::Voltmeter: return QStringLiteral("МОм");
    }
    return QString();
}

QString partWord(PartType type) {
    switch (type) {
    case PartType::Source:    return QStringLiteral("источник");
    case PartType::Resistor:  return QStringLiteral("резистор");
    case PartType::Capacitor: return QStringLiteral("конденсатор");
    case PartType::Inductor:  return QStringLiteral("катушка");
    case PartType::Switch:    return QStringLiteral("ключ");
    case PartType::Ammeter:   return QStringLiteral("амперметр");
    case PartType::Voltmeter: return QStringLiteral("вольтметр");
    }
    return QStringLiteral("элемент");
}

/** Родительный падеж названия: им подписывают сообщения проверки схемы. */
QString partGenitive(PartType type) {
    switch (type) {
    case PartType::Source:    return QStringLiteral("источника");
    case PartType::Resistor:  return QStringLiteral("резистора");
    case PartType::Capacitor: return QStringLiteral("конденсатора");
    case PartType::Inductor:  return QStringLiteral("катушки");
    case PartType::Switch:    return QStringLiteral("ключа");
    case PartType::Ammeter:   return QStringLiteral("амперметра");
    case PartType::Voltmeter: return QStringLiteral("вольтметра");
    }
    return QStringLiteral("элемента");
}

// ------------------------------------------------------------------
// Точки сетки
// ------------------------------------------------------------------

bool operator==(const GridPoint& left, const GridPoint& right) {
    return left.column == right.column && left.row == right.row;
}

bool operator!=(const GridPoint& left, const GridPoint& right) {
    return !(left == right);
}

quint32 pointKey(const GridPoint& point) {
    // Координаты сетки умещаются в 16 бит каждая.
    return (quint32(uint16_t(point.column)) << 16) | quint32(uint16_t(point.row));
}

// ------------------------------------------------------------------
// Сетка, элементы, провода
// ------------------------------------------------------------------

void CircuitNet::setGridSize(int columns, int rows) {
    m_columns = std::max(4, columns);
    m_rows = std::max(3, rows);
}

bool CircuitNet::insideGrid(const GridPoint& point) const {
    return point.column >= 0 && point.column <= m_columns && point.row >= 0 && point.row <= m_rows;
}

void CircuitNet::clear() {
    m_parts.clear();
    m_wires.clear();
    m_nextId = 1;
    for (int& counter : m_nameCounters)
        counter = 0;
    m_nodeOfPoint.clear();
    m_nodeCount = 0;
}

QString CircuitNet::nextName(PartType type) {
    const int index = std::clamp(int(type), 0, 7);
    m_nameCounters[index] += 1;
    return shortName(type) + QString::number(m_nameCounters[index]);
}

int CircuitNet::addPart(PartType type, const GridPoint& start, bool horizontal, double value) {
    Part part;
    part.id = m_nextId++;
    part.type = type;
    part.start = start;
    part.horizontal = horizontal;
    part.value = value;
    part.name = nextName(type);

    const QVector<GridPoint> terminals = terminalsOf(part);
    if (!insideGrid(terminals.first()) || !insideGrid(terminals.last()))
        return -1;
    // Символы двух элементов не должны стоять в одной точке.
    const GridPoint symbol = symbolPoint(part);
    for (const Part& other : m_parts) {
        if (symbolPoint(other) == symbol)
            return -1;
    }
    m_parts.push_back(part);
    return part.id;
}

Part* CircuitNet::findPart(int id) {
    for (Part& part : m_parts) {
        if (part.id == id)
            return &part;
    }
    return nullptr;
}

const Part* CircuitNet::findPart(int id) const {
    for (const Part& part : m_parts) {
        if (part.id == id)
            return &part;
    }
    return nullptr;
}

bool CircuitNet::removePart(int id) {
    for (int i = 0; i < m_parts.size(); ++i) {
        if (m_parts.at(i).id == id) {
            m_parts.removeAt(i);
            return true;
        }
    }
    return false;
}

bool CircuitNet::rotatePart(int id) {
    Part* part = findPart(id);
    if (!part)
        return false;
    Part moved = *part;
    moved.horizontal = !moved.horizontal;
    const QVector<GridPoint> terminals = terminalsOf(moved);
    if (!insideGrid(terminals.first()) || !insideGrid(terminals.last()))
        return false;
    const GridPoint symbol = symbolPoint(moved);
    for (const Part& other : m_parts) {
        if (other.id != id && symbolPoint(other) == symbol)
            return false;
    }
    part->horizontal = moved.horizontal;
    return true;
}

bool CircuitNet::setPartValue(int id, double value) {
    Part* part = findPart(id);
    if (!part || !std::isfinite(value))
        return false;
    part->value = value;
    return true;
}

int CircuitNet::partAt(const GridPoint& point) const {
    for (const Part& part : m_parts) {
        const QVector<GridPoint> terminals = terminalsOf(part);
        if (terminals.first() == point || terminals.last() == point || symbolPoint(part) == point)
            return part.id;
    }
    return -1;
}

QVector<GridPoint> CircuitNet::terminalsOf(const Part& part) {
    GridPoint finish = part.start;
    if (part.horizontal)
        finish.column += 2;
    else
        finish.row += 2;
    return {part.start, finish};
}

GridPoint CircuitNet::symbolPoint(const Part& part) {
    GridPoint point = part.start;
    if (part.horizontal)
        point.column += 1;
    else
        point.row += 1;
    return point;
}

int CircuitNet::addWire(const QVector<GridPoint>& ends) {
    QVector<GridPoint> path;
    if (!expandPath(ends, path))
        return -1;
    for (const GridPoint& point : path) {
        if (!insideGrid(point))
            return -1;
    }
    Wire wire;
    wire.id = m_nextId++;
    wire.path = path;
    m_wires.push_back(wire);
    return wire.id;
}

bool CircuitNet::removeWire(int id) {
    for (int i = 0; i < m_wires.size(); ++i) {
        if (m_wires.at(i).id == id) {
            m_wires.removeAt(i);
            return true;
        }
    }
    return false;
}

int CircuitNet::wireNear(const GridPoint& point) const {
    int best = -1;
    int bestDistance = std::numeric_limits<int>::max();
    for (const Wire& wire : m_wires) {
        for (const GridPoint& cell : wire.path) {
            const int distance = std::abs(cell.column - point.column) + std::abs(cell.row - point.row);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = wire.id;
            }
        }
    }
    return bestDistance <= 1 ? best : -1;
}

QVector<GridPoint> CircuitNet::terminalPoints() const {
    QVector<GridPoint> points;
    points.reserve(m_parts.size() * 2);
    for (const Part& part : m_parts) {
        const QVector<GridPoint> terminals = terminalsOf(part);
        points.push_back(terminals.first());
        points.push_back(terminals.last());
    }
    std::sort(points.begin(), points.end(),
              [](const GridPoint& a, const GridPoint& b) { return pointKey(a) < pointKey(b); });
    points.erase(std::unique(points.begin(), points.end()), points.end());
    return points;
}

// ------------------------------------------------------------------
// Узлы: слияние проводников
// ------------------------------------------------------------------

int CircuitNet::findRoot(QVector<int>& parent, int index) const {
    while (parent.at(index) != index) {
        parent[index] = parent.at(parent.at(index));
        index = parent.at(index);
    }
    return index;
}

void CircuitNet::collectConductors(QVector<quint32>& keys, QVector<int>& parent) const {
    /*
     * Каждая точка провода и каждая клемма — отдельная клетка, которую
     * потом сливают правила соединения. Одна и та же точка сетки может
     * принадлежать двум несоединённым проводам, поэтому клетки различают
     * провод: иначе пересечение «без пайки» слилось бы в один узел.
     */
    keys.clear();
    parent.clear();

    QVector<Cell> cells;
    cells.reserve(m_wires.size() * 8 + m_parts.size() * 2);
    for (int w = 0; w < m_wires.size(); ++w) {
        const Wire& wire = m_wires.at(w);
        for (int i = 0; i < wire.path.size(); ++i)
            cells.push_back({w, i, pointKey(wire.path.at(i))});
    }
    for (const Part& part : m_parts) {
        const QVector<GridPoint> terminals = terminalsOf(part);
        cells.push_back({-1, 0, pointKey(terminals.first())});
        cells.push_back({-1, 1, pointKey(terminals.last())});
    }

    std::sort(cells.begin(), cells.end(), [](const Cell& a, const Cell& b) {
        if (a.key != b.key)
            return a.key < b.key;
        if (a.wire != b.wire)
            return a.wire < b.wire;
        return a.index < b.index;
    });

    for (const Cell& cell : cells) {
        keys.push_back(cell.key);
        parent.push_back(int(parent.size()));
    }

    // 1. Последовательные точки одного провода — один проводник.
    for (int w = 0; w < m_wires.size(); ++w) {
        QVector<int> indexOf(int(m_wires.at(w).path.size()), -1);
        for (int i = 0; i < cells.size(); ++i) {
            if (cells.at(i).wire == w)
                indexOf[cells.at(i).index] = i;
        }
        for (int i = 1; i < indexOf.size(); ++i) {
            if (indexOf.at(i) < 0 || indexOf.at(i - 1) < 0)
                continue;
            const int left = findRoot(parent, indexOf.at(i - 1));
            const int right = findRoot(parent, indexOf.at(i));
            if (left != right)
                parent[right] = left;
        }
    }

    // 2. Точки, совпавшие в одной клетке сетки: клеммы и пересечения.
    int start = 0;
    while (start < cells.size()) {
        int finish = start + 1;
        while (finish < cells.size() && cells.at(finish).key == cells.at(start).key)
            ++finish;
        for (int i = start; i < finish; ++i) {
            for (int j = i + 1; j < finish; ++j) {
                const Cell& a = cells.at(i);
                const Cell& b = cells.at(j);
                const bool terminalInvolved = a.wire < 0 || b.wire < 0;
                const bool join = terminalInvolved ||
                                  wiresJoinAt(m_wires.at(a.wire), a.index, m_wires.at(b.wire), b.index);
                if (!join)
                    continue;
                const int left = findRoot(parent, i);
                const int right = findRoot(parent, j);
                if (left != right)
                    parent[right] = left;
            }
        }
        start = finish;
    }
}

int CircuitNet::rebuildNodes() {
    QVector<quint32> keys;
    QVector<int> parent;
    collectConductors(keys, parent);
    m_nodeOfPoint.clear();
    m_nodeCount = 0;
    if (keys.isEmpty())
        return 0;

    /*
     * Каждое множество точек, соединённых проводниками, — узел. Номера
     * узлам дают в порядке первых точек множеств в отсортированном списке
     * клеток: нумерация определена одной топологией и не зависит от порядка
     * добавления элементов, поэтому тесты сверяют потенциалы узлов.
     */
    QHash<int, int> nodeByRoot;
    for (int i = 0; i < parent.size(); ++i) {
        const int root = findRoot(parent, i);
        if (!nodeByRoot.contains(root))
            nodeByRoot.insert(root, nodeByRoot.size());
    }
    for (int i = 0; i < parent.size(); ++i) {
        const quint32 key = keys.at(i);
        if (m_nodeOfPoint.contains(key))
            continue;  // через точку проходят два несоединённых проводника: берём узел с меньшим номером
        m_nodeOfPoint.insert(key, nodeByRoot.value(findRoot(parent, i), -1));
    }
    m_nodeCount = nodeByRoot.size();
    return m_nodeCount;
}

int CircuitNet::nodeIndex(const GridPoint& point) const {
    const auto it = m_nodeOfPoint.constFind(pointKey(point));
    return it == m_nodeOfPoint.constEnd() ? -1 : it.value();
}

bool CircuitNet::connected(const GridPoint& left, const GridPoint& right) const {
    const int a = nodeIndex(left);
    return a >= 0 && a == nodeIndex(right);
}

QVector<CircuitNet::TerminalRef> CircuitNet::branchNodes(int partId) const {
    const Part* part = findPart(partId);
    if (!part)
        return {};
    const QVector<GridPoint> terminals = terminalsOf(*part);
    QVector<TerminalRef> result;
    result.push_back({terminals.first(), nodeIndex(terminals.first())});
    result.push_back({terminals.last(), nodeIndex(terminals.last())});
    return result;
}

QVector<GridPoint> CircuitNet::freeTerminals() const {
    QVector<GridPoint> free;
    const QVector<GridPoint> all = terminalPoints();
    for (const Part& part : m_parts) {
        const QVector<GridPoint> terminals = terminalsOf(part);
        for (const GridPoint& terminal : terminals) {
            // Клемма соединена, если в её узле есть другая клемма или провод.
            const int node = nodeIndex(terminal);
            if (node < 0)
                continue;
            bool joined = false;
            for (const GridPoint& other : all) {
                if (other != terminal && connected(terminal, other)) {
                    joined = true;
                    break;
                }
            }
            if (!joined) {
                for (const Wire& wire : m_wires) {
                    for (const GridPoint& point : wire.path) {
                        if (connected(point, terminal)) {
                            joined = true;
                            break;
                        }
                    }
                    if (joined)
                        break;
                }
            }
            if (!joined)
                free.push_back(terminal);
        }
    }
    return free;
}

QVector<CircuitNet::BranchPath> CircuitNet::branchPaths() const {
    QVector<BranchPath> paths;
    for (const Part& part : m_parts) {
        const QVector<GridPoint> terminals = terminalsOf(part);
        BranchPath path;
        path.partId = part.id;
        path.path = {terminals.first(), symbolPoint(part), terminals.last()};
        paths.push_back(path);
    }
    for (const Wire& wire : m_wires) {
        BranchPath path;
        path.wireId = wire.id;
        path.path = wire.path;
        paths.push_back(path);
    }
    return paths;
}

int CircuitNet::groundNode() const {
    /*
     * Нулевой потенциал выбирают на минусовой клемме источника: у вывода с
     * меньшим номером точки. Тогда все потенциалы в цепи положительные, и
     * подписи узлов на схеме не конкурируют со знаком.
     */
    for (const Part& part : m_parts) {
        if (part.type != PartType::Source)
            continue;
        const QVector<TerminalRef> nodes = branchNodes(part.id);
        if (!nodes.isEmpty() && nodes.first().node >= 0)
            return nodes.first().node;
    }
    for (const Part& part : m_parts) {
        const QVector<TerminalRef> nodes = branchNodes(part.id);
        if (!nodes.isEmpty() && nodes.first().node >= 0)
            return nodes.first().node;
    }
    return -1;
}

CircuitNet::Check CircuitNet::checkTopology(const QSet<int>& energized) const {
    Check check;
    if (m_parts.isEmpty()) {
        check.ok = false;
        check.message = QStringLiteral("Схема пуста: выберите элемент в палитре и поставьте его на сетку");
        check.hint = QStringLiteral("Начните с источника ЭДС и резистора, затем соедините их клеммы");
        return check;
    }

    /*
     * Напряжение узлам задаёт не только источник ЭДС: заряженный конденсатор
     * или катушка с током тоже двигают заряды, поэтому цепь без батареи
     * считается наравне. Иначе схему «разряд конденсатора» проверка считала бы
     * ошибочной, хотя физика в ней полная.
     */
    const Part* source = nullptr;
    for (const Part& part : m_parts) {
        if (part.type == PartType::Source) {
            source = &part;
            break;
        }
    }
    if (!source) {
        for (const Part& part : m_parts) {
            if ((part.type == PartType::Capacitor || part.type == PartType::Inductor) &&
                energized.contains(part.id)) {
                source = &part;
                break;
            }
        }
    }
    if (!source) {
        check.ok = false;
        check.message = QStringLiteral("В цепи нет источника ЭДС и заряженного конденсатора: тока не будет");
        check.hint = QStringLiteral("Поставьте источник или конденсатор — они задают напряжение на узлах");
        return check;
    }
    const QString driverWord = partGenitive(source->type);

    const QVector<TerminalRef> sourceNodes = branchNodes(source->id);
    if (sourceNodes.first().node < 0 || sourceNodes.last().node < 0) {
        check.ok = false;
        check.message = QStringLiteral("Клеммы %1 ни к чему не подключены").arg(driverWord);
        check.hint = QStringLiteral("Кликните по клемме %1, затем по клемме другого элемента").arg(driverWord);
        return check;
    }
    if (sourceNodes.first().node == sourceNodes.last().node) {
        check.ok = false;
        check.message = QStringLiteral("Обе клеммы %1 соединены проводом: короткое замыкание").arg(driverWord);
        check.hint = QStringLiteral("Включите в цепь резистор — иначе ток ограничен только внутренним сопротивлением");
        return check;
    }

    // Путь от одной клеммы источника до другой по элементам (провода узлы не дают).
    struct Edge {
        int from;
        int to;
    };
    QVector<Edge> edges;
    for (const Part& part : m_parts) {
        const QVector<TerminalRef> nodes = branchNodes(part.id);
        if (nodes.first().node >= 0 && nodes.last().node >= 0 && nodes.first().node != nodes.last().node)
            edges.push_back({nodes.first().node, nodes.last().node});
    }

    QVector<char> reached(size_t(nodeCount()), 0);
    QVector<int> queue{sourceNodes.first().node};
    reached[size_t(sourceNodes.first().node)] = 1;
    while (!queue.isEmpty()) {
        const int node = queue.takeFirst();
        for (const Edge& edge : edges) {
            const int next = edge.from == node ? edge.to : (edge.to == node ? edge.from : -1);
            if (next >= 0 && !reached[size_t(next)]) {
                reached[size_t(next)] = 1;
                queue.push_back(next);
            }
        }
    }
    if (!reached[size_t(sourceNodes.last().node)]) {
        check.ok = false;
        check.message = QStringLiteral("Цепь не замкнута: между клеммами %1 пути нет").arg(driverWord);
        check.hint = QStringLiteral("Соедините свободные клеммы проводами — ток идёт только по замкнутому контуру");
        return check;
    }
    for (const Part& part : m_parts) {
        const QVector<TerminalRef> nodes = branchNodes(part.id);
        if (nodes.first().node < 0 || nodes.last().node < 0) {
            check.message = QStringLiteral("У элемента %1 клемма не соединена: тока в этой ветви нет")
                                .arg(part.name);
            check.hint = QStringLiteral("Подсоедините клемму проводом или удалите элемент правой кнопкой");
            return check;
        }
    }
    return check;
}

}  // namespace lab::circuit
