#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

#include "core/ModelTypes.h"

namespace lab {

/**
 * Универсальное описание одного объекта сцены внутри состояния модели.
 * Смысл числовых полей задаёт конкретная модель (скорость, уровень,
 * производительность и т. п.), но набор полей един для всех моделей:
 * это позволяет построить общий механизм анимации и панелей.
 */
struct EntityVisual {
    QString id;
    QString label;      // короткая подпись на сцене: «Автомобиль 1»
    double position = 0.0;   // положение в единицах модели (км, м, %)
    double value1 = 0.0;     // скорость / производительность / концентрация
    double value2 = 0.0;     // второе числовое значение (по смыслу модели)
    bool active = true;      // участвует ли объект в процессе сейчас
    bool visible = true;
    int variant = 0;         // визуальный вариант (цвет/направление)
};

/**
 * Мгновенное состояние системы в момент модельного времени t.
 * Состояние всегда вычисляется «напрямую» из исходных данных,
 * поэтому анимация математически точна, а параметры можно менять на лету.
 */
struct SimState {
    double time = 0.0;
    QVector<EntityVisual> entities;
    double primary = 0.0;    // расстояние между телами / объём воды / глубина ...
    double secondary = 0.0;  // вспомогательная величина
    bool resolved = false;   // ключевое событие задачи уже произошло
    QString eventLabel;      // «Встреча», «Обгон», «Резервуар заполнен» ...
};

/**
 * Точка сцены, которую пользователь может переместить мышью.
 * Значение задаётся в единицах модели, поэтому перетаскивание меняет
 * математическое состояние системы, а не только картинку.
 */
struct HandleInfo {
    QString id;
    QString label;
    double value = 0.0;
    double minValue = 0.0;
    double maxValue = 0.0;
    double step = 0.1;         // дискретизация при перетаскивании
    QString unit;
    /** Ось перемещения ручки: "x" — горизонталь, "y" — вертикаль. */
    QString axis = QStringLiteral("x");
    QString readbackLabel;     // величина, которую видно на сцене при перетаскивании
    QString readbackValue;
};

/**
 * Базовый класс физической модели.
 *
 * Класс не зависит от кода отрисовки: вся математика вынесена из методов
 * рисования и может тестироваться без окна.
 *
 * Потомки реализуют: параметры, математическое ядро (stateAt/solve),
 * запись решения, показания и точки drag-взаимодействия.
 */
class Simulation : public QObject {
    Q_OBJECT
public:
    explicit Simulation(QObject* parent = nullptr);
    ~Simulation() override;

    // ---------------- идентификация ----------------
    virtual QString id() const = 0;
    virtual QString title() const = 0;
    virtual QString subtitle() const = 0;     // короткое описание для каталога
    virtual QString statement() const = 0;    // текст типовой задачи ЕГЭ
    virtual QString problemText() const;      // условие с текущими числами (plain text)

    // ---------------- единицы и время ----------------
    virtual QString timeUnit() const = 0;     // "h" | "min" | "s"
    virtual double timePerSecond() const;     // единиц модельного времени за 1 с при скорости 1x
    virtual double duration() const;          // общая длительность наблюдения
    virtual double stepSize() const;          // шаг пошагового выполнения
    double time() const { return m_time; }
    bool isFinished() const { return m_runState == RunState::Finished; }
    RunState runState() const { return m_runState; }

    // ---------------- исходные параметры ----------------
    virtual QVector<ParamSpec> paramSpecs() const = 0;
    double param(const QString& id) const;
    void setParam(const QString& id, double value);
    void setParamRaw(const QString& id, double value);   // без клампинга (для Choice/Toggle)
    bool paramFlag(const QString& id) const;             // значение Toggle/Choice как bool
    int paramChoiceIndex(const QString& id) const;
    QString paramChoiceText(const QString& id) const;

    // ---------------- допустимость ----------------
    virtual ValidationResult validate() const = 0;
    bool isValid() const { return validate().ok; }

    // ---------------- математическое ядро ----------------
    virtual SimState stateAt(double t) const = 0;
    SimState currentState() const { return stateAt(m_time); }
    /** Момент, в который происходит ключевое событие задачи (NaN — события нет). */
    virtual double eventTime() const = 0;

    // ---------------- панель вычислений ----------------
    virtual Derivation derivation() const = 0;
    virtual QVector<Readout> readouts() const = 0;   // текущие значения величин

    // ---------------- интерактивность ----------------
    virtual QVector<HandleInfo> handles() const = 0;
    virtual void setHandleValue(const QString& id, double value);
    /** Вызывается сценой перед началом перетаскивания (для паузы/фиксации времени). */
    virtual void beginInteraction();
    virtual void endInteraction();
    bool isInteracting() const { return m_interacting; }

    // ---------------- управление экспериментом ----------------
    void reset();
    void start();
    void pause();
    void resume();
    /**
     * Продвинуть модельное время на realSeconds * speed * timePerSecond().
     * realSeconds — сколько секунд реального времени прошло с прошлого кадра.
     */
    void advance(double realSeconds, double speed = 1.0);
    /** Продвинуть модельное время на dtModel (единицы модельного времени). */
    void advanceModelTime(double dtModel);
    /** Перемотка на произвольный момент (для ползунка времени). */
    void seekTo(double t);
    /** Один шаг пошагового выполнения. */
    void stepOnce();

signals:
    /** Параметры изменились — сцена и панели должны обновиться. */
    void parametersChanged();
    /** Модельное время изменилось (кадр анимации). */
    void timeChanged(double t);
    void runStateChanged(lab::RunState state);
    /** Обновилась запись решения — панель вычислений перерисовывается. */
    void derivationChanged();

protected:
    /** Заполняет m_params значениями по умолчанию из paramSpecs(). Вызывается из конструкторов потомков. */
    void seedDefaults();

    /**
     * Сброс флага завершения: при изменении параметров точки событий
     * пересчитываются «на лету», и анимация может идти дальше.
     */
    void invalidateSolution();

    /**
     * Устанавливает модельное время. stopAtEvent = true (используется при
     * проигрывании и шаге) — если событие задачи лежит внутри шага, время
     * останавливается ровно на нём, чтобы анимация его не «перепрыгнула».
     */
    void setStateTime(double t, bool stopAtEvent = false);

    QHash<QString, double> m_params;
    bool m_interacting = false;
    double m_time = 0.0;
    RunState m_runState = RunState::Stopped;

private:
    ParamSpec findSpec(const QString& id) const;
};

}  // namespace lab
