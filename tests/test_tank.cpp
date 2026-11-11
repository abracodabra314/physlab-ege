#include <gtest/gtest.h>

#include <cmath>

#include "TestSupport.h"
#include "models/TankModel.h"

using lab::models::TankModel;
using lab::test::isInvalid;
using lab::test::setSetup;

namespace {

// Бак 2 м × 2 м в основании и 2.5 м высотой: S = 4 м², вместимость 10 000 л.
// Начальный уровень 0.5 м: V₀ = 4 · 0.5 · 1000 = 2000 л.
void fillBaseTank(TankModel& model) {
    setSetup(model, QStringLiteral("width=2; height=2.5; level=0.5; qin1=60; in1on=1; in2on=0;"
                                   "qout1=0; out1on=0; qout2=0; out2on=0"));
}

}  // namespace

TEST(TankModel, GeometryAndCapacity) {
    TankModel model;
    fillBaseTank(model);
    EXPECT_NEAR(model.baseArea(), 4.0, 1e-9);
    EXPECT_NEAR(model.capacity(), 10000.0, 1e-9);
    EXPECT_NEAR(model.initialVolume(), 2000.0, 1e-9);
}

TEST(TankModel, FillingLinearLaw) {
    TankModel model;
    fillBaseTank(model);
    ASSERT_FALSE(isInvalid(model));
    // V(t) = 2000 + 60t.
    EXPECT_NEAR(model.netRate(), 60.0, 1e-9);
    EXPECT_NEAR(model.volumeAt(10.0), 2600.0, 1e-9);
    EXPECT_NEAR(model.levelAt(10.0), 0.65, 1e-9);
    // Доверху: (10000 − 2000)/60 мин.
    EXPECT_NEAR(model.timeToFill(), 8000.0 / 60.0, 1e-6);
    EXPECT_NEAR(model.eventTime(), 8000.0 / 60.0, 1e-6);
}

TEST(TankModel, DrainWithSimultaneousFill) {
    TankModel model;
    fillBaseTank(model);
    // Одновременно наполнение 60 и слив 100: Q = −40 л/мин, опустошение за 50 мин.
    setSetup(model, QStringLiteral("qout1=100; out1on=1"));
    ASSERT_FALSE(isInvalid(model));
    EXPECT_NEAR(model.netRate(), -40.0, 1e-9);
    EXPECT_NEAR(model.volumeAt(25.0), 1000.0, 1e-9);
    EXPECT_NEAR(model.timeToEmpty(), 50.0, 1e-9);
    EXPECT_NEAR(model.eventTime(), 50.0, 1e-9);
}

TEST(TankModel, VolumeNeverExceedsCapacityOrBelowZero) {
    TankModel model;
    fillBaseTank(model);
    // Перебор по времени не может «наполнить больше вместимости».
    EXPECT_NEAR(model.volumeAt(100000.0), model.capacity(), 1e-9);
    EXPECT_NEAR(model.levelAt(100000.0), model.height(), 1e-9);

    setSetup(model, QStringLiteral("in1on=0; qout1=100; out1on=1"));
    // Пустой бак не может отдать больше воды, чем в нём есть.
    EXPECT_NEAR(model.volumeAt(100000.0), 0.0, 1e-9);
}

TEST(TankModel, ClosedTapsAreRejected) {
    TankModel model;
    fillBaseTank(model);
    setSetup(model, QStringLiteral("in1on=0; out1on=0"));
    EXPECT_TRUE(isInvalid(model));
}

TEST(TankModel, EmptyTankWithDrainAndFullTankWithFillRejected) {
    TankModel model;
    fillBaseTank(model);
    setSetup(model, QStringLiteral("level=0; in1on=0; qout1=50; out1on=1"));
    EXPECT_TRUE(isInvalid(model));   // сливать нечего

    setSetup(model, QStringLiteral("level=2.5; in1on=1; qin1=60; out1on=0"));
    EXPECT_TRUE(isInvalid(model));   // бак полон, а наполнение продолжается
}

TEST(TankModel, WaterLevelDragChangesVolumeAndTime) {
    TankModel model;
    fillBaseTank(model);
    // Перетаскивание воды мышью меняет начальный объём: 1.5 м ⇒ 6000 л.
    model.setHandleValue(QStringLiteral("level"), 1.5);
    EXPECT_NEAR(model.initialVolume(), 6000.0, 1e-6);
    // И время наполнения пересчитано: (10000 − 6000)/60.
    EXPECT_NEAR(model.timeToFill(), 4000.0 / 60.0, 1e-6);
}

TEST(TankModel, DerivationContainsFillEquation) {
    TankModel model;
    fillBaseTank(model);
    const auto d = model.derivation();
    ASSERT_TRUE(d.valid);
    bool hasVolumeFormula = false;
    bool hasAnswer = false;
    for (const auto& step : d.steps) {
        if (step.content.contains(QStringLiteral("V_{0} + Q")))
            hasVolumeFormula = true;
        if (step.kind.contains(QStringLiteral("Ответ")))
            hasAnswer = true;
    }
    EXPECT_TRUE(hasVolumeFormula);
    EXPECT_TRUE(hasAnswer);
    // t = 8000/60 ≈ 133,3 мин присутствует в ответе (разделитель — запятая).
    EXPECT_TRUE(d.answerTemplate.contains(QStringLiteral("133")));
}

TEST(TankModel, StateFlagsFillEvent) {
    TankModel model;
    fillBaseTank(model);
    const auto before = model.stateAt(100.0);
    EXPECT_FALSE(before.resolved);
    const auto after = model.stateAt(model.timeToFill());
    EXPECT_TRUE(after.resolved);
    EXPECT_TRUE(after.eventLabel.contains(QStringLiteral("заполнен")));
}
