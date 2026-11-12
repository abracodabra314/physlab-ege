#pragma once

#include <QPointF>

#include "core/Simulation.h"

namespace lab::models {

/**
 * Модель 7B. Груз, подвешенный на вертикальной пружине.
 *
 * Ось Y направлена вниз (в сторону силы тяжести), начало отсчёта —
 * натуральная длина пружины. Равновесие наступает при
 *   k·Δl₀ = m·g  ⇒  Δl₀ = m·g/k
 * Если x — смещение относительно положения равновесия, то
 *   m·x'' = -k·x
 * (сила тяжести входит в уравнение, но её действие уже учтено выбором
 * начала отсчёта: она смещает положение равновесия и не влияет на период).
 *
 * Поверхности нет, трения в модели нет, поэтому задача линейна и решается
 * точно:
 *   ω = √(k/m),   T = 2π√(m/k),   x(t) = A·cos(ωt + φ)
 * В отличие от горизонтальной сцены здесь численное интегрирование не
 * требуется — stateAt(t) вычисляется по формуле и потому точна.
 */
class SpringVerticalModel : public Simulation {
    Q_OBJECT
public:
    explicit SpringVerticalModel(QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("spring_v"); }
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
    /** Перетаскивание груза начинает эксперимент заново. */
    void beginInteraction() override;

    // ---------------- параметры ----------------
    double mass() const;
    double stiffness() const;
    double initialDisplacement() const;
    double gravity() const;

    // ---------------- величины по формулам ----------------
    /** Растяжение пружины в равновесии Δl₀ = mg/k. */
    double equilibriumElongation() const;
    /** Циклическая частота ω = √(k/m). */
    double omega() const;
    /** Период T = 2π√(m/k). */
    double period() const;
    /** Фаза начальных условий: x(0) = A cos φ, v(0) = 0. */
    double initialPhase() const;
    /** Амплитуда A = |x₀|. */
    double amplitude() const;

    // ---------------- состояние движения ----------------
    /** Смещение относительно положения равновесия (вниз положительно). */
    double displacementAt(double t) const;
    double velocityAt(double t) const;
    /** Ускорение из уравнения движения a = -(k/m)x. */
    double accelerationOf(double x) const;
    double accelerationAt(double t) const;
    /** Растяжение пружины в момент t: Δl = Δl₀ + x. */
    double elongationAt(double t) const;
    /** Модуль силы упругости k·Δl в момент t. */
    double elasticForceAt(double t) const;
    /** Сила тяжести mg. */
    double weight() const;

    /** Максимальное и минимальное растяжение пружины за время наблюдения. */
    double maxElongation() const;
    double minElongation() const;

    // ---------------- ряды для графиков ----------------
    QVector<QPointF> displacementSeries() const;
    QVector<QPointF> velocitySeries() const;
    QVector<QPointF> accelerationSeries() const;
};

}  // namespace lab::models
