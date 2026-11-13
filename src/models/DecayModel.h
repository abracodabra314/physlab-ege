#pragma once

#include <QPointF>
#include <QStringList>

#include "core/Simulation.h"

namespace lab::models {

/**
 * Модель 12. Радиоактивный распад: закон N = N₀·2^(−t/T½) и его связь с
 * вероятностью распада одного ядра.
 *
 * Распад случайен для каждого ядра, но закономерен для большого их числа.
 * Вероятность распада одного ядра за малый промежуток Δt равна
 *   Δp = λ·Δt,
 * поэтому за время t ядро не распалось с вероятностью e^(−λt), а число
 * нераспавшихся ядер
 *   N(t) = N₀·e^(−λt).
 * Через период полураспада тот же закон записывают удобнее:
 *   T½ = ln 2 / λ   ⇒   N(t) = N₀·2^(−t/T½),
 * потому что за время, кратное T½, уменьшается степень двойки, а не «число
 * Эйлера». Среднее время жизни связано с периодом полураспада как
 *   τ = 1/λ = T½ / ln 2 ≈ 1,443·T½.
 *
 * В модели два разных счёта, и различие между ними показано на графиках:
 *  1) теория — гладкая кривая N₀·e^(−λt); в ней число ядер дробное, потому
 *     что это математическое ожидание, а не результат одного опыта;
 *  2) дискретный счёт — модель шаг за шагом «перебирает» ядра с генератором
 *     случайных чисел, у которого фиксированное семя. Он даёт целое число
 *     ядер и около теоретического колеблется: при N₀ = 1000 отклонение
 *     порядка √N ≈ 30 ядер, то есть единицы процентов.
 * Случайность воспроизводима: при одних и тех же параметрах ряд наблюдений
 * всегда тот же, поэтому картинка и проверки не «дрожат».
 *
 * Число дочерних ядер N₀ − N(t) дополняет нераспавшиеся до N₀ — сумма
 * остаётся прежней, модель ничего не теряет. Активность A = λN убывает по
 * тому же закону; она же есть число распадов в час, которое показывает
 * счётчик на схеме.
 */
class DecayModel : public Simulation {
    Q_OBJECT
public:
    explicit DecayModel(QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("decay"); }
    QString title() const override;
    QString subtitle() const override;
    QString statement() const override;
    QString problemText() const override;

    /** Время модели течёт в часах: периоды полураспада удобны в них. */
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
    void setHandleValue(const QString& id, double value) override;
    /** Перетаскивание ручек начинает наблюдение заново. */
    void beginInteraction() override;

    // ---------------- изотопы из справочника ----------------
    /** Число пунктов списка, включая «свой период полураспада». */
    static int isotopeCount();
    static QString isotopeName(int index);
    /** Период полураспада из справочника, ч (для своего пункта — 0). */
    static double isotopeHalfLife(int index);
    /** Период наблюдений по умолчанию для изотопа: 10 периодов полураспада. */
    static double isotopeObservation(int index);
    /** Индекс пункта «свой период полураспада». */
    static int customIsotopeIndex();

    // ---------------- параметры ----------------
    /** Название изотопа из списка. */
    QString isotopeTitle() const;
    /** Начальное число ядер N₀. */
    double initialNuclei() const;
    /** Постоянная распада λ, 1/ч. */
    double decayConstant() const;
    /** Период полураспада T½, ч. */
    double halfLife() const;
    /** Среднее время жизни τ = 1/λ, ч. */
    double meanLifetime() const;
    /**
     * Текст постоянной распада: λ бывает порядка 10⁻⁸ ч⁻¹ (у долгоживущих
     * изотопов), поэтому число знаков подбирают по порядку величины — иначе
     * в подстановке и на панели остался бы ноль.
     */
    QString decayConstantText() const;
    /** Текст вероятности распада за один шаг наблюдения. */
    QString stepProbabilityText() const;
    /**
     * Время в подписях и подстановках: «8 ч», «45,0 мин». Общая функция
     * formatDuration пишет «8 ч 0 мин», а здесь минуты показываются только
     * тогда, когда они есть, — иначе шкала опыта не помещается.
     */
    static QString durationText(double hours);
    /** Целое число ядер в подписях: дробная часть здесь не нужна. */
    static QString nucleiText(double count);
    /** Длительность наблюдения, ч. */
    double observationTime() const;
    /** Целевая доля нераспавшихся ядер d — отметка на шкале сцены. */
    double targetFraction() const;
    /** Момент, когда по теории останется доля d ядер: ln(1/d)/λ, ч. */
    double targetTime() const;
    /** Число узлов наблюдения: их подбирает шаг, малый для опыта. */
    int sampleNodes() const;
    /** Шаг, с которым модель перебирает ядра (интервал наблюдения), ч. */
    double sampleInterval() const;
    /** Вероятность распада одного ядра за один шаг: 1 − e^(−λΔt) ≈ λΔt. */
    double stepProbability() const;
    /** Ожидаемое число распадов за один шаг в момент t. */
    double expectedDecaysPerStepAt(double t) const;

    // ---------------- теория ----------------
    /** Теоретическое число нераспавшихся ядер N₀·e^(−λt). */
    double theoryNucleiAt(double t) const;
    /** Тот же закон через степень двойки: N₀·2^(−t/T½). */
    double theoryNucleiByHalfLife(double t) const;
    /** Доля нераспавшихся ядер e^(−λt) = 2^(−t/T½). */
    double survivingFractionAt(double t) const;
    /** Вероятность того, что одно ядро доживёт до момента t. */
    double survivalProbabilityAt(double t) const { return survivingFractionAt(t); }
    /** Распавшиеся по теории ядра N₀ − N(t). */
    double decayedTheoryAt(double t) const;
    /** Число периодов полураспада, прошедших к моменту t. */
    double halfLivesElapsedAt(double t) const;

    // ---------------- дискретный опыт ----------------
    /** Число нераспавшихся ядер по дискретному счёту в момент t. */
    double countedNucleiAt(double t) const;
    /** Число распадов по дискретному счёту к моменту t. */
    double countedDecaysAt(double t) const;
    /** Распады за один шаг наблюдения: их показывает счётчик на схеме. */
    double decaysInLastStepAt(double t) const;
    /** Активность по теории A = λN, распадов в час. */
    double activityAt(double t) const;
    /** Активность по дискретному счёту, распадов в час. */
    double countedActivityAt(double t) const;
    /** Оценочное отклонение счёта от теории: √N ядер. */
    double expectedDeviationAt(double t) const;
    /** Расхождение счёта и теории как доля от N₀, %. */
    double relativeDeviationAt(double t) const;

    // ---------------- ряды данных ----------------
    /** Ряд наблюдений: ступенька целого числа ядер. */
    QVector<QPointF> countedSeries() const;
    /** Гладкая теоретическая кривая N₀·e^(−λt). */
    QVector<QPointF> theorySeries() const;
    /** Теоретическое число распадов N₀ − N(t). */
    QVector<QPointF> decayedTheorySeries() const;
    /** Распавшиеся ядра по дискретному счёту (ступенька). */
    QVector<QPointF> countedDecaysSeries() const;
    /** Активность A(t) = λN. */
    QVector<QPointF> activitySeries() const;
    /** Активность по показаниям счётчика: распады интервала / его длительность. */
    QVector<QPointF> countedActivitySeries() const;
    /** Натуральный логарифм числа ядер по дискретному счёту. */
    QVector<QPointF> countedLogarithmSeries() const;
    /** Распады по интервалам отображения: показания счётчика, снятые реже. */
    QVector<QPointF> countsPerStepSeries() const;
    /** Натуральный логарифм числа ядер по теории: прямая с наклоном −λ. */
    QVector<QPointF> logarithmSeries() const;
    /** Расхождение счёта и теории, % от N₀. */
    QVector<QPointF> deviationSeries() const;

private:
    /** Строит ряд наблюдений, если параметры постановки изменились. */
    void ensureSamples() const;
    /** Позиция момента t среди узлов ряда. */
    int nodeIndexBefore(double t) const;
    /** Ключ кэша: меняется вместе с параметрами, влияющими на опыт. */
    quint64 cacheKey() const;
    /**
     * Шаг прореживания ряда наблюдений для графиков. Опыт может идти
     * несколькими тысячами шагов, а на рисунке различимы лишь сотни
     * переломов, поэтому лишние узлы пропускают — сама ступенчатая форма
     * при этом сохраняется.
     */
    int seriesStride() const;

    /** Ограничения на шаг опыта: он должен оставаться малым. */
    static constexpr int kMinSampleNodes = 301;
    static constexpr int kMaxSampleNodes = 3001;
    /** Верхняя граница вероятности распада за шаг при подборе узлов. */
    static constexpr double kStepProbabilityCap = 0.02;
    /** Точек в рядах, которые уходят на графики. */
    static constexpr int kPlotPoints = 241;

    // Кэш ряда наблюдений: он не должен перестраиваться каждый кадр.
    mutable QVector<double> m_timeNodes;
    mutable QVector<double> m_alive;
    mutable QVector<double> m_stepDecays;
    mutable quint64 m_cacheKey = 0;
};

}  // namespace lab::models
