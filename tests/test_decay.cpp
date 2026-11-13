#include <gtest/gtest.h>

#include <cmath>

#include "TestSupport.h"
#include "models/DecayModel.h"
#include "utils/MathText.h"

using lab::models::DecayModel;
using lab::test::isInvalid;
using lab::test::setSetup;

namespace {

/** Логарифм двух — та же константа, что в модели. */
constexpr double kLnTwo = 0.6931471805599453;

/** Учебная постановка: 1000 ядер, T½ = 8 ч, наблюдение 40 ч, доля 25 %. */
void configure(DecayModel& model) {
    setSetup(model, QStringLiteral("isotope=7; half=8; N0=1000; T=40; share=25; seed=7"));
}

/** Та же постановка, но с другим семенем счётчика. */
void configureWithSeed(DecayModel& model, double seed) {
    setSetup(model, QStringLiteral("isotope=7; half=8; N0=1000; T=40; share=25; seed=%1").arg(seed));
}

/** Много ядер — относительный разброс счёта мал. */
void configureLarge(DecayModel& model) {
    setSetup(model, QStringLiteral("isotope=7; half=8; N0=5000; T=40; share=25; seed=7"));
}

/** Суммарная статистика по дискретному счёту за всё наблюдение. */
double totalDecaysAtEnd(const DecayModel& model) {
    return model.initialNuclei() - model.countedNucleiAt(model.observationTime());
}

}  // namespace

// ------------------------------------------------------------------
// Период полураспада, постоянная распада и среднее время жизни
// ------------------------------------------------------------------

TEST(Decay, DecayConstantComesFromHalfLife) {
    DecayModel model;
    configure(model);

    // λ = ln2 / T½: связь выводится из N(T½) = N₀/2.
    EXPECT_NEAR(model.decayConstant(), kLnTwo / 8.0, 1e-12);
    EXPECT_NEAR(model.meanLifetime(), 1.0 / model.decayConstant(), 1e-12);
    // τ = T½/ln2 ≈ 1,443·T½ — ядро живёт дольше, чем половина распалась.
    EXPECT_NEAR(model.meanLifetime(), model.halfLife() / kLnTwo, 1e-12);
    EXPECT_GT(model.meanLifetime(), model.halfLife());
}

TEST(Decay, IsotopeChoiceSetsHalfLifeAndObservation) {
    DecayModel custom;
    configure(custom);
    EXPECT_NEAR(custom.halfLife(), 8.0, 1e-12);

    DecayModel listed;
    setSetup(listed, QStringLiteral("isotope=1; N0=1000; T=40; share=25; seed=7"));
    // Пункт списка задаёт и период полураспада, и разумное окно наблюдения.
    EXPECT_GT(listed.halfLife(), 0.0);
    EXPECT_NE(listed.observationTime(), listed.param(QStringLiteral("T")));
    EXPECT_FALSE(isInvalid(listed));

    // Свой пункт берёт T½ и T из полей.
    EXPECT_EQ(DecayModel::customIsotopeIndex(), DecayModel::isotopeCount() - 1);
    EXPECT_NEAR(custom.halfLife(), custom.param(QStringLiteral("half")), 1e-12);
    EXPECT_NEAR(custom.observationTime(), custom.param(QStringLiteral("T")), 1e-12);
}

TEST(Decay, ObservationCoversSeveralHalfLives) {
    DecayModel model;
    configure(model);

    // Наблюдение длиннее периода полураспада: иначе распад не увидеть.
    EXPECT_GT(model.observationTime(), model.halfLife());
    EXPECT_GT(model.halfLivesElapsedAt(model.observationTime()), 1.0);
    EXPECT_NEAR(model.halfLivesElapsedAt(model.halfLife()), 1.0, 1e-12);
    EXPECT_NEAR(model.halfLivesElapsedAt(3.0 * model.halfLife()), 3.0, 1e-12);

    EXPECT_GT(model.duration(), model.observationTime());
    EXPECT_GT(model.stepSize(), 0.0);
    EXPECT_GT(model.timePerSecond(), 0.0);
}

// ------------------------------------------------------------------
// Закон распада: две записи одной величины
// ------------------------------------------------------------------

TEST(Decay, ExponentialAndPowerOfTwoAreTheSameLaw) {
    DecayModel model;
    configure(model);

    // N₀·e^(−λt) и N₀·2^(−t/T½) дают одно и то же число во всех точках.
    for (double fraction : {0.0, 0.25, 0.5, 1.0, 1.5}) {
        const double t = model.observationTime() * fraction;
        EXPECT_NEAR(model.theoryNucleiAt(t), model.theoryNucleiByHalfLife(t), 1e-9) << "момент " << t;
    }
}

TEST(Decay, EveryHalfLifeHalvesTheCount) {
    DecayModel model;
    configure(model);

    const double n0 = model.initialNuclei();
    for (int n = 1; n <= 5; ++n) {
        const double t = model.halfLife() * n;
        const double expected = n0 * std::pow(0.5, n);
        EXPECT_NEAR(model.theoryNucleiAt(t), expected, 1e-9) << "периодов: " << n;
    }
}

TEST(Decay, SurvivingFractionMatchesExponential) {
    DecayModel model;
    configure(model);

    EXPECT_NEAR(model.survivingFractionAt(0.0), 1.0, 1e-15);
    EXPECT_NEAR(model.survivingFractionAt(model.halfLife()), 0.5, 1e-12);
    EXPECT_NEAR(model.survivingFractionAt(2.0 * model.halfLife()), 0.25, 1e-12);
    // Доля — это и есть вероятность того, что одно ядро доживёт до t.
    EXPECT_NEAR(model.survivalProbabilityAt(13.0), std::exp(-model.decayConstant() * 13.0), 1e-15);

    const double n0 = model.initialNuclei();
    for (double t : {3.0, 17.0, 39.0}) {
        EXPECT_NEAR(model.theoryNucleiAt(t), n0 * model.survivingFractionAt(t), 1e-12) << "момент " << t;
    }
}

TEST(Decay, TheoryCountDecaysMonotonically) {
    DecayModel model;
    configure(model);

    double previous = model.initialNuclei();
    for (int i = 1; i <= 80; ++i) {
        const double t = model.observationTime() * i / 80.0;
        const double alive = model.theoryNucleiAt(t);
        EXPECT_LT(alive, previous) << "момент " << t;
        EXPECT_GT(alive, 0.0);
        // Распавшиеся дополняют нераспавшиеся до N₀.
        EXPECT_NEAR(model.decayedTheoryAt(t), model.initialNuclei() - alive, 1e-12);
        previous = alive;
    }
}

TEST(Decay, TargetTimeComesFromLogarithm) {
    DecayModel model;
    configure(model);

    // d = e^(−λt) ⇒ t = ln(1/d)/λ. При d = 25 % это ровно два периода.
    EXPECT_NEAR(model.targetTime(), std::log(4.0) / model.decayConstant(), 1e-12);
    EXPECT_NEAR(model.targetTime(), 2.0 * model.halfLife(), 1e-9);
    EXPECT_NEAR(model.theoryNucleiAt(model.targetTime()), model.initialNuclei() * model.targetFraction(), 1e-9);

    // Другая доля — другой момент:меньше ядер, позже.
    DecayModel smaller;
    configure(smaller);
    smaller.setParam(QStringLiteral("share"), 5.0);
    EXPECT_GT(smaller.targetTime(), model.targetTime());
    EXPECT_NEAR(smaller.theoryNucleiAt(smaller.targetTime()), 50.0, 1e-9);
}

// ------------------------------------------------------------------
// Активность
// ------------------------------------------------------------------

TEST(Decay, ActivityIsLambdaTimesN) {
    DecayModel model;
    configure(model);

    EXPECT_NEAR(model.activityAt(0.0), model.decayConstant() * model.initialNuclei(), 1e-12);
    EXPECT_NEAR(model.activityAt(0.0), 1000.0 * kLnTwo / 8.0, 1e-12);
    for (double t : {5.0, 19.0, 40.0}) {
        EXPECT_NEAR(model.activityAt(t), model.decayConstant() * model.theoryNucleiAt(t), 1e-12) << "момент " << t;
        // Активность убывает с тем же показателем, что и число ядер.
        EXPECT_NEAR(model.activityAt(t) / model.activityAt(0.0), model.survivingFractionAt(t), 1e-12);
    }
}

TEST(Decay, ShorterHalfLifeMeansHigherActivity) {
    DecayModel slow;
    configure(slow);
    DecayModel fast;
    configure(fast);
    fast.setParam(QStringLiteral("half"), 2.0);

    // При том же числе ядер короткоживущий препарат активнее.
    EXPECT_GT(fast.activityAt(0.0), slow.activityAt(0.0));
    EXPECT_GT(fast.decayConstant(), slow.decayConstant());
}

// ------------------------------------------------------------------
// Дискретный опыт: шаг, вероятность и воспроизводимость
// ------------------------------------------------------------------

TEST(Decay, StepProbabilityIsLambdaDeltaT) {
    DecayModel model;
    configure(model);

    // Шаг наблюдения мал: вероятность распада за шаг почти равна λΔt.
    EXPECT_GT(model.sampleInterval(), 0.0);
    EXPECT_LT(model.sampleInterval(), model.halfLife() * 0.1);
    EXPECT_NEAR(model.stepProbability(), 1.0 - std::exp(-model.decayConstant() * model.sampleInterval()), 1e-15);
    EXPECT_NEAR(model.stepProbability(), model.decayConstant() * model.sampleInterval(),
                0.01 * model.stepProbability());
    EXPECT_GT(model.stepProbability(), 0.0);
    EXPECT_LT(model.stepProbability(), 0.25);

    // Ожидаемое число распадов за шаг — λΔt·N в текущий момент.
    EXPECT_NEAR(model.expectedDecaysPerStepAt(0.0), model.stepProbability() * model.initialNuclei(), 1e-12);
}

TEST(Decay, CountedSeriesIsIntegerAndNeverGrows) {
    DecayModel model;
    configure(model);

    const int nodes = model.sampleNodes();
    ASSERT_GT(nodes, 1);
    double previous = model.countedNucleiAt(0.0);
    EXPECT_NEAR(previous, model.initialNuclei(), 1e-12);

    for (int node = 1; node < nodes; ++node) {
        const double t = model.sampleInterval() * node;
        const double alive = model.countedNucleiAt(t);
        EXPECT_NEAR(alive, std::round(alive), 1e-12) << "узел " << node;
        EXPECT_LE(alive, previous) << "узел " << node;
        previous = alive;
    }
    EXPECT_GE(previous, 0.0);
}

TEST(Decay, CountFollowsTheoryWithinExpectedScatter) {
    DecayModel model;
    configure(model);

    // Счёт целыми ядрами колеблется около теории: при N₀ = 1000 отклонение
    // единицы процентов, а не разы.
    for (double fraction : {0.25, 0.5, 0.75, 1.0}) {
        const double t = model.observationTime() * fraction;
        const double theory = model.theoryNucleiAt(t);
        const double counted = model.countedNucleiAt(t);
        const double sigma = model.expectedDeviationAt(t);
        // Оценочный разброс — корень из числа ядер.
        EXPECT_NEAR(sigma, std::sqrt(theory), 1e-9) << "доля времени " << fraction;
        EXPECT_LE(std::abs(counted - theory), 5.0 * sigma + 2.0) << "доля времени " << fraction;
        EXPECT_LE(std::abs(model.relativeDeviationAt(t)), 10.0) << "доля времени " << fraction;
    }

    // Средний распад за время наблюдения соответствует закону.
    const double decays = totalDecaysAtEnd(model);
    EXPECT_NEAR(decays, model.decayedTheoryAt(model.observationTime()), 5.0 *
                        model.expectedDeviationAt(model.observationTime()) + 5.0);
}

TEST(Decay, ExperienceIsReproducibleWithSameSeed) {
    DecayModel first;
    configureWithSeed(first, 7.0);
    DecayModel second;
    configureWithSeed(second, 7.0);

    for (double fraction : {0.2, 0.5, 0.9}) {
        const double t = first.observationTime() * fraction;
        EXPECT_DOUBLE_EQ(first.countedNucleiAt(t), second.countedNucleiAt(t)) << "доля времени " << fraction;
    }
    EXPECT_DOUBLE_EQ(first.decaysInLastStepAt(first.observationTime()),
                     second.decaysInLastStepAt(second.observationTime()));
}

TEST(Decay, OtherSeedGivesAnotherExperience) {
    // Тот же опыт с другим семенем даёт другой набор чисел — в этом и есть
    // случайность. Теоретическая кривая при этом не меняется.
    DecayModel first;
    configureWithSeed(first, 7.0);
    DecayModel second;
    configureWithSeed(second, 21.0);

    int differing = 0;
    for (int i = 1; i <= 20; ++i) {
        const double t = first.observationTime() * i / 20.0;
        if (first.countedNucleiAt(t) != second.countedNucleiAt(t))
            ++differing;
        EXPECT_NEAR(first.theoryNucleiAt(t), second.theoryNucleiAt(t), 1e-12) << "момент " << t;
    }
    // Отличаются не в одной случайной точке, а во многих: это другой опыт.
    EXPECT_GE(differing, 5);
}

TEST(Decay, MoreNucleiReduceRelativeScatter) {
    DecayModel few;
    setSetup(few, QStringLiteral("isotope=7; half=8; N0=50; T=40; share=25; seed=7"));
    DecayModel many;
    configureLarge(many);

    // Оценочный разброс √N доля от начального числа убывает с ростом N.
    EXPECT_GT(few.expectedDeviationAt(0.0), 0.0);
    EXPECT_NEAR(few.expectedDeviationAt(0.0), std::sqrt(few.theoryNucleiAt(0.0)), 1e-12);
    const double fewRelative = few.expectedDeviationAt(0.0) / few.initialNuclei();
    const double manyRelative = many.expectedDeviationAt(0.0) / many.initialNuclei();
    EXPECT_LT(manyRelative, fewRelative);
}

TEST(Decay, CounterSeesDecaysAndActivityMatchesTheory) {
    DecayModel model;
    configure(model);

    // Распады за шаг — целые неотрицательные числа, их активность равна
    // числу распадов в час.
    const double step = model.sampleInterval();
    ASSERT_GT(step, 0.0);
    double sum = 0.0;
    int silent = 0;
    for (int node = 1; node < model.sampleNodes(); ++node) {
        const double t = step * node;
        const double decays = model.decaysInLastStepAt(t);
        EXPECT_NEAR(decays, std::round(decays), 1e-12) << "узел " << node;
        EXPECT_GE(decays, 0.0);
        EXPECT_NEAR(model.countedActivityAt(t), decays / step, 1e-9) << "узел " << node;
        if (decays == 0.0)
            ++silent;
        sum += decays;
    }
    EXPECT_NEAR(sum, model.countedDecaysAt(model.observationTime()), 1e-9);
    EXPECT_GT(sum, 0.0);
    // Доля пустых шагов мала: счётчик щёлкает почти на каждом шаге.
    EXPECT_LT(double(silent) / (model.sampleNodes() - 1), 0.5);

    // Средняя активность счёта близка к теоретической.
    EXPECT_NEAR(model.countedActivityAt(model.halfLife()) * step, model.decaysInLastStepAt(model.halfLife()), 1e-9);
}

TEST(Decay, NoDecaysRecordedBeforeObservationStarts) {
    DecayModel model;
    configure(model);
    EXPECT_NEAR(model.decaysInLastStepAt(0.0), 0.0, 1e-12);
    EXPECT_NEAR(model.countedDecaysAt(0.0), 0.0, 1e-12);
    EXPECT_NEAR(model.countedNucleiAt(0.0), model.initialNuclei(), 1e-12);
}

// ------------------------------------------------------------------
// Время наблюдения
// ------------------------------------------------------------------

TEST(Decay, EventIsMomentOfGivenFraction) {
    DecayModel model;
    configure(model);

    // Ключевое событие задачи — момент, когда останется заданная доля ядер.
    EXPECT_NEAR(model.eventTime(), model.targetTime(), 1e-12);

    DecayModel late;
    configure(late);
    late.setParam(QStringLiteral("share"), 99.0);
    EXPECT_GT(late.targetTime(), 0.0);
    EXPECT_TRUE(std::isfinite(late.eventTime()));
}

TEST(Decay, StateMarksEndOfObservation) {
    DecayModel model;
    configure(model);

    const lab::SimState during = model.stateAt(model.observationTime() * 0.5);
    EXPECT_FALSE(during.resolved);
    EXPECT_EQ(during.entities.size(), 3);

    const lab::SimState after = model.stateAt(model.duration());
    EXPECT_TRUE(after.resolved);
    EXPECT_FALSE(after.eventLabel.isEmpty());
}

TEST(Decay, StateCarriesTheoryAndCounterSideBySide) {
    DecayModel model;
    configure(model);
    const double t = model.observationTime() * 0.5;

    const lab::SimState state = model.stateAt(t);
    ASSERT_EQ(state.entities.size(), 3);

    const lab::EntityVisual& nuclei = state.entities.at(0);
    EXPECT_EQ(nuclei.id, QStringLiteral("nuclei"));
    EXPECT_NEAR(nuclei.value1, model.theoryNucleiAt(t), 1e-9);     // теория
    EXPECT_NEAR(nuclei.value2, model.countedNucleiAt(t), 1e-12);   // целые ядра
    EXPECT_NEAR(nuclei.position, model.countedNucleiAt(t), 1e-12);

    const lab::EntityVisual& daughters = state.entities.at(1);
    EXPECT_EQ(daughters.id, QStringLiteral("daughters"));
    // Сумма нераспавшихся и дочерних ядер равна N₀ — модель ничего не теряет.
    EXPECT_NEAR(nuclei.position + daughters.position, model.initialNuclei(), 1e-9);
    EXPECT_NEAR(daughters.value1, model.decayedTheoryAt(t), 1e-9);

    const lab::EntityVisual& counter = state.entities.at(2);
    EXPECT_EQ(counter.id, QStringLiteral("counter"));
    EXPECT_NEAR(counter.value1, model.activityAt(t), 1e-9);
    EXPECT_NEAR(counter.value2, model.decaysInLastStepAt(t), 1e-12);

    EXPECT_NEAR(state.primary, model.countedNucleiAt(t), 1e-12);
    EXPECT_NEAR(state.secondary, model.theoryNucleiAt(t), 1e-9);
}

// ------------------------------------------------------------------
// Допустимость постановки
// ------------------------------------------------------------------

TEST(Decay, RejectsFractionOutsideZeroAndOne) {
    DecayModel model;
    configure(model);
    // Панель не пропускает 0 % и 100 %: ln(1/d) не существовал бы.
    model.setParam(QStringLiteral("share"), 1.0);
    EXPECT_FALSE(isInvalid(model));
    EXPECT_NEAR(model.targetFraction(), 0.01, 1e-12);
    EXPECT_TRUE(std::isfinite(model.targetTime()));
}

TEST(Decay, RejectsStepTooCoarseForHalfLife) {
    DecayModel model;
    // Короткий период полураспада при длинном наблюдении: шаг опыта остался
    // бы крупным, за один шаг распадалась бы заметная доля ядер, и ступенька
    // счёта перестала бы походить на плавный закон.
    setSetup(model, QStringLiteral("isotope=7; half=0.5; N0=1000; T=240; share=25; seed=7"));
    EXPECT_GT(model.stepProbability(), 0.25);
    EXPECT_TRUE(isInvalid(model));

    const lab::Derivation derivation = model.derivation();
    EXPECT_FALSE(derivation.valid);
    EXPECT_FALSE(derivation.answerTemplate.isEmpty());
    EXPECT_TRUE(derivation.answerTemplate.contains(QStringLiteral("Расчёт невозможен")));
    // Пустые показания там, где расчёт не определён.
    EXPECT_TRUE(model.readouts().isEmpty());

    // Обычная постановка при этом допустима.
    DecayModel ordinary;
    configure(ordinary);
    EXPECT_FALSE(isInvalid(ordinary));
}

TEST(Decay, OutOfRangeValuesAreClampedNotRejected) {
    DecayModel model;
    setSetup(model, QStringLiteral("isotope=7; half=0; N0=0; T=0; share=0; seed=0"));
    EXPECT_GT(model.halfLife(), 0.0);
    EXPECT_GT(model.initialNuclei(), 0.0);
    EXPECT_GT(model.observationTime(), 0.0);
    EXPECT_GT(model.targetFraction(), 0.0);
    EXPECT_LT(model.targetFraction(), 1.0);
    EXPECT_FALSE(isInvalid(model));
    EXPECT_TRUE(std::isfinite(model.theoryNucleiAt(model.observationTime())));
}

TEST(Decay, LongLivingIsotopeIsStillCountable) {
    // Углерод-14: за трое суток распадается малая часть ядер, и счётчик
    // показывает почти постоянное число — это физический факт, не сбой.
    DecayModel model;
    setSetup(model, QStringLiteral("isotope=5; N0=5000; T=40; share=25; seed=7"));
    EXPECT_FALSE(isInvalid(model));
    EXPECT_TRUE(std::isfinite(model.decayConstant()));
    EXPECT_GT(model.theoryNucleiAt(model.observationTime()), 0.99 * model.initialNuclei());
    EXPECT_GE(totalDecaysAtEnd(model), 0.0);
}

// ------------------------------------------------------------------
// Запись решения
// ------------------------------------------------------------------

TEST(Decay, DerivationContainsKeyFormulas) {
    DecayModel model;
    configure(model);

    const lab::Derivation derivation = model.derivation();
    ASSERT_TRUE(derivation.valid);
    ASSERT_FALSE(derivation.steps.isEmpty());

    QString all;
    for (const lab::DerivationStep& step : derivation.steps)
        all += step.content;

    EXPECT_TRUE(all.contains(QStringLiteral("\\lambda = \\frac{\\ln 2}{T_{1/2}}")));
    EXPECT_TRUE(all.contains(QStringLiteral("N(t) = N_{0} e^{-\\lambda t}")));
    EXPECT_TRUE(all.contains(QStringLiteral("2^{-t/T_{1/2}}")));
    EXPECT_TRUE(all.contains(QStringLiteral("t = \\frac{\\ln (1/d)}{\\lambda}")));
    EXPECT_TRUE(all.contains(QStringLiteral("A = \\lambda N")));
    EXPECT_TRUE(all.contains(QStringLiteral("\\Delta p = \\lambda \\Delta t")));
    EXPECT_TRUE(all.contains(QStringLiteral("\\sigma = \\sqrt{N}")));
    // Среднее время жизни и связь с периодом полураспада.
    EXPECT_TRUE(all.contains(QStringLiteral("\\tau = \\frac{1}{\\lambda}")));
}

TEST(Decay, DerivationQuotesModelNumbers) {
    DecayModel model;
    configure(model);

    const lab::Derivation derivation = model.derivation();
    QString all;
    for (const lab::DerivationStep& step : derivation.steps)
        all += step.content;

    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.decayConstant(), 6)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.initialNuclei(), 0)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.activityAt(0.0), 1)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.expectedDeviationAt(0.0), 1)));
    // Число ядер в конце наблюдения берётся из экспоненты, а не «на глаз».
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(std::round(model.theoryNucleiAt(model.observationTime())), 0)));
}

TEST(Decay, DerivationDistinguishesTheoryAndCounter) {
    DecayModel model;
    configure(model);
    model.seekTo(model.observationTime() * 0.5);

    const lab::Derivation derivation = model.derivation();
    QString all;
    for (const lab::DerivationStep& step : derivation.steps)
        all += step.content;

    // Оба числа стоят рядом: теоретическое и показание счётчика.
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(std::round(model.countedNucleiAt(model.time())), 0)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(std::round(model.theoryNucleiAt(model.time())), 0)));
    EXPECT_TRUE(all.contains(QStringLiteral("\\sqrt{N}")));
}

TEST(Decay, DerivationRendersWithoutUnknownCommands) {
    DecayModel model;
    configure(model);

    const lab::Derivation derivation = model.derivation();
    ASSERT_TRUE(derivation.valid);
    for (const lab::DerivationStep& step : derivation.steps) {
        const QString html = lab::utils::toRichMath(step.content);
        EXPECT_EQ(html.count(QLatin1Char('\\')), 0) << step.kind.toUtf8().constData() << ": "
                                                    << html.toUtf8().constData();
        EXPECT_EQ(html.count(QLatin1Char('{')), 0) << step.kind.toUtf8().constData();
        EXPECT_EQ(html.count(QLatin1Char('}')), 0) << step.kind.toUtf8().constData();
    }
}

TEST(Decay, DerivationAndAnswerQuoteTheSameEndState) {
    DecayModel model;
    configure(model);

    const lab::Derivation derivation = model.derivation();
    EXPECT_TRUE(derivation.answerTemplate.contains(lab::utils::formatNumber(model.decayConstant(), 6)));
    EXPECT_TRUE(derivation.answerTemplate.contains(
        lab::utils::formatNumber(std::round(model.theoryNucleiAt(model.targetTime())), 0)));
    EXPECT_TRUE(derivation.answerTemplate.contains(lab::utils::formatNumber(model.activityAt(0.0), 1)));
}

TEST(Decay, DerivationOfListedIsotopeIsStillComplete) {
    // Долгоживущий изотоп: постоянная распада мала, и её всё равно должно
    // быть видно в записи решения (обычное округление дало бы ноль).
    DecayModel model;
    setSetup(model, QStringLiteral("isotope=5; N0=5000; T=40; share=25; seed=7"));
    ASSERT_FALSE(isInvalid(model));

    const lab::Derivation derivation = model.derivation();
    ASSERT_TRUE(derivation.valid);
    QString all;
    for (const lab::DerivationStep& step : derivation.steps)
        all += step.content;
    EXPECT_FALSE(all.contains(QStringLiteral("= 0 \\text{ ч}^{-1}")));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.decayConstant(), model.decayConstantText().size())) ||
                all.contains(model.decayConstantText()));
    EXPECT_TRUE(derivation.answerTemplate.contains(model.decayConstantText()));
}

TEST(Decay, ReadoutsFollowModelState) {
    DecayModel model;
    configure(model);
    model.seekTo(model.observationTime() * 0.5);
    const double t = model.time();

    const QVector<lab::Readout> readouts = model.readouts();
    ASSERT_FALSE(readouts.isEmpty());

    QString all;
    for (const lab::Readout& readout : readouts)
        all += readout.label + readout.value;
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(std::round(model.theoryNucleiAt(t)), 0)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(std::round(model.countedNucleiAt(t)), 0)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.decayConstant(), 6)));
    EXPECT_TRUE(all.contains(lab::utils::formatNumber(model.activityAt(t), 1)));
    EXPECT_TRUE(all.contains(QStringLiteral("√N")));
}

// ------------------------------------------------------------------
// Точки взаимодействия
// ------------------------------------------------------------------

TEST(Decay, HandlesCoverSampleHalfLifeWindowAndFraction) {
    DecayModel model;
    configure(model);

    const QVector<lab::HandleInfo> handles = model.handles();
    ASSERT_EQ(handles.size(), 4);
    QStringList ids;
    for (const lab::HandleInfo& handle : handles) {
        ids.push_back(handle.id);
        EXPECT_FALSE(handle.readbackValue.isEmpty()) << handle.id.toUtf8().constData();
        EXPECT_FALSE(handle.axis.isEmpty()) << handle.id.toUtf8().constData();
    }
    EXPECT_TRUE(ids.contains(QStringLiteral("nuclei")));
    EXPECT_TRUE(ids.contains(QStringLiteral("half")));
    EXPECT_TRUE(ids.contains(QStringLiteral("window")));
    EXPECT_TRUE(ids.contains(QStringLiteral("share")));
}

TEST(Decay, HandlesWriteIntoPanelParameters) {
    DecayModel model;
    configure(model);

    const double previousNuclei = model.initialNuclei();
    model.setHandleValue(QStringLiteral("nuclei"), 2000.0);
    EXPECT_NEAR(model.initialNuclei(), 2000.0, 1e-9);
    EXPECT_NE(model.initialNuclei(), previousNuclei);
    // Больше ядер — тот же закон, но меньший относительный разброс.
    EXPECT_NEAR(model.survivingFractionAt(model.halfLife()), 0.5, 1e-12);

    model.setHandleValue(QStringLiteral("half"), 20.0);
    // Перетаскивание периода полураспада переводит список на свой пункт:
    // иначе справочное значение перебило бы ручку.
    EXPECT_NEAR(model.halfLife(), 20.0, 1e-9);
    EXPECT_NEAR(model.decayConstant(), kLnTwo / 20.0, 1e-12);

    model.setHandleValue(QStringLiteral("window"), 100.0);
    EXPECT_NEAR(model.observationTime(), 100.0, 1e-9);

    model.setHandleValue(QStringLiteral("share"), 12.0);
    EXPECT_NEAR(model.targetFraction(), 0.12, 1e-9);
    EXPECT_NEAR(model.theoryNucleiAt(model.targetTime()), 0.12 * model.initialNuclei(), 1e-9);
}

TEST(Decay, HandleReadbackShowsComputedValues) {
    DecayModel model;
    configure(model);

    const QVector<lab::HandleInfo> handles = model.handles();
    ASSERT_EQ(handles.size(), 4);
    for (const lab::HandleInfo& handle : handles) {
        if (handle.id == QLatin1String("half"))
            EXPECT_TRUE(handle.readbackValue.contains(lab::utils::formatNumber(model.decayConstant(), 6)));
        if (handle.id == QLatin1String("nuclei"))
            EXPECT_TRUE(handle.readbackValue.contains(lab::utils::formatNumber(model.activityAt(0.0), 1)));
    }
}

TEST(Decay, DraggingRestartsObservation) {
    DecayModel model;
    configure(model);
    model.seekTo(model.duration());
    ASSERT_GT(model.time(), 0.0);

    model.beginInteraction();
    EXPECT_NEAR(model.time(), 0.0, 1e-12);
}

// ------------------------------------------------------------------
// Ряды данных
// ------------------------------------------------------------------

TEST(Decay, CountedSeriesIsAStepFunction) {
    DecayModel model;
    configure(model);

    const QVector<QPointF> series = model.countedSeries();
    ASSERT_GE(series.size(), 20);
    EXPECT_NEAR(series.first().x(), 0.0, 1e-12);
    EXPECT_NEAR(series.first().y(), model.initialNuclei(), 1e-12);

    for (int i = 1; i < series.size(); ++i) {
        EXPECT_GE(series.at(i).x(), series.at(i - 1).x());
        EXPECT_LE(series.at(i).y(), series.at(i - 1).y() + 1e-9) << "точка " << i;
        EXPECT_NEAR(series.at(i).y(), std::round(series.at(i).y()), 1e-9) << "точка " << i;
    }
    // Ступенька: у соседних точек одного шага значение совпадает.
    bool flatFound = false;
    for (int i = 1; i < series.size(); ++i) {
        if (series.at(i).x() > series.at(i - 1).x() + 1e-12 &&
            std::abs(series.at(i).y() - series.at(i - 1).y()) < 1e-12)
            flatFound = true;
    }
    EXPECT_TRUE(flatFound);
}

TEST(Decay, TheorySeriesMatchesLawAndSpansObservation) {
    DecayModel model;
    configure(model);

    const QVector<QPointF> series = model.theorySeries();
    ASSERT_GE(series.size(), 100);
    EXPECT_NEAR(series.first().x(), 0.0, 1e-12);
    EXPECT_NEAR(series.first().y(), model.initialNuclei(), 1e-9);
    EXPECT_NEAR(series.last().x(), model.duration(), 1e-9);
    for (const QPointF& point : series) {
        EXPECT_NEAR(point.y(), model.theoryNucleiAt(point.x()), 1e-9) << "t = " << point.x();
        EXPECT_GT(point.y(), 0.0);
    }
}

TEST(Decay, CountedSeriesFollowsModelStateValues) {
    DecayModel model;
    configure(model);

    const QVector<QPointF> series = model.countedSeries();
    ASSERT_GE(series.size(), 4);
    // Ряд прорежен: точки идут парами — начало интервала отображения и его
    // конец. В начале интервала значение равно счёту модели в этот момент,
    // а на конце интервала ступенька удерживает то же значение.
    for (int i = 0; i + 1 < series.size(); i += 2) {
        EXPECT_NEAR(series.at(i).y(), model.countedNucleiAt(series.at(i).x()), 1e-12) << "точка " << i;
        EXPECT_GT(series.at(i + 1).x(), series.at(i).x()) << "точка " << i;
        EXPECT_DOUBLE_EQ(series.at(i + 1).y(), series.at(i).y()) << "точка " << i + 1;
        // Счёт модели на конце интервала не больше показанного: ступенька
        // только отстаёт, но не обгоняет реальный ход распада.
        EXPECT_GE(series.at(i + 1).y(), model.countedNucleiAt(series.at(i + 1).x())) << "точка " << i + 1;
    }
    EXPECT_NEAR(series.first().y(), model.countedNucleiAt(0.0), 1e-12);
}

TEST(Decay, DecaysSeriesComplementsNuclei) {
    DecayModel model;
    configure(model);

    const QVector<QPointF> theory = model.decayedTheorySeries();
    const QVector<QPointF> counted = model.countedDecaysSeries();
    ASSERT_GE(theory.size(), 100);
    ASSERT_FALSE(counted.isEmpty());

    EXPECT_NEAR(theory.first().y(), 0.0, 1e-12);
    for (const QPointF& point : theory)
        EXPECT_NEAR(theory.first().y() + model.theoryNucleiAt(point.x()) + point.y(), model.initialNuclei(), 1e-9);
    // Ряд распадов по счёту — та же ступенька, что и ряд ядер: в начале
    // интервала он равен счёту модели, на конце интервала удерживается.
    for (int i = 0; i + 1 < counted.size(); i += 2) {
        EXPECT_NEAR(counted.at(i).y(), model.countedDecaysAt(counted.at(i).x()), 1e-12) << "точка " << i;
        EXPECT_DOUBLE_EQ(counted.at(i + 1).y(), counted.at(i).y()) << "точка " << i + 1;
        EXPECT_LE(counted.at(i + 1).y(), model.countedDecaysAt(counted.at(i + 1).x()) + 1e-12);
    }
    for (const QPointF& point : counted) {
        EXPECT_GE(point.y(), 0.0);
        EXPECT_NEAR(point.y(), std::round(point.y()), 1e-9);
    }
    EXPECT_LE(counted.last().y(), model.countedDecaysAt(model.observationTime()) + 1e-12);
    EXPECT_NEAR(model.countedDecaysAt(model.observationTime()),
                model.initialNuclei() - model.countedNucleiAt(model.observationTime()), 1e-12);
}

TEST(Decay, ActivitySeriesIsProportionalToNuclei) {
    DecayModel model;
    configure(model);

    const QVector<QPointF> series = model.activitySeries();
    ASSERT_GE(series.size(), 100);
    for (const QPointF& point : series) {
        EXPECT_NEAR(point.y(), model.decayConstant() * model.theoryNucleiAt(point.x()), 1e-9);
        EXPECT_GT(point.y(), 0.0);
    }
}

TEST(Decay, CountsPerStepSeriesIsNonNegativeAndConsistent) {
    DecayModel model;
    configure(model);

    const QVector<QPointF> series = model.countsPerStepSeries();
    ASSERT_FALSE(series.isEmpty());
    ASSERT_LT(series.size(), model.sampleNodes());   // ряд прорежен для графика
    double sum = 0.0;
    for (const QPointF& point : series) {
        EXPECT_GE(point.y(), 0.0);
        EXPECT_NEAR(point.y(), std::round(point.y()), 1e-9);
        sum += point.y();
    }
    EXPECT_NEAR(sum, model.countedDecaysAt(model.observationTime()), 1e-9);
}

TEST(Decay, LogarithmIsStraightLineWithSlopeMinusLambda) {
    DecayModel model;
    configure(model);

    // ln N = ln N₀ − λt: по этой прямой наклон дают постоянную распада.
    const QVector<QPointF> series = model.logarithmSeries();
    ASSERT_GE(series.size(), 50);
    EXPECT_NEAR(series.first().y(), std::log(model.initialNuclei()), 1e-9);
    for (const QPointF& point : series)
        EXPECT_NEAR(point.y(), std::log(model.initialNuclei()) - model.decayConstant() * point.x(), 1e-9);

    const double slope = (series.last().y() - series.first().y()) / (series.last().x() - series.first().x());
    EXPECT_NEAR(slope, -model.decayConstant(), 1e-9);
    EXPECT_LT(slope, 0.0);
}

TEST(Decay, DeviationSeriesStaysNearZero) {
    DecayModel model;
    configureLarge(model);

    const QVector<QPointF> series = model.deviationSeries();
    ASSERT_FALSE(series.isEmpty());
    EXPECT_NEAR(series.first().y(), 0.0, 1e-12);
    double maxDeviation = 0.0;
    for (const QPointF& point : series) {
        ASSERT_TRUE(std::isfinite(point.y()));
        maxDeviation = std::max(maxDeviation, std::abs(point.y()));
    }
    // При N₀ = 5000 отклонение счёта от теории — доли процента.
    EXPECT_LT(maxDeviation, 2.0);
}
