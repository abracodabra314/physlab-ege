#pragma once

#include <QPointF>

#include "core/Simulation.h"

namespace lab::models {

/**
 * Модель 9. Дальность горизонта над поверхностью планеты.
 *
 * Геометрия задачи (O — центр планеты радиусом R, A — наблюдатель на высоте
 * h, B — ближайшая точка горизонта):
 *   OB ⊥ AB   (луч взгляда касается поверхности)
 *   OA = R + h,  OB = R
 * По теореме Пифагора
 *   d = AB = √((R + h)² − R²) = √(2Rh + h²)
 * Центральный угол между направлением на наблюдателя и на точку горизонта:
 *   cos α = R / (R + h)  ⇒  α = arccos(R / (R + h))
 * Расстояние вдоль поверхности (дуга):
 *   s = R·α = R·arccos(R / (R + h))
 * Площадь видимого участка — площадь сферического сегмента:
 *   S = 2πR²(1 − cos α),  доля поверхности = S / (4πR²) = (1 − cos α)/2
 *
 * Угол понижения горизонта (насколько линия взгляда ниже истинного горизонта)
 * равен тому же центральному углу α: в прямоугольном треугольнике OAB острые
 * углы в сумме дают 90°.
 *
 * Движение в модели — равномерный подъём наблюдателя: h(t) = v·t, но не выше
 * заданной высоты h. Дальность горизонта и длина дуги в каждый момент считаются
 * по текущей высоте, поэтому графики d(t) и s(t) строятся из тех же формул,
 * что и статический расчёт.
 */
class HorizonModel : public Simulation {
    Q_OBJECT
public:
    explicit HorizonModel(QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("horizon"); }
    QString title() const override;
    QString subtitle() const override;
    QString statement() const override;
    QString problemText() const override;

    QString timeUnit() const override { return QStringLiteral("s"); }
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
    /** Перетаскивание наблюдателя начинает подъём заново. */
    void beginInteraction() override;

    // ---------------- параметры ----------------
    /** Радиус планеты в километрах. */
    double radiusKilometers() const;
    /** Радиус планеты в метрах. */
    double radiusMeters() const;
    /** Целевая высота наблюдателя, м. */
    double maxHeight() const;
    /** Скорость равномерного подъёма, м/с. */
    double ascentSpeed() const;

    // ---------------- геометрия по заданной высоте ----------------
    /** Расстояние до горизонта по прямой: d = √(2Rh + h²). */
    double horizonDistance(double height) const;
    /** Центральный угол α (рад): cos α = R/(R + h). */
    double centralAngle(double height) const;
    /** Центральный угол в градусах — он же угол понижения горизонта. */
    double dipAngleDegrees(double height) const;
    /** Расстояние вдоль поверхности: s = R·α. */
    double arcDistance(double height) const;
    /** Площадь видимого участка поверхности (сферический сегмент). */
    double visibleArea(double height) const;
    /** Доля поверхности планеты, которую видит наблюдатель. */
    double visibleFraction(double height) const;
    /** Приближённая формула √(2Rh), справедливая при h ≪ R. */
    double approximateDistance(double height) const;

    // ---------------- движение ----------------
    /** Высота наблюдателя в момент t (равномерный подъём, но не выше h). */
    double heightAt(double t) const;
    /** Вертикальная скорость в момент t (0, когда подъём завершён). */
    double verticalVelocityAt(double t) const;
    /** Время, за которое наблюдатель достигает заданной высоты. */
    double ascentTime() const;

    double horizonDistanceAt(double t) const;
    double arcDistanceAt(double t) const;
    double dipAngleAt(double t) const;

    // ---------------- ряды данных ----------------
    /** Зависимости от времени: точки (t, значение). */
    QVector<QPointF> heightSeries() const;
    QVector<QPointF> distanceSeries() const;
    QVector<QPointF> arcSeries() const;
    /** Зависимости от высоты: (h, d) и (h, s) — это не временные графики. */
    QVector<QPointF> distanceVersusHeight() const;
    QVector<QPointF> arcVersusHeight() const;
};

}  // namespace lab::models
