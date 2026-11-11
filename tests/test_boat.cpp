#include <gtest/gtest.h>

#include <cmath>

#include "TestSupport.h"
#include "models/BoatModel.h"

using lab::models::BoatModel;
using lab::test::isInvalid;
using lab::test::setSetup;

namespace {

// Река между пристанями A (координата 0) и B (координата L); течение — в сторону B.
void fillBase(BoatModel& model, double L = 120.0, double current = 3.0) {
    setSetup(model, QStringLiteral("L=%1; current=%2").arg(L).arg(current));
}

}  // namespace

TEST(BoatModel, DownstreamSpeedAndTime) {
    BoatModel model;
    fillBase(model, 120.0, 3.0);
    // v = v_соб + v_т = 15 + 3 = 18; время рейса L/v = 120/18.
    setSetup(model, QStringLiteral("own1=15; dir1=0; x1=0; dep1=0"));
    ASSERT_FALSE(isInvalid(model));

    EXPECT_NEAR(model.shoreSpeedOf(0), 18.0, 1e-9);
    EXPECT_NEAR(model.travelTimeOf(0), 120.0 / 18.0, 1e-9);
    EXPECT_NEAR(model.arrivalTimeOf(0), 120.0 / 18.0, 1e-9);
}

TEST(BoatModel, UpstreamSpeedAndTime) {
    BoatModel model;
    fillBase(model, 135.0, 5.0);
    // Против течения: v = 20 − 5 = 15; время 135/15 = 9 ч.
    setSetup(model, QStringLiteral("own1=20; dir1=1; x1=135; dep1=0"));
    ASSERT_FALSE(isInvalid(model));

    EXPECT_NEAR(std::abs(model.shoreSpeedOf(0)), 15.0, 1e-9);
    EXPECT_NEAR(model.travelTimeOf(0), 9.0, 1e-9);
}

TEST(BoatModel, UpstreamRequiresOwnSpeedGreaterThanCurrent) {
    BoatModel model;
    fillBase(model, 120.0, 5.0);
    setSetup(model, QStringLiteral("own1=4; dir1=1; x1=120"));
    // Собственная скорость меньше скорости течения — идти против течения нельзя.
    EXPECT_TRUE(isInvalid(model));
}

TEST(BoatModel, DepartureDelayShiftsArrival) {
    BoatModel model;
    fillBase(model, 180.0, 2.0);
    setSetup(model, QStringLiteral("own1=16; dir1=0; x1=0; dep1=1.5"));
    ASSERT_FALSE(isInvalid(model));
    // Рейс 180/18 = 10 ч, прибытие через 1.5 + 10 = 11.5 ч.
    EXPECT_NEAR(model.arrivalTimeOf(0), 11.5, 1e-9);
    EXPECT_NEAR(model.positionOf(0, 1.0), 0.0, 1e-9);  // ещё стоит у пристани
}

TEST(BoatModel, PositionAtTimeStopsAtBerth) {
    BoatModel model;
    fillBase(model, 120.0, 3.0);
    setSetup(model, QStringLiteral("own1=15; dir1=0; x1=0; dep1=0"));
    ASSERT_FALSE(isInvalid(model));
    // s = 18 · 2 = 36 км к моменту t = 2.
    EXPECT_NEAR(model.positionOf(0, 2.0), 36.0, 1e-9);
    // После прибытия теплоход не идёт дальше причала.
    EXPECT_NEAR(model.positionOf(0, 100.0), 120.0, 1e-9);
}

TEST(BoatModel, EarlierArrivalIsTheEvent) {
    BoatModel model;
    fillBase(model, 120.0, 3.0);
    // Теплоход 1: по течению 18 км/ч (время 120/18 ≈ 6.67 ч);
    // теплоход 2: против течения 24 − 3 = 21 км/ч (120/21 ≈ 5.71 ч) — прибудет раньше.
    setSetup(model, QStringLiteral("own1=15; dir1=0; x1=0; dep1=0; own2=24; dir2=1; x2=120; dep2=0"));
    ASSERT_FALSE(isInvalid(model));

    EXPECT_NEAR(model.arrivalTimeOf(1), 120.0 / 21.0, 1e-9);
    EXPECT_LT(model.arrivalTimeOf(1), model.arrivalTimeOf(0));
    // Наблюдение завершается последним рейсом.
    EXPECT_NEAR(model.eventTime(), model.lastArrivalTime(), 1e-9);
    EXPECT_NEAR(model.eventTime(), model.arrivalTimeOf(0), 1e-9);
}

TEST(BoatModel, StateMatchesPositions) {
    BoatModel model;
    fillBase(model, 120.0, 3.0);
    setSetup(model, QStringLiteral("own1=15; dir1=0; x1=0; dep1=0"));
    const auto state = model.stateAt(2.0);
    EXPECT_NEAR(state.entities[0].position, 36.0, 1e-9);
    EXPECT_TRUE(state.entities[0].active);
}

TEST(BoatModel, PierDragChangesDistanceAndTime) {
    BoatModel model;
    fillBase(model, 120.0, 3.0);
    setSetup(model, QStringLiteral("own1=15; dir1=0; x1=0"));
    // Перетаскивание пристани Б меняет расстояние и все времена рейсов.
    model.setHandleValue(QStringLiteral("L"), 240.0);
    EXPECT_NEAR(model.distanceBetweenPorts(), 240.0, 1e-9);
    EXPECT_NEAR(model.travelTimeOf(0), 240.0 / 18.0, 1e-9);
}

TEST(BoatModel, DerivationHasDownstreamAndUpstreamFormulas) {
    BoatModel model;
    fillBase(model, 120.0, 3.0);
    setSetup(model, QStringLiteral("own1=15; dir1=0; x1=0; own2=24; dir2=1; x2=120"));
    const auto d = model.derivation();
    ASSERT_TRUE(d.valid);
    bool hasDownstream = false;
    bool hasUpstream = false;
    for (const auto& step : d.steps) {
        if (step.content.contains(QStringLiteral("v_{\\text{по}}")))
            hasDownstream = true;
        if (step.content.contains(QStringLiteral("v_{\\text{пр}}")))
            hasUpstream = true;
    }
    EXPECT_TRUE(hasDownstream);
    EXPECT_TRUE(hasUpstream);
}
