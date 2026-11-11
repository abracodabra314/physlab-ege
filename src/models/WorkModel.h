#pragma once

#include "core/Simulation.h"

namespace lab::models {

/**
 * Модель 5. Совместная работа механизмов.
 *
 * Математическая модель (вся работа A = 1, время в минутах):
 *   A = (p1 + p2 + ...) * t,  где p_i — производительность механизма
 *   (доля всей работы в минуту).
 *   Время совместной работы: t = 1 / (p1 + p2 + ...).
 *
 * Поддерживается раздельная работа: выбранный механизм выполняет
 * заданную часть работы сам, затем подключаются остальные
 * (классическая схема задач ЕГЭ «сначала один, потом вместе»).
 */
class WorkModel : public Simulation {
    Q_OBJECT
public:
    enum class Mode { Together = 0, Sequential = 1 };

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
    /** Время выполнения всей работы при выбранном сценарии; inf — невозможно. */
    double completionTime() const;
    /** Время, в течение которого работает только механизм soloIndex (режим «раздельно»). */
    double soloTime() const;
    /** Выбранный сценарий работы. */
    Mode mode() const;
    /** Индекс механизма, работающего в одиночку (режим «раздельно»). */
    int soloIndex() const;
    /** Производительность механизма index с учётом его состояния (0, если выключен). */
    double rateOf(int index) const;
    /** Включён ли механизм index. */
    bool enabled(int index) const;

private:
};

}  // namespace lab::models
