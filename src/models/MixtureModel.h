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
 * Управление кранами: линия с закрытым краном не участвует ни в переливе,
 * ни в итоговой концентрации. Режим «по очереди» переключает насос на
 * вторую линию после опорожнения первой; режим «одновременно» открывает
 * обе линии в t = 0. Концентрация смеси в промежуточный момент
 * пересчитывается по текущим объёмам, уже перенесённым в смеситель,
 * поэтому график концентрации физически корректен.
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
    /** Открыт ли кран линии number. */
    bool tapOpen(int number) const;
    /** true — оба крана открываются сразу, false — по очереди. */
    bool pumpsSimultaneously() const;
    /** Время перекачки содержимого линии number при закрытой конкурентной нагрузке, мин. */
    double pumpDuration(int number) const;
    /** Момент старта линии number (учитывает краны и режим). */
    double lineStartTime(int number) const;
    /** Момент переключения насоса на второй раствор (старт линии 2). */
    double pumpSwitchTime() const;
    /** Масса растворённого вещества раствора number (0..1), усл. ед. (л·доля). */
    double soluteOf(int number) const;
    /** Итоговый объём смеси (по открытым кранам). */
    double totalVolume() const;
    /** Итоговая масса вещества (по открытым кранам). */
    double totalSolute() const;
    /** Итоговая концентрация смеси, доля. */
    double finalConcentration() const;
    /** Объём раствора, уже перекачанный из ёмкости number к моменту t. */
    double transferredAt(int number, double t) const;
    /** Текущая концентрация в смесителе в момент t (0, если смеси ещё нет). */
    double currentConcentrationAt(double t) const;

private:
    static constexpr double kPumpRate = 60.0;  // л/мин — скорость перекачки

    double secondPhaseStart() const;
};

}  // namespace lab::models
