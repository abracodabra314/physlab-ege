#pragma once

#include <QHash>
#include <QMainWindow>
#include <QVector>

#include "core/Simulation.h"
#include "scenes/LabScene.h"
#include "ui/DerivationPanel.h"
#include "ui/ExperimentController.h"
#include "ui/LabView.h"
#include "ui/ParamPanel.h"
#include "ui/ReadoutPanel.h"

class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QToolButton;

namespace lab::ui {

/**
 * Главное окно лаборатории: каталог моделей + рабочая область
 * (сцена, панели параметров, показаний и вычислений, пульт управления).
 *
 * Все команды пульта идут в ExperimentController и Simulation; панель
 * вычислений можно скрыть без остановки эксперимента.
 */
class LabWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit LabWindow(QWidget* parent = nullptr);

    /** Открывает каталог (кнопка «К моделям» и Esc). */
    void showCatalog();
    /** Открывает лабораторную установку для модели с указанным id. */
    void openModel(const QString& modelId);

    /** Активная модель (пусто, если открыт каталог). Использует режим --shots. */
    Simulation* activeModel() const;

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    /** Пара «модель + представление» для одной темы, кэшируется при открытии. */
    struct Page {
        Simulation* model = nullptr;
        LabScene* scene = nullptr;
        LabView* view = nullptr;
    };

    void applyStyleSheet();
    void buildTopBar();
    void buildCatalog();
    void buildWorkspace();
    void buildBottomBar();
    void updateRunButtons(RunState state);
    void toggleFullScreen();
    void toggleDerivation();
    Page& pageFor(const QString& modelId);

    QHash<QString, Page> m_pages;

    QStackedWidget* m_stack = nullptr;
    QWidget* m_catalogPage = nullptr;
    QWidget* m_workspacePage = nullptr;
    QWidget* m_sceneHost = nullptr;
    LabView* m_activeView = nullptr;

    QLabel* m_titleLabel = nullptr;
    QLabel* m_subtitleLabel = nullptr;
    QLabel* m_stateLabel = nullptr;
    QPushButton* m_backButton = nullptr;
    QPushButton* m_fullScreenButton = nullptr;
    QPushButton* m_settingsButton = nullptr;

    ExperimentController* m_controller = nullptr;
    ParamPanel* m_paramPanel = nullptr;
    ReadoutPanel* m_readoutPanel = nullptr;
    DerivationPanel* m_derivationPanel = nullptr;
    QWidget* m_derivationContainer = nullptr;

    QToolButton* m_startButton = nullptr;
    QToolButton* m_pauseButton = nullptr;
    QToolButton* m_resumeButton = nullptr;
    QToolButton* m_resetButton = nullptr;
    QToolButton* m_stepButton = nullptr;
    QComboBox* m_speedCombo = nullptr;

    QMenu* m_settingsMenu = nullptr;
    QAction* m_showDerivationAction = nullptr;
    bool m_fullScreenOn = false;
};

}  // namespace lab::ui
