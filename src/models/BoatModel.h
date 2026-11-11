#pragma once

#include "core/Simulation.h"

namespace lab::models {

/**
 * Модель 2. Два теплохода между пристанями.
 *
 * Математическая модель:
 *   скорость относительно берега v = v_собственная ± v_течения
 *   («+» по течению, «−» против течения);
 *   время пути между пристанями: T = L / v.
 *
 * Координата теплохода вдоль реки (км от нижней пристани):
 *   s(t) = s_порт + v_берег * (t - t_отправления), теплоход идёт до причала.
 *
 * Ограничение физики: при движении против течения собственная скорость
 * должна быть больше скорости течения, иначе теплоход не сможет идти вверх.
 */
class BoatModel : public Simulation {
    Q_OBJECT
public:
    enum class Direction { Downstream = 0, Upstream = 1 };

    explicit BoatModel(QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("boat"); }
    QString title() const override;
    QString subtitle() const override;
    QString statement() const override;
    QString problemText() const override;

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

    // ---------------- чистая математика (для тестов) ----------------
    /** Расстояние между пристанями, км. */
    double distanceBetweenPorts() const;
    /** Скорость течения реки, км/ч. */
    double currentSpeed() const;
    /** Скорость относительно берега (км/ч, знакованная вниз по течению). */
    double shoreSpeedOf(int number) const;
    /** Время рейса теплохода number от порта до причала. */
    double travelTimeOf(int number) const;
    /** Время прибытия теплохода number (отсчёт от общего нуля). */
    double arrivalTimeOf(int number) const;
    /** Координата теплохода number в момент t (км, стоянка — на берегу). */
    double positionOf(int number, double t) const;
    /** Момент последнего прибытия (ключевое событие наблюдения). */
    double lastArrivalTime() const;

    /** Собственная скорость теплохода number, км/ч. */
    double ownSpeedOf(int number) const;
    /** Задержка отправления теплохода number, ч. */
    double departureOf(int number) const;
    /** Направление рейса теплохода number. */
    Direction directionOf(int number) const;
    /** Координата пункта отправления теплохода number, км. */
    double portOf(int number) const;
    /** Координата причала назначения теплохода number, км. */
    double berthOf(int number) const;

private:
};

}  // namespace lab::models
