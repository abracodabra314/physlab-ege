#include "scenes/LabScene.h"

#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>

#include "scenes/DragHandleItem.h"
#include "scenes/Style.h"
#include "utils/MathText.h"

namespace lab {

LabScene::LabScene(Simulation* model, QObject* parent) : QGraphicsScene(parent), m_model(model) {
    setSceneRect(0, 0, 1280, 720);
    if (m_model) {
        connect(m_model, &Simulation::timeChanged, this, &LabScene::refresh);
        connect(m_model, &Simulation::parametersChanged, this, &LabScene::refresh);
        connect(m_model, &Simulation::runStateChanged, this, &LabScene::refresh);
    }
}

LabScene::~LabScene() = default;

void LabScene::setRanges(const QRectF& area, double horizontalMin, double horizontalMax,
                         double verticalMin, double verticalMax) {
    m_viewArea = area;
    m_worldMin = horizontalMin;
    m_worldMax = std::max(horizontalMin + 1e-9, horizontalMax);
    m_valueMin = verticalMin;
    m_valueMax = std::max(verticalMin + 1e-9, verticalMax);
    setSceneRect(area.adjusted(-30, -30, 30, 30));
}

double LabScene::pixelsPerUnit() const {
    if (m_viewArea.width() <= 0.0)
        return 1.0;
    return m_viewArea.width() / (m_worldMax - m_worldMin);
}

double LabScene::pixelsPerValueUnit() const {
    if (m_viewArea.height() <= 0.0)
        return 1.0;
    return m_viewArea.height() / (m_valueMax - m_valueMin);
}

double LabScene::toSceneX(double modelValue) const {
    if (m_viewArea.width() <= 0.0)
        return m_viewArea.center().x();
    const double fraction = (modelValue - m_worldMin) / (m_worldMax - m_worldMin);
    return m_viewArea.left() + fraction * m_viewArea.width();
}

double LabScene::toModelValue(qreal sceneX) const {
    if (m_viewArea.width() <= 0.0)
        return m_worldMin;
    const double fraction = (sceneX - m_viewArea.left()) / m_viewArea.width();
    return m_worldMin + fraction * (m_worldMax - m_worldMin);
}

double LabScene::toSceneY(double modelValue) const {
    if (m_viewArea.height() <= 0.0)
        return m_viewArea.center().y();
    // Значения модели растут вверх, экранная ось Y — вниз.
    const double fraction = (modelValue - m_valueMin) / (m_valueMax - m_valueMin);
    return m_viewArea.bottom() - fraction * m_viewArea.height();
}

double LabScene::toModelValueY(qreal sceneY) const {
    if (m_viewArea.height() <= 0.0)
        return m_valueMin;
    const double fraction = (m_viewArea.bottom() - sceneY) / m_viewArea.height();
    return m_valueMin + fraction * (m_valueMax - m_valueMin);
}

void LabScene::refresh() {
    rebuildHandles();
    positionHandles();
    update(sceneRect());
}

void LabScene::rebuildHandles() {
    if (!m_model)
        return;
    const QVector<HandleInfo> infos = m_model->handles();

    // Синхронизируем существующие ручки с описанием модели, лишние удаляем.
    QHash<QString, DragHandleItem*> byId;
    for (DragHandleItem* handle : m_handles)
        byId.insert(handle->id(), handle);

    QVector<DragHandleItem*> wanted;
    for (const HandleInfo& info : infos) {
        DragHandleItem* handle = byId.take(info.id);
        if (!handle) {
            handle = new DragHandleItem(info);
            addItem(handle);
            connect(handle, &DragHandleItem::valueDragged, this, [this](const QString& id, double value) {
                if (!m_model)
                    return;
                m_model->setHandleValue(id, value);
                emit handleMoved(id);
            });
            connect(handle, &DragHandleItem::dragStarted, this, [this](const QString&) {
                if (m_model)
                    m_model->beginInteraction();
            });
            connect(handle, &DragHandleItem::dragFinished, this, [this](const QString&) {
                if (m_model)
                    m_model->endInteraction();
            });
        } else {
            handle->setInfo(info);
        }
        const bool vertical = info.axis == QLatin1String("y");
        handle->setAxis(vertical ? DragHandleItem::Axis::Vertical : DragHandleItem::Axis::Horizontal);
        const HandleInfo captured = info;
        handle->setValueMapper([this, captured](const QPointF& pos) {
            return mapHandlePosition(captured, pos);
        });
        wanted.push_back(handle);
    }
    for (DragHandleItem* orphan : byId) {
        removeItem(orphan);
        delete orphan;
    }
    m_handles = wanted;
}

void LabScene::positionHandles() {
    // Реализация по умолчанию: координата ручки берётся из текущего значения
    // параметра модели по соответствующей шкале; сцены с собственной
    // геометрией переопределяют метод.
    for (DragHandleItem* handle : m_handles) {
        if (!handle || !m_model)
            continue;
        HandleInfo info = handle->info();
        info.value = m_model->param(handle->id());
        const double along = handleScenePosition(info);
        if (info.axis == QLatin1String("y"))
            handle->setAnchor(QPointF(m_viewArea.center().x(), along));
        else
            handle->setAnchor(QPointF(along, m_viewArea.center().y()));
    }
}

double LabScene::mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const {
    return info.axis == QLatin1String("y") ? toModelValueY(scenePos.y()) : toModelValue(scenePos.x());
}

double LabScene::handleScenePosition(const HandleInfo& info) const {
    return info.axis == QLatin1String("y") ? toSceneY(info.value) : toSceneX(info.value);
}

void LabScene::drawBackground(QPainter* painter, const QRectF& rect) {
    painter->fillRect(rect, style::background());
    // Мелкая учебная сетка.
    painter->setPen(QPen(style::grid(), 0.6));
    constexpr double kGrid = 40.0;
    const double left = std::floor(rect.left() / kGrid) * kGrid;
    const double top = std::floor(rect.top() / kGrid) * kGrid;
    for (double x = left; x <= rect.right(); x += kGrid)
        painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
    for (double y = top; y <= rect.bottom(); y += kGrid)
        painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));

    painter->setRenderHint(QPainter::Antialiasing, true);
    paintScheme(painter);
}

void LabScene::drawForeground(QPainter* painter, const QRectF& rect) {
    QGraphicsScene::drawForeground(painter, rect);
    if (!m_model)
        return;
    const ValidationResult check = m_model->validate();
    if (check.ok)
        return;
    // Предупреждение о недопустимых параметрах рисуем прямо на сцене.
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setFont(style::uiFont(11.5, true));
    const QRectF box(QRectF(sceneRect()).center() + QPointF(-280, -40), QSizeF(560, 80));
    painter->setPen(QPen(style::danger(), 1.6));
    painter->setBrush(QColor(255, 246, 245, 242));
    painter->drawRoundedRect(box, 10, 10);
    painter->setPen(style::ink());
    const QRectF text = box.adjusted(16, 10, -16, -10);
    painter->drawText(text, Qt::TextWordWrap | Qt::AlignHCenter, check.message);
}

void LabScene::drawTag(QPainter* painter, const QPointF& center, const QString& text,
                       const QColor& background, const QColor& textColor) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setFont(style::uiFont(9.5, true));
    const QFontMetricsF metrics(painter->font());
    const QRectF textRect = metrics.boundingRect(text);
    QRectF box(center.x() - textRect.width() / 2.0, center.y() - textRect.height() / 2.0,
               textRect.width(), textRect.height());
    box.adjust(-8, -4, 8, 4);
    painter->setPen(QPen(background.darker(150), 1.0));
    painter->setBrush(background);
    painter->drawRoundedRect(box, 5, 5);
    painter->setPen(textColor);
    painter->drawText(box, Qt::AlignCenter, text);
    painter->restore();
}

void LabScene::drawDimension(QPainter* painter, const QPointF& from, const QPointF& to,
                             const QString& text, const QColor& color, double offset) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.3);
    painter->setPen(pen);

    QPointF dir = to - from;
    const double length = std::hypot(dir.x(), dir.y());
    if (length < 1e-6) {
        painter->restore();
        return;
    }
    dir /= length;
    const QPointF normal(-dir.y(), dir.x());
    const QPointF a = from + normal * offset;
    const QPointF b = to + normal * offset;

    painter->drawLine(a, b);
    // Засечки на концах.
    painter->drawLine(a - normal * 5.0, a + normal * 5.0);
    painter->drawLine(b - normal * 5.0, b + normal * 5.0);
    // Стрелки.
    const QPointF arrowA = dir * 7.0;
    painter->drawLine(a, a + arrowA + QPointF(-normal.y(), normal.x()) * 3.4);
    painter->drawLine(a, a + arrowA + QPointF(normal.y(), -normal.x()) * 3.4);
    painter->drawLine(b, b - arrowA + QPointF(-normal.y(), normal.x()) * 3.4);
    painter->drawLine(b, b - arrowA + QPointF(normal.y(), -normal.x()) * 3.4);

    drawTag(painter, (a + b) / 2.0 + normal * 12.0, text, QColor(255, 255, 255, 235), color);
    painter->restore();
}

void LabScene::drawPlot(QPainter* painter, const QRectF& rect, const QString& title,
                        const QString& timeUnit, const QVector<PlotSeries>& series,
                        double tMax, double currentT, double eventT) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Рамка и заголовок.
    painter->setPen(QPen(style::inkSoft(), 1.0));
    painter->setBrush(QColor(255, 255, 255, 235));
    painter->drawRoundedRect(rect, 8, 8);
    painter->setFont(style::uiFont(9.0, true));
    painter->setPen(style::ink());
    painter->drawText(rect.adjusted(10, 6, -10, -6), Qt::AlignLeft | Qt::AlignTop, title);

    const QRectF plot = rect.adjusted(40, 26, -12, -20);
    if (plot.width() < 20.0 || plot.height() < 20.0 || tMax <= 0.0) {
        painter->restore();
        return;
    }

    // Оценки осей Y по всем точкам.
    double yMin = 0.0, yMax = 0.0;
    bool first = true;
    for (const PlotSeries& s : series) {
        for (const QPointF& p : s.points) {
            if (first) {
                yMin = yMax = p.y();
                first = false;
            } else {
                yMin = std::min(yMin, p.y());
                yMax = std::max(yMax, p.y());
            }
        }
    }
    if (first || yMax - yMin < 1e-9) {
        yMin -= 1.0;
        yMax += 1.0;
    }
    const double yPadding = (yMax - yMin) * 0.08;
    yMin -= yPadding;
    yMax += yPadding;

    auto mapX = [&](double t) { return plot.left() + t / tMax * plot.width(); };
    auto mapY = [&](double y) { return plot.bottom() - (y - yMin) / (yMax - yMin) * plot.height(); };

    // Сетка и подписи осей.
    painter->setFont(style::uiFont(7.5));
    painter->setPen(QPen(style::grid(), 0.7));
    for (int i = 0; i <= 4; ++i) {
        const double t = tMax * i / 4.0;
        const double x = mapX(t);
        painter->setPen(QPen(style::grid(), 0.7));
        painter->drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        painter->setPen(style::inkSoft());
        painter->drawText(QRectF(x - 24, plot.bottom() + 2, 48, 14), Qt::AlignHCenter,
                          utils::formatNumber(t, tMax < 8 ? 1 : 0));
    }
    for (int i = 0; i <= 3; ++i) {
        const double y = yMin + (yMax - yMin) * i / 3.0;
        const double yy = mapY(y);
        painter->setPen(QPen(style::grid(), 0.7));
        painter->drawLine(QPointF(plot.left(), yy), QPointF(plot.right(), yy));
        painter->setPen(style::inkSoft());
        painter->drawText(QRectF(rect.left() + 2, yy - 7, 36, 14), Qt::AlignRight,
                          utils::formatNumber(y, std::abs(yMax) < 10 ? 1 : 0));
    }
    // Подпись оси времени.
    painter->setPen(style::inkSoft());
    painter->drawText(QRectF(plot.right() - 46, plot.bottom() + 12, 50, 14), Qt::AlignRight,
                      QStringLiteral("t, %1").arg(timeUnit));

    // Кривые.
    for (const PlotSeries& s : series) {
        if (s.points.size() < 2)
            continue;
        QPen pen(s.color, s.width);
        if (s.dashed)
            pen.setStyle(Qt::DashLine);
        painter->setPen(pen);
        QPainterPath path;
        path.moveTo(mapX(s.points.first().x()), mapY(s.points.first().y()));
        for (const QPointF& p : s.points)
            path.lineTo(mapX(std::clamp(p.x(), 0.0, tMax)), mapY(p.y()));
        painter->drawPath(path);
    }

    // Точка события.
    if (std::isfinite(eventT) && eventT >= 0.0 && eventT <= tMax) {
        const double x = mapX(eventT);
        painter->setPen(QPen(style::success(), 1.2, Qt::DashLine));
        painter->drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }

    // Текущий момент.
    if (currentT >= 0.0 && currentT <= tMax) {
        const double x = mapX(currentT);
        painter->setPen(QPen(style::danger(), 1.4));
        painter->drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }
    painter->restore();
}

}  // namespace lab
