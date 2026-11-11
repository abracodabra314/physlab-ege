#include "ui/LabView.h"

#include <QPainter>
#include <QResizeEvent>

#include <algorithm>

#include "scenes/Style.h"

namespace lab::ui {

LabView::LabView(LabScene* scene, QWidget* parent) : QGraphicsView(scene, parent) {
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::TextAntialiasing, true);
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setBackgroundBrush(lab::style::background());
    setMouseTracking(true);
}

void LabView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    // Сцена пересчитывает геометрию под новый размер окна:
    // полноэкранный режим и разные разрешения поддерживаются автоматически.
    notifySceneGeometry();
}

void LabView::notifySceneGeometry() {
    if (!scene())
        return;
    auto* lab = qobject_cast<LabScene*>(scene());
    if (!lab)
        return;
    const QRectF area(20.0, 12.0, std::max(320.0, viewport()->width() - 40.0),
                      std::max(240.0, viewport()->height() - 24.0));
    lab->reconfigure(area);
}

}  // namespace lab::ui
