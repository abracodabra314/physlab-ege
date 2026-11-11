#pragma once

#include "core/Simulation.h"

namespace lab::models {

/**
 * Модель 5. Совместная работа механизмов.
 *
 * Единая математическая модель (вся работа A = 1, время в минутах):
 *   A(t) = Σ pᵢ · max(0, t − tᵢ),
 * где pᵢ — производительность i-го механизма (доля всей работы в минуту),
 * tᵢ — момент его включения. Механизм, отмеченный «выключен», в sums
 * не участвует (p = 0).
 *
 * Частные случаи:
 *   все tᵢ = 0            ⇒ A = (p₁ + p₂ + …)·t,  t = 1 / Σp;
 *   один t₁ = 0, у others t₂ > 0 ⇒ сначала работает один, потом вместе
 *                          (классическая схема задач ЕГЭ).
 * Время завершения — кусочно-линейное уравнение A(t) = 1, решается по
 * интервалам между моментами включения.
 */
class WorkModel : public Simulation {
    Q_OBJECT
public:
    explicit WorkModel(QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("work"); }
    QString title() const override;
    QString subtitle() const override;
    QString statement() const override;
    QString problemText() const override;

    QString timeUnit() const override { return QStringLiteral("min"); }
    double timePerSecond() const override;
    double duration() const override;
    double stepSize() const override;

    QVector<ParamSpec> paramSpecs() const override;
    ValidationResult validate() const override;

    SimState stateAt(double t) const override;
    double eventTime() const override;   // момент выполнения всей работы

    Derivation derivation() const override;
    QVector<Readout> readouts() const override;

    QVector<HandleInfo> handles() const override;

    // ---------------- чистая математика (для тестов) ----------------
    /** Производительности включённых механизмов, доля работы в минуту. */
    QVector<double> activeRates() const;
    /** Суммарная производительность всех включённых механизмов. */
    double totalRate() const;
    /** Выполненная часть работы к моменту t (0..1). */
    double progressAt(double t) const;
    /** Время выполнения всей работы; inf — работа не завершится. */
    double completionTime() const;
    /** Личный вклад механизма index к моменту t (0..1). */
    double contributionOf(int index, double t) const;
    /** Работает ли механизм index в момент t (включён, время пришло, работа не завершена). */
    bool isWorking(int index, double t) const;
    /** Момент включения механизма index, мин. */
    double startTimeOf(int index) const;
    /** Производительность механизма index с учётом его состояния (0, если выключен). */
    double rateOf(int index) const;
    /** Включён ли механизм index. */
    bool enabled(int index) const;

private:
};

}  // namespace lab::models
