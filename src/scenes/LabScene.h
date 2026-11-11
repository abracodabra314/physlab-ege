#pragma once

#include <QGraphicsScene>
#include <QVector>

#include "core/Simulation.h"
#include "scenes/DragHandleItem.h"
#include "scenes/Style.h"

class QPainter;

namespace lab {

/**
 * Базовая QGraphicsScene для всех моделей лаборатории.
 *
 * Обязанности:
 *  - хранить ссылку на математическую модель и обновляться по её сигналам;
 *  - переводить значения модели (км, м, л, %) в координаты сцены (пиксели)
 *    и обратно — за счёт этого перетаскивание мышью меняет математику;
 *  - создавать drag-ручки по описанию model->handles() и применять их
 *    новые значения через Simulation::setHandleValue;
 *  - рисовать схему: потомки реализуют только paintScheme().
 *
 * Схема рисуется в слое фона (поскольку она — «учебный чертёж»), а ручки
 * остаются отдельными QGraphicsItem и всегда находятся поверх неё.
 */
class LabScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit LabScene(Simulation* model, QObject* parent = nullptr);
    ~LabScene() override;

    Simulation* model() const { return m_model; }

    /**
     * Задаёт пиксельную область просмотра и диапазоны значений модели
     * (горизонтальный «мир» и вертикальная шкала величин).
     */
    void setRanges(const QRectF& area, double horizontalMin, double horizontalMax,
                   double verticalMin, double verticalMax);

    /**
     * Пересчитывает диапазоны модели под новую область просмотра и
     * перестраивает сцену. Вызывается из LabView при каждом изменении
     * размера и при каждом обновлении состояния; сцены переопределяют
     * метод под свою геометрию.
     */
    virtual void reconfigure(const QRectF& area) = 0;

    /** Область, запрошенная представлением; используется при refresh(). */
    void requestViewArea(const QRectF& area) { m_requestedArea = area; }

    QRectF viewArea() const { return m_viewArea; }
    double worldMin() const { return m_worldMin; }
    double worldMax() const { return m_worldMax; }
    double valueMin() const { return m_valueMin; }
    double valueMax() const { return m_valueMax; }

    /** Перевод значения модели в X координаты сцены. */
    double toSceneX(double modelValue) const;
    /** Перевод значения модели в Y координаты сцены (значение растёт вверх). */
    double toSceneY(double modelValue) const;
    /** Обратный перевод: X сцены -> значение модели. */
    double toModelValue(qreal sceneX) const;
    /** Обратный перевод: Y сцены -> значение модели (ось Y экрана направлена вниз). */
    double toModelValueY(qreal sceneY) const;
    /** Сколько пикселей приходится на одну единицу модели по горизонтали. */
    double pixelsPerUnit() const;
    /** Сколько пикселей приходится на одну единицу модели по вертикали. */
    double pixelsPerValueUnit() const;

    /** Синхронизирует drag-ручки с описанием model->handles(). */
    void rebuildHandles();

    /**
     * Перевод позиции курсора в значение модели для конкретной ручки.
     * По умолчанию: горизонтальные ручки — по шкале world, вертикальные —
     * по шкале величин. Сцены с особой геометрией (глубина колодца растёт
     * вниз) переопределяют метод.
     */
    virtual double mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const;

    /** Перевод значения модели в экранную позицию ручки (согласован с mapHandlePosition). */
    virtual double handleScenePosition(const HandleInfo& info) const;
    /**
     * Размещает ручки в координатах сцены. По умолчанию — по соответствующей
     * оси в центре области просмотра; сцены переопределяют под свою геометрию.
     */
    virtual void positionHandles();
    QVector<DragHandleItem*> handles() const { return m_handles; }

public slots:
    /** Полный пересчёт содержимого — вызывается при любом изменении модели. */
    void refresh();

signals:
    /** Пользователь перетащил ручку: панель параметров должна обновиться. */
    void handleMoved(const QString& handleId);

public:
    /** Текстовая подпись-«пилюля» поверх элемента. */
    static void drawTag(QPainter* painter, const QPointF& center, const QString& text,
                        const QColor& background = QColor(255, 255, 255, 230),
                        const QColor& textColor = QColor(32, 32, 32));

    /** Размерная линия со стрелками и подписью (расстояния, глубины). */
    static void drawDimension(QPainter* painter, const QPointF& from, const QPointF& to,
                              const QString& text, const QColor& color = QColor(55, 71, 89),
                              double offset = 0.0);

protected:
    /** Потомок рисует схему по текущему состоянию модели. */
    virtual void paintScheme(QPainter* painter) = 0;

    /** Фон: цвет и учебная сетка, затем — paintScheme(), затем — ось времени. */
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    /** Предупреждение о недопустимых параметрах поверх сцены. */
    void drawForeground(QPainter* painter, const QRectF& rect) override;

    /** Нажатие на нижнюю ось времени — перемотка; захват точки — живая перемотка. */
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

    /** Прямоугольник нижней оси времени (сцены резервируют под неё место). */
    QRectF timeAxisRect() const;
    /** Модельное время под точкой оси (для перемотки). */
    double timeAtScenePosition(const QPointF& pos) const;
    /** Рисует ось времени с бегущей точкой состояния и отметкой события. */
    void drawTimeAxis(QPainter* painter);

    /** Одна кривая графика: точки (время, значение) и стиль. */
    struct PlotSeries {
        QVector<QPointF> points;
        QColor color = style::accent();
        qreal width = 2.2;
        bool dashed = false;
        QString label;
    };

    /**
     * Мини-график зависимости величины от времени. Рисует оси, кривые,
     * вертикальную отметку текущего момента и точку ключевого события.
     * Точки передаются в «модельных» координатах (t, y).
     */
    static void drawPlot(QPainter* painter, const QRectF& rect, const QString& title,
                         const QString& timeUnit, const QVector<PlotSeries>& series,
                         double tMax, double currentT, double eventT);

    Simulation* m_model = nullptr;
    QRectF m_viewArea;
    /** Последняя запрошенная представлением область (для перестройки масштаба). */
    QRectF m_requestedArea;
    double m_worldMin = 0.0;
    double m_worldMax = 1.0;
    double m_valueMin = 0.0;
    double m_valueMax = 1.0;
    QVector<DragHandleItem*> m_handles;
    bool m_scrubbingTime = false;
};

}  // namespace lab
