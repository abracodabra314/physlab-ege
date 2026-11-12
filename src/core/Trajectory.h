#pragma once

#include <QPointF>
#include <QVector>

#include <functional>

namespace lab {

/**
 * Кэш численного интегрирования дифференциальной системы.
 *
 * Базовая архитектура лаборатории требует, чтобы состояние вычислялось
 * «напрямую» из параметров: stateAt(t) не должна зависеть от порядка
 * вызовов и от частоты кадров. Задачи с сухим трением или с переходными
 * процессами в цепи такого замкнутого решения не имеют, поэтому модель
 * один раз интегрирует уравнение с фиксированным шагом, сохраняет узлы
 * и далее только интерполирует их.
 *
 * Интегрирование воспроизводимо: при одних и тех же параметрах
 * траектория всегда одна и та же, а перематывать время можно в обе
 * стороны без накопления ошибки.
 */
class Trajectory {
public:
    /** Правая часть системы: dy/dt = f(t, y). */
    using Rhs = std::function<void(double t, const QVector<double>& y, QVector<double>& dydt)>;

    /**
     * Обработчик шага: вызывается после каждого шага интегрирования.
     * Может исправить состояние (например, обнулить скорость) и вернуть true,
     * если движение завершилось — с этого момента состояние замораживается.
     * prev — состояние до шага, что позволяет поймать смену знака скорости.
     */
    using StepHook = std::function<bool(double t, QVector<double>& y, const QVector<double>& prev)>;

    Trajectory() = default;

    /**
     * Интегрирует систему от 0 до tMax с фиксированным шагом (метод Рунге–Кутты 4-го
     * порядка). Количество узлов — не больше maxSamples, шаг при этом
     * укрупняется, чтобы объём расчёта оставался постоянным.
     */
    void simulate(double tMax, double requestedStep, const Rhs& rhs, const QVector<double>& initial,
                  int maxSamples = 1600, const StepHook& hook = {});

    /** Состояние в момент t (линейная интерполяция; за границами — крайнее значение). */
    QVector<double> stateAt(double t) const;

    /** Одна компонента состояния в момент t. */
    double componentAt(int index, double t) const;

    /** Узлы интегрирования как готовый ряд (t, значение) — для графиков. */
    QVector<QPointF> samples(int index) const;

    /** Время i-го узла и значение j-й компоненты в этом узле. */
    double timeAt(int sampleIndex) const;
    double valueAt(int sampleIndex, int component) const;

    /** Момент, когда впервые выполняется предикат (NaN — если не выполняется). */
    double findFirst(const std::function<bool(const QVector<double>&)>& predicate) const;

    /**
     * Момент первого пересечения компонентой уровня level. Между узлами
     * корень уточняется линейно, поэтому время события точнее шага
     * интегрирования (нужно для момента остановки груза или падения снаряда).
     */
    double findCrossing(int component, double level) const;

    bool empty() const { return m_samples.isEmpty(); }
    int componentCount() const { return m_components; }
    int sampleCount() const { return m_samples.size(); }
    /** Фактический шаг сохранения узлов. */
    double step() const { return m_step; }

private:
    struct Sample {
        double time = 0.0;
        QVector<double> values;
    };

    QVector<Sample> m_samples;
    int m_components = 0;
    double m_step = 0.0;
};

/**
 * Детерминированный генератор случайных чисел (алгоритм «вырожденный
 * линейный конгруэнтный», как в NumPy MT19937 упрощённой формы).
 *
 * Нужен там, где модель содержит случайность (распад ядер, расположение
 * бактерий): при фиксированном seed эксперимент повторяется полностью,
 * включая перематывание времени назад.
 */
class SeededRandom {
public:
    explicit SeededRandom(quint64 seed = 0x9E3779B97F4A7C15ULL) : m_state(seed) {}

    /** Следующее число в [0, 1). */
    double next();
    /** Число в [minValue, maxValue). */
    double range(double minValue, double maxValue);
    /** Целое в [0, bound). */
    int bounded(int bound);

    void reseed(quint64 seed) { m_state = seed; }

private:
    quint64 m_state;
};

}  // namespace lab
