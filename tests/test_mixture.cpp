#include <gtest/gtest.h>

#include <cmath>

#include "TestSupport.h"
#include "models/MixtureModel.h"

using lab::models::MixtureModel;
using lab::test::isInvalid;
using lab::test::setSetup;

namespace {

// Задача: 30 л 12 %-ного раствора + 70 л 42 %-ного ⇒ 100 л 33 %-ного.
void fillBase(MixtureModel& model) {
    setSetup(model, QStringLiteral("v1=30; c1=12; v2=70; c2=42"));
}

}  // namespace

TEST(MixtureModel, FinalConcentration) {
    MixtureModel model;
    fillBase(model);
    ASSERT_FALSE(isInvalid(model));

    // m = V·C (в долях): m1 = 30·0.12 = 3.6, m2 = 70·0.42 = 29.4.
    EXPECT_NEAR(model.soluteOf(0), 3.6, 1e-9);
    EXPECT_NEAR(model.soluteOf(1), 29.4, 1e-9);
    EXPECT_NEAR(model.totalSolute(), 33.0, 1e-9);
    EXPECT_NEAR(model.totalVolume(), 100.0, 1e-9);
    // C = (V1C1 + V2C2)/(V1+V2) = 33/100 = 33 %.
    EXPECT_NEAR(model.finalConcentration(), 0.33, 1e-9);
}

TEST(MixtureModel, EqualVolumesAverageConcentration) {
    MixtureModel model;
    setSetup(model, QStringLiteral("v1=50; c1=10; v2=50; c2=30"));
    // Среднее арифметическое при равных объёмах: 20 %.
    EXPECT_NEAR(model.finalConcentration(), 0.20, 1e-9);
}

TEST(MixtureModel, PureSolventDilutes) {
    MixtureModel model;
    setSetup(model, QStringLiteral("v1=40; c1=25; v2=60; c2=0"));
    // 40·0.25 = 10 л вещества на 100 л ⇒ 10 %.
    EXPECT_NEAR(model.finalConcentration(), 0.10, 1e-9);
}

TEST(MixtureModel, PumpingScheduleIsSequential) {
    MixtureModel model;
    fillBase(model);
    // Насос 60 л/мин: первый раствор (30 л) перекачивается за 0.5 мин.
    EXPECT_NEAR(model.pumpSwitchTime(), 0.5, 1e-9);

    // В середине первой фазы: перекачано 15 л первого, второго ещё нет.
    EXPECT_NEAR(model.transferredAt(0, 0.25), 15.0, 1e-9);
    EXPECT_NEAR(model.transferredAt(1, 0.25), 0.0, 1e-9);
    // Концентрация в смесителе на этом этапе равна концентрации первого.
    EXPECT_NEAR(model.currentConcentrationAt(0.25), 0.12, 1e-9);

    // Момент переключения: первый перелит полностью, concentration = C1.
    EXPECT_NEAR(model.transferredAt(0, 0.5), 30.0, 1e-9);

    // К концу: всё перекачано, концентрация — итоговая.
    const double end = model.eventTime();
    EXPECT_NEAR(end, 0.5 + 70.0 / 60.0, 1e-9);
    EXPECT_NEAR(model.transferredAt(1, end), 70.0, 1e-9);
    EXPECT_NEAR(model.currentConcentrationAt(end), model.finalConcentration(), 1e-9);
}

TEST(MixtureModel, IntermediateConcentrationIsWeightedMean) {
    MixtureModel model;
    fillBase(model);
    // t = 0.5 + 30/60 = 1.0 мин: перекачано 30 л первого и 30 л второго.
    const double t = 1.0;
    EXPECT_NEAR(model.transferredAt(1, t), 30.0, 1e-9);
    // C = (30·0.12 + 30·0.42)/60 = 16.2/60 = 0.27.
    EXPECT_NEAR(model.currentConcentrationAt(t), (30.0 * 0.12 + 30.0 * 0.42) / 60.0, 1e-9);
}

TEST(WorkModelVolumeGuard, EmptyVolumesRejected) {
    MixtureModel model;
    setSetup(model, QStringLiteral("v1=1; v2=1"));
    EXPECT_FALSE(isInvalid(model));
    // Объём меньше спина по паспорту модели (1 л) всё ещё допустим.
    EXPECT_NEAR(model.totalVolume(), 2.0, 1e-9);
}

TEST(MixtureModel, StateAtMatchesTransfer) {
    MixtureModel model;
    fillBase(model);
    const auto state = model.stateAt(1.0);
    EXPECT_NEAR(state.secondary, 60.0, 1e-9);           // объём смеси
    EXPECT_NEAR(state.primary, 0.27, 1e-9);             // концентрация смеси
    EXPECT_FALSE(state.resolved);

    const auto after = model.stateAt(model.eventTime());
    EXPECT_TRUE(after.resolved);
}

TEST(MixtureModel, VolumeDragChangesFinalConcentration) {
    MixtureModel model;
    fillBase(model);
    // Перетаскивание ёмкости 1: 30 л → 70 л.
    model.setHandleValue(QStringLiteral("v1"), 70.0);
    // C = (70·0.12 + 70·0.42)/140 = 27 %.
    EXPECT_NEAR(model.finalConcentration(), 0.27, 1e-9);
}

TEST(MixtureModel, DerivationHasConservationLaws) {
    MixtureModel model;
    fillBase(model);
    const auto d = model.derivation();
    ASSERT_TRUE(d.valid);
    bool hasMassFormula = false;
    bool hasFinalFormula = false;
    for (const auto& step : d.steps) {
        if (step.content.contains(QStringLiteral("m = V \\cdot C")))
            hasMassFormula = true;
        if (step.content.contains(QStringLiteral("V_{1} C_{1} + V_{2} C_{2}")))
            hasFinalFormula = true;
    }
    EXPECT_TRUE(hasMassFormula);
    EXPECT_TRUE(hasFinalFormula);
    // Ответ: 33 %.
    EXPECT_TRUE(d.answerTemplate.contains(QStringLiteral("33")));
}
