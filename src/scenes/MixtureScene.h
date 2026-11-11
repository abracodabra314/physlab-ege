#pragma once

#include "scenes/LabScene.h"

namespace lab::scenes {

/**
 * Сцена модели «Смешивание растворов»: две исходные ёмкости слева и справа,
 * смеситель по центру ниже, соединительные трубы с анимированными потоками,
 * шкалы объёма, шкала концентрации и полосы массы вещества m = V·C.
 * Уровень жидкости в каждой ёмкости соответствует её объёму в едином
 * масштабе литров на пиксель.
 */
class MixtureScene : public LabScene {
    Q_OBJECT
public:
    explicit MixtureScene(Simulation* model, QObject* parent = nullptr);

    void reconfigure(const QRectF& area) override;
    void positionHandles() override;
    double mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const override;
    double handleScenePosition(const HandleInfo& info) const override;

protected:
    void paintScheme(QPainter* painter) override;

private:
    /** Цвет жидкости по концентрации (от почти белого к насыщенному). */
    static QColor liquidColor(double concentrationPercent);
    QRectF vesselRect(int index) const;   // 0, 1 — растворы, 2 — смеситель
    /** Линия, на которой стоят исходные ёмкости. */
    double sourceBaseline() const;
    /** Максимальная высота столба жидкости в масштабе сцены. */
    double columnHeight() const;
    /** Литров на один пиксель: единый масштаб для всех трёх ёмкостей. */
    double litersPerPixel() const;
};

}  // namespace lab::scenes
