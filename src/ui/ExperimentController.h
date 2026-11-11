#pragma once

#include <QObject>
#include <QTimer>

#include "core/Simulation.h"

namespace lab::ui {

/**
 * Управляет жизненным циклом эксперимента: таймер кадров переводит реальное
 * время в модельное (с поправкой на выбранную скорость) и передаёт его модели.
 * Все команды (пуск/пауза/шаг/сброс) делегируются модели — контроллер не
 * хранит физическое состояние, только флаги и темп.
 */
class ExperimentController : public QObject {
    Q_OBJECT
public:
    explicit ExperimentController(QObject* parent = nullptr);

    void setSimulation(Simulation* simulation);
    Simulation* simulation() const { return m_simulation; }

    void setSpeed(double speed);
    double speed() const { return m_speed; }

public slots:
    void start();
    void pause();
    void resume();
    void reset();
    void step();

private slots:
    void onTick();

signals:
    void speedChanged(double speed);

private:
    Simulation* m_simulation = nullptr;
    QTimer m_timer;
    double m_speed = 1.0;
    qint64 m_lastMs = 0;
};

}  // namespace lab::ui
