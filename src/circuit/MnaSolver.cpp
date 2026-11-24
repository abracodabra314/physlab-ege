#include "circuit/MnaSolver.h"

#include <algorithm>
#include <cmath>

namespace lab::circuit {
namespace {

/**
 * Плотная матрица системы Ax = b с решением методом Гаусса.
 *
 * Неизвестных в цепи немного — по числу узлов плюс токи ветвей с заданным
 * напряжением, — поэтому прямой метод точнее и быстрее итерационного, а
 * выбор ведущего элемента спасает, когда проводимости различаются на
 * порядки (амперметр 0,05 Ом и вольтметр 50 МОм в одной матрице).
 */
class Matrix {
public:
    explicit Matrix(int size)
        : m_size(size),
          m_rows(size_t(std::max(0, size)), QVector<double>(size_t(std::max(0, size)), 0.0)),
          m_right(size_t(std::max(0, size)), 0.0) {}

    int size() const { return m_size; }
    void add(int row, int column, double value);
    void set(int row, int column, double value);
    double at(int row, int column) const;
    void clearRow(int row);
    void setRight(int row, double value) { m_right[size_t(row)] = value; }
    /** Приводит к верхнему треугольному виду; false — ведущий элемент слишком мал. */
    bool factorize(double tolerance);
    /** Обратный ход после успешной factorize(). */
    void solve();
    double solution(int index) const { return m_solution.at(size_t(index)); }

private:
    int m_size = 0;
    QVector<QVector<double>> m_rows;
    QVector<double> m_right;
    QVector<double> m_solution;
};

void Matrix::add(int row, int column, double value) {
    if (row < 0 || column < 0)
        return;
    m_rows[size_t(row)][size_t(column)] += value;
}

void Matrix::set(int row, int column, double value) {
    if (row < 0 || column < 0)
        return;
    m_rows[size_t(row)][size_t(column)] = value;
}

double Matrix::at(int row, int column) const {
    if (row < 0 || column < 0)
        return 0.0;
    return m_rows[size_t(row)][size_t(column)];
}

void Matrix::clearRow(int row) {
    if (row < 0)
        return;
    std::fill(m_rows[size_t(row)].begin(), m_rows[size_t(row)].end(), 0.0);
}

bool Matrix::factorize(double tolerance) {
    m_solution.clear();
    for (int step = 0; step < m_size; ++step) {
        int pivot = step;
        double best = std::abs(m_rows[size_t(step)][size_t(step)]);
        for (int r = step + 1; r < m_size; ++r) {
            const double candidate = std::abs(m_rows[size_t(r)][size_t(step)]);
            if (candidate > best) {
                best = candidate;
                pivot = r;
            }
        }
        if (best < tolerance)
            return false;
        if (pivot != step) {
            std::swap(m_rows[size_t(pivot)], m_rows[size_t(step)]);
            std::swap(m_right[size_t(pivot)], m_right[size_t(step)]);
        }
        const double diagonal = m_rows[size_t(step)][size_t(step)];
        for (int r = step + 1; r < m_size; ++r) {
            const double factor = m_rows[size_t(r)][size_t(step)] / diagonal;
            if (factor == 0.0)
                continue;
            for (int c = step; c < m_size; ++c)
                m_rows[size_t(r)][size_t(c)] -= factor * m_rows[size_t(step)][size_t(c)];
            m_right[size_t(r)] -= factor * m_right[size_t(step)];
        }
    }
    return true;
}

void Matrix::solve() {
    m_solution = QVector<double>(size_t(m_size), 0.0);
    for (int row = m_size - 1; row >= 0; --row) {
        double sum = m_right[size_t(row)];
        for (int c = row + 1; c < m_size; ++c)
            sum -= m_rows[size_t(row)][size_t(c)] * m_solution[size_t(c)];
        m_solution[size_t(row)] = sum / m_rows[size_t(row)][size_t(row)];
    }
}

/** Проводимость ветви по её типу и значению; 0 — ветвь задаёт ток или напряжение. */
double branchConductance(const Part& part, bool switchClosed) {
    switch (part.type) {
    case PartType::Resistor:
        return part.value > 0.0 ? 1.0 / part.value : 0.0;
    case PartType::Switch:
        return 1.0 / (switchClosed ? parts::kClosedResistance : parts::kOpenResistance);
    case PartType::Ammeter:
        return 1.0 / parts::kClosedResistance;
    case PartType::Voltmeter:
        return 1.0 / parts::kVoltmeterResistance;
    case PartType::Source:
    case PartType::Capacitor:
    case PartType::Inductor:
        return 0.0;
    }
    return 0.0;
}

}  // namespace

// ------------------------------------------------------------------
// Drive
// ------------------------------------------------------------------

void Drive::setInternalResistance(double resistance) {
    m_internal = std::max(0.0, resistance);
}

int Drive::indexOf(int partId) const {
    if (!m_net)
        return -1;
    for (int i = 0; i < m_net->parts().size(); ++i) {
        if (m_net->parts().at(size_t(i)).id == partId)
            return i;
    }
    return -1;
}

bool Drive::setEmf(int partId, double value) {
    const int row = indexOf(partId);
    if (row < 0 || !std::isfinite(value))
        return false;
    m_rows[size_t(row)].emf = value;
    return true;
}

bool Drive::setCapacitorVoltage(int partId, double value) {
    const int row = indexOf(partId);
    if (row < 0 || !std::isfinite(value))
        return false;
    m_rows[size_t(row)].capacitorVoltage = value;
    return true;
}

bool Drive::setInductorCurrent(int partId, double value) {
    const int row = indexOf(partId);
    if (row < 0 || !std::isfinite(value))
        return false;
    m_rows[size_t(row)].inductorCurrent = value;
    return true;
}

double Drive::emfOf(int partId) const {
    const int row = indexOf(partId);
    return row < 0 ? 0.0 : m_rows.at(size_t(row)).emf;
}

double Drive::capacitorVoltageOf(int partId) const {
    const int row = indexOf(partId);
    return row < 0 ? 0.0 : m_rows.at(size_t(row)).capacitorVoltage;
}

double Drive::inductorCurrentOf(int partId) const {
    const int row = indexOf(partId);
    return row < 0 ? 0.0 : m_rows.at(size_t(row)).inductorCurrent;
}

double Drive::branchResistanceOf(int partId) const {
    if (!m_net)
        return 0.0;
    const Part* part = m_net->findPart(partId);
    if (!part)
        return 0.0;
    switch (part->type) {
    case PartType::Resistor:
        return part->value;
    case PartType::Switch:
        // Значение ключа — момент замыкания, а не сопротивление.
        return parts::kClosedResistance;
    case PartType::Ammeter:
        return parts::kClosedResistance;
    case PartType::Voltmeter:
        return parts::kVoltmeterResistance;
    case PartType::Source:
        return m_internal;
    case PartType::Capacitor:
    case PartType::Inductor:
        return 0.0;
    }
    return 0.0;
}

// ------------------------------------------------------------------
// MnaSystem
// ------------------------------------------------------------------

bool MnaSystem::compile(const CircuitNet& net, int ground, const QVector<int>& closedSwitches) {
    m_nodeCount = net.nodeCount();
    m_ground = ground;
    m_nodeRow = QVector<int>(size_t(std::max(0, m_nodeCount)), -1);
    m_nodeDiagonal = QVector<double>(size_t(std::max(0, m_nodeCount)), 0.0);
    m_branches.clear();
    m_equations = 0;
    if (m_nodeCount <= 0 || ground < 0 || ground >= m_nodeCount)
        return false;

    /*
     * Неизвестные: потенциалы всех узлов, кроме заземлённого, плюс ток каждой
     * ветви, где напряжение задано (источник ЭДС, заряженный конденсатор).
     * Такой набор закрывает любую топологию: узел, к которому подходят только
     * такие ветви, тоже получает уравнение — через неизвестный ток.
     */
    for (int node = 0; node < m_nodeCount; ++node) {
        if (node == ground)
            continue;
        m_nodeRow[size_t(node)] = m_equations++;
    }

    for (const Part& part : net.parts()) {
        const QVector<CircuitNet::TerminalRef> nodes = net.branchNodes(part.id);
        Branch branch;
        branch.partId = part.id;
        branch.type = part.type;
        branch.fromNode = nodes.first().node;
        branch.toNode = nodes.last().node;
        if (branch.fromNode < 0 || branch.toNode < 0)
            continue;   // клемма висит в воздухе: ветвь в расчёт не входит

        branch.conductance = branchConductance(part, closedSwitches.contains(part.id));
        if (part.type == PartType::Source || part.type == PartType::Capacitor)
            branch.sourceIndex = m_equations++;
        m_branches.push_back(branch);
    }

    // Суммы проводимостей узлов: по ним сценой показывают «слабое место» цепи.
    for (const Branch& branch : m_branches) {
        if (branch.conductance <= 0.0)
            continue;
        if (branch.fromNode != ground)
            m_nodeDiagonal[size_t(branch.fromNode)] += branch.conductance;
        if (branch.toNode != ground)
            m_nodeDiagonal[size_t(branch.toNode)] += branch.conductance;
    }
    return !m_branches.isEmpty();
}

int MnaSystem::nodeEquation(int node) const {
    return node >= 0 && node < m_nodeRow.size() ? m_nodeRow.at(size_t(node)) : -1;
}

int MnaSystem::branchIndexOfPart(int partId) const {
    for (int i = 0; i < m_branches.size(); ++i) {
        if (m_branches.at(i).partId == partId)
            return i;
    }
    return -1;
}

double MnaSystem::nodeConductance(int node) const {
    return node >= 0 && node < m_nodeDiagonal.size() ? m_nodeDiagonal.at(size_t(node)) : 0.0;
}

// ------------------------------------------------------------------
// Solution
// ------------------------------------------------------------------

const Solution::Value* Solution::value(int partId) const {
    for (const Value& entry : values) {
        if (entry.partId == partId)
            return &entry;
    }
    return nullptr;
}

double Solution::sourceCurrent() const {
    for (const Value& entry : values) {
        if (entry.type == PartType::Source)
            return entry.current;   // внутри источника ток идёт от «−» к «+»
    }
    return 0.0;
}

double Solution::sourcePower() const {
    /*
     * Мощность посчитана как «поглощённая»: для источника она отрицательна,
     * потому что ток внутри него идёт от «−» к «+». Отданная мощность —
     * противоположный знак; именно её отдают резисторам.
     */
    for (const Value& entry : values) {
        if (entry.type == PartType::Source)
            return -entry.power;
    }
    return 0.0;
}

double Solution::dissipatedPower() const {
    double total = 0.0;
    for (const Value& entry : values) {
        if (entry.type == PartType::Resistor || entry.type == PartType::Ammeter ||
            entry.type == PartType::Voltmeter || entry.type == PartType::Switch)
            total += entry.power;
    }
    return total;
}

// ------------------------------------------------------------------
// MnaSolver
// ------------------------------------------------------------------

Solution MnaSolver::solve(const MnaSystem& system, const CircuitNet& net, const Drive& drive) {
    Solution solution;
    if (system.empty()) {
        solution.message = QStringLiteral("Ветви цепи не соединены в узлы: тока нет");
        solution.hint = QStringLiteral("Соедините клеммы элементов проводами");
        return solution;
    }

    const int size = system.unknownCount();
    Matrix matrix(size);
    QVector<double> right(size_t(size), 0.0);
    QVector<int> constrainedBranches;   // ветви с заданным напряжением

    for (int index = 0; index < system.branches().size(); ++index) {
        const MnaSystem::Branch& branch = system.branches().at(size_t(index));
        const Part* part = net.findPart(branch.partId);
        if (!part)
            continue;
        const int fromRow = system.nodeEquation(branch.fromNode);
        const int toRow = system.nodeEquation(branch.toNode);

        if (branch.conductance > 0.0) {
            /*
             * Проводящая ветвь: ток, вытекающий из узла, равен G·(φ₁ − φ₂).
             * Отсюда симметричная матрица проводимостей: +G на диагоналях
             * обоих узлов и −G вне диагонали.
             */
            matrix.add(fromRow, fromRow, branch.conductance);
            matrix.add(toRow, toRow, branch.conductance);
            if (fromRow >= 0 && toRow >= 0) {
                matrix.add(fromRow, toRow, -branch.conductance);
                matrix.add(toRow, fromRow, -branch.conductance);
            }
            continue;
        }

        if (branch.sourceIndex >= 0) {
            // Неизвестный ток ветви вытекает из первой клеммы и втекает во вторую.
            matrix.add(fromRow, branch.sourceIndex, 1.0);
            matrix.add(toRow, branch.sourceIndex, -1.0);
            constrainedBranches.push_back(index);
            continue;
        }

        if (part->type == PartType::Inductor) {
            /*
             * Ток ветви задан предысторией: ветвь — источник известного тока.
             * В уравнении узла он уходит в правую часть: сумма токов
             * проводимых ветвей равна −I.
             */
            const double current = drive.inductorCurrentOf(branch.partId);
            if (fromRow >= 0)
                right[size_t(fromRow)] -= current;
            if (toRow >= 0)
                right[size_t(toRow)] += current;
        }
    }

    for (int row = 0; row < size; ++row)
        matrix.setRight(row, right[size_t(row)]);

    for (const int index : constrainedBranches) {
        /*
         * Ветвь с заданным напряжением (источник ЭДС или конденсатор). Ток x
         * течёт внутри ветви от первой клеммы ко второй; у источника первая
         * клемма — «минус». Реальный источник — ЭДС с внутренним
         * сопротивлением: ток выходит из второй клеммы, поэтому напряжение на
         * зажимах U = φ₂ − φ₁ = E − r·x (его и показывает вольтметр на
         * клеммах), а уравнение ветви выглядит так
         *      φ₁ − φ₂ − r·x = −E.
         * Потери на r входят в баланс мощности автоматически:source отдаёт U·x,
         * а разность E·x − U·x = r·x² сгорает внутри него.
         *
         * Для конденсатора r = 0, а правую часть берут равной его
         * напряжению: уравнение становится условием «разность потенциалов
         * обкладок равна U_C».
         */
        const MnaSystem::Branch& branch = system.branches().at(size_t(index));
        const Part* part = net.findPart(branch.partId);
        if (!part)
            continue;
        const int row = branch.sourceIndex;
        matrix.clearRow(row);
        matrix.set(row, system.nodeEquation(branch.fromNode), 1.0);
        matrix.set(row, system.nodeEquation(branch.toNode), -1.0);
        if (part->type == PartType::Source) {
            matrix.add(row, row, -drive.internalResistance());
            matrix.setRight(row, -drive.emfOf(branch.partId));
        } else {
            matrix.setRight(row, drive.capacitorVoltageOf(branch.partId));
        }
    }

    // Масштаб коэффициентов задаёт порог: проводимости сильно различаются.
    double scale = 0.0;
    for (int r = 0; r < size; ++r) {
        for (int c = 0; c < size; ++c)
            scale = std::max(scale, std::abs(matrix.at(r, c)));
    }
    const double tolerance = std::max(1e-18, scale * parts::kPivotTolerance);
    if (!matrix.factorize(tolerance)) {
        solution.message = QStringLiteral("Такая цепь не имеет единственного решения");
        solution.hint = QStringLiteral("Уберите один из идеальных источников, соединённых с теми же узлами");
        return solution;
    }
    matrix.solve();

    auto potential = [&](int node) {
        if (node < 0 || node == system.ground())
            return 0.0;
        const int row = system.nodeEquation(node);
        return row >= 0 ? matrix.solution(row) : 0.0;
    };

    solution.nodePotentials = QVector<double>(size_t(std::max(0, system.nodeCount())), 0.0);
    for (int node = 0; node < system.nodeCount(); ++node)
        solution.nodePotentials[size_t(node)] = potential(node);

    solution.values.reserve(system.branches().size());
    for (const MnaSystem::Branch& branch : system.branches()) {
        const Part* part = net.findPart(branch.partId);
        if (!part)
            continue;
        Solution::Value value;
        value.partId = branch.partId;
        value.type = part->type;
        value.connected = true;
        value.voltage = potential(branch.fromNode) - potential(branch.toNode);

        if (branch.sourceIndex >= 0)
            value.current = matrix.solution(branch.sourceIndex);
        else if (branch.conductance > 0.0)
            value.current = branch.conductance * value.voltage;
        else
            value.current = drive.inductorCurrentOf(branch.partId);

        value.power = value.voltage * value.current;
        value.conducting = std::abs(value.current) > 1e-12;

        if (part->type == PartType::Capacitor) {
            /*
             * Ток x ветви вытекает из первой клеммы в ветвь, поэтому первая
             * обкладка по закону сохранения заряда получает заряд со скоростью
             * x: dU_C/dt = I_C/C. Знак out of the box показывает, идёт ли
             * заряд или разряд: при разряде ток отрицательный.
             */
            const double capacity = part->value * 1e-6;   // мкФ → Ф
            value.charge = capacity * value.voltage;
            value.derivative = capacity > 0.0 ? value.current / capacity : 0.0;
            solution.storedEnergy += 0.5 * capacity * value.voltage * value.voltage;
        } else if (part->type == PartType::Inductor) {
            // U = L·dI/dt: напряжение на катушке задаёт скорость роста тока.
            value.flux = part->value * value.current;
            value.derivative = part->value > 0.0 ? value.voltage / part->value : 0.0;
            solution.storedEnergy += 0.5 * part->value * value.current * value.current;
        }
        solution.values.push_back(value);
    }

    distributeCurrents(net, solution);
    solution.ok = true;
    return solution;
}

// ------------------------------------------------------------------
// Токи участков чертежа
// ------------------------------------------------------------------

QVector<GridPoint> MnaSolver::junctionPoints(const CircuitNet& net) {
    /*
     * Ток разветвляется там, где сходятся разные участки: у клемм, у концов
     * проводов и на его изломах. Точку, через которую провод идёт прямо,
     * узлом не считают: по обе её стороны ток один и тот же.
     */
    QVector<GridPoint> points = net.terminalPoints();
    for (const Wire& wire : net.wires()) {
        for (int i = 0; i < wire.path.size(); ++i) {
            const bool end = i == 0 || i + 1 == wire.path.size();
            const bool bend = !end && (wire.path.at(i - 1).column != wire.path.at(i + 1).column ||
                                       wire.path.at(i - 1).row != wire.path.at(i + 1).row);
            if (end || bend)
                points.push_back(wire.path.at(i));
        }
    }
    std::sort(points.begin(), points.end(),
              [](const GridPoint& a, const GridPoint& b) { return pointKey(a) < pointKey(b); });
    points.erase(std::unique(points.begin(), points.end()), points.end());
    return points;
}

void MnaSolver::distributeCurrents(const CircuitNet& net, Solution& solution) {
    /*
     * Токи элементов решатель нашёл; токи проводов находят из первого закона
     * Кирхгофа для точек чертежа: сколько в точку приходит, столько из неё и
     * уходит. Для дерева проводов этого достаточно: ток ветви дерева равен
     * суммарному приходу тока во всей ветвящейся за ней части схемы. Если же
     * провода образуют замкнутый контур, ток между его участками зависит от
     * их сопротивлений, а провода в модели идеальны: такие участки помечают
     * недетерминированными, и носители по ним не движутся — иначе направление
     * движения зависело бы от порядка обхода, а не от физики.
     */
    struct Run {
        int wireId = 0;
        QVector<GridPoint> path;
        int from = -1;      // узел чертежа, откуда участок идёт
        int to = -1;        // узел чертежа, куда он приходит
        double current = 0.0;
        bool determined = false;
    };

    const QVector<GridPoint> junctions = junctionPoints(net);
    auto junctionIndex = [&](const GridPoint& point) {
        const auto it = std::lower_bound(junctions.begin(), junctions.end(), point,
                                         [](const GridPoint& a, const GridPoint& b) {
                                             return pointKey(a) < pointKey(b);
                                         });
        return it != junctions.end() && *it == point ? int(it - junctions.begin()) : -1;
    };

    QVector<Run> runs;
    for (const Wire& wire : net.wires()) {
        int start = 0;
        for (int i = 1; i <= wire.path.size(); ++i) {
            const bool cut = i == wire.path.size() || junctionIndex(wire.path.at(i)) >= 0;
            if (!cut)
                continue;
            Run run;
            run.wireId = wire.id;
            run.path = wire.path.mid(start, i - start + 1);
            run.from = junctionIndex(run.path.first());
            run.to = junctionIndex(run.path.last());
            if (run.from >= 0 && run.to >= 0 && run.from != run.to)
                runs.push_back(run);
            start = i;
        }
    }

    /*
     * Приход тока из элементов в точки чертежа. Ток ветви идёт внутри
     * элемента от первой клеммы ко второй, поэтому из первой клеммы он
     * уходит в элемент (приход отрицательный), а во вторую — приходит.
     */
    QVector<double> arrival(size_t(junctions.size()), 0.0);
    for (const Part& part : net.parts()) {
        const Solution::Value* value = solution.value(part.id);
        if (!value || !value->connected)
            continue;
        const QVector<CircuitNet::TerminalRef> nodes = net.branchNodes(part.id);
        const int first = junctionIndex(nodes.first().point);
        const int second = junctionIndex(nodes.last().point);
        if (first >= 0)
            arrival[size_t(first)] -= value->current;
        if (second >= 0)
            arrival[size_t(second)] += value->current;
    }

    struct Edge {
        int run = -1;
        int other = -1;
        bool forward = true;   // true: участок идёт от этой точки к other
    };
    QVector<QVector<Edge>> at(size_t(junctions.size()));
    for (int r = 0; r < runs.size(); ++r) {
        at[size_t(runs.at(size_t(r)).from)].push_back({r, runs.at(size_t(r)).to, true});
        at[size_t(runs.at(size_t(r)).to)].push_back({r, runs.at(size_t(r)).from, false});
    }

    /*
     * Каркас из участков проводов: обход в ширину по всем точкам. Участок,
     * приведший в уже посещённую точку, в каркас не входит — это лишняя
     * перемычка, её ток остаётся неопределённым.
     */
    QVector<int> parentEdge(size_t(junctions.size()), -1);
    QVector<int> parentPoint(size_t(junctions.size()), -1);
    QVector<int> roots;
    QVector<char> visited(size_t(junctions.size()), 0);
    for (int start = 0; start < junctions.size(); ++start) {
        if (visited[size_t(start)] || at[size_t(start)].isEmpty())
            continue;
        roots.push_back(start);
        visited[size_t(start)] = 1;
        QVector<int> queue{start};
        while (!queue.isEmpty()) {
            const int point = queue.takeFirst();
            for (const Edge& edge : at[size_t(point)]) {
                if (visited[size_t(edge.other)])
                    continue;
                visited[size_t(edge.other)] = 1;
                parentPoint[size_t(edge.other)] = point;
                parentEdge[size_t(edge.other)] = edge.run;
                queue.push_back(edge.other);
            }
        }
    }

    /*
     * Токи участков каркаса считают снизу вверх: для любой точки суммарный
     * приход тока из элементов равен току, уходящему по участку к её
     * родителю. Поэтому обходят в обратном порядке обхода в ширину.
     */
    QVector<QVector<int>> order;
    order.reserve(size_t(roots.size()));
    for (const int root : roots) {
        QVector<int> list{root};
        QVector<int> queue{root};
        while (!queue.isEmpty()) {
            const int point = queue.takeFirst();
            for (const Edge& edge : at[size_t(point)]) {
                if (parentPoint[size_t(edge.other)] == point && parentEdge[size_t(edge.other)] == edge.run) {
                    list.push_back(edge.other);
                    queue.push_back(edge.other);
                }
            }
        }
        order.push_back(list);
    }
    QVector<double> subtree(size_t(junctions.size()), 0.0);
    for (const QVector<int>& list : order) {
        for (int i = list.size() - 1; i >= 0; --i) {
            const int point = list.at(size_t(i));
            subtree[size_t(point)] += arrival[size_t(point)];
            const int edge = parentEdge[size_t(point)];
            if (edge < 0)
                continue;
            /*
             * Через единственный участок, связывающий точку с её родителем,
             * проходит весь ток, который забирает эта точка и всё, что ниже
             * неё. Если участок идёт от точки к родителю, он несёт именно
             * subtree; если навстречу — тот же ток с обратным знаком.
             */
            const bool outward = runs.at(size_t(edge)).from == point;
            Run& target = runs[size_t(edge)];
            if (!target.determined) {
                target.current = outward ? subtree[size_t(point)] : -subtree[size_t(point)];
                target.determined = true;
            }
            subtree[size_t(parentPoint[size_t(point)])] += subtree[size_t(point)];
        }
    }

    for (const Run& run : runs) {
        Solution::Segment segment;
        segment.wireId = run.wireId;
        segment.path = run.path;
        segment.current = run.determined ? run.current : 0.0;
        segment.determined = run.determined;
        solution.segments.push_back(segment);
    }

    // Ветви элементов: по ним носители идут через сам элемент.
    for (const Part& part : net.parts()) {
        const Solution::Value* value = solution.value(part.id);
        if (!value)
            continue;
        Solution::Segment segment;
        segment.partId = part.id;
        segment.path = {part.start, CircuitNet::symbolPoint(part), CircuitNet::terminalsOf(part).last()};
        segment.current = value->current;
        segment.determined = value->connected;
        solution.segments.push_back(segment);
    }
}

}  // namespace lab::circuit
