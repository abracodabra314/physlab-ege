#pragma once

#include <QPointF>

#include "core/Simulation.h"

namespace lab::models {

/**
 * Модель 10. Гудок судна: распространение звука и приём сигналов.
 *
 * Ось X направлена от источника звука к наблюдателю. Источник (гудок судна)
 * испускает сигналы через равные промежутки T; каждый фронт расходится из
 * ТОЙ точки, где он был издан:
 *
 *   t_k = t₀ + k·T — момент излучения k-го сигнала,
 *   x_k = v₁·t_k   — положение источника в этот момент,
 *   r_k(t) = c·(t − t_k),  t ≥ t_k — радиус фронта.
 *
 * Фронт не следует за судном: где звук родился, там его центр и будет
 * вечно. Это и есть физический смысл задачи — центр окружности фиксирован,
 * а радист растёт со скоростью звука c.
 *
 * Наблюдатель движется по той же оси: x₂(t) = L₀ + v₂·t. Условие приёма
 * k-го сигнала — фронт настигает наблюдателя:
 *
 *   x_k + c(t − t_k) = L₀ + v₂ t
 *   ⇒  t_приём(k) = (L₀ + (c − v₁)·t_k) / (c − v₂)
 *
 * Промежуток между приёмами соседних сигналов (эффект Доплера):
 *
 *   T′ = t_приём(k+1) − t_приём(k) = T·(c − v₁)/(c − v₂)
 *
 * Направления движения задаются выбором в панели: «сближаются» — скорость
 * положительная, «удаляются» — отрицательная. Формулы при этом не меняются:
 * знак скорости сам переносит смысл в расчёт, а T′ получается меньше T при
 * сближении и больше — при удалении.
 *
 * Скорость звука берётся из температуры воздуха по школьной формуле
 * c = 331 + 0,6·t° — поэтому температуру можно менять и видеть, как
 * меняются все времена.
 */
class SoundModel : public Simulation {
    Q_OBJECT
public:
    explicit SoundModel(QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("sound"); }
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
    /** Перетаскивание начинает наблюдение заново, чтобы моменты приёма не «прыгали». */
    void beginInteraction() override;

    /** Число фронтов, которые судно испускает по условию наблюдения. */
    static int signalCount() { return kSignals; }

    // ---------------- параметры ----------------
    /** Температура воздуха, °C. */
    double temperatureCelsius() const;
    /** Скорость звука в воздухе при этой температуре: c = 331 + 0,6·t. */
    double soundSpeed() const;
    /** Начальное расстояние между судами, м. */
    double initialGap() const;
    /** Скорость судна с гудком вдоль оси X (в сторону наблюдателя — положительно), м/с. */
    double sourceSpeed() const;
    /** Скорость судна наблюдателя вдоль той же оси, м/с. */
    double observerSpeed() const;
    /** Промежуток между излучениями сигнала, с. */
    double signalPeriod() const;

    // ---------------- закон движения ----------------
    /** Положение источника звука, м. */
    double sourcePositionAt(double t) const;
    /** Положение наблюдателя, м. */
    double observerPositionAt(double t) const;
    /** Расстояние между судами, м. */
    double gapAt(double t) const;

    // ---------------- фронты звука ----------------
    /** Момент излучения k-го сигнала, с. */
    double emissionTime(int index) const;
    /** Точка, из которой излучён k-й фронт (она не меняется со временем). */
    double emissionPoint(int index) const;
    /** Радиус k-го фронта в момент t, м (0, если фронт ещё не излучён). */
    double frontRadius(int index, double t) const;
    /** Момент приёма k-го сигнала наблюдателем, с (NaN — не будет принят). */
    double arrivalTime(int index) const;
    /** Число сигналов, уже излучённых к моменту t. */
    int emittedCount(double t) const;
    /** Число сигналов, уже принятых к моменту t. */
    int receivedCount(double t) const;
    /** Промежуток между приёмами соседних сигналов, с. */
    double receptionPeriod() const;
    /** Промежуток между приёмами при других скоростях судов. */
    double receptionPeriodFor(double sourceSpeed, double observerSpeed) const;

    // ---------------- ряды данных ----------------
    /** Расстояние между судами: точки (t, L). */
    QVector<QPointF> gapSeries() const;
    /** Радиус k-го фронта: точки (t, r). */
    QVector<QPointF> frontSeries(int index) const;
    /** Закон движения наблюдателя: точки (t, x₂ − L₀). */
    QVector<QPointF> observerPathSeries() const;
    /** Закон движения судна с гудком: точки (t, x₁). */
    QVector<QPointF> sourcePathSeries() const;
    /** Не временные зависимости: (v₂, T′) и (v₁, T′). Обе построены на одной
     *  оси скоростей, поэтому их можно наложить на один график. */
    QVector<QPointF> periodVersusObserverSpeed() const;
    QVector<QPointF> periodVersusSourceSpeed() const;

private:
    static constexpr int kSignals = 3;
    /**
     * Общая граница оси скоростей для графиков T′(v): ряд строится от
     * «навстречу звуку» до скорости звука, при v₂ → c промежуток между
     * приёмами уходит в бесконечность.
     */
    double seriesSpeedMax() const;
};

}  // namespace lab::models
