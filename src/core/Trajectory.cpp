#include "core/Trajectory.h"

#include <algorithm>
#include <cmath>

namespace lab {

// ------------------------------------------------------------------
// Trajectory
// ------------------------------------------------------------------

void Trajectory::simulate(double tMax, double requestedStep, const Rhs& rhs,
                          const QVector<double>& initial, int maxSamples, const StepHook& hook) {
    m_samples.clear();
    m_components = initial.size();
    if (m_components == 0 || tMax <= 0.0 || !rhs) {
        m_step = 0.0;
        if (m_components > 0)
            m_samples.push_back({0.0, initial});
        return;
    }

    // Количество шагов ограничено: при большом tMax шаг укрупняется,
    // поэтому стоимость расчёта остаётся постоянной и предсказуемой.
    const int intervals = std::max(1, std::min(maxSamples, static_cast<int>(std::ceil(tMax / requestedStep))));
    m_step = tMax / intervals;

    m_samples.reserve(intervals + 1);
    QVector<double> y = initial;
    m_samples.push_back({0.0, y});

    QVector<double> k1(m_components), k2(m_components), k3(m_components), k4(m_components);
    QVector<double> tmp(m_components);
    QVector<double> prev = y;

    for (int i = 0; i < intervals; ++i) {
        const double t = i * m_step;

        rhs(t, y, k1);
        for (int c = 0; c < m_components; ++c)
            tmp[c] = y[c] + 0.5 * m_step * k1[c];
        rhs(t + 0.5 * m_step, tmp, k2);
        for (int c = 0; c < m_components; ++c)
            tmp[c] = y[c] + 0.5 * m_step * k2[c];
        rhs(t + 0.5 * m_step, tmp, k3);
        for (int c = 0; c < m_components; ++c)
            tmp[c] = y[c] + m_step * k3[c];
        rhs(t + m_step, tmp, k4);

        prev = y;
        for (int c = 0; c < m_components; ++c) {
            y[c] += m_step / 6.0 * (k1[c] + 2.0 * k2[c] + 2.0 * k3[c] + k4[c]);
            // Численная неустойчивость не должна превращать решение в NaN:
            // при выходе за разумные пределы состояние просто замораживается.
            if (!std::isfinite(y[c]))
                y[c] = m_samples.last().values[c];
        }

        // Хук может поправить состояние (остановка тела при сухом трении,
        // остановка снаряда при падении) и сообщить, что дальше идти некуда.
        bool finished = false;
        if (hook)
            finished = hook(t + m_step, y, prev);
        m_samples.push_back({(i + 1) * m_step, y});
        if (finished) {
            // Оставшееся время заполняем неизменным состоянием, чтобы
            // stateAt(t) корректно работал до конца шкалы времени.
            for (int j = i + 1; j < intervals; ++j)
                m_samples.push_back({(j + 1) * m_step, y});
            break;
        }
    }
}

QVector<double> Trajectory::stateAt(double t) const {
    if (m_samples.isEmpty())
        return {};
    if (t <= m_samples.first().time)
        return m_samples.first().values;
    if (t >= m_samples.last().time)
        return m_samples.last().values;

    // Бинарный поиск интервала + линейная интерполяция между узлами.
    int low = 0;
    int high = m_samples.size() - 1;
    while (high - low > 1) {
        const int mid = (low + high) / 2;
        if (m_samples[mid].time <= t)
            low = mid;
        else
            high = mid;
    }
    const Sample& a = m_samples[low];
    const Sample& b = m_samples[high];
    const double span = std::max(1e-12, b.time - a.time);
    const double w = std::clamp((t - a.time) / span, 0.0, 1.0);

    QVector<double> result(a.values.size());
    for (int c = 0; c < a.values.size(); ++c)
        result[c] = a.values[c] + w * (b.values[c] - a.values[c]);
    return result;
}

QVector<QPointF> Trajectory::samples(int index) const {
    QVector<QPointF> out;
    if (index < 0 || index >= m_components)
        return out;
    out.reserve(m_samples.size());
    for (const Sample& s : m_samples)
        out.append(QPointF(s.time, s.values.value(index, 0.0)));
    return out;
}

double Trajectory::timeAt(int sampleIndex) const {
    if (sampleIndex < 0 || sampleIndex >= m_samples.size())
        return std::numeric_limits<double>::quiet_NaN();
    return m_samples.at(sampleIndex).time;
}

double Trajectory::valueAt(int sampleIndex, int component) const {
    if (sampleIndex < 0 || sampleIndex >= m_samples.size())
        return std::numeric_limits<double>::quiet_NaN();
    return m_samples.at(sampleIndex).values.value(component, 0.0);
}

double Trajectory::componentAt(int index, double t) const {
    if (index < 0 || index >= m_components)
        return 0.0;
    const QVector<double> y = stateAt(t);
    return index < y.size() ? y[index] : 0.0;
}

double Trajectory::findFirst(const std::function<bool(const QVector<double>&)>& predicate) const {
    if (m_samples.isEmpty() || !predicate)
        return std::numeric_limits<double>::quiet_NaN();
    for (const Sample& s : m_samples) {
        if (predicate(s.values))
            return s.time;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

double Trajectory::findCrossing(int component, double level) const {
    if (component < 0 || component >= m_components || m_samples.size() < 2)
        return std::numeric_limits<double>::quiet_NaN();

    double previous = m_samples.first().values.value(component, 0.0) - level;
    for (int i = 1; i < m_samples.size(); ++i) {
        const double current = m_samples.at(i).values.value(component, 0.0) - level;
        // Смена знака на интервале — корень внутри, уточняем линейно.
        if ((previous <= 0.0 && current > 0.0) || (previous >= 0.0 && current < 0.0)) {
            const double t0 = m_samples.at(i - 1).time;
            const double t1 = m_samples.at(i).time;
            const double span = std::abs(current - previous);
            const double weight = span < 1e-15 ? 0.0 : std::abs(previous) / span;
            return t0 + weight * (t1 - t0);
        }
        if (std::abs(current) < 1e-12)
            return m_samples.at(i).time;
        previous = current;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

// ------------------------------------------------------------------
// SeededRandom
// ------------------------------------------------------------------

double SeededRandom::next() {
    // xorshift64*: быстрый, компактный и полностью воспроизводимый генератор.
    m_state ^= m_state >> 12;
    m_state ^= m_state << 25;
    m_state ^= m_state >> 27;
    const quint64 value = m_state * 0x2545F4914F6CDD1DULL;
    return double(value >> 11) / double(1ULL << 53);
}

double SeededRandom::range(double minValue, double maxValue) {
    return minValue + (maxValue - minValue) * next();
}

int SeededRandom::bounded(int bound) {
    if (bound <= 0)
        return 0;
    return std::min(bound - 1, static_cast<int>(next() * bound));
}

}  // namespace lab
