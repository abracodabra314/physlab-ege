#pragma once

#include "core/Simulation.h"

namespace lab::models {

/**
 * Модель 6. Смешивание двух растворов.
 *
 * Математическая модель (объёмы в литрах, концентрация в долях):
 *   m_i = V_i * C_i                      — масса растворённого вещества
 *   C_смеси = (V1*C1 + V2*C2) / (V1 + V2)
 *
 * Анимация: сначала содержимое первой ёмкости перекачивается в смеситель,
 * затем — второй (переключение направлений). Концентрация смеси в
 * промежуточный момент пересчитывается по текущим объёмам, уже перенесённым
 * в смеситель, поэтому график концентрации физически корректен.
 */
class MixtureModel : public Simulation {
    Q_OBJECT
public:
    explicit MixtureModel(QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("mixture"); }
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
    double eventTime() const override;   // момент, когда оба раствора перелиты

    Derivation derivation() const override;
    QVector<Readout> readouts() const override;

    QVector<HandleInfo> handles() const override;

    // ---------------- чистая математика (для тестов) ----------------
    /** Объём раствора number, л. */
    double volumeOf(int number) const;
    /** Концентрация раствора number (доля, 0..1). */
    double concentrationOf(int number) const;
    /** Момент переключения насоса на второй раствор. */
    double pumpSwitchTime() const;
    /** Масса растворённого вещества раствора number (0..1), усл. ед. (л·доля). */
    double soluteOf(int number) const;
    /** Итоговый объём смеси. */
    double totalVolume() const;
    /** Итоговая масса вещества. */
    double totalSolute() const;
    /** Итоговая концентрация смеси, доля. */
    double finalConcentration() const;
    /** Объём раствора, уже перекачанный из ёмкости number к моменту t. */
    double transferredAt(int number, double t) const;
    /** Текущая концентрация в смесителе в момент t (0, если смеси ещё нет). */
    double currentConcentrationAt(double t) const;

private:
    static constexpr double kPumpRate = 60.0;  // л/мин — скорость перекачки

    double pumpDuration(int number) const;
    double secondPhaseStart() const;
};

}  // namespace lab::models
