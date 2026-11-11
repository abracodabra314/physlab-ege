#pragma once

#include <QScrollArea>

#include "core/Simulation.h"

class QLabel;
class QSlider;
class QVBoxLayout;

namespace lab::ui {

/**
 * Панель текущих значений физических величин (показания «приборов»)
 * и строка модельного времени с ползунком перемотки.
 */
class ReadoutPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit ReadoutPanel(QWidget* parent = nullptr);

    void setModel(Simulation* simulation);
    /** Обновляет подписи и значения полей без пересоздания интерфейса. */
    void refreshValues();

signals:
    /** Пользователь перемотал время ползунком. */
    void timeSeekRequested(double t);

private:
    struct Row {
        QLabel* label = nullptr;
        QLabel* value = nullptr;
    };

    void rebuild();

    Simulation* m_model = nullptr;
    QWidget* m_content = nullptr;
    QVBoxLayout* m_layout = nullptr;
    QVector<Row> m_rows;

    QLabel* m_timeLabel = nullptr;
    QSlider* m_timeSlider = nullptr;
    bool m_seeking = false;
};

}  // namespace lab::ui
