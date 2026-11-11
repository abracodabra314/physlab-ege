#include "ui/ExperimentController.h"

#include <QDateTime>

#include <algorithm>

namespace lab::ui {
namespace {

// Кадровая частота анимации: достаточно для плавного движения.
constexpr int kFrameIntervalMs = 16;

}  // namespace

ExperimentController::ExperimentController(QObject* parent) : QObject(parent) {
    m_timer.setInterval(kFrameIntervalMs);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &ExperimentController::onTick);
}

void ExperimentController::setSimulation(Simulation* simulation) {
    if (m_simulation == simulation)
        return;
    m_simulation = simulation;
    m_timer.stop();
    m_lastMs = 0;
}

void ExperimentController::setSpeed(double speed) {
    m_speed = qMax(0.01, speed);
    emit speedChanged(m_speed);
}

void ExperimentController::start() {
    if (!m_simulation)
        return;
    m_simulation->start();
    m_lastMs = QDateTime::currentMSecsSinceEpoch();
    m_timer.start();
}

void ExperimentController::pause() {
    if (!m_simulation)
        return;
    m_simulation->pause();
    m_timer.stop();
}

void ExperimentController::resume() {
    if (!m_simulation)
        return;
    if (m_simulation->runState() == RunState::Stopped) {
        // «Продолжить» после сброса равносильно запуску.
        start();
        return;
    }
    m_simulation->resume();
    if (m_simulation->runState() == RunState::Running) {
        m_lastMs = QDateTime::currentMSecsSinceEpoch();
        m_timer.start();
    }
}

void ExperimentController::reset() {
    if (!m_simulation)
        return;
    m_timer.stop();
    m_lastMs = 0;
    m_simulation->reset();
}

void ExperimentController::step() {
    if (!m_simulation)
        return;
    m_timer.stop();
    m_simulation->stepOnce();
}

void ExperimentController::onTick() {
    if (!m_simulation)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastMs == 0)
        m_lastMs = now;
    // Ограничиваем шаг, чтобы после паузы или лага модель не «прыгала».
    const double dtSeconds = std::clamp(double(now - m_lastMs) / 1000.0, 0.0, 0.1);
    m_lastMs = now;

    m_simulation->advance(dtSeconds, m_speed);

    if (m_simulation->runState() != RunState::Running)
        m_timer.stop();
}

}  // namespace lab::ui
