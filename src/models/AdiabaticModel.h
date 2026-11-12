#pragma once

#include <QPointF>

#include "core/Simulation.h"

namespace lab::models {

/**
 * Модель 11. Адиабатное сжатие (расширение) газа в цилиндре с поршнем.
 *
 * Сосуд теплоизолирован, поэтому Q = 0 и первое начало принимает вид
 *   A = -ΔU,
 * где A — работа газа, U = ν·Cv·T — внутренняя энергия. Для идеального газа
 * с подвижками только у поступательных и вращательных степеней свободы
 *   Cv = R / (γ - 1),   Cp = γR / (γ - 1),   γ = Cp / Cv,
 * откуда интегрирование dA = p·dV вместе с уравнением Менделеева–Клапейрона
 * даёт уравнение Пуассона
 *   p·V^γ = const,   T·V^(γ-1) = const,   p^(1-γ)·T^γ = const.
 *
 * Процесс в модели медленный (квазистатический): объём меняется по линейному
 * закону V(t) = V₁ + (V₂ - V₁)·t/τ, а давление и температура в каждый момент
 * считаются по уравнениям адиабаты, а не подставляются «конечными» числами.
 * Именно поэтому все графики строятся из состояний модели.
 *
 * Сила, которой газ давит на поршень, F = p·S тоже берётся из текущего
 * давления: при быстром сжатии поршень приходилось бы толкать сильнее, и
 * модель это показывает (внешняя сила Fвнеш = (p - p₀)·S растёт вместе с p).
 *
 * Среднеквадратичная скорость молекул
 *   v_кв = √(3RT / M)
 * зависит только от температуры, поэтому отношение скоростей в начале и в
 * конце процесса равно √(T₂/T₁) = (V₁/V₂)^((γ-1)/2): сжатие разгоняет
 * молекулы, и на схеме они движутся быстрее.
 */
class AdiabaticModel : public Simulation {
    Q_OBJECT
public:
    explicit AdiabaticModel(QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("adiabatic"); }
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
    /** Перетаскивание поршня начинает процесс заново: новое состояние — начало. */
    void beginInteraction() override;

    // ---------------- параметры ----------------
    /** Количество вещества, моль. */
    double substance() const;
    /** Начальная температура T₁, К. */
    double initialTemperature() const;
    /** Начальный объём V₁, м³ (на панели показывается в литрах). */
    double initialVolume() const;
    /** Объём в конце процесса V₂, м³. */
    double finalVolume() const;
    /** Показатель адиабаты γ = Cp/Cv. */
    double gamma() const;
    /** Молярная масса газа, кг/моль (на панели — в г/моль). */
    double molarMass() const;
    /** Площадь поршня S, м² (на панели — в см²). */
    double pistonArea() const;
    /** Внешнее давление p₀, Па. */
    double outsidePressure() const;
    /** Длительность процесса τ, с. */
    double processTime() const;
    /** Название газа из списка: для подсказки о γ и молярной массе. */
    QString gasName() const;

    // ---------------- величины процесса ----------------
    /** Молярная теплоёмкость Cv = R/(γ − 1), Дж/(моль·К). */
    double molarHeatCapacity() const;
    /** Молярная теплоёмкость Cp = γR/(γ − 1), Дж/(моль·К). */
    double molarHeatCapacityP() const;
    /** Число молекул N = ν·N_A. */
    double moleculeCount() const;

    /** Начальное давление из уравнения МK: p₁ = νRT₁/V₁, Па. */
    double initialPressure() const;
    /** Конечное давление p₂ = p₁(V₁/V₂)^γ, Па. */
    double finalPressure() const;
    /** Конечная температура T₂ = T₁(V₁/V₂)^(γ−1), К. */
    double finalTemperature() const;
    /** Степень сжатия V₁/V₂ (больше 1 — сжатие, меньше 1 — расширение). */
    double compressionRatio() const;

    /** Объём в момент t (линейный закон, после τ объём не меняется). */
    double volumeAt(double t) const;
    /** Давление в момент t из уравнения адиабаты, Па. */
    double pressureAt(double t) const;
    /** Температура в момент t из уравнения адиабаты, К. */
    double temperatureAt(double t) const;
    /** Давление как функция объёма: p(V) = p₁(V₁/V)^γ, Па. */
    double pressureForVolume(double volume) const;
    /** Температура как функция объёма: T(V) = T₁(V₁/V)^(γ−1), К. */
    double temperatureForVolume(double volume) const;

    /** Внутренняя энергия U = νCvT в момент t, Дж. */
    double internalEnergyAt(double t) const;
    /** Работа газа A(t) = (p₁V₁ − p₂V(t))/(γ − 1), Дж (отрицательна при сжатии). */
    double workAt(double t) const;
    /** Изменение внутренней энергии ΔU(t) = U(t) − U₁, Дж. */
    double energyChangeAt(double t) const;
    /** Полная работа газа за процесс, Дж. */
    double totalWork() const;
    /** Теплообмен процесса: ноль, потому что сосуд теплоизолирован. */
    double heatAmount() const { return 0.0; }

    /** Сила давления газа на поршень F = pS в момент t, Н. */
    double gasForceAt(double t) const;
    /** Сила внешнего давления p₀S, Н. */
    double outsideForce() const;
    /** Внешняя сила, needed для медленного процесса: (p − p₀)S по модулю, Н. */
    double externalForceAt(double t) const;
    /** Скорость поршня в момент t, м/с (0 после конца процесса). */
    double pistonSpeedAt(double t) const;

    /** Среднеквадратичная скорость молекул v = √(3RT/M) при температуре T, м/с. */
    double rmsSpeedAt(double temperature) const;
    /** Среднеквадратичная скорость молекул в момент t, м/с. */
    double rmsSpeedAtTime(double t) const;

    // ---------------- ряды данных ----------------
    /** Адиабата в координатах (V, p) и (V, T); V меняется от V₁ до V₂. */
    QVector<QPointF> pressureVersusVolume() const;
    QVector<QPointF> temperatureVersusVolume() const;
    /** Изотерма p = νRT₁/V через начальное состояние — для сравнения крутизны. */
    QVector<QPointF> isothermVersusVolume() const;
    /** Зависимость (T, p) вдоль адиабаты: p ∝ T^(γ/(γ−1)). */
    QVector<QPointF> pressureVersusTemperature() const;
    /** Временные ряды: объём, давление, температура, работа, ΔU. */
    QVector<QPointF> volumeSeries() const;
    QVector<QPointF> pressureSeries() const;
    QVector<QPointF> temperatureSeries() const;
    QVector<QPointF> workSeries() const;
    QVector<QPointF> energyChangeSeries() const;

private:
    /** Границы ряда по объёму: от меньшего из V₁, V₂ до большего. */
    void volumeRange(double& minVolume, double& maxVolume) const;
};

}  // namespace lab::models
