#pragma once

#include <QGraphicsView>

#include "scenes/LabScene.h"

namespace lab::ui {

/**
 * QGraphicsView, отображающий сцену лаборатории.
 *
 * Сцена живёт в «пиксельных» координатах (1:1), поэтому view не масштабирует
 * содержимое, а при изменении размера сообщает сцене новую доступную область
 * (LabScene::reconfigure) — так схема адаптируется к любому разрешению.
 */
class LabView : public QGraphicsView {
    Q_OBJECT
public:
    explicit LabView(LabScene* scene, QWidget* parent = nullptr);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;

private:
    void notifySceneGeometry();
};

}  // namespace lab::ui
