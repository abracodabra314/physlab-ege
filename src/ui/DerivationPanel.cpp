#include "ui/DerivationPanel.h"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

#include "utils/MathText.h"

namespace lab::ui {
namespace {

/** Заголовок раздела решения. */
QLabel* makeSectionLabel(QWidget* parent, const QString& text) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("derivationSection"));
    return label;
}

/** Строка формулы в rich-разметке. */
QLabel* makeFormulaLabel(QWidget* parent, const QString& mathSource) {
    auto* label = new QLabel(parent);
    label->setObjectName(QStringLiteral("derivationFormula"));
    label->setTextFormat(Qt::RichText);
    label->setText(utils::toRichMathLine(mathSource));
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

/** Текстовая строка (условие, пояснение) в rich-разметке. */
QLabel* makeTextLabel(QWidget* parent, const QString& text) {
    auto* label = new QLabel(parent);
    label->setObjectName(QStringLiteral("derivationText"));
    label->setTextFormat(Qt::RichText);
    label->setText(text);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

}  // namespace

DerivationPanel::DerivationPanel(QWidget* parent) : QScrollArea(parent) {
    setObjectName(QStringLiteral("derivationPanel"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void DerivationPanel::setModel(Simulation* simulation) {
    if (m_model == simulation)
        return;
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    m_model = simulation;
    if (m_model) {
        // Вычисления перестраиваются при любом изменении исходных данных.
        connect(m_model, &Simulation::derivationChanged, this, &DerivationPanel::rebuild);
        connect(m_model, &Simulation::parametersChanged, this, &DerivationPanel::rebuild);
    }
    rebuild();
}

void DerivationPanel::rebuild() {
    if (!m_content) {
        m_content = new QWidget(this);
        m_content->setObjectName(QStringLiteral("derivationContent"));
        m_layout = new QVBoxLayout(m_content);
        m_layout->setContentsMargins(14, 12, 14, 16);
        m_layout->setSpacing(7);
        m_layout->addStretch();
        setWidget(m_content);
    } else {
        QLayoutItem* item = nullptr;
        while ((item = m_layout->takeAt(0)) != nullptr) {
            if (QWidget* w = item->widget())
                w->deleteLater();
            delete item;
        }
    }

    if (!m_model) {
        m_layout->addStretch();
        return;
    }

    const Derivation derivation = m_model->derivation();

    // ---- Заголовок ----
    m_layout->addWidget(makeSectionLabel(m_content, QStringLiteral("Решение задачи")));

    // ---- Условие ----
    if (!derivation.problemText.isEmpty()) {
        auto* box = new QFrame(m_content);
        box->setObjectName(QStringLiteral("derivationProblem"));
        auto* boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(10, 8, 10, 8);
        boxLayout->setSpacing(4);
        boxLayout->addWidget(makeTextLabel(box, QStringLiteral("<b>Условие.</b> %1").arg(derivation.problemText)));
        if (!derivation.unknown.isEmpty())
            boxLayout->addWidget(makeTextLabel(box, QStringLiteral("<b>Найти:</b> %1").arg(derivation.unknown)));
        m_layout->addWidget(box);
    }

    if (!derivation.valid) {
        auto* warning = new QFrame(m_content);
        warning->setObjectName(QStringLiteral("derivationWarning"));
        auto* warningLayout = new QVBoxLayout(warning);
        warningLayout->setContentsMargins(10, 8, 10, 8);
        warningLayout->addWidget(makeTextLabel(warning,
                                               QStringLiteral("Параметры недопустимы — измените исходные данные, "
                                                              "чтобы получить решение.")));
        m_layout->addWidget(warning);
        m_layout->addStretch();
        return;
    }

    // ---- Шаги решения ----
    for (const DerivationStep& step : derivation.steps) {
        m_layout->addWidget(makeSectionLabel(m_content, step.kind));
        auto* box = new QFrame(m_content);
        box->setObjectName(QStringLiteral("derivationStep"));
        auto* boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(8, 6, 8, 6);
        boxLayout->setSpacing(2);
        // Шаг может содержать текст или формулу — оба варианта рендерим
        // через математическую разметку.
        boxLayout->addWidget(makeFormulaLabel(box, step.content));
        m_layout->addWidget(box);
    }

    // ---- Ответ ----
    if (!derivation.answerTemplate.isEmpty()) {
        auto* answer = new QFrame(m_content);
        answer->setObjectName(QStringLiteral("derivationAnswer"));
        auto* answerLayout = new QVBoxLayout(answer);
        answerLayout->setContentsMargins(10, 8, 10, 8);
        answerLayout->addWidget(makeTextLabel(answer,
                                              QStringLiteral("<b>Ответ.</b> %1").arg(derivation.answerTemplate)));
        m_layout->addWidget(answer);
    }

    m_layout->addStretch();
}

}  // namespace lab::ui
