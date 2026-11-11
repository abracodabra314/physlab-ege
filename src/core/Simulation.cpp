#include "core/Simulation.h"

#include <algorithm>
#include <cmath>

namespace lab {

Simulation::Simulation(QObject* parent) : QObject(parent) {}

Simulation::~Simulation() = default;

QString Simulation::problemText() const {
    return statement();
}

double Simulation::timePerSecond() const { return 1.0; }

double Simulation::duration() const { return 10.0; }

double Simulation::stepSize() const {
    const double total = duration();
    return total > 0.0 ? total / 50.0 : 0.1;
}

void Simulation::seedDefaults() {
    m_params.clear();
    for (const ParamSpec& spec : paramSpecs()) {
        double def = spec.defaultValue;
        if (!std::isfinite(def))
            def = spec.minValue + (spec.maxValue - spec.minValue) * 0.5;
        // Округляем значение по умолчанию к шагу, чтобы поля выглядели «чисто».
        if (spec.stepValue > 0.0)
            def = std::round(def / spec.stepValue) * spec.stepValue;
        m_params.insert(spec.id, std::clamp(def, spec.minValue, spec.maxValue));
    }
}

ParamSpec Simulation::findSpec(const QString& id) const {
    const QVector<ParamSpec> specs = paramSpecs();
    for (const ParamSpec& spec : specs) {
        if (spec.id == id)
            return spec;
    }
    ParamSpec fallback;
    fallback.id = id;
    return fallback;
}

double Simulation::param(const QString& id) const {
    const auto it = m_params.constFind(id);
    if (it == m_params.constEnd())
        return 0.0;
    const ParamSpec spec = findSpec(id);
    if (spec.kind == ParamKind::Choice || spec.kind == ParamKind::Toggle)
        return it.value();
    return std::clamp(it.value(), spec.minValue, spec.maxValue);
}

void Simulation::setParam(const QString& id, double value) {
    const ParamSpec spec = findSpec(id);
    if (spec.kind != ParamKind::Choice && spec.kind != ParamKind::Toggle) {
        // Не допускаем нулевых значений там, где деление на ноль бессмысленно.
        if (!spec.allowZero && std::abs(value) < 1e-9)
            value = spec.stepValue > 0.0 ? spec.stepValue : spec.minValue;
        value = std::clamp(value, spec.minValue, spec.maxValue);
    }
    const auto it = m_params.constFind(id);
    if (it != m_params.constEnd() && qFuzzyCompare(1.0 + it.value(), 1.0 + value))
        return;
    m_params.insert(id, value);
    // Сначала сбрасываем «завершено», чтобы подписчики увидели корректное
    // состояние (ключевые события пересчитываются из новых параметров).
    invalidateSolution();
    emit parametersChanged();
    emit derivationChanged();
}

void Simulation::setParamRaw(const QString& id, double value) {
    const auto it = m_params.constFind(id);
    if (it != m_params.constEnd() && qFuzzyCompare(1.0 + it.value(), 1.0 + value))
        return;
    m_params.insert(id, value);
    invalidateSolution();
    emit parametersChanged();
    emit derivationChanged();
}

bool Simulation::paramFlag(const QString& id) const {
    return param(id) > 0.5;
}

int Simulation::paramChoiceIndex(const QString& id) const {
    return qRound(param(id));
}

QString Simulation::paramChoiceText(const QString& id) const {
    const ParamSpec spec = findSpec(id);
    const int index = paramChoiceIndex(id);
    if (index >= 0 && index < spec.choices.size())
        return spec.choices.at(index);
    return {};
}

void Simulation::setHandleValue(const QString& id, double value) {
    const QVector<HandleInfo> all = handles();
    for (const HandleInfo& handle : all) {
        if (handle.id != id)
            continue;
        setParam(id, std::clamp(value, handle.minValue, handle.maxValue));
        return;
    }
}

void Simulation::beginInteraction() {
    m_interacting = true;
    if (m_runState == RunState::Running) {
        m_runState = RunState::Paused;
        emit runStateChanged(m_runState);
    }
}

void Simulation::endInteraction() {
    m_interacting = false;
}

void Simulation::reset() {
    m_time = 0.0;
    m_runState = RunState::Stopped;
    emit timeChanged(m_time);
    emit runStateChanged(m_runState);
}

void Simulation::start() {
    m_time = 0.0;
    m_runState = RunState::Running;
    emit timeChanged(m_time);
    emit runStateChanged(m_runState);
}

void Simulation::pause() {
    if (m_runState != RunState::Running)
        return;
    m_runState = RunState::Paused;
    emit runStateChanged(m_runState);
}

void Simulation::resume() {
    if (m_runState != RunState::Paused)
        return;
    m_runState = RunState::Running;
    emit runStateChanged(m_runState);
}

void Simulation::advance(double realSeconds, double speed) {
    advanceModelTime(realSeconds * std::max(0.0, speed) * timePerSecond());
}

void Simulation::advanceModelTime(double dtModel) {
    if (m_runState != RunState::Running)
        return;
    setStateTime(m_time + dtModel, /*stopAtEvent=*/true);
}

void Simulation::setShowsVelocities(bool on) {
    if (m_showVelocities == on)
        return;
    m_showVelocities = on;
    emit vectorOptionsChanged();
}

void Simulation::setShowsAccelerations(bool on) {
    if (m_showAccelerations == on)
        return;
    m_showAccelerations = on;
    emit vectorOptionsChanged();
}

void Simulation::setShowsForces(bool on) {
    if (m_showForces == on)
        return;
    m_showForces = on;
    emit vectorOptionsChanged();
}

void Simulation::setVectorScale(double scale) {
    const double clamped = std::clamp(scale, 0.2, 5.0);
    if (qFuzzyCompare(1.0 + m_vectorScale, 1.0 + clamped))
        return;
    m_vectorScale = clamped;
    emit vectorOptionsChanged();
}

void Simulation::seekTo(double t) {
    if (m_runState == RunState::Stopped)
        m_runState = RunState::Paused;  // перемотка выводит эксперимент в «просмотр»
    setStateTime(t);
    // Если перемотали назад за пределы события — эксперимент снова идущий.
    if (m_runState == RunState::Finished) {
        const double event = eventTime();
        const bool beforeEvent = (!std::isfinite(event) || m_time < event - 1e-9) &&
                                 (duration() <= 0.0 || m_time < duration() - 1e-9);
        if (beforeEvent) {
            m_runState = RunState::Paused;
            emit runStateChanged(m_runState);
        }
    }
}

void Simulation::stepOnce() {
    if (m_runState == RunState::Finished)
        return;
    if (m_runState == RunState::Stopped)
        m_runState = RunState::Paused;  // первый шаг выводит модель в «паузу»
    setStateTime(m_time + std::abs(stepSize()), /*stopAtEvent=*/true);
    if (m_runState == RunState::Running) {  // шаг из режима воспроизведения — снова пауза
        m_runState = RunState::Paused;
        emit runStateChanged(m_runState);
    }
}

void Simulation::setStateTime(double t, bool stopAtEvent) {
    const double total = duration();
    double next = std::max(0.0, t);
    if (total > 0.0)
        next = std::min(next, total);
    const bool reachedEnd = total > 0.0 && next >= total - 1e-9;
    const double event = eventTime();
    const bool eventHappened = std::isfinite(event) && next >= event - 1e-9;

    // Во время проигрывания останавливаемся ровно на событии задачи
    // (иначе кадр анимации «перепрыгивает» момент встречи/заполнения).
    if (stopAtEvent && eventHappened && event >= 0.0)
        next = std::min(next, event);

    m_time = next;
    emit timeChanged(m_time);

    if (reachedEnd || eventHappened) {
        m_runState = RunState::Finished;
        emit runStateChanged(m_runState);
    }
}

void Simulation::invalidateSolution() {
    // Ключевые точки (встреча, заполнение, эхо) пересчитываются «на лету»
    // в eventTime(), поэтому при смене параметров достаточно снять флаг
    // завершения — и анимация снова сможет дойти до нового события.
    if (m_runState == RunState::Finished) {
        m_runState = m_interacting ? RunState::Paused : RunState::Stopped;
        if (m_time > duration() && duration() > 0.0)
            setStateTime(m_time);  // время вышло за новый горизонт — подтягиваем
        emit runStateChanged(m_runState);
    }
}

}  // namespace lab
