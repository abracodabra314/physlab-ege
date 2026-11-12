#include "scenes/LabScene.h"

#include <QGraphicsSceneMouseEvent>
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
        connect(m_model, &Simulation::vectorOptionsChanged, this, &LabScene::refresh);
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
    // Запас снизу — под общую ось времени.
    setSceneRect(area.adjusted(-30, -30, 30, 64));
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
    // Мир сцены перестраивается под новую область окна и под изменившиеся
    // параметры (например, переставленный автомобиль не должен «выехать»
    // за край дороги). m_requestedArea при этом не меняется — повторный
    // вызов reconfigure детерминирован и не создаёт цикла перерисовок.
    if (!m_requestedArea.isEmpty())
        reconfigure(m_requestedArea);
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
    drawTimeAxis(painter);
}

// ------------------------------------------------------------------
// Ось времени: полоса внизу сцены, по которой бежит точка состояния.
// Клик или перетаскивание по оси перемоткой меняет модельное время.
// ------------------------------------------------------------------

QRectF LabScene::timeAxisRect() const {
    // Ось времени лежит под расчётной областью сцены: сцены не должны
    // резервировать под неё место, им достаточно рисовать в m_viewArea.
    const double height = 30.0;
    return QRectF(m_viewArea.left() + 6.0, m_viewArea.bottom() + 12.0, m_viewArea.width() - 12.0, height);
}

QString LabScene::timeUnitLabel() const {
    const QString unit = m_model ? m_model->timeUnit() : QStringLiteral("s");
    if (unit == QLatin1String("h"))
        return QStringLiteral("ч");
    if (unit == QLatin1String("min"))
        return QStringLiteral("мин");
    return QStringLiteral("с");
}

double LabScene::timeAtScenePosition(const QPointF& pos) const {
    const QRectF axis = timeAxisRect();
    const double fraction = std::clamp((pos.x() - axis.left()) / std::max(1.0, axis.width()), 0.0, 1.0);
    return m_model ? m_model->duration() * fraction : 0.0;
}

void LabScene::drawTimeAxis(QPainter* painter) {
    if (!m_model)
        return;
    const QRectF axis = timeAxisRect();
    const double duration = std::max(1e-6, m_model->duration());
    const QString unit = m_model->timeUnit() == QLatin1String("h")
                             ? QStringLiteral("ч")
                             : m_model->timeUnit() == QLatin1String("min") ? QStringLiteral("мин")
                                                                           : QStringLiteral("с");

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(style::inkSoft(), 1.2));
    painter->setBrush(QColor(255, 255, 255, 225));
    painter->drawRoundedRect(axis, 8, 8);

    const double trackY = axis.center().y() + 3.0;
    const double trackLeft = axis.left() + 12.0;
    const double trackRight = axis.right() - 12.0;
    const double trackWidth = std::max(10.0, trackRight - trackLeft);

    // Шкала времени.
    painter->setPen(QPen(style::ink(), 1.6));
    painter->drawLine(QPointF(trackLeft, trackY), QPointF(trackRight, trackY));
    painter->setFont(style::uiFont(7.5));
    painter->setPen(style::inkSoft());
    for (int i = 0; i <= 4; ++i) {
        const double t = duration * i / 4.0;
        const double x = trackLeft + trackWidth * i / 4.0;
        painter->setPen(QPen(style::inkSoft(), 1.0));
        painter->drawLine(QPointF(x, trackY - 4.0), QPointF(x, trackY + 4.0));
        painter->setPen(style::inkSoft());
        painter->drawText(QRectF(x - 30, trackY + 5.0, 60, 12), Qt::AlignHCenter,
                          utils::formatNumber(t, duration < 8 ? 1 : 0));
    }

    // Момент ключевого события задачи.
    const double event = m_model->eventTime();
    if (std::isfinite(event) && event >= 0.0 && event <= duration) {
        const double x = trackLeft + trackWidth * (event / duration);
        painter->setPen(QPen(style::success(), 2.0, Qt::DashLine));
        painter->drawLine(QPointF(x, axis.top() + 3.0), QPointF(x, trackY + 6.0));
    }

    // Заголовок оси.
    painter->setFont(style::uiFont(8.0, true));
    painter->setPen(style::ink());
    painter->drawText(QRectF(axis.left() + 6, axis.top() - 2, axis.width() - 12, 12), Qt::AlignLeft,
                      QStringLiteral("Ось времени, %1").arg(unit));

    // Точка состояния: бежит по оси синхронно с модельным временем.
    const double dotX = trackLeft + trackWidth * std::clamp(m_model->time() / duration, 0.0, 1.0);
    painter->setPen(QPen(style::danger(), 1.4));
    painter->setBrush(style::danger());
    painter->drawEllipse(QPointF(dotX, trackY), 6.0, 6.0);
    // Внутренняя метка: paused / running.
    painter->setPen(QPen(Qt::white, 1.0));
    painter->setBrush(Qt::NoBrush);

    // Подсказка о текущем режиме справа.
    QString stateText;
    switch (m_model->runState()) {
        case RunState::Stopped:  stateText = QStringLiteral("t = 0"); break;
        case RunState::Running:  stateText = QStringLiteral("идёт"); break;
        case RunState::Paused:   stateText = QStringLiteral("пауза"); break;
        case RunState::Finished: stateText = QStringLiteral("готово"); break;
    }
    painter->setFont(style::uiFont(8.0, true));
    painter->setPen(style::danger().darker(110));
    painter->drawText(QRectF(axis.right() - 140, axis.top() - 2, 134, 12), Qt::AlignRight,
                      QStringLiteral("t = %1 · %2").arg(utils::formatNumber(m_model->time(), 2), stateText));
    painter->restore();
}

void LabScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    // Касание оси времени: останавливаем эксперимент и перематываем.
    if (m_model && event->button() == Qt::LeftButton && timeAxisRect().adjusted(-4, -10, 4, 10).contains(event->scenePos())) {
        m_scrubbingTime = true;
        m_model->seekTo(timeAtScenePosition(event->scenePos()));
        event->accept();
        return;
    }
    QGraphicsScene::mousePressEvent(event);
}

void LabScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (m_scrubbingTime && m_model) {
        m_model->seekTo(timeAtScenePosition(event->scenePos()));
        event->accept();
        return;
    }
    QGraphicsScene::mouseMoveEvent(event);
}

void LabScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (m_scrubbingTime) {
        m_scrubbingTime = false;
        event->accept();
        return;
    }
    QGraphicsScene::mouseReleaseEvent(event);
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

void LabScene::drawGraph(QPainter* painter, const QRectF& rect, const QString& title,
                         const QString& xAxisLabel, const QString& yAxisLabel,
                         const QVector<PlotSeries>& series, const QPointF& currentPoint,
                         bool showLegend) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    painter->setPen(QPen(style::inkSoft(), 1.0));
    painter->setBrush(QColor(255, 255, 255, 235));
    painter->drawRoundedRect(rect, 8, 8);
    painter->setFont(style::uiFont(9.0, true));
    painter->setPen(style::ink());
    painter->drawText(rect.adjusted(10, 6, -10, -6), Qt::AlignLeft | Qt::AlignTop, title);

    const QRectF plot = rect.adjusted(50, 26, -16, -30);
    if (plot.width() < 20.0 || plot.height() < 20.0) {
        painter->restore();
        return;
    }

    // Диапазон обеих осей — по всем конечным точкам (NaN пропускаются).
    double xMin = 0.0, xMax = 0.0, yMin = 0.0, yMax = 0.0;
    bool first = true;
    for (const PlotSeries& s : series) {
        for (const QPointF& p : s.points) {
            if (!std::isfinite(p.x()) || !std::isfinite(p.y()))
                continue;
            if (first) {
                xMin = xMax = p.x();
                yMin = yMax = p.y();
                first = false;
            } else {
                xMin = std::min(xMin, p.x());
                xMax = std::max(xMax, p.x());
                yMin = std::min(yMin, p.y());
                yMax = std::max(yMax, p.y());
            }
        }
    }
    if (first) {
        painter->restore();
        return;
    }
    if (xMax - xMin < 1e-12) {
        xMin -= 1.0;
        xMax += 1.0;
    }
    if (yMax - yMin < 1e-12) {
        yMin -= 1.0;
        yMax += 1.0;
    }
    const double xPad = (xMax - xMin) * 0.06;
    const double yPad = (yMax - yMin) * 0.08;
    xMin -= xPad;
    xMax += xPad;
    yMin -= yPad;
    yMax += yPad;

    auto mapX = [&](double x) { return plot.left() + (x - xMin) / (xMax - xMin) * plot.width(); };
    auto mapY = [&](double y) { return plot.bottom() - (y - yMin) / (yMax - yMin) * plot.height(); };

    // Сетка и подписи делений обеих осей.
    painter->setFont(style::uiFont(7.5));
    for (int i = 0; i <= 4; ++i) {
        const double x = xMin + (xMax - xMin) * i / 4.0;
        const double xx = mapX(x);
        painter->setPen(QPen(style::grid(), 0.7));
        painter->drawLine(QPointF(xx, plot.top()), QPointF(xx, plot.bottom()));
        painter->setPen(style::inkSoft());
        painter->drawText(QRectF(xx - 30, plot.bottom() + 2, 60, 13), Qt::AlignHCenter,
                          utils::formatNumber(x, std::abs(xMax) < 10 ? 1 : 0));
    }
    for (int i = 0; i <= 3; ++i) {
        const double y = yMin + (yMax - yMin) * i / 3.0;
        const double yy = mapY(y);
        painter->setPen(QPen(style::grid(), 0.7));
        painter->drawLine(QPointF(plot.left(), yy), QPointF(plot.right(), yy));
        painter->setPen(style::inkSoft());
        painter->drawText(QRectF(rect.left() + 2, yy - 7, 44, 14), Qt::AlignRight,
                          utils::formatNumber(y, std::abs(yMax) < 10 ? 1 : 0));
    }

    // Подписи осей.
    painter->setPen(style::inkSoft());
    painter->drawText(QRectF(plot.left(), plot.bottom() + 14, plot.width(), 14), Qt::AlignCenter,
                      xAxisLabel);
    painter->save();
    painter->translate(rect.left() + 13, plot.center().y());
    painter->rotate(-90.0);
    painter->drawText(QRectF(-70, -10, 140, 14), Qt::AlignCenter, yAxisLabel);
    painter->restore();

    // Кривые: пары NaN разрывают линию.
    for (const PlotSeries& s : series) {
        if (s.points.size() < 2)
            continue;
        QPen pen(s.color, s.width);
        if (s.dashed)
            pen.setStyle(Qt::DashLine);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        QPainterPath path;
        bool penDown = false;
        for (const QPointF& p : s.points) {
            if (!std::isfinite(p.x()) || !std::isfinite(p.y())) {
                penDown = false;
                continue;
            }
            const QPointF mapped(mapX(p.x()), mapY(p.y()));
            if (!penDown) {
                path.moveTo(mapped);
                penDown = true;
            } else {
                path.lineTo(mapped);
            }
        }
        painter->drawPath(path);
    }

    // Текущее состояние системы.
    if (std::isfinite(currentPoint.x()) && std::isfinite(currentPoint.y())) {
        painter->setPen(QPen(style::danger(), 1.4));
        painter->setBrush(style::paper());
        painter->drawEllipse(QPointF(mapX(currentPoint.x()), mapY(currentPoint.y())), 5.0, 5.0);
    }

    if (showLegend) {
        double legendY = plot.top() + 4.0;
        painter->setFont(style::uiFont(7.5));
        for (const PlotSeries& s : series) {
            if (s.label.isEmpty())
                continue;
            const QRectF swatch(plot.right() - 92, legendY, 13, 8);
            painter->setPen(Qt::NoPen);
            painter->setBrush(s.color);
            painter->drawRect(swatch);
            painter->setPen(style::ink());
            painter->drawText(QRectF(swatch.right() + 4, legendY - 4, 92, 14), Qt::AlignLeft, s.label);
            legendY += 14.0;
        }
    }
    painter->restore();
}

}  // namespace lab
