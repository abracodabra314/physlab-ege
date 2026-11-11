#include "ui/ReadoutPanel.h"

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>

#include "utils/MathText.h"

namespace lab::ui {
namespace {

constexpr int kTimeSliderSteps = 1000;

}  // namespace

ReadoutPanel::ReadoutPanel(QWidget* parent) : QScrollArea(parent) {
    setObjectName(QStringLiteral("readoutPanel"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void ReadoutPanel::setModel(Simulation* simulation) {
    if (m_model == simulation)
        return;
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    m_model = simulation;
    rebuild();
}

void ReadoutPanel::rebuild() {
    if (!m_content) {
        m_content = new QWidget(this);
        m_content->setObjectName(QStringLiteral("readoutContent"));
        m_layout = new QVBoxLayout(m_content);
        m_layout->setContentsMargins(14, 12, 14, 14);
        m_layout->setSpacing(6);
        setWidget(m_content);
    } else {
        QLayoutItem* item = nullptr;
        while ((item = m_layout->takeAt(0)) != nullptr) {
            if (QWidget* w = item->widget())
                w->deleteLater();
            delete item;
        }
    }

    m_rows.clear();
    m_timeLabel = nullptr;
    m_timeSlider = nullptr;
    if (!m_model)
        return;

    auto* caption = new QLabel(QStringLiteral("Текущие значения"), m_content);
    caption->setObjectName(QStringLiteral("paramGroupTitle"));
    m_layout->addWidget(caption);

    const QVector<Readout> readouts = m_model->readouts();
    m_rows.reserve(readouts.size());
    for (const Readout& readout : readouts) {
        auto* row = new QFrame(m_content);
        row->setObjectName(readout.highlight ? QStringLiteral("readoutRowHighlight")
                                             : QStringLiteral("readoutRow"));
        auto* grid = new QGridLayout(row);
        grid->setContentsMargins(10, 6, 10, 6);
        auto* label = new QLabel(readout.label, row);
        label->setObjectName(QStringLiteral("readoutLabel"));
        label->setWordWrap(true);
        auto* value = new QLabel(readout.value, row);
        value->setObjectName(readout.highlight ? QStringLiteral("readoutValueHighlight")
                                               : QStringLiteral("readoutValue"));
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(label, 0, 0);
        grid->addWidget(value, 0, 1);
        m_layout->addWidget(row);
        m_rows.push_back({label, value});
    }

    // ---- Строка модельного времени с перемоткой ----
    auto* timeFrame = new QFrame(m_content);
    timeFrame->setObjectName(QStringLiteral("readoutRow"));
    auto* timeGrid = new QGridLayout(timeFrame);
    timeGrid->setContentsMargins(10, 8, 10, 8);

    m_timeLabel = new QLabel(timeFrame);
    m_timeLabel->setObjectName(QStringLiteral("readoutValue"));
    m_timeSlider = new QSlider(Qt::Horizontal, timeFrame);
    m_timeSlider->setObjectName(QStringLiteral("timeSlider"));
    m_timeSlider->setRange(0, kTimeSliderSteps);

    timeGrid->addWidget(m_timeLabel, 0, 0, 1, 2);
    timeGrid->addWidget(m_timeSlider, 1, 0, 1, 2);
    m_layout->addWidget(timeFrame);

    connect(m_timeSlider, &QSlider::sliderPressed, this, [this]() { m_seeking = true; });
    connect(m_timeSlider, &QSlider::sliderReleased, this, [this]() { m_seeking = false; });
    connect(m_timeSlider, &QSlider::valueChanged, this, [this](int value) {
        if (!m_seeking || !m_model)
            return;
        emit timeSeekRequested(m_model->duration() * value / kTimeSliderSteps);
    });

    m_layout->addStretch();
    refreshValues();
}

void ReadoutPanel::refreshValues() {
    if (!m_model || m_rows.isEmpty())
        return;
    const QVector<Readout> readouts = m_model->readouts();
    for (int i = 0; i < m_rows.size() && i < readouts.size(); ++i) {
        m_rows[i].label->setText(readouts[i].label);
        m_rows[i].value->setText(readouts[i].value);
    }

    const double duration = std::max(1e-6, m_model->duration());
    const double t = m_model->time();
    const QString unit = m_model->timeUnit() == QLatin1String("h")    ? QStringLiteral("ч")
                         : m_model->timeUnit() == QLatin1String("min") ? QStringLiteral("мин")
                                                                      : QStringLiteral("с");
    if (m_timeLabel) {
        m_timeLabel->setText(QStringLiteral("t = %1 из %2 %3")
                                 .arg(utils::formatNumber(t, 2), utils::formatNumber(duration, 2), unit));
    }
    if (m_timeSlider && !m_seeking) {
        const int pos = int(t / duration * kTimeSliderSteps);
        m_timeSlider->setValue(qBound(0, pos, kTimeSliderSteps));
    }
}

}  // namespace lab::ui
