#pragma once

#include <QScrollArea>

#include "core/Simulation.h"

class QLabel;
class QVBoxLayout;

namespace lab::ui {

/**
 * Панель математической модели: условие, неизвестное, формулы, подстановка,
 * промежуточные вычисления и ответ. Строится из Derivation модели через
 * компактную математическую разметку (utils/MathText.h) — формулы содержат
 * дроби, индексы и степени.
 *
 * Панель можно полностью скрыть переключателем «Показать вычисления»
 * (это делает главное окно); при скрытой панели сцена продолжает работать.
 */
class DerivationPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit DerivationPanel(QWidget* parent = nullptr);

    void setModel(Simulation* simulation);
    /** Перестраивает все формулы из текущего состояния модели. */
    void rebuild();

private:
    Simulation* m_model = nullptr;
    QWidget* m_content = nullptr;
    QVBoxLayout* m_layout = nullptr;
};

}  // namespace lab::ui
