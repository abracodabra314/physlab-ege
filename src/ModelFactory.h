#pragma once

#include <QVector>

#include "core/Simulation.h"
#include "scenes/LabScene.h"

namespace lab {

/**
 * Создаёт экземпляр модели и соответствующую ей визуальную сцену.
 * Связывает модель и сцену (сцена подписана на сигналы модели),
 * поэтому математика и отрисовка всегда синхронны.
 *
 * Новый набор моделей добавляется одной строкой в allDescriptors().
 */
struct LabSetup {
    Simulation* model = nullptr;
    LabScene* scene = nullptr;
};

struct ModelDescriptor {
    QString id;
    QString title;
    QString subtitle;
    /** Создаёт пару «модель + сцена»; владение передаётся вызывающему. */
    LabSetup (*create)();
};

/** Все доступные модели лаборатории в порядке их демонстрации. */
QVector<ModelDescriptor> allDescriptors();

}  // namespace lab
