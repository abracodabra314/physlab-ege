#include "ModelFactory.h"

#include "models/AdiabaticModel.h"
#include "models/BacteriaModel.h"
#include "models/BoatModel.h"
#include "models/DecayModel.h"
#include "models/HorizonModel.h"
#include "models/MixtureModel.h"
#include "models/MotionModel.h"
#include "models/ProjectileModel.h"
#include "models/SoundModel.h"
#include "models/SpringHorizontalModel.h"
#include "models/SpringVerticalModel.h"
#include "models/TankModel.h"
#include "models/WellModel.h"
#include "models/WorkModel.h"

#include "scenes/AdiabaticScene.h"
#include "scenes/BacteriaScene.h"
#include "scenes/BoatScene.h"
#include "scenes/DecayScene.h"
#include "scenes/HorizonScene.h"
#include "scenes/MixtureScene.h"
#include "scenes/MotionScene.h"
#include "scenes/ProjectileScene.h"
#include "scenes/SoundScene.h"
#include "scenes/SpringHorizontalScene.h"
#include "scenes/SpringVerticalScene.h"
#include "scenes/TankScene.h"
#include "scenes/WellScene.h"
#include "scenes/WorkScene.h"

namespace lab {
namespace {

LabSetup makeMotion() {
    auto* model = new models::MotionModel;
    return {model, new scenes::MotionScene(model)};
}

LabSetup makeBoat() {
    auto* model = new models::BoatModel;
    return {model, new scenes::BoatScene(model)};
}

LabSetup makeTank() {
    auto* model = new models::TankModel;
    return {model, new scenes::TankScene(model)};
}

LabSetup makeWell() {
    auto* model = new models::WellModel;
    return {model, new scenes::WellScene(model)};
}

LabSetup makeWork() {
    auto* model = new models::WorkModel;
    return {model, new scenes::WorkScene(model)};
}

LabSetup makeMixture() {
    auto* model = new models::MixtureModel;
    return {model, new scenes::MixtureScene(model)};
}

LabSetup makeSpringHorizontal() {
    auto* model = new models::SpringHorizontalModel;
    return {model, new scenes::SpringHorizontalScene(model)};
}

LabSetup makeProjectile() {
    auto* model = new models::ProjectileModel;
    return {model, new scenes::ProjectileScene(model)};
}

LabSetup makeAdiabatic() {
    auto* model = new models::AdiabaticModel;
    return {model, new scenes::AdiabaticScene(model)};
}

LabSetup makeBacteria() {
    auto* model = new models::BacteriaModel;
    return {model, new scenes::BacteriaScene(model)};
}

LabSetup makeDecay() {
    auto* model = new models::DecayModel;
    return {model, new scenes::DecayScene(model)};
}

LabSetup makeSound() {
    auto* model = new models::SoundModel;
    return {model, new scenes::SoundScene(model)};
}

LabSetup makeHorizon() {
    auto* model = new models::HorizonModel;
    return {model, new scenes::HorizonScene(model)};
}

LabSetup makeSpringVertical() {
    auto* model = new models::SpringVerticalModel;
    return {model, new scenes::SpringVerticalScene(model)};
}

}  // namespace

QVector<ModelDescriptor> allDescriptors() {
    return {
        {QStringLiteral("motion"), QStringLiteral("Два движущихся автомобиля"),
         QStringLiteral("Встречное движение, обгон, задержка старта: s₁ = s₁₀ + v₁t"), makeMotion},
        {QStringLiteral("boat"), QStringLiteral("Движение теплоходов"),
         QStringLiteral("Скорость по и против течения, время рейса между пристанями"), makeBoat},
        {QStringLiteral("tank"), QStringLiteral("Резервуар с водой"),
         QStringLiteral("Краны наполнения и слива: V(t) = V₀ + (q₁ + q₂ − q₃)·t"), makeTank},
        {QStringLiteral("well"), QStringLiteral("Колодец: камень и эхо"),
         QStringLiteral("Свободное падение h = gt²/2 и возврат звука T = t₁ + t₂"), makeWell},
        {QStringLiteral("work"), QStringLiteral("Совместная работа механизмов"),
         QStringLiteral("A = (p₁ + p₂)·t, раздельный и совместный этапы"), makeWork},
        {QStringLiteral("mixture"), QStringLiteral("Смешивание растворов"),
         QStringLiteral("C = (V₁C₁ + V₂C₂)/(V₁ + V₂), масса вещества m = V·C"), makeMixture},
        {QStringLiteral("projectile"), QStringLiteral("Полёт снаряда по параболе"),
         QStringLiteral("x = x₀ + v₀t·cosθ; y = y₀ + v₀t·sinθ − gt²/2"), makeProjectile},
        {QStringLiteral("spring_h"), QStringLiteral("Пружина с грузом: горизонтальные колебания"),
         QStringLiteral("m·x″ = −kx − μmg·sign(v); ω = √(k/m), T = 2π√(m/k)"), makeSpringHorizontal},
        {QStringLiteral("spring_v"), QStringLiteral("Пружина с грузом: вертикальные колебания"),
         QStringLiteral("k·Δl = mg; m·x″ = −kx — трения нет, груз подвешен"), makeSpringVertical},
        {QStringLiteral("horizon"), QStringLiteral("Дальность видимого горизонта"),
         QStringLiteral("d = √(2Rh + h²); s = R·arccos(R/(R + h))"), makeHorizon},
        {QStringLiteral("sound"), QStringLiteral("Гудок судна: распространение звука"),
         QStringLiteral("rₖ = c(t − tₖ) из точки излучения; T′ = T(c − v₁)/(c − v₂)"), makeSound},
        {QStringLiteral("adiabatic"), QStringLiteral("Адиабатное сжатие газа"),
         QStringLiteral("pVᵧ = const; A = −ΔU; v = √(3RT/M)"), makeAdiabatic},
        {QStringLiteral("decay"), QStringLiteral("Радиоактивный распад ядер"),
         QStringLiteral("N = N₀·2^(−t/T½); λ = ln2/T½; A = λN"), makeDecay},
        {QStringLiteral("bacteria"), QStringLiteral("Деление бактерий в чашке Петри"),
         QStringLiteral("N = N₀·2^(t/T_d); n = log₂(N/N₀); N′ = kN"), makeBacteria},
    };
}

}  // namespace lab
