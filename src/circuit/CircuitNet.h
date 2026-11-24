#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

namespace lab::circuit {

/**
 * Тип элемента, который пользователь ставит на сетку.
 *
 * Все элементы линейные: их вклад в уравнения метода узловых потенциалов
 * однотипен (сопротивление, источник ЭДС, источник тока), а конденсатор и
 * катушка в переходном процессе заменяются сопровождающими схемами.
 * Поэтому расчёт цепи не нуждается ни в каких частных формулах для
 * отдельных соединений: топология любой сложности даёт одну систему
 * уравнений.
 */
enum class PartType {
    Source,      // источник ЭДС (значение — ЭДС, В)
    Resistor,    // резистор (Ом)
    Capacitor,   // конденсатор (мкФ)
    Inductor,    // катушка индуктивности (Гн)
    Switch,      // ключ: замкнут с момента t₀
    Ammeter,     // амперметр (малое сопротивление, читает ток ветви)
    Voltmeter,   // вольтметр (большое сопротивление, читает напряжение ветви)
};

/** Короткое обозначение элемента для подписей на схеме: «R», «C». */
QString shortName(PartType type);

/** Единица измерения значения элемента. */
QString valueUnit(PartType type);

/** Название элемента в подписях и показаниях: «резистор», «конденсатор». */
QString partWord(PartType type);

/**
 * Точка логической сетки чертежа: по ней идут линии, в ней стоят клеммы.
 */
struct GridPoint {
    int column = 0;
    int row = 0;
};

bool operator==(const GridPoint& left, const GridPoint& right);
bool operator!=(const GridPoint& left, const GridPoint& right);

/** Ключ точки для хэша: координаты сетки укладываются в одно число. */
quint32 pointKey(const GridPoint& point);

/**
 * Элемeнт схемы. Занимает два узла: клемму start и клемму через два шага
 * сетки вправо (horizontal) или вниз. Средняя точка — место символа.
 */
struct Part {
    int id = 0;
    PartType type = PartType::Resistor;
    GridPoint start;
    bool horizontal = true;
    double value = 100.0;   // Ом | мкФ | Гн | В — по типу элемента
    QString name;           // «R1», «C2» — подпись на схеме
};

/**
 * Провод: ломаная по линиям сетки. Соседние точки отличаются на один шаг
 * по горизонтали или вертикали, поэтому у провода только прямые участки и
 * повороты на 90°.
 */
struct Wire {
    int id = 0;
    QVector<GridPoint> path;
};

/**
 * Топология цепи: элементы, провода и узлы, которые из них получаются.
 *
 * Узел — множество точек сетки, соединённых идеальными проводниками.
 * Проводники сливаются по правилам, которые проверяют тесты:
 *  - все точки одного провода — один проводник;
 *  - точки двух проводов совпадают, если хотя бы одна из них — конец
 *    провода или его излом: тогда это узел (соединение);
 *  - если оба провода идут через общую точку по прямой, соединения нет:
 *    это пересечение «без пайки», как на настоящих чертежах.
 *
 * Класс не решает физику: он отвечает, какие клеммы оказались соединены.
 * Уравнения по найденным узлам строит MnaSolver.
 */
class CircuitNet {
public:
    CircuitNet() = default;

    /** Размеры логической сетки чертежа: точки 0..columns, 0..rows. */
    void setGridSize(int columns, int rows);
    int gridColumns() const { return m_columns; }
    int gridRows() const { return m_rows; }
    bool insideGrid(const GridPoint& point) const;

    /** Удаляет элементы, провода и узлы. */
    void clear();

    // ---------------- редактирование ----------------

    /**
     * Ставит элемент: клеммы в start и в двух шагах по ориентации.
     * Возвращает id или -1, если элемент не помещается на сетку.
     */
    int addPart(PartType type, const GridPoint& start, bool horizontal, double value);
    /** Удаляет элемент и провода, которые держались только за его клеммы. */
    bool removePart(int id);
    /** Поворачивает элемент на 90° вокруг первой клеммы. */
    bool rotatePart(int id);
    /** Меняет значение элемента (Ом, мкФ, Гн, В). */
    bool setPartValue(int id, double value);

    /**
     * Добавляет провод по ломаной: промежуточные узлы вставляются сами,
     * поэтому достаточно указать концы. Возвращает id или -1, если концы
     * не лежат на одной линии сетки или вышли за сетку.
     */
    int addWire(const QVector<GridPoint>& ends);
    /** Удаляет провод по id. */
    bool removeWire(int id);

    const QVector<Part>& parts() const { return m_parts; }
    const QVector<Wire>& wires() const { return m_wires; }
    const Part* findPart(int id) const;
    Part* findPart(int id);
    /** Имя нового элемента данного типа: «R3». */
    QString nextName(PartType type);

    /** Элемент, точка которого ближе всех к указанной; -1 — ничего. */
    int partAt(const GridPoint& point) const;
    /** Провод, участок которого ближе всех к точке; -1 — ничего. */
    int wireNear(const GridPoint& point) const;

    // ---------------- геометрия клемм ----------------

    /** Клеммы элемента: начальная и конечная. */
    static QVector<GridPoint> terminalsOf(const Part& part);
    /** Точка, где рисуют символ элемента (между клеммами). */
    static GridPoint symbolPoint(const Part& part);
    /** Точки клемм всех элементов. */
    QVector<GridPoint> terminalPoints() const;
    /**
     * Клеммы, которые ещё не соединены ни с какой другой клеммой: их
     * предлагают пользователю для следующего соединения.
     */
    QVector<GridPoint> freeTerminals() const;
    /** Соединены ли две точки (один узел). Требует rebuildNodes(). */
    bool connected(const GridPoint& left, const GridPoint& right) const;

    // ---------------- узлы ----------------

    /** Клемма элемента с её узлом: -1 — узла нет (клемма висит в воздухе). */
    struct TerminalRef {
        GridPoint point;
        int node = -1;
    };

    /**
     * Пересчитывает узлы: объединяет точки, соединённые проводами и
     * общими клеммами. Возвращает число узлов; после вызова работают
     * nodeIndex(), connected() и branchNodes().
     */
    int rebuildNodes();

    /** Номер узла для точки сетки; -1 — точка не проводник. */
    int nodeIndex(const GridPoint& point) const;
    /** Число узлов после rebuildNodes(). */
    int nodeCount() const { return m_nodeCount; }
    /** Узлы обеих клемм элемента (в порядке клемм). */
    QVector<TerminalRef> branchNodes(int partId) const;

    /**
     * Линии, по которым течёт ток: для элемента — путь от клеммы до клеммы
     * через символ, для провода — его ломаная. Сцена рисует по этим путям
     * движущиеся носители заряда, поэтому ток виден только там, где цепь
     * действительно соединена.
     */
    struct BranchPath {
        int partId = 0;
        int wireId = 0;            // 0 — путь принадлежит элементу
        QVector<GridPoint> path;   // точки от клеммы a к клемме b
    };
    QVector<BranchPath> branchPaths() const;

    /**
     * Узел, который считают землёй (0 В): клемма источника ЭДС, дальше всех
     * от второго его вывода по схеме. -1, если источника в цепи нет.
     */
    int groundNode() const;

    /** Проверка топологии: текст проблемы или пустая строка. */
    struct Check {
        bool ok = true;
        QString message;
        QString hint;
    };
    /**
     * Проверка схемы. `energized` — элементы, в которых уже есть энергия:
     * заряженные конденсаторы и катушки с током. Они задают напряжения узлов
     * вместо источника ЭДС, поэтому цепь без батареи остаётся рабочей.
     */
    Check checkTopology(const QSet<int>& energized = {}) const;

private:
    /** Точки-проводники и их объединение в множества. */
    void collectConductors(QVector<quint32>& keys, QVector<int>& parent) const;
    int findRoot(QVector<int>& parent, int index) const;

    int m_columns = 21;
    int m_rows = 9;
    int m_nextId = 1;
    QVector<Part> m_parts;
    QVector<Wire> m_wires;
    /** Счётчики имён по типам: R1, R2, C1… */
    int m_nameCounters[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    QHash<quint32, int> m_nodeOfPoint;
    int m_nodeCount = 0;
};

}  // namespace lab::circuit
