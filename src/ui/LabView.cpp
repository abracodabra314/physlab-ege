#include "ui/LabView.h"

#include <QPainter>
#include <QResizeEvent>

#include <algorithm>

namespace lab::ui {

LabView::LabView(LabScene* scene, QWidget* parent) : QGraphicsView(scene, parent) {
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::TextAntialiasing, true);
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    // Кисть фона у представления намеренно не задаётся: при непустом
    // backgroundBrush QGraphicsView::drawBackground() рисует только её и
    // не вызывает QGraphicsScene::drawBackground(), а именно там схема
    // лаборатории рисует саму себя (фон, сетку, объекты и ось времени).
    setBackgroundRole(QPalette::NoRole);
    setMouseTracking(true);
}

void LabView::drawBackground(QPainter* painter, const QRectF& rect) {
    // Явно отдаём фон сцене: LabScene::drawBackground() заполняет область
    // цветом бумаги, рисует учебную сетку, схему (paintScheme) и ось времени.
    auto* lab = qobject_cast<LabScene*>(scene());
    if (!lab) {
        QGraphicsView::drawBackground(painter, rect);
        return;
    }
    lab->paintBackgroundInto(painter, rect);
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
    // Снизу резервируется полоса под общую ось времени сцены (LabScene::drawTimeAxis).
    constexpr double kTimeAxisReserve = 58.0;
    const QRectF area(20.0, 12.0, std::max(320.0, viewport()->width() - 40.0),
                      std::max(220.0, viewport()->height() - 24.0 - kTimeAxisReserve));
    lab->requestViewArea(area);
    lab->reconfigure(area);
}

}  // namespace lab::ui
