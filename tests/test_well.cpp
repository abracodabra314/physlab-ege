#include <gtest/gtest.h>

#include <cmath>

#include "TestSupport.h"
#include "models/WellModel.h"

using lab::models::WellModel;
using lab::test::isInvalid;
using lab::test::setSetup;

TEST(WellModel, FallTimeAndSoundTime) {
    WellModel model;
    // h = 19.6 м, g = 9.8: t₁ = sqrt(2*19.6/9.8) = 2 с; c = 340: t₂ = 19.6/340.
    setSetup(model, QStringLiteral("depth=19.6; g=9.8; c=340"));
    ASSERT_FALSE(isInvalid(model));

    EXPECT_NEAR(model.fallTime(), 2.0, 1e-9);
    EXPECT_NEAR(model.soundTime(), 19.6 / 340.0, 1e-9);
    EXPECT_NEAR(model.echoTime(), 2.0 + 19.6 / 340.0, 1e-9);
}

TEST(WellModel, StonePositionMatchesFreeFall) {
    WellModel model;
    setSetup(model, QStringLiteral("depth=19.6; g=9.8; c=340"));
    // h(t) = g t² / 2.
    EXPECT_NEAR(model.stoneDepthAt(1.0), 4.9, 1e-9);
    EXPECT_NEAR(model.stoneDepthAt(2.0), 19.6, 1e-9);   // у воды
    // После удара камень остаётся на дне.
    EXPECT_NEAR(model.stoneDepthAt(5.0), 19.6, 1e-9);
}

TEST(WellModel, SoundFrontPositions) {
    WellModel model;
    setSetup(model, QStringLiteral("depth=19.6; g=9.8; c=340"));
    const double tf = model.fallTime();
    // До удара звука нет.
    EXPECT_TRUE(std::isnan(model.soundFrontAt(tf - 0.1)));
    // Спустя 0.01 с фронт поднялся на c·0.01 = 3.4 м.
    EXPECT_NEAR(model.soundFrontAt(tf + 0.01), 19.6 - 3.4, 1e-9);
}

TEST(WellModel, EchoTimeIsEvent) {
    WellModel model;
    setSetup(model, QStringLiteral("depth=19.6; g=9.8; c=340"));
    EXPECT_NEAR(model.eventTime(), model.echoTime(), 1e-9);

    const auto before = model.stateAt(model.echoTime() - 0.05);
    EXPECT_FALSE(before.resolved);
    const auto after = model.stateAt(model.echoTime());
    EXPECT_TRUE(after.resolved);
}

TEST(WellModel, InverseProblemDepthFromEchoTime) {
    // Прямая задача: h = 45 м, g = 9.8, c = 340.
    const double gravity = 9.8;
    const double sound = 340.0;
    const double h = 45.0;
    const double totalTime = std::sqrt(2.0 * h / gravity) + h / sound;

    // Обратная задача должна восстановить глубину с высокой точностью.
    const double recovered = WellModel::depthFromEchoTime(totalTime, gravity, sound);
    EXPECT_NEAR(recovered, h, 1e-6);

    // Проверка подстановкой: t₁ + t₂ = T.
    const double t1 = std::sqrt(2.0 * recovered / gravity);
    const double t2 = recovered / sound;
    EXPECT_NEAR(t1 + t2, totalTime, 1e-9);
}

TEST(WellModel, InverseProblemRejectsInvalidData) {
    EXPECT_TRUE(std::isnan(WellModel::depthFromEchoTime(0.0, 9.8, 340.0)));
    EXPECT_TRUE(std::isnan(WellModel::depthFromEchoTime(-1.0, 9.8, 340.0)));
    EXPECT_TRUE(std::isnan(WellModel::depthFromEchoTime(5.0, 0.0, 340.0)));
}

TEST(WellModel, DerivationMatchesNumbers) {
    WellModel model;
    setSetup(model, QStringLiteral("depth=19.6; g=9.8; c=340"));
    const auto d = model.derivation();
    ASSERT_TRUE(d.valid);
    // Формула h = gt²/2 и ответ ≈ 2.0576 с.
    bool hasFallFormula = false;
    for (const auto& step : d.steps) {
        if (step.content.contains(QStringLiteral("\\frac{g t_{1}^{2}}{2}")))
            hasFallFormula = true;
    }
    EXPECT_TRUE(hasFallFormula);
    EXPECT_TRUE(d.answerTemplate.contains(QStringLiteral("T =")));
    // Время эха ≈ 2 с (разделитель — запятая, как в форматировании чисел).
    EXPECT_TRUE(d.answerTemplate.contains(QStringLiteral("2,")));
}
