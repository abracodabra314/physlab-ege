#include "ui/ParamPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

#include "utils/MathText.h"

namespace lab::ui {
namespace {

// Делений у ползунка достаточно много для плавной настройки, но не чрезмерно.
constexpr int kSliderSteps = 1000;

int decimalsFor(const ParamSpec& spec) {
    return qBound(0, static_cast<int>(spec.decimals), 6);
}

}  // namespace

ParamPanel::ParamPanel(QWidget* parent) : QScrollArea(parent) {
    setObjectName(QStringLiteral("paramPanel"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void ParamPanel::setModel(Simulation* simulation) {
    if (m_model == simulation)
        return;
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    m_model = simulation;
    m_rows.clear();
    rebuild();
    if (m_model) {
        // Внешние изменения параметров (перетаскивание мышью) должны отражаться на полях.
        connect(m_model, &Simulation::parametersChanged, this, [this]() {
            if (!m_syncing)
                syncValues();
        });
    }
}

ParamPanel::Row* ParamPanel::findRow(const QString& id) {
    for (Row& row : m_rows) {
        if (row.spec.id == id)
            return &row;
    }
    return nullptr;
}

void ParamPanel::addGroupTitle(QVBoxLayout* layout, const QString& title) {
    auto* label = new QLabel(title, m_content);
    label->setObjectName(QStringLiteral("paramGroupTitle"));
    layout->addWidget(label);
}

void ParamPanel::rebuild() {
    if (!m_content) {
        m_content = new QWidget(this);
        m_content->setObjectName(QStringLiteral("paramContent"));
        m_contentLayout = new QVBoxLayout(m_content);
        m_contentLayout->setContentsMargins(14, 12, 14, 18);
        m_contentLayout->setSpacing(8);
        m_contentLayout->addStretch();
        setWidget(m_content);
    } else {
        // Удаляем прежние строки, оставляя растягиваемую подпорку последней.
        QLayoutItem* item = nullptr;
        while ((item = m_contentLayout->takeAt(0)) != nullptr) {
            if (QWidget* w = item->widget())
                w->deleteLater();
            delete item;
        }
    }

    m_rows.clear();
    if (!m_model) {
        m_contentLayout->addStretch();
        return;
    }

    const QVector<ParamSpec> specs = m_model->paramSpecs();
    m_rows.reserve(specs.size());
    QString currentGroup;

    for (const ParamSpec& spec : specs) {
        if (spec.group != currentGroup) {
            currentGroup = spec.group;
            if (!currentGroup.isEmpty())
                addGroupTitle(m_contentLayout, currentGroup);
        }

        m_rows.push_back(Row{});
        Row& row = m_rows.last();
        row.spec = spec;

        auto* box = new QFrame(m_content);
        box->setObjectName(QStringLiteral("paramRow"));
        auto* grid = new QGridLayout(box);
        grid->setContentsMargins(10, 8, 10, 8);
        grid->setHorizontalSpacing(8);
        grid->setVerticalSpacing(4);

        auto* title = new QLabel(spec.label, box);
        title->setObjectName(QStringLiteral("paramLabel"));
        if (!spec.helpText.isEmpty())
            title->setToolTip(spec.helpText);
        grid->addWidget(title, 0, 0, 1, 2);

        const int index = m_rows.size() - 1;

        switch (spec.kind) {
            case ParamKind::Slider: {
                auto* slider = new QSlider(Qt::Horizontal, box);
                slider->setRange(0, kSliderSteps);
                slider->setObjectName(QStringLiteral("paramSlider"));
                auto* spin = new QDoubleSpinBox(box);
                spin->setDecimals(decimalsFor(spec));
                spin->setRange(spec.minValue, spec.maxValue);
                spin->setSingleStep(spec.stepValue);
                if (spec.stepValue > 0.0)
                    spin->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
                spin->setButtonSymbols(QAbstractSpinBox::PlusMinus);
                spin->setObjectName(QStringLiteral("paramSpin"));
                grid->addWidget(slider, 1, 0);
                grid->addWidget(spin, 1, 1);
                row.slider = slider;
                row.spin = spin;

                connect(slider, &QSlider::valueChanged, this, [this, index](int value) {
                    Row& r = m_rows[index];
                    if (r.updating)
                        return;
                    const double span = r.spec.maxValue - r.spec.minValue;
                    applyRowValue(r, r.spec.minValue + span * value / kSliderSteps, true);
                });
                connect(spin, &QDoubleSpinBox::valueChanged, this, [this, index](double value) {
                    Row& r = m_rows[index];
                    if (r.updating)
                        return;
                    applyRowValue(r, value, true);
                });
                break;
            }
            case ParamKind::Number: {
                auto* spin = new QDoubleSpinBox(box);
                spin->setDecimals(decimalsFor(spec));
                spin->setRange(spec.minValue, spec.maxValue);
                spin->setSingleStep(spec.stepValue);
                if (spec.stepValue > 0.0)
                    spin->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
                spin->setObjectName(QStringLiteral("paramSpin"));
                auto* unit = new QLabel(spec.unit, box);
                unit->setObjectName(QStringLiteral("paramUnit"));
                grid->addWidget(spin, 1, 0);
                grid->addWidget(unit, 1, 1);
                row.spin = spin;
                row.unitLabel = unit;

                connect(spin, &QDoubleSpinBox::valueChanged, this, [this, index](double value) {
                    Row& r = m_rows[index];
                    if (r.updating)
                        return;
                    applyRowValue(r, value, true);
                });
                break;
            }
            case ParamKind::Choice: {
                auto* combo = new QComboBox(box);
                combo->addItems(spec.choices);
                combo->setObjectName(QStringLiteral("paramCombo"));
                grid->addWidget(combo, 1, 0, 1, 2);
                row.combo = combo;

                connect(combo, &QComboBox::currentIndexChanged, this, [this, index](int value) {
                    Row& r = m_rows[index];
                    if (r.updating || !m_model)
                        return;
                    m_syncing = true;
                    m_model->setParamRaw(r.spec.id, value);
                    m_syncing = false;
                    emit parametersEdited();
                });
                break;
            }
            case ParamKind::Toggle: {
                auto* check = new QCheckBox(spec.unit.isEmpty() ? QStringLiteral("Включено") : spec.unit, box);
                check->setObjectName(QStringLiteral("paramCheck"));
                grid->addWidget(check, 1, 0, 1, 2);
                row.check = check;

                connect(check, &QCheckBox::toggled, this, [this, index](bool checked) {
                    Row& r = m_rows[index];
                    if (r.updating || !m_model)
                        return;
                    m_syncing = true;
                    m_model->setParamRaw(r.spec.id, checked ? 1.0 : 0.0);
                    m_syncing = false;
                    emit parametersEdited();
                });
                break;
            }
        }

        if (!spec.unit.isEmpty() && spec.kind != ParamKind::Number && spec.kind != ParamKind::Toggle) {
            auto* unit = new QLabel(spec.unit, box);
            unit->setObjectName(QStringLiteral("paramUnit"));
            grid->addWidget(unit, 2, 1, Qt::AlignRight);
        }

        m_contentLayout->insertWidget(m_contentLayout->count() - 1, box);
    }

    syncValues();
}

void ParamPanel::applyRowValue(Row& row, double value, bool fromUi) {
    if (!m_model)
        return;
    row.updating = true;
    m_syncing = true;
    m_model->setParam(row.spec.id, value);
    m_syncing = false;
    row.updating = false;
    if (fromUi)
        emit parametersEdited();
}

void ParamPanel::syncValues() {
    if (!m_model)
        return;
    for (Row& row : m_rows) {
        const double value = m_model->param(row.spec.id);
        row.updating = true;
        switch (row.spec.kind) {
            case ParamKind::Slider: {
                const double span = row.spec.maxValue - row.spec.minValue;
                const int pos = span > 0.0 ? static_cast<int>(
                                    std::lround((value - row.spec.minValue) / span * kSliderSteps))
                                           : 0;
                if (row.slider)
                    row.slider->setValue(qBound(0, pos, kSliderSteps));
                if (row.spin)
                    row.spin->setValue(value);
                break;
            }
            case ParamKind::Number:
                if (row.spin)
                    row.spin->setValue(value);
                break;
            case ParamKind::Choice:
                if (row.combo)
                    row.combo->setCurrentIndex(qBound(0, static_cast<int>(value), row.combo->count() - 1));
                break;
            case ParamKind::Toggle:
                if (row.check)
                    row.check->setChecked(value > 0.5);
                break;
        }
        row.updating = false;
    }
}

}  // namespace lab::ui
