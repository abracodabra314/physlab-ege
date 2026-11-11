#include <gtest/gtest.h>

#include "TestSupport.h"
#include "models/MotionModel.h"
#include "utils/MathText.h"

using lab::RunState;
using lab::models::MotionModel;
using lab::test::fillMeetingFor;
using lab::test::setSetup;

TEST(SimulationCore, LifecycleTransitions) {
    MotionModel model;
    fillMeetingFor(model);
    EXPECT_EQ(model.runState(), RunState::Stopped);
    EXPECT_NEAR(model.time(), 0.0, 1e-12);

    model.start();
    EXPECT_EQ(model.runState(), RunState::Running);

    model.pause();
    EXPECT_EQ(model.runState(), RunState::Paused);

    model.resume();
    EXPECT_EQ(model.runState(), RunState::Running);

    // Продвижение времени с реальной скоростью 1× (timePerSecond подобран модели).
    const double before = model.time();
    model.advance(1.0, 1.0);
    EXPECT_GT(model.time(), before);

    model.reset();
    EXPECT_EQ(model.runState(), RunState::Stopped);
    EXPECT_NEAR(model.time(), 0.0, 1e-12);
}

TEST(SimulationCore, AdvanceDoesNothingWhenStopped) {
    MotionModel model;
    fillMeetingFor(model);
    model.advance(5.0, 4.0);
    EXPECT_NEAR(model.time(), 0.0, 1e-12);
}

TEST(SimulationCore, FinishedWhenEventTimeReached) {
    MotionModel model;
    fillMeetingFor(model);
    model.start();
    // Встреча на t = 1; model->timePerSecond() > 0 ⇒ 10 секунд реального времени хватит.
    model.advance(10.0, 1.0);
    EXPECT_EQ(model.runState(), RunState::Finished);
    const double event = model.meetingTime();
    EXPECT_LE(model.time(), event + 1e-6);
}

TEST(SimulationCore, StepOnceIsDiscreteAndPauses) {
    MotionModel model;
    fillMeetingFor(model);
    model.stepOnce();
    EXPECT_GT(model.time(), 0.0);
    EXPECT_EQ(model.runState(), RunState::Paused);

    const double first = model.time();
    model.stepOnce();
    EXPECT_GT(model.time(), first);
}

TEST(SimulationCore, TimeClampedToDuration) {
    MotionModel model;
    fillMeetingFor(model);
    model.start();
    model.advance(1e9, 1e9);
    EXPECT_LE(model.time(), model.duration() + 1e-9);
}

TEST(SimulationCore, ParameterChangeReopensFinishedExperiment) {
    MotionModel model;
    fillMeetingFor(model);
    model.start();
    model.advance(10.0, 1.0);
    ASSERT_EQ(model.runState(), RunState::Finished);

    // Изменение параметра во время завершения: точки событий пересчитываются,
    // и эксперимент снова можно продолжить.
    model.setParam(QStringLiteral("x2"), 300.0);
    EXPECT_NE(model.runState(), RunState::Finished);
    model.resume();
    model.stepOnce();
    EXPECT_GT(model.time(), 0.0);
}

TEST(SimulationCore, ParametersAreClampedToRange) {
    MotionModel model;
    model.setParam(QStringLiteral("v1"), 100000.0);
    EXPECT_LE(model.param(QStringLiteral("v1")), 150.0);
    model.setParam(QStringLiteral("v1"), -50.0);
    EXPECT_GE(model.param(QStringLiteral("v1")), 1.0);
    // Нулевая скорость недопустима (allowZero = false).
    model.setParam(QStringLiteral("v1"), 0.0);
    EXPECT_GT(model.param(QStringLiteral("v1")), 0.0);
}

TEST(SimulationCore, HandlesRespectModelRanges) {
    MotionModel model;
    fillMeetingFor(model);
    model.setHandleValue(QStringLiteral("x1"), 1e9);
    double maxAllowed = 0.0;
    for (const auto& handle : model.handles()) {
        if (handle.id == QStringLiteral("x1"))
            maxAllowed = handle.maxValue;
    }
    EXPECT_LE(model.startOf(0), maxAllowed);
}

TEST(MathText, FormatsNumbersWithCommaAndNoTrailingZeros) {
    EXPECT_EQ(lab::utils::formatNumber(1.5, 3), QStringLiteral("1,5"));
    EXPECT_EQ(lab::utils::formatNumber(2.0, 3), QStringLiteral("2"));
    EXPECT_EQ(lab::utils::formatNumber(133.3333, 1), QStringLiteral("133,3"));
    EXPECT_EQ(lab::utils::formatNumber(-0.0001, 2), QStringLiteral("0"));
}

TEST(MathText, RendersFractionsAndSubscripts) {
    const QString html = lab::utils::toRichMath(QStringLiteral("t = \\frac{s_{0}}{v_{1} + v_{2}}"));
    EXPECT_TRUE(html.contains(QStringLiteral("<table")));       // дробь
    EXPECT_TRUE(html.contains(QStringLiteral("border-top")));   // черта дроби
    EXPECT_TRUE(html.contains(QStringLiteral("<sub>0</sub>"))); // подстрочный индекс
    EXPECT_TRUE(html.contains(QStringLiteral("<sub>1</sub>")));
}

TEST(MathText, EscapesHtmlAndRendersSymbols) {
    const QString html = lab::utils::toRichMath(QStringLiteral("a < b \\approx c \\cdot d"));
    EXPECT_TRUE(html.contains(QStringLiteral("&lt;")));
    EXPECT_TRUE(html.contains(QStringLiteral("&approx;")));
    EXPECT_TRUE(html.contains(QStringLiteral("&#8901;")));
}

TEST(MathText, DurationFormatting) {
    EXPECT_EQ(lab::utils::formatDuration(1.5, QStringLiteral("h")), QStringLiteral("1 ч 30 мин"));
    EXPECT_EQ(lab::utils::formatDuration(0.5, QStringLiteral("h")), QStringLiteral("30 мин"));
}
