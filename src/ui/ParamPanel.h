#pragma once

#include <QHash>
#include <QScrollArea>

#include "core/Simulation.h"

class QDoubleSpinBox;
class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QVBoxLayout;

namespace lab::ui {

/**
 * Боковая панель исходных параметров.
 * Строит элементы управления по ParamSpec модели: ползунки, числовые поля,
 * списки и флажки. Изменения сразу передаются в модель (двусторонняя связь:
 * слайдер -> модель, модель -> слайдер при внешних изменениях, напр. drag).
 */
class ParamPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit ParamPanel(QWidget* parent = nullptr);

    void setModel(Simulation* simulation);
    /** Обновляет значения полей без пересоздания интерфейса (вызывается при drag). */
    void syncValues();

signals:
    void parametersEdited();

private:
    struct Row {
        ParamSpec spec;
        QSlider* slider = nullptr;
        QDoubleSpinBox* spin = nullptr;
        QComboBox* combo = nullptr;
        QCheckBox* check = nullptr;
        QLabel* unitLabel = nullptr;
        bool updating = false;   // защита от рекурсии при программном обновлении
    };

    void rebuild();
    void addGroupTitle(QVBoxLayout* layout, const QString& title);
    Row* findRow(const QString& id);
    void applyRowValue(Row& row, double value, bool fromUi);

    Simulation* m_model = nullptr;
    QVector<Row> m_rows;
    QWidget* m_content = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;
    bool m_syncing = false;
};

}  // namespace lab::ui
