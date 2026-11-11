#include "ui/LabWindow.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "ModelFactory.h"
#include "utils/MathText.h"

namespace lab::ui {
namespace {

/** Иконка-пиктограмма кнопки управления (рисуются вектором, без ассетов). */
QIcon makeIcon(const QString& kind) {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(38, 44, 52), 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(QColor(38, 44, 52));

    if (kind == QLatin1String("start")) {
        QPolygonF triangle;
        triangle << QPointF(11, 8) << QPointF(24, 16) << QPointF(11, 24);
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(triangle);
    } else if (kind == QLatin1String("pause")) {
        painter.setPen(Qt::NoPen);
        painter.drawRect(QRectF(11, 8, 4, 16));
        painter.drawRect(QRectF(19, 8, 4, 16));
    } else if (kind == QLatin1String("resume")) {
        QPolygonF triangle;
        triangle << QPointF(10, 8) << QPointF(20, 16) << QPointF(10, 24);
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(triangle);
        painter.drawRect(QRectF(21, 8, 3.4, 16));
    } else if (kind == QLatin1String("reset")) {
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(QRectF(8, 8, 16, 16), 55 * 16, 260 * 16);
        painter.setPen(Qt::NoPen);
        QPolygonF head;
        head << QPointF(21.5, 6.0) << QPointF(25.5, 11.5) << QPointF(18.5, 12.0);
        painter.drawPolygon(head);
    } else if (kind == QLatin1String("step")) {
        QPolygonF triangle;
        triangle << QPointF(9, 8) << QPointF(19, 16) << QPointF(9, 24);
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(triangle);
        painter.drawRect(QRectF(21, 8, 3.4, 16));
    }
    return QIcon(pixmap);
}

QToolButton* makeToolButton(const QString& kind, const QString& text) {
    auto* button = new QToolButton;
    button->setObjectName(QStringLiteral("runButton"));
    button->setIcon(makeIcon(kind));
    button->setIconSize(QSize(26, 26));
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setText(text);
    button->setMinimumSize(74, 54);
    return button;
}

}  // namespace

LabWindow::LabWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Физическая лаборатория ЕГЭ · задача №10"));
    setMinimumSize(1100, 680);

    m_controller = new ExperimentController(this);

    m_stack = new QStackedWidget(this);
    setCentralWidget(m_stack);

    applyStyleSheet();
    buildTopBar();
    buildCatalog();
    buildWorkspace();
    buildBottomBar();

    m_stack->setCurrentWidget(m_catalogPage);

    QSettings settings(QStringLiteral("PhysLabEge"), QStringLiteral("PhysLab"));
    const bool fullscreen = settings.value(QStringLiteral("fullscreen"), true).toBool();
    if (fullscreen)
        toggleFullScreen();
    else
        resize(1440, 860);
}

void LabWindow::applyStyleSheet() {
    setStyleSheet(QStringLiteral(
        // Шрифт не задаём явно: берётся системный UI-шрифт платформы
        // (Windows — Segoe UI, macOS —苹系统ный, Linux — шрифт окружения).
        "QMainWindow, QWidget { background: #f7f6f2; color: #262c34; font-size: 14px; }"
        "#topBar, #bottomBar { background: #ffffff; border: 1px solid #dfe3e8; }"
        "#pageTitle { font-size: 19px; font-weight: 700; color: #1f2a36; }"
        "#pageSubtitle { font-size: 12.5px; color: #5c6673; }"
        "#catalogTitle { font-size: 27px; font-weight: 800; color: #1f2a36; }"
        "#catalogSubtitle { font-size: 14.5px; color: #5c6673; }"
        "#catalogCard { background: #ffffff; border: 1px solid #dfe3e8; border-radius: 12px; }"
        "#catalogCard:hover { border: 1px solid #2176c6; background: #f4f9ff; }"
        "#catalogCardTitle { font-size: 16.5px; font-weight: 700; color: #1f2a36; }"
        "#catalogCardText { font-size: 13px; color: #5c6673; }"
        "#catalogCardNumber { font-size: 12px; font-weight: 700; color: #ffffff; }"
        "#panelTitle { font-size: 14px; font-weight: 700; color: #1f2a36; padding: 2px 0; }"
        "#sidePanel { background: #ffffff; border: 1px solid #dfe3e8; border-radius: 10px; }"
        "#paramGroupTitle { font-size: 12px; font-weight: 700; color: #2176c6; "
        "text-transform: uppercase; padding-top: 6px; }"
        "#paramRow, #readoutRow { background: #f4f5f7; border: 1px solid #e4e7ec; border-radius: 8px; }"
        "#readoutRowHighlight { background: #eef6ff; border: 1px solid #bcd9f2; border-radius: 8px; }"
        "#paramLabel, #readoutLabel { font-size: 12.5px; color: #3b4552; }"
        "#paramUnit { font-size: 11.5px; color: #6b7480; }"
        "#readoutValue { font-size: 13px; font-weight: 600; color: #1f2a36; }"
        "#readoutValueHighlight { font-size: 13.5px; font-weight: 800; color: #16558f; }"
        "#derivationProblem, #derivationStep, #derivationAnswer, #derivationWarning "
        "{ background: #ffffff; border: 1px solid #e4e7ec; border-radius: 8px; }"
        "#derivationProblem { border-left: 3px solid #2176c6; }"
        "#derivationAnswer { background: #eefaf1; border: 1px solid #b9e2c6; }"
        "#derivationWarning { background: #fff4f2; border: 1px solid #f0c2bb; }"
        "#derivationSection { font-size: 12px; font-weight: 700; color: #2176c6; padding-top: 4px; }"
        "#derivationFormula { font-family: 'Cambria Math', 'Latin Modern Math', 'Times New Roman', serif; "
        "font-size: 15px; color: #1f2a36; }"
        "#derivationText { font-size: 13.5px; color: #3b4552; }"
        "QPushButton { background: #ffffff; border: 1px solid #cfd6de; border-radius: 8px; "
        "padding: 7px 14px; font-weight: 600; color: #262c34; }"
        "QPushButton:hover { background: #eef4fb; border-color: #2176c6; color: #16558f; }"
        "QPushButton:pressed { background: #dbe9f7; }"
        "QPushButton:checked { background: #2176c6; border-color: #2176c6; color: #ffffff; }"
        "QPushButton:disabled { color: #a7aeb6; background: #f1f2f4; border-color: #e2e5ea; }"
        "#runButton { background: #ffffff; border: 1px solid #cfd6de; border-radius: 10px; "
        "padding: 4px 6px; font-size: 12px; }"
        "#runButton:hover { background: #eef4fb; border-color: #2176c6; }"
        "#runButton:disabled { color: #aeb4bb; }"
        "QDoubleSpinBox, QSpinBox, QComboBox { background: #ffffff; border: 1px solid #cfd6de; "
        "border-radius: 6px; padding: 3px 6px; min-height: 22px; }"
        "QSlider::groove:horizontal { height: 5px; background: #dfe3e8; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: #2176c6; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 16px; margin: -6px 0; border-radius: 8px; "
        "background: #ffffff; border: 2px solid #2176c6; }"
        "QCheckBox::indicator { width: 17px; height: 17px; }"));
}

void LabWindow::buildTopBar() {
    auto* bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("topBar"));
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(16, 8, 12, 8);
    layout->setSpacing(10);

    m_backButton = new QPushButton(QStringLiteral("← К моделям"), bar);
    m_backButton->setObjectName(QStringLiteral("backButton"));
    m_backButton->setToolTip(QStringLiteral("Вернуться к списку моделей (Esc)"));
    connect(m_backButton, &QPushButton::clicked, this, &LabWindow::showCatalog);

    auto* titles = new QWidget(bar);
    auto* titleLayout = new QVBoxLayout(titles);
    titleLayout->setContentsMargins(4, 0, 0, 0);
    titleLayout->setSpacing(1);
    m_titleLabel = new QLabel(QStringLiteral("Интерактивная физическая лаборатория"), titles);
    m_titleLabel->setObjectName(QStringLiteral("pageTitle"));
    m_subtitleLabel = new QLabel(QStringLiteral("Задача №10 ЕГЭ по профильной математике"), titles);
    m_subtitleLabel->setObjectName(QStringLiteral("pageSubtitle"));
    titleLayout->addWidget(m_titleLabel);
    titleLayout->addWidget(m_subtitleLabel);

    m_settingsButton = new QPushButton(QStringLiteral("Настройки"), bar);
    m_settingsButton->setObjectName(QStringLiteral("settingsButton"));
    m_settingsMenu = new QMenu(this);
    m_showDerivationAction = m_settingsMenu->addAction(QStringLiteral("Показать вычисления"));
    m_showDerivationAction->setCheckable(true);
    m_showDerivationAction->setChecked(true);
    connect(m_showDerivationAction, &QAction::toggled, this, [this]() { toggleDerivation(); });

    // Отдельные переключатели категорий векторов: скорости, ускорения, силы.
    // Они действуют на все сцены и сохраняются между запусками.
    QSettings settings(QStringLiteral("PhysLabEge"), QStringLiteral("PhysLab"));
    auto* vectorsMenu = m_settingsMenu->addMenu(QStringLiteral("Векторы на сценах"));
    m_showVelocitiesAction = vectorsMenu->addAction(QStringLiteral("Скорости (v)"));
    m_showVelocitiesAction->setCheckable(true);
    m_showVelocitiesAction->setChecked(settings.value(QStringLiteral("vectors/velocities"), true).toBool());
    m_showAccelerationsAction = vectorsMenu->addAction(QStringLiteral("Ускорения (a)"));
    m_showAccelerationsAction->setCheckable(true);
    m_showAccelerationsAction->setChecked(settings.value(QStringLiteral("vectors/accelerations"), true).toBool());
    m_showForcesAction = vectorsMenu->addAction(QStringLiteral("Силы (mg, N, F)"));
    m_showForcesAction->setCheckable(true);
    m_showForcesAction->setChecked(settings.value(QStringLiteral("vectors/forces"), true).toBool());
    m_vectorScaleCombo = new QComboBox(m_settingsMenu);
    m_vectorScaleCombo->addItem(QStringLiteral("Стрелки мелкие"), 0.6);
    m_vectorScaleCombo->addItem(QStringLiteral("Стрелки обычные"), 1.0);
    m_vectorScaleCombo->addItem(QStringLiteral("Стрелки крупные"), 1.6);
    const int storedScaleIndex = m_vectorScaleCombo->findData(settings.value(QStringLiteral("vectors/scale"), 1.0));
    m_vectorScaleCombo->setCurrentIndex(storedScaleIndex >= 0 ? storedScaleIndex : 1);
    auto* vectorsScaleWidget = new QWidgetAction(m_settingsMenu);
    {
        auto* rowWidget = new QWidget;
        auto* row = new QHBoxLayout(rowWidget);
        row->setContentsMargins(8, 4, 8, 4);
        row->addWidget(new QLabel(QStringLiteral("Масштаб стрелок:"), rowWidget));
        row->addWidget(m_vectorScaleCombo, 1);
        vectorsScaleWidget->setDefaultWidget(rowWidget);
    }
    vectorsMenu->addAction(vectorsScaleWidget);

    connect(m_showVelocitiesAction, &QAction::toggled, this, [this](bool on) {
        applyVectorOptions();
        QSettings s(QStringLiteral("PhysLabEge"), QStringLiteral("PhysLab"));
        s.setValue(QStringLiteral("vectors/velocities"), on);
    });
    connect(m_showAccelerationsAction, &QAction::toggled, this, [this](bool on) {
        applyVectorOptions();
        QSettings s(QStringLiteral("PhysLabEge"), QStringLiteral("PhysLab"));
        s.setValue(QStringLiteral("vectors/accelerations"), on);
    });
    connect(m_showForcesAction, &QAction::toggled, this, [this](bool on) {
        applyVectorOptions();
        QSettings s(QStringLiteral("PhysLabEge"), QStringLiteral("PhysLab"));
        s.setValue(QStringLiteral("vectors/forces"), on);
    });
    connect(m_vectorScaleCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        applyVectorOptions();
        QSettings s(QStringLiteral("PhysLabEge"), QStringLiteral("PhysLab"));
        s.setValue(QStringLiteral("vectors/scale"), m_vectorScaleCombo->currentData());
    });
    m_settingsButton->setMenu(m_settingsMenu);

    m_fullScreenButton = new QPushButton(QStringLiteral("Во весь экран"), bar);
    m_fullScreenButton->setObjectName(QStringLiteral("fullScreenButton"));
    connect(m_fullScreenButton, &QPushButton::clicked, this, &LabWindow::toggleFullScreen);

    layout->addWidget(m_backButton);
    layout->addWidget(titles, 1);
    layout->addWidget(m_settingsButton);
    layout->addWidget(m_fullScreenButton);

    setMenuBar(nullptr);
    setMenuWidget(bar);
}

void LabWindow::buildCatalog() {
    m_catalogPage = new QWidget(this);
    auto* layout = new QVBoxLayout(m_catalogPage);
    layout->setContentsMargins(30, 22, 30, 22);
    layout->setSpacing(16);

    auto* title = new QLabel(QStringLiteral("Интерактивная физическая лаборатория"), m_catalogPage);
    title->setObjectName(QStringLiteral("catalogTitle"));
    auto* subtitle = new QLabel(QStringLiteral(
                                    "Шесть моделей задачи №10 ЕГЭ: двигайте объекты мышью, меняйте параметры, "
                                    "запускайте движение и следите за тем, как меняется математическая модель."),
                                m_catalogPage);
    subtitle->setObjectName(QStringLiteral("catalogSubtitle"));
    subtitle->setWordWrap(true);
    layout->addWidget(title);
    layout->addWidget(subtitle);

    auto* gridHost = new QWidget(m_catalogPage);
    auto* grid = new QGridLayout(gridHost);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(14);

    const QVector<ModelDescriptor> descriptors = allDescriptors();
    for (int i = 0; i < descriptors.size(); ++i) {
        const ModelDescriptor& descriptor = descriptors[i];
        auto* card = new QPushButton(gridHost);
        card->setObjectName(QStringLiteral("catalogCard"));
        card->setMinimumSize(360, 128);
        card->setCursor(Qt::PointingHandCursor);
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(16, 14, 16, 14);
        cardLayout->setSpacing(6);

        auto* number = new QLabel(QStringLiteral("Модель %1 · №10").arg(i + 1), card);
        number->setStyleSheet(QStringLiteral("color: #2176c6; font-weight: 700; font-size: 12px; background: transparent; border: none;"));
        auto* cardTitle = new QLabel(descriptor.title, card);
        cardTitle->setObjectName(QStringLiteral("catalogCardTitle"));
        cardTitle->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        cardTitle->setWordWrap(true);
        auto* cardText = new QLabel(descriptor.subtitle, card);
        cardText->setObjectName(QStringLiteral("catalogCardText"));
        cardText->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        cardText->setWordWrap(true);

        cardLayout->addWidget(number);
        cardLayout->addWidget(cardTitle);
        cardLayout->addWidget(cardText, 1);
        cardLayout->addStretch();

        const QString id = descriptor.id;
        connect(card, &QPushButton::clicked, this, [this, id]() { openModel(id); });

        grid->addWidget(card, i / 3, i % 3);
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    layout->addWidget(gridHost, 1);
    layout->addStretch();

    m_stack->addWidget(m_catalogPage);
}

void LabWindow::buildWorkspace() {
    m_workspacePage = new QWidget(this);
    auto* layout = new QVBoxLayout(m_workspacePage);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(8);

    auto* contentRow = new QHBoxLayout();
    contentRow->setSpacing(8);

    // Левая панель — исходные параметры.
    auto* leftHost = new QWidget(m_workspacePage);
    leftHost->setObjectName(QStringLiteral("sidePanel"));
    auto* leftLayout = new QVBoxLayout(leftHost);
    leftLayout->setContentsMargins(10, 10, 10, 10);
    leftLayout->setSpacing(6);
    auto* leftTitle = new QLabel(QStringLiteral("Исходные параметры"), leftHost);
    leftTitle->setObjectName(QStringLiteral("panelTitle"));
    leftLayout->addWidget(leftTitle);
    m_paramPanel = new ParamPanel(leftHost);
    leftLayout->addWidget(m_paramPanel, 1);
    leftHost->setMinimumWidth(318);
    leftHost->setMaximumWidth(380);

    // Центр — физическая сцена.
    auto* sceneHost = new QWidget(m_workspacePage);
    sceneHost->setObjectName(QStringLiteral("sidePanel"));
    auto* sceneLayout = new QVBoxLayout(sceneHost);
    sceneLayout->setContentsMargins(4, 4, 4, 4);
    sceneLayout->setSpacing(0);

    // Правая колонка — показания и вычисления.
    auto* rightHost = new QWidget(m_workspacePage);
    rightHost->setObjectName(QStringLiteral("sidePanel"));
    auto* rightLayout = new QVBoxLayout(rightHost);
    rightLayout->setContentsMargins(10, 10, 10, 10);
    rightLayout->setSpacing(8);

    auto* readoutTitle = new QLabel(QStringLiteral("Текущие величины"), rightHost);
    readoutTitle->setObjectName(QStringLiteral("panelTitle"));
    m_readoutPanel = new ReadoutPanel(rightHost);
    m_readoutPanel->setMinimumWidth(280);

    auto* derivationTitle = new QLabel(QStringLiteral("Математическая модель"), rightHost);
    derivationTitle->setObjectName(QStringLiteral("panelTitle"));
    m_derivationPanel = new DerivationPanel(rightHost);

    rightLayout->addWidget(readoutTitle);
    rightLayout->addWidget(m_readoutPanel, 2);
    rightLayout->addWidget(derivationTitle);
    rightLayout->addWidget(m_derivationPanel, 3);
    rightHost->setMinimumWidth(340);
    rightHost->setMaximumWidth(520);

    m_derivationContainer = new QWidget(rightHost);
    m_derivationContainer->setObjectName(QStringLiteral("sidePanel"));
    auto* derivationContainerLayout = new QVBoxLayout(m_derivationContainer);
    derivationContainerLayout->setContentsMargins(10, 10, 10, 10);
    derivationContainerLayout->setSpacing(6);
    derivationContainerLayout->addWidget(derivationTitle);
    derivationContainerLayout->addWidget(m_derivationPanel, 1);
    // Переносим заголовок и панель в контейнер, чтобы вместе скрывать.
    rightLayout->removeWidget(derivationTitle);
    rightLayout->removeWidget(m_derivationPanel);
    rightLayout->addWidget(m_derivationContainer, 3);

    connect(m_readoutPanel, &ReadoutPanel::timeSeekRequested, this, [this](double t) {
        if (m_controller->simulation()) {
            m_controller->pause();
            m_controller->simulation()->seekTo(t);
        }
        if (m_readoutPanel)
            m_readoutPanel->refreshValues();
    });

    contentRow->addWidget(leftHost);
    contentRow->addWidget(sceneHost, 5);
    contentRow->addWidget(rightHost);
    layout->addLayout(contentRow, 1);

    m_stack->addWidget(m_workspacePage);

    // Сцена и представление создаются для каждой модели отдельно;
    // sceneHost принимает активную.
    m_sceneHost = sceneHost;
}

void LabWindow::buildBottomBar() {
    auto* statusBar = new QStatusBar(this);
    statusBar->setObjectName(QStringLiteral("bottomBar"));
    statusBar->setSizeGripEnabled(false);
    auto* bar = new QFrame(statusBar);
    bar->setObjectName(QStringLiteral("bottomBarFrame"));
    statusBar->addWidget(bar, 1);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(16, 8, 16, 8);
    layout->setSpacing(10);

    m_startButton = makeToolButton(QStringLiteral("start"), QStringLiteral("Запуск"));
    m_pauseButton = makeToolButton(QStringLiteral("pause"), QStringLiteral("Пауза"));
    m_resumeButton = makeToolButton(QStringLiteral("resume"), QStringLiteral("Продолжить"));
    m_stepButton = makeToolButton(QStringLiteral("step"), QStringLiteral("Шаг"));
    m_resetButton = makeToolButton(QStringLiteral("reset"), QStringLiteral("Сброс"));

    connect(m_startButton, &QPushButton::clicked, m_controller, &ExperimentController::start);
    connect(m_pauseButton, &QPushButton::clicked, m_controller, &ExperimentController::pause);
    connect(m_resumeButton, &QPushButton::clicked, m_controller, &ExperimentController::resume);
    connect(m_stepButton, &QPushButton::clicked, m_controller, &ExperimentController::step);
    connect(m_resetButton, &QPushButton::clicked, m_controller, &ExperimentController::reset);

    layout->addWidget(m_startButton);
    layout->addWidget(m_pauseButton);
    layout->addWidget(m_resumeButton);
    layout->addWidget(m_stepButton);
    layout->addWidget(m_resetButton);

    layout->addSpacing(18);
    auto* speedLabel = new QLabel(QStringLiteral("Скорость анимации"), bar);
    speedLabel->setObjectName(QStringLiteral("paramLabel"));
    m_speedCombo = new QComboBox(bar);
    const QVector<double> speeds = {0.1, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0};
    for (double s : speeds)
        m_speedCombo->addItem(QStringLiteral("%1×").arg(utils::formatNumber(s, 2)), s);
    m_speedCombo->setCurrentIndex(m_speedCombo->findData(1.0));
    connect(m_speedCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        m_controller->setSpeed(m_speedCombo->currentData().toDouble());
    });
    connect(m_controller, &ExperimentController::speedChanged, this, [this](double) {
        if (m_readoutPanel)
            m_readoutPanel->refreshValues();
    });
    layout->addWidget(speedLabel);
    layout->addWidget(m_speedCombo);

    layout->addStretch();
    m_stateLabel = new QLabel(QStringLiteral("Готово к запуску"), bar);
    m_stateLabel->setObjectName(QStringLiteral("pageSubtitle"));
    layout->addWidget(m_stateLabel);

    setStatusBar(statusBar);
    updateRunButtons(RunState::Stopped);
}

void LabWindow::updateRunButtons(RunState state) {
    const bool hasModel = m_controller->simulation() != nullptr;
    m_startButton->setEnabled(hasModel);
    m_resetButton->setEnabled(hasModel);
    m_stepButton->setEnabled(hasModel);
    m_pauseButton->setEnabled(state == RunState::Running);
    m_resumeButton->setEnabled(state == RunState::Paused);

    switch (state) {
        case RunState::Stopped:
            m_stateLabel->setText(QStringLiteral("Готово к запуску"));
            break;
        case RunState::Running:
            m_stateLabel->setText(QStringLiteral("Идёт эксперимент"));
            break;
        case RunState::Paused:
            m_stateLabel->setText(QStringLiteral("Пауза — можно продолжить или перематывать"));
            break;
        case RunState::Finished:
            m_stateLabel->setText(QStringLiteral("Эксперимент завершён"));
            break;
    }
}

LabWindow::Page& LabWindow::pageFor(const QString& modelId) {
    const auto existing = m_pages.find(modelId);
    if (existing != m_pages.end())
        return existing.value();

    const QVector<ModelDescriptor> descriptors = allDescriptors();
    const auto descriptor = std::find_if(descriptors.begin(), descriptors.end(),
                                         [&](const ModelDescriptor& d) { return d.id == modelId; });
    if (descriptor == descriptors.end())
        return m_pages[QStringLiteral("motion")];

    LabSetup setup = descriptor->create();
    setup.scene->setParent(this);

    auto* view = new LabView(setup.scene, m_sceneHost);
    view->setObjectName(QStringLiteral("labView"));

    Page page;
    page.model = setup.model;
    page.scene = setup.scene;
    page.view = view;

    connect(setup.model, &Simulation::timeChanged, this, [this]() {
        if (m_readoutPanel)
            m_readoutPanel->refreshValues();
    });
    connect(setup.model, &Simulation::runStateChanged, this, [this](RunState state) {
        updateRunButtons(state);
    });

    auto stored = m_pages.insert(modelId, page);
    return stored.value();
}

void LabWindow::openModel(const QString& modelId) {
    Page& page = pageFor(modelId);

    // В области сцены одновременно находится только активное представление:
    // остальные скрываем (они остаются в макете, чтобы не пересоздаваться).
    for (auto it = m_pages.begin(); it != m_pages.end(); ++it)
        it.value().view->setVisible(it.value().view == page.view);
    m_activeView = page.view;

    auto* hostLayout = m_sceneHost->layout();
    if (hostLayout->indexOf(page.view) < 0)
        hostLayout->addWidget(page.view);
    page.view->show();

    m_controller->setSimulation(page.model);
    m_controller->reset();
    applyVectorOptions();
    m_paramPanel->setModel(page.model);
    m_readoutPanel->setModel(page.model);
    m_derivationPanel->setModel(page.model);

    m_titleLabel->setText(page.model->title());
    m_subtitleLabel->setText(page.model->subtitle());
    m_stateLabel->setText(QStringLiteral("Готово к запуску"));
    updateRunButtons(RunState::Stopped);

    m_stack->setCurrentWidget(m_workspacePage);
    page.scene->refresh();
    m_activeView->viewport()->update();
}

Simulation* LabWindow::activeModel() const {
    return m_controller->simulation();
}

void LabWindow::showCatalog() {
    m_controller->reset();
    m_titleLabel->setText(QStringLiteral("Интерактивная физическая лаборатория"));
    m_subtitleLabel->setText(QStringLiteral("Задача №10 ЕГЭ по профильной математике"));
    m_stack->setCurrentWidget(m_catalogPage);
}

void LabWindow::toggleFullScreen() {
    m_fullScreenOn = !m_fullScreenOn;
    if (m_fullScreenOn) {
        showFullScreen();
        m_fullScreenButton->setText(QStringLiteral("В окне"));
    } else {
        showNormal();
        resize(1440, 860);
        m_fullScreenButton->setText(QStringLiteral("Во весь экран"));
    }
    QSettings settings(QStringLiteral("PhysLabEge"), QStringLiteral("PhysLab"));
    settings.setValue(QStringLiteral("fullscreen"), m_fullScreenOn);
}

void LabWindow::toggleDerivation() {
    const bool show = m_showDerivationAction->isChecked();
    // Вычисления можно скрыть — сцена и интерактивность продолжают работать.
    m_derivationContainer->setVisible(show);
    m_derivationPanel->setVisible(show);
}

void LabWindow::applyVectorOptions() {
    Simulation* model = m_controller->simulation();
    if (!model)
        return;
    model->setShowsVelocities(m_showVelocitiesAction && m_showVelocitiesAction->isChecked());
    model->setShowsAccelerations(m_showAccelerationsAction && m_showAccelerationsAction->isChecked());
    model->setShowsForces(m_showForcesAction && m_showForcesAction->isChecked());
    if (m_vectorScaleCombo)
        model->setVectorScale(m_vectorScaleCombo->currentData().toDouble());
}

void LabWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        if (m_stack->currentWidget() == m_workspacePage)
            showCatalog();
        return;
    }
    if (m_stack->currentWidget() == m_workspacePage && m_controller->simulation()) {
        switch (event->key()) {
            case Qt::Key_Space:
                if (m_controller->simulation()->runState() == RunState::Running)
                    m_controller->pause();
                else
                    m_controller->resume();
                return;
            case Qt::Key_S:
                m_controller->start();
                return;
            case Qt::Key_R:
                m_controller->reset();
                return;
            case Qt::Key_Right:
                m_controller->step();
                return;
            case Qt::Key_F:
                toggleFullScreen();
                return;
            case Qt::Key_D:
                m_showDerivationAction->toggle();
                return;
            default:
                break;
        }
    }
    QMainWindow::keyPressEvent(event);
}

}  // namespace lab::ui
