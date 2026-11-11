#pragma once

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "core/Simulation.h"
#include "models/MotionModel.h"

namespace lab::test {

/** Базовая встречная задача: 150 км, скорости 60 и 90 км/ч, встреча через 1 ч. */
inline void fillMeetingFor(lab::models::MotionModel& model) {
    model.setParamRaw(QStringLiteral("x1"), 0.0);
    model.setParamRaw(QStringLiteral("x2"), 150.0);
    model.setParamRaw(QStringLiteral("v1"), 60.0);
    model.setParamRaw(QStringLiteral("v2"), 90.0);
    model.setParamRaw(QStringLiteral("dir1"), 0.0);
    model.setParamRaw(QStringLiteral("dir2"), 1.0);
    model.setParamRaw(QStringLiteral("delay1"), 0.0);
    model.setParamRaw(QStringLiteral("delay2"), 0.0);
}

/** Установить параметр модели и проверить, что значение принято (не скорректировано). */
inline void setParam(lab::Simulation& model, const char* id, double value) {
    model.setParam(QString::fromUtf8(id), value);
}

inline double param(const lab::Simulation& model, const char* id) {
    return model.param(QString::fromUtf8(id));
}

/** Сбрасывает все числовые параметры модели в значения «из учебной задачи». */
inline void resetToDefaults(lab::Simulation& model) {
    for (const lab::ParamSpec& spec : model.paramSpecs())
        model.setParamRaw(spec.id, spec.defaultValue);
}

/**
 * Задаёт набор параметров одной строкой вида «v1=60;dir1=0;x1=0».
 * Используется, чтобы тесты читались как условие задачи.
 */
inline void setSetup(lab::Simulation& model, const QString& assignments) {
    for (const QString& part : assignments.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QStringList kv = part.split(QLatin1Char('='));
        ASSERT_EQ(kv.size(), 2) << "Некорректная строка параметров: " << part.toStdString();
        model.setParamRaw(kv[0].trimmed(), kv[1].trimmed().toDouble());
    }
}

inline bool isNear(double actual, double expected, double tolerance = 1e-6) {
    return std::abs(actual - expected) <= tolerance;
}

inline bool isNotFinite(double value) {
    return !std::isfinite(value);
}

/** Возвращает true, если модель считает текущие параметры недопустимыми. */
inline bool isInvalid(const lab::Simulation& model) {
    return !model.validate().ok;
}

}  // namespace lab::test
