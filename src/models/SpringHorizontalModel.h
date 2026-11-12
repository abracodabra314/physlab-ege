#pragma once

#include "core/Simulation.h"
#include "core/Trajectory.h"

namespace lab::models {

/**
 * Модель 7A. Груз на горизонтальной пружине, закреплённой на стене.
 *
 * Физическая модель (x — смещение от положения равновесия, м; ось X вправо):
 *   m·x'' = Fупр + Fтр,   Fупр = -k·x,   Fтр = -μ·N·sign(v),   N = m·g
 *
 * Сухое трение делает задачу нелинейной: замкнутого гармонического решения
 * нет, поэтому движение считается численно (Trajectory, метод Рунге–Кутты 4
 * порядка). При каждом изменении параметров расчёт выполняется заново и
 * кэшируется, поэтому stateAt(t) остаётся чистой функцией и время можно
 * перематывать в обе стороны.
 *
 * Состояние покоя обрабатывается по физическому условию: груз остаётся
 * неподвижным, если сила упругости не превосходит максимум силы трения покоя
 * (|k·x| ≤ μ·m·g). Так трение не «разворачивается» само и не создаёт
 * самопроизвольного движения.
 *
 * При μ = 0 численное решение совпадает с идеальными колебаниями
 *   ω = √(k/m),   T = 2π√(m/k),   x(t) = A·cos(ωt + φ)
 * — эти величины считаются по формулам и выводятся в панели вычислений.
 */
class SpringHorizontalModel : public Simulation {
    Q_OBJECT
public:
    explicit SpringHorizontalModel(QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("spring_h"); }
    QString title() const override;
    QString subtitle() const override;
    QString statement() const override;
    QString problemText() const override;

    QString timeUnit() const override { return QStringLiteral("s"); }
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
    /**
     * Начало перетаскивания груза: эксперимент возвращается в t = 0,
     * чтобы новое положение стало началом движения (груз «отводят и
     * отпускают»), а не серединой уже идущего расчёта.
     */
    void beginInteraction() override;

    // ---------------- параметры ----------------
    double mass() const;
    double stiffness() const;
    double frictionCoeff() const;
    double initialDisplacement() const;
    double gravity() const;

    // ---------------- величины, считаемые по формулам ----------------
    /** Нормальная реакция опоры N = m·g. */
    double normalForce() const;
    /** Модуль силы трения скольжения μ·N. */
    double frictionForce() const;
    /** Циклическая частота идеальной системы ω = √(k/m). */
    double omega() const;
    /** Период идеальных колебаний T = 2π√(k/m). */
    double period() const;
    /** Ширина «мёртвой зоны» |x| ≤ μmg/k, в которой груз остаётся в покое. */
    double deadZone() const;

    // ---------------- состояние движения ----------------
    double displacementAt(double t) const;
    double velocityAt(double t) const;
    /** Ускорение из уравнения движения по заданному состоянию (x, v). */
    double accelerationOf(double x, double v) const;
    double accelerationAt(double t) const;
    /** Сила упругости в момент t (со знаком оси X). */
    double elasticForceAt(double t) const;
    /** Сила трения в момент t (0, если груз покоится). */
    double frictionForceAt(double t) const;

    /** Момент, после которого груз остаётся в покое (NaN — не останавливается). */
    double restTime() const;
    /** Смещение, на котором груз остановился (NaN — не останавливался). */
    double restPosition() const;

    /**
     * Расчётное время остановки по сухому трению: при каждом полу колебании
     * амплитуда убывает ровно на 2μmg/k, поэтому движение прекращается через
     * n полу периодов, где |x₀| − 2n·μmg/k ≤ μmg/k. Замкнутое решение задачи
     * о движении с трением скольжения — оно используется только для того,
     * чтобы выбрать разумную длительность наблюдения.
     */
    double predictedRestTime() const;

    /** Кэш интегрирования — узлы используются сценой для графиков. */
    const Trajectory& trajectory() const;

    // ---------------- готовые ряды данных для графиков ----------------
    /**
     * Узлы численного решения в виде рядов (t, значение). Ряды строятся по
     * рассчитанной траектории, поэтому графики синхронны с анимацией и не
     * требуют физических вычислений при отрисовке.
     */
    QVector<QPointF> displacementSeries() const;
    QVector<QPointF> velocitySeries() const;
    QVector<QPointF> accelerationSeries() const;

    // ---------------- масштабные оценки для сцены ----------------
    /** Максимальный модуль смещения за весь расчёт. */
    double maxDisplacement() const;
    /** Максимальный модуль скорости за весь расчёт. */
    double maxSpeed() const;
    /** Максимальный модуль ускорения за весь расчёт. */
    double maxAcceleration() const;

private:
    /** Интегрирует движение, если параметры изменились с прошлого вызова. */
    void ensureSimulated() const;
    /** Проверка условия покоя после шага; true — движение завершено. */
    bool checkSticking(double t, QVector<double>& y, const QVector<double>& previous) const;

    mutable Trajectory m_trajectory;
    mutable double m_restTime = std::numeric_limits<double>::quiet_NaN();
    mutable double m_restPosition = std::numeric_limits<double>::quiet_NaN();
    // Ключ кэша: при изменении любого из значений траектория пересчитывается.
    mutable double m_cacheMass = std::numeric_limits<double>::quiet_NaN();
    mutable double m_cacheStiffness = std::numeric_limits<double>::quiet_NaN();
    mutable double m_cacheFriction = std::numeric_limits<double>::quiet_NaN();
    mutable double m_cacheOffset = std::numeric_limits<double>::quiet_NaN();
    mutable double m_cacheHorizon = std::numeric_limits<double>::quiet_NaN();
};

}  // namespace lab::models
