#pragma once

#include <QPointF>
#include <QStringList>

#include "core/Simulation.h"

namespace lab::models {

/**
 * Модель 13. Деление бактерий: рост колонии по закону N = N₀·2^(t/T_d).
 *
 * Бактерия делится одна на две, поэтому за каждый интервал удвоения T_d их
 * число удваивается:
 *   N = N₀·2^(t/T_d).
 * Число делений, которые прошла линия за время t, —
 *   n = t / T_d,
 * и его же получают из измеренного числа клеток:
 *   n = log₂(N / N₀) = ln(N / N₀) / ln 2.
 * Отсюда же период удвоения через два наблюдения:
 *   T_d = t·ln 2 / ln(N / N₀).
 *
 * Та же запись в непрерывном виде (k — постоянная скорости размножения):
 *   N(t) = N₀·e^(kt),   k = ln 2 / T_d,   N′(t) = k·N(t),
 * то есть скорость прироста пропорциональна числу клеток: чем их больше,
 * тем быстрее их становится. Именно из-за этого кривая N(t) круче с каждым
 * часом, а на графике ln N она становится прямой с наклоном k.
 *
 * В модели два счёта, и различие между ними видно на сцене:
 *  1) теория — гладкая кривая N₀·2^(t/T_d), в ней число клеток дробное,
 *     потому что деления идут не одновременно;
 *  2) дискретный счёт — целое число клеток, которое даёт наблюдатель под
 *     микроскопом: N₀·2^(t/T_d) округлённое до целого, причём деления
 *     случаются порциями, а не непрерывно.
 * Случайности здесь нет: деление детерминировано, поэтому ряд наблюдений
 * один и тот же при любой перемотке.
 *
 * Число клеток не ограничивают ёмкостью чашки: удвоение продолжается и
 * тогда, когда колония перестала помещаться. Чашка — только окно наблюдения
 * фиксированной площади, поэтому «теснота» видна как переполнение кадра, а
 * не как остановка роста.
 */
class BacteriaModel : public Simulation {
    Q_OBJECT
public:
    explicit BacteriaModel(QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("bacteria"); }
    QString title() const override;
    QString subtitle() const override;
    QString statement() const override;
    QString problemText() const override;

    /** Время модели течёт в часах: период удвоения удобен в них. */
    QString timeUnit() const override { return QStringLiteral("h"); }
    double timePerSecond() const override;
    double duration() const override;
    double stepSize() const override;

    QVector<ParamSpec> paramSpecs() const override;
    ValidationResult validate() const override;

    SimState stateAt(double t) const override;
    double eventTime() const override;

    Derivation derivation() const override;
    QVector<Readout> readouts() const override;

    QVector<HandleInfo> handles() const override;
    void setHandleValue(const QString& id, double value) override;
    /** Перетаскивание ручек начинает наблюдение заново. */
    void beginInteraction() override;

    // ---------------- виды бактерий из справочника ----------------
    /** Число пунктов списка, включая «свой период удвоения». */
    static int speciesCount();
    static QString speciesName(int index);
    /** Период удвоения из справочника, ч (для своего пункта — 0). */
    static double speciesDoubling(int index);
    /** Длительность наблюдения по умолчанию: чтобы было видно ~6 делений. */
    static double speciesObservation(int index);
    /** Индекс пункта «свой период удвоения». */
    static int customSpeciesIndex();

    // ---------------- параметры ----------------
    /** Название вида из списка. */
    QString speciesTitle() const;
    /** Начальное число клеток N₀ (посев): вид микроорганизма его не задаёт. */
    double initialCells() const;
    /** Период удвоения T_d, ч. */
    double doublingTime() const;
    /** Постоянная скорости размножения k = ln 2 / T_d, 1/ч. */
    double growthRate() const;
    /** Длительность наблюдения, ч. */
    double observationTime() const;
    /** Целевое число клеток — отметка на шкале сцены. */
    double targetCells() const;
    /** Шаг наблюдателя: с какой частотой считают клетки. */
    double observationInterval() const;
    /**
     * Время в подписях и подстановках: «60 мин», «2 ч 30 мин», «2,5 сут».
     * Общая функция formatDuration писала бы «2 ч 30 мин» и для суток, и для
     * долей часа, а здесь единица подбирается по порядку величины.
     */
    static QString durationText(double hours);
    /** Целое число клеток в подписях: дробная часть здесь не нужна. */
    static QString cellsText(double count);
    /**
     * Малые безразмерные числа (k, n, доли) без потери знаков: k бывает
     * порядка 0,001 ч⁻¹, и обычное форматирование оставила бы ноль.
     */
    static QString ratioText(double value);

    // ---------------- теория ----------------
    /** Число клеток по теории N₀·2^(t/T_d). */
    double theoryCellsAt(double t) const;
    /** Тот же закон через экспоненту: N₀·e^(kt). */
    double theoryCellsByRate(double t) const;
    /** Число делений, прошедших к моменту t: n = t / T_d. */
    double divisionsElapsedAt(double t) const;
    /** Целое число делений, завершённых к моменту t: n(t) = ⌊t / T_d⌋. */
    double completedDivisionsAt(double t) const;
    /** Число клеток по завершённым делениям: N₀·2^⌊t/T_d⌋ — ступенька опыта. */
    double steppedCellsAt(double t) const;
    /** Целое число клеток, которое видит наблюдатель: округлённая теория. */
    double countedCellsAt(double t) const;
    /** Прирост клеток по теории к моменту t: N(t) − N₀. */
    double growthTheoryAt(double t) const;
    /**
     * Последний завершённый интервал учёта [left, right] не позже момента t;
     * возвращает false, если такого интервала ещё нет.
     */
    bool countingIntervalBefore(double t, double& left, double& right) const;
    /** Прирост за последний завершённый интервал учёта: его показывает подсчёт. */
    double growthInLastStepAt(double t) const;
    /** Мгновенная скорость прироста N′(t) = k·N, клеток в час. */
    double growthRateOfCellsAt(double t) const;
    /** Средняя скорость прироста на последнем завершённом интервале: ΔN/Δt. */
    double averageGrowthAt(double t) const;
    /** Во сколько раз выросло число клеток к моменту t: N/N₀ = 2^(t/T_d). */
    double multiplicationFactorAt(double t) const;
    /** Момент, когда по теории клеток станет targetCells: T_d·log₂(N/N₀). */
    double targetTime() const;
    /** Число делений до целевого числа: log₂(N/N₀). */
    double divisionsToTarget() const;
    /**
     * Во сколько раз колония вышла за пределы поля зрения: площадь слоя
     * клеток пропорциональна их числу, поэтому переполнение — это N/N_мест.
     */
    double overflowFactorAt(double t) const;
    /** Число клеток, которое умещается в поле зрения чашки. */
    double dishCapacity() const;

    // ---------------- ряды данных ----------------
    /** Гладкая теоретическая кривая N(t). */
    QVector<QPointF> theorySeries() const;
    /** Ступенька завершённых делений. */
    QVector<QPointF> steppedSeries() const;
    /** Прирост клеток по теории N(t) − N₀. */
    QVector<QPointF> growthSeries() const;
    /** Прирост за шаг наблюдения ΔN (столбцы под микроскопом). */
    QVector<QPointF> growthPerStepSeries() const;
    /** Мгновенная скорость N′(t) = k·N. */
    QVector<QPointF> instantaneousGrowthSeries() const;
    /** Средняя скорость ΔN/Δt по шагам наблюдения. */
    QVector<QPointF> averageGrowthSeries() const;
    /** Деления, прошедшие к моменту t: n = t/T_d (прямая). */
    QVector<QPointF> divisionsSeries() const;
    /** Натуральный логарифм числа клеток: прямая ln N = ln N₀ + kt. */
    QVector<QPointF> logarithmSeries() const;
    /**
     * Зависимость скорости прироста от числа клеток N′(N) = k·N: по ней
     * видно, что рост пропорционален самому числу (прямая через начало).
     */
    QVector<QPointF> growthVersusCells() const;

private:
    /** Точек в рядах, которые уходят на графики. */
    static constexpr int kPlotPoints = 241;
    /**
     * Число клеток, которые умещаются в поле зрения слоем в одну клетку:
     * площадь колонии S = N·s₀, поэтому кадр заполняется при N = S_кадра/s₀.
     * Это фиксированное число — оно не зависит от посевного материала, и
     * именно поэтому переполнение видно как одно и то же число клеток при
     * любом T_d.
     */
    static constexpr double kDishCapacity = 20000.0;
};

}  // namespace lab::models
