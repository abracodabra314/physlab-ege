#pragma once

#include <QPointF>

#include "core/Simulation.h"

namespace lab::models {

/**
 * Модель 8. Полёт снаряда по параболе без сопротивления воздуха.
 *
 * Система координат: начало — на поверхности земли под точкой выстрела,
 * ось X вправо, ось Y вверх. Снаряд вылетает из точки (x₀, y₀) со
 * скоростью v₀ под углом θ к горизонту:
 *   v₀x = v₀·cos θ,   v₀y = v₀·sin θ
 *   x(t) = x₀ + v₀·cos θ·t
 *   y(t) = y₀ + v₀·sin θ·t − g·t²/2
 *   vₓ(t) = v₀·cos θ,   vᵧ(t) = v₀·sin θ − g·t
 *   aₓ = 0,   aᵧ = −g
 *
 * Момент падения учитывает высоту выстрела: y(T) = 0 при
 *   T = (v₀·sin θ + √(v₀²·sin²θ + 2·g·y₀)) / g
 * — случай выстрела с поверхности (y₀ = 0) является частным, а не единственным.
 *
 * Уравнение траектории:
 *   y(x) = y₀ + (x − x₀)·tan θ − g(x − x₀)² / (2 v₀² cos²θ)
 */
class ProjectileModel : public Simulation {
    Q_OBJECT
public:
    explicit ProjectileModel(QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("projectile"); }
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
    /** Перетаскивание снаряда или направления выстрела начинает полёт заново. */
    void beginInteraction() override;

    // ---------------- параметры ----------------
    double speed() const;
    /** Угол выстрела в градусах. */
    double angleDegrees() const;
    /** Угол выстрела в радианах. */
    double angleRadians() const;
    double launchHeight() const;
    double launchPosition() const;
    double gravity() const;

    // ---------------- величины по формулам ----------------
    double initialVx() const;
    double initialVy() const;
    /** Время падения на землю с учётом высоты выстрела. */
    double flightTime() const;
    /** Дальность полёта по горизонтали. */
    double range() const;
    /** Максимальная высота подъёма. */
    double maxHeight() const;
    /** Момент прохождения верхней точки. */
    double apexTime() const;

    // ---------------- состояние в момент t ----------------
    double xAt(double t) const;
    double yAt(double t) const;
    double vxAt(double t) const;
    double vyAt(double t) const;
    /** Движется ли снаряд в момент t (после падения проекции скорости равны нулю). */
    bool isFlying(double t) const;
    /** Вертикальная координата траектории над точкой x (формула y(x)). */
    double trajectoryY(double x) const;
    /** Скорость в момент падения (по модулю и под углом к горизонту). */
    double impactVx() const;
    double impactVy() const;
    double impactSpeed() const;
    double impactAngleDegrees() const;
    /** Скорость в момент t. */
    double speedAt(double t) const;

    // ---------------- ряды для графиков и трассы ----------------
    QVector<QPointF> trajectoryPoints() const;
    QVector<QPointF> xSeries() const;
    QVector<QPointF> ySeries() const;
    QVector<QPointF> vxSeries() const;
    QVector<QPointF> vySeries() const;

private:
    /** Модельное время останавливается на падении: дальше состояние не меняется. */
    double clampedTime(double t) const;
};

}  // namespace lab::models
