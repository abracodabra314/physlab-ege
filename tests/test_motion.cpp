#include <gtest/gtest.h>

#include <cmath>

#include "TestSupport.h"
#include "models/MotionModel.h"

using lab::models::MotionModel;
using lab::test::isInvalid;
using lab::test::isNotFinite;
using lab::test::setSetup;

namespace {

// Классическая встречная задача: s0 = 150 км, v1 = 60 км/ч, v2 = 90 км/ч ⇒ t = 1 ч.
// MotionModel наследует QObject, поэтому модель настраивается на месте.
void fillMeeting(MotionModel& model) {
    setSetup(model, QStringLiteral("x1=0; v1=60; dir1=0; x2=150; v2=90; dir2=1; delay1=0; delay2=0"));
}

}  // namespace

TEST(MotionModel, Meeting) {
    MotionModel model;
    fillMeeting(model);
    ASSERT_FALSE(isInvalid(model));

    EXPECT_NEAR(model.distanceAt(0.0), 150.0, 1e-9);
    EXPECT_NEAR(model.signedVelocityOf(0), 60.0, 1e-9);
    EXPECT_NEAR(model.signedVelocityOf(1), -90.0, 1e-9);

    // s₁(t) = s₁₀ + v₁t, s₂(t) = s₂₀ + v₂t.
    EXPECT_NEAR(model.positionOf(0, 0.4), 24.0, 1e-9);
    EXPECT_NEAR(model.positionOf(1, 0.4), 114.0, 1e-9);
    EXPECT_NEAR(model.distanceAt(0.4), 90.0, 1e-9);

    // Встреча: 60t + 90t = 150 ⇒ t = 1.
    EXPECT_NEAR(model.meetingTime(), 1.0, 1e-9);
    EXPECT_NEAR(model.distanceAt(1.0), 0.0, 1e-9);
}

TEST(MotionModel, Overtake) {
    MotionModel model;
    // Автомобиль 1 догоняет автомобиль 2: разрыв 120 км, скорости 90 и 40.
    setSetup(model, QStringLiteral("x1=0; v1=90; dir1=0; x2=120; v2=40; dir2=0"));
    ASSERT_FALSE(isInvalid(model));

    // |v₂ − v₁| t = 120 ⇒ t = 120 / 50 = 2.4 ч.
    EXPECT_NEAR(model.meetingTime(), 2.4, 1e-9);
    EXPECT_NEAR(model.positionOf(0, 2.4), 216.0, 1e-9);
    EXPECT_NEAR(model.positionOf(1, 2.4), 216.0, 1e-9);
    EXPECT_NEAR(model.distanceAt(2.4), 0.0, 1e-9);
}

TEST(MotionModel, DelayedStart) {
    MotionModel model;
    // Второй автомобиль выезжает через 1 ч: к этому моменту первый проехал 60 км,
    // остающийся разрыв 210 км сокращается со скоростью 150 км/ч ⇒ ещё 1.4 ч.
    setSetup(model, QStringLiteral("x1=0; v1=60; dir1=0; x2=270; v2=90; dir2=1; delay1=0; delay2=1"));
    ASSERT_FALSE(isInvalid(model));

    EXPECT_NEAR(model.positionOf(1, 0.5), 270.0, 1e-9);                       // ещё стоит
    EXPECT_NEAR(model.positionOf(1, 1.5), 270.0 - 90.0 * 0.5, 1e-9);          // едет 0.5 ч
    EXPECT_NEAR(model.positionOf(0, 1.0), 60.0, 1e-9);
    EXPECT_NEAR(model.meetingTime(), 2.4, 1e-9);
    EXPECT_NEAR(model.distanceAt(model.meetingTime()), 0.0, 1e-9);
}

TEST(MotionModel, NoMeetingWhenSpeedsAreEqualInSameDirection) {
    MotionModel model;
    setSetup(model, QStringLiteral("x1=0; v1=60; dir1=0; x2=120; v2=60; dir2=0"));
    EXPECT_TRUE(isNotFinite(model.meetingTime()));
    EXPECT_NEAR(model.distanceAt(5.0), 120.0, 1e-9);
}

TEST(MotionModel, NoMeetingWhenGapGrows) {
    MotionModel model;
    // Ведущий автомобиль быстрее — разрыв только растёт.
    setSetup(model, QStringLiteral("x1=0; v1=60; dir1=0; x2=40; v2=90; dir2=0"));
    EXPECT_TRUE(isNotFinite(model.meetingTime()));
    EXPECT_GT(model.distanceAt(3.0), model.distanceAt(0.0));
}

TEST(MotionModel, IntersectionBeforeDelayedStartIsNotAnEvent) {
    MotionModel model;
    // Формальное пересечение прямых лежит раньше выезда второго автомобиля:
    // физически события нет, поскольку он ещё стоит на месте.
    setSetup(model, QStringLiteral("x1=0; v1=60; dir1=0; x2=20; v2=200; dir2=1; delay2=5"));
    EXPECT_TRUE(isNotFinite(model.meetingTime()));
}

TEST(MotionModel, StateAtTimeMatchesFormulas) {
    MotionModel model;
    fillMeeting(model);

    const auto state = model.stateAt(0.5);
    EXPECT_NEAR(state.entities[0].position, 30.0, 1e-9);
    EXPECT_NEAR(state.entities[1].position, 105.0, 1e-9);
    EXPECT_NEAR(state.primary, 75.0, 1e-9);
    EXPECT_FALSE(state.resolved);

    const auto after = model.stateAt(1.0);
    EXPECT_TRUE(after.resolved);
    EXPECT_TRUE(after.eventLabel.contains(QStringLiteral("Встреча")));
}

TEST(MotionModel, InvalidParametersAreRejected) {
    MotionModel samePlace;
    setSetup(samePlace, QStringLiteral("x1=50; x2=50"));
    EXPECT_TRUE(isInvalid(samePlace));
    EXPECT_TRUE(isNotFinite(samePlace.meetingTime()));

    // Нулевая скорость недопустима: поле не принимает ноль.
    MotionModel zeroSpeed;
    zeroSpeed.setParam(QStringLiteral("v1"), 0.0);
    EXPECT_GT(zeroSpeed.param(QStringLiteral("v1")), 0.0);
}

TEST(MotionModel, DerivationContainsMeetingSolution) {
    MotionModel model;
    fillMeeting(model);

    const auto derivation = model.derivation();
    EXPECT_TRUE(derivation.valid);
    EXPECT_FALSE(derivation.steps.isEmpty());
    // В записи есть скорость сближения 150 и ответ 1 ч.
    bool hasClosingSpeed = false;
    bool hasAnswer = false;
    for (const auto& step : derivation.steps) {
        if (step.content.contains(QStringLiteral("150")))
            hasClosingSpeed = true;
        if (step.kind.contains(QStringLiteral("Ответ")))
            hasAnswer = true;
    }
    EXPECT_TRUE(hasClosingSpeed);
    EXPECT_TRUE(hasAnswer);
    EXPECT_TRUE(derivation.answerTemplate.contains(QStringLiteral("1")));
}

TEST(MotionModel, DragChangesCoordinateDistanceAndEvent) {
    MotionModel model;
    fillMeeting(model);

    // Перемещение автомобиля мышью меняет координату и расстояние...
    model.setHandleValue(QStringLiteral("x1"), 20.0);
    EXPECT_NEAR(model.startOf(0), 20.0, 1e-9);
    EXPECT_NEAR(std::abs(model.startOf(1) - model.startOf(0)), 130.0, 1e-9);
    // ...и математическое событие: 130 / 150 ч.
    EXPECT_NEAR(model.meetingTime(), 130.0 / 150.0, 1e-9);
}

TEST(MotionModel, HeadOnFromBothSidesOfRoad) {
    MotionModel model;
    // Автомобиль 1 движется влево от отметки 200 км, автомобиль 2 — вправо от 0 км.
    setSetup(model, QStringLiteral("x1=200; v1=70; dir1=1; x2=0; v2=80; dir2=0"));
    ASSERT_FALSE(isInvalid(model));

    const double expectedTime = 200.0 / 150.0;
    EXPECT_NEAR(model.meetingTime(), expectedTime, 1e-9);
    EXPECT_NEAR(model.positionOf(0, expectedTime), 200.0 - 70.0 * expectedTime, 1e-9);
    EXPECT_NEAR(model.positionOf(1, expectedTime), 80.0 * expectedTime, 1e-9);
}
