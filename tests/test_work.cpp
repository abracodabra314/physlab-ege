#include <gtest/gtest.h>

#include <cmath>

#include "TestSupport.h"
#include "models/WorkModel.h"

using lab::models::WorkModel;
using lab::test::isInvalid;
using lab::test::setSetup;

namespace {

// Классическая задача: первый механизм делает работу за 12 мин (p = 1/12),
// второй — за 8 мин (p = 1/8). Вместе: 1/(1/12 + 1/8) = 4.8 мин.
void fillTwoMachines(WorkModel& model) {
    setSetup(model, QStringLiteral("mode=0; p1=0.08333333333333333; on1=1; "
                                   "p2=0.125; on2=1; p3=0; on3=0"));
}

}  // namespace

TEST(WorkModel, TogetherWorkTime) {
    WorkModel model;
    fillTwoMachines(model);
    ASSERT_FALSE(isInvalid(model));

    // Суммарная производительность 1/12 + 1/8 = 5/24.
    EXPECT_NEAR(model.totalRate(), 5.0 / 24.0, 1e-12);
    // t = 1 / (5/24) = 4.8 мин.
    EXPECT_NEAR(model.completionTime(), 24.0 / 5.0, 1e-9);
    EXPECT_NEAR(model.eventTime(), 4.8, 1e-9);
}

TEST(WorkModel, ProgressAtTime) {
    WorkModel model;
    fillTwoMachines(model);
    // A = (p1 + p2) t; при t = 2.4 выполнено ровно половина.
    EXPECT_NEAR(model.progressAt(2.4), 0.5, 1e-9);
    EXPECT_NEAR(model.progressAt(4.8), 1.0, 1e-9);
    // Работа не может превысить единицу.
    EXPECT_NEAR(model.progressAt(100.0), 1.0, 1e-9);
}

TEST(WorkModel, DisabledMachinesAreIgnored) {
    WorkModel model;
    fillTwoMachines(model);
    setSetup(model, QStringLiteral("on2=0"));
    // Остался только первый механизм: t = 12 мин.
    EXPECT_NEAR(model.totalRate(), 1.0 / 12.0, 1e-12);
    EXPECT_NEAR(model.completionTime(), 12.0, 1e-9);
}

TEST(WorkModel, ThreeMachinesTogether) {
    WorkModel model;
    // 1/12 + 1/8 + 1/24 = 1/4 ⇒ вся работа за 4 мин.
    setSetup(model, QStringLiteral("mode=0; p1=0.08333333333333333; on1=1; p2=0.125; on2=1; "
                                   "p3=0.041666666666666664; on3=1"));
    ASSERT_FALSE(isInvalid(model));
    EXPECT_NEAR(model.totalRate(), 0.25, 1e-12);
    EXPECT_NEAR(model.completionTime(), 4.0, 1e-9);
}

TEST(WorkModel, SequentialStageThenTogether) {
    WorkModel model;
    // Первый механизм один делает 0.3 работы: t1 = 0.3·12 = 3.6 мин;
    // остальные 0.7 выполняют вместе (5/24): t2 = 0.7·24/5 = 3.36 мин.
    setSetup(model, QStringLiteral("mode=1; solo=0; soloWork=0.3; "
                                   "p1=0.08333333333333333; on1=1; p2=0.125; on2=1; on3=0"));
    ASSERT_FALSE(isInvalid(model));

    EXPECT_NEAR(model.soloTime(), 3.6, 1e-9);
    EXPECT_NEAR(model.completionTime(), 3.6 + 3.36, 1e-9);
    // К концу одиночного этапа выполнено ровно 0.3.
    EXPECT_NEAR(model.progressAt(3.6), 0.3, 1e-9);
    EXPECT_NEAR(model.progressAt(model.completionTime()), 1.0, 1e-9);
}

TEST(WorkModel, AllDisabledIsRejected) {
    WorkModel model;
    setSetup(model, QStringLiteral("on1=0; on2=0; on3=0"));
    EXPECT_TRUE(isInvalid(model));
}

TEST(WorkModel, SoloMachineMustBeEnabled) {
    WorkModel model;
    fillTwoMachines(model);
    setSetup(model, QStringLiteral("mode=1; solo=1; on2=0"));
    // Сценарий требует, чтобы механизм 2 работал первым, но он выключен.
    EXPECT_TRUE(isInvalid(model));
}

TEST(WorkModel, DragChangesRateAndTime) {
    WorkModel model;
    fillTwoMachines(model);
    // Перетаскивание шестерни меняет производительность и время всей работы.
    model.setHandleValue(QStringLiteral("p2"), 0.25);   // 1/4 работы в минуту
    EXPECT_NEAR(model.totalRate(), 1.0 / 12.0 + 0.25, 1e-9);
    EXPECT_NEAR(model.completionTime(), 1.0 / (1.0 / 12.0 + 0.25), 1e-9);
}

TEST(WorkModel, DerivationHasSumFormula) {
    WorkModel model;
    fillTwoMachines(model);
    const auto d = model.derivation();
    ASSERT_TRUE(d.valid);
    bool hasSum = false;
    for (const auto& step : d.steps) {
        if (step.content.contains(QStringLiteral("p_{1} + p_{2}")))
            hasSum = true;
    }
    EXPECT_TRUE(hasSum);
    EXPECT_TRUE(d.answerTemplate.contains(QStringLiteral("4")));
}

TEST(WorkModel, StateContributionSumsToProgress) {
    WorkModel model;
    fillTwoMachines(model);
    const auto state = model.stateAt(3.0);
    double sum = 0.0;
    for (const auto& machine : state.entities)
        sum += machine.position;
    EXPECT_NEAR(sum, state.primary, 1e-9);
}
