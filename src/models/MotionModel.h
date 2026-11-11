#pragma once

#include <vector>

#include "core/Simulation.h"

namespace lab::models {

/**
 * Модель 1. Два движущихся автомобиля на прямой дороге.
 *
 * Математическая модель (координаты вдоль дороги в км, время в ч):
 *   s1(t) = s10 + v1 * max(0, t - t1_start)
 *   s2(t) = s20 + v2 * max(0, t - t2_start)
 * знаковое направление задаётся выбором направления (v > 0 — вправо).
 *
 * Ключевое событие задачи — встреча (движение навстречу) или обгон
 * (один догоняет другого): моменты, когда s1(t) = s2(t).
 */
class MotionModel : public Simulation {
    Q_OBJECT
public:
    enum class Direction { Right = 0, Left = 1 };

    explicit MotionModel(QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("motion"); }
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
    /** Начальная координата автомобиля number (0 или 1). */
    double startOf(int number) const;
    /** Координата автомобиля number (0 или 1) в момент t. */
    double positionOf(int number, double t) const;
    /** Скорость автомобиля с учётом направления (знакованная, км/ч). */
    double signedVelocityOf(int number) const;
    /** Расстояние между автомобилями в момент t (|s1 - s2|). */
    double distanceAt(double t) const;
    /** Момент, когда координаты равны (встреча/обгон); NaN, если его нет. */
    double meetingTime() const;

private:
    struct Kinematics {
        double s10 = 0.0;
        double s20 = 0.0;
        double v1 = 0.0;   // знакованные скорости
        double v2 = 0.0;
        double t1 = 0.0;   // задержки старта
        double t2 = 0.0;
    };
    Kinematics kinematics() const;

    double speedOf(int number) const;      // модуль скорости из параметров
    Direction directionOf(int number) const;
    double delayOf(int number) const;
};

}  // namespace lab::models
