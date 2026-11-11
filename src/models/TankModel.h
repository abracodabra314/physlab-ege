#pragma once

#include "core/Simulation.h"

namespace lab::models {

/**
 * Модель 3. Вертикальный резервуар с кранами наполнения и слива.
 *
 * Математическая модель (объём в литрах, время в минутах):
 *   V(t) = V0 + (q_нап1 + q_нап2 - q_сл1 - q_сл2) * t,
 *   уровень h(t) = V(t) / S, где S — площадь основания резервуара.
 *
 * Ограничения физики: 0 <= V(t) <= V_max (резервуар нельзя наполнить
 * сверх вместимости и нельзя вылить больше воды, чем в нём есть).
 * Производительность каждого потока постоянна (первая версия модели).
 */
class TankModel : public Simulation {
    Q_OBJECT
public:
    explicit TankModel(QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("tank"); }
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
    double eventTime() const override;

    Derivation derivation() const override;
    QVector<Readout> readouts() const override;

    QVector<HandleInfo> handles() const override;

    // ---------------- чистая математика (для тестов) ----------------
    /** Сторона основания резервуара, м. */
    double width() const;
    /** Высота резервуара, м. */
    double height() const;
    /** Начальный объём воды, л. */
    double initialVolume() const;
    /** Площадь основания резервуара, м^2 (V = S * h, 1 м^3 = 1000 л). */
    double baseArea() const;
    /** Вместимость резервуара, л. */
    double capacity() const;
    /** Открыт ли кран с указанным id (in1, in2, out1, out2). */
    bool tapOpen(const QString& tapId) const;
    /** Производительность крана с учётом его состояния (0, если закрыт), л/мин. */
    double tapRate(const QString& tapId) const;
    /** Суммарная производительность наполнения, л/мин (учитывает выключенные краны). */
    double inflowRate() const;
    /** Суммарная производительность слива, л/мин. */
    double outflowRate() const;
    /** Итоговый приток: Q = inflow - outflow (л/мин), может быть отрицательным. */
    double netRate() const;
    /** Объём воды в момент t с учётом ограничений 0 <= V <= V_max. */
    double volumeAt(double t) const;
    /** Уровень воды в момент t, м. */
    double levelAt(double t) const;
    /** Время наполнения текущего объёма доверху; inf, если невозможно. */
    double timeToFill() const;
    /** Время опустошения до нуля; inf, если невозможно. */
    double timeToEmpty() const;

private:
    struct Flows {
        double qIn1 = 0.0;
        double qIn2 = 0.0;
        double qOut1 = 0.0;
        double qOut2 = 0.0;
    };
    Flows flows() const;
};

}  // namespace lab::models
