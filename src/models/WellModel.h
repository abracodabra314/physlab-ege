#pragma once

#include "core/Simulation.h"

namespace lab::models {

/**
 * Модель 4. Колодец: падение камня и звуковое эхо.
 *
 * Математическая модель (глубина h в метрах, время в секундах):
 *   t_падения = sqrt(2h / g)            (свободное падение без сопротивления)
 *   t_звука   = h / c                   (звук поднимается равномерно)
 *   T = t_падения + t_звука             (время до возвращения эха)
 *
 * Если по известному времени эха T требуется найти глубину, получается
 * уравнение, квадратичное относительно u = sqrt(h):
 *   h/c + sqrt(2/g) * u - T = 0,
 * которое решается аналитически; метод depthFromEchoTime() использует
 * это решение и проверяется численно в тестах.
 */
class WellModel : public Simulation {
    Q_OBJECT
public:
    explicit WellModel(QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("well"); }
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
    double eventTime() const override;   // момент возвращения эха

    Derivation derivation() const override;
    QVector<Readout> readouts() const override;

    QVector<HandleInfo> handles() const override;

    // ---------------- чистая математика (для тестов) ----------------
    /** Глубина колодца, м. */
    double depth() const;
    /** Глубина точки отпуска камня от устья, м (0 — из устья). */
    double releaseDepth() const;
    /** Ускорение свободного падения, м/с^2. */
    double gravity() const;
    /** Скорость звука в воздухе, м/с. */
    double soundSpeed() const;
    /** Время падения камня до воды: sqrt(2(h − h₀)/g). */
    double fallTime() const;
    /** Время подъёма звука от воды до устья: h/c. */
    double soundTime() const;
    /** Полное время до эха: t_падения + t_звука. */
    double echoTime() const;
    /** Глубина камня в момент t (м от устья, 0 <= y <= h). */
    double stoneDepthAt(double t) const;
    /** Глубина фронта звука в момент t (м от дна вверх; NaN, если звука ещё нет). */
    double soundFrontAt(double t) const;
    /**
     * Глубина колодца по измеренному времени эха T — обратная задача.
     * Решается квадратное уравнение относительно u = sqrt(h);
     * возвращается физический (положительный) корень, NaN при недопустимых данных.
     */
    static double depthFromEchoTime(double totalTime, double gravity, double soundSpeed);

private:
};

}  // namespace lab::models
