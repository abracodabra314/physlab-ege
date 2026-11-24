#pragma once

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

class QPainter;

namespace lab::labels {

/**
 * Размещение подписей на учебных схемах.
 *
 * Подписи чертежа — самый частый источник «грязной» картинки: стоит
 * переместить объект, и текст наползает на соседний текст или на линии.
 * Поэтому сцены собирают подписи в список и один раз просят развести их:
 * каждая подпись получает позицию рядом со своей геометрической точкой,
 * но свободную от уже занятых мест и внутри границ чертежа.
 *
 * Поиск позиции детерминирован (перебор по кольцам вокруг предельной
 * точки), поэтому картинка не «дрожит» от кадра к кадру.
 */
struct Item {
    QString id;
    QString text;
    QPointF anchor;                            // центр подписи
    QSizeF size;                               // габарит «пилюли»
    QColor color = QColor(38, 44, 52);
    QColor background = QColor(255, 255, 255, 245);

    QRectF rect() const {
        return QRectF(anchor.x() - size.width() / 2.0, anchor.y() - size.height() / 2.0, size.width(),
                      size.height());
    }
};

/** Габарит подписи с текстом — те же припуски, что у LabScene::drawTag. */
QSizeF tagSize(const QString& text);

/** Пересекаются ли две подписи (пересечение меньше пикселя — не считается). */
bool overlaps(const QRectF& a, const QRectF& b);

/**
 * Свободная позиция для подписи: сначала предельная точка, затем кольцо
 * за кольцом вокруг неё. Если граница задана (не нулевая), подпись целиком
 * должна в неё попадать.
 */
QPointF place(const QSizeF& size, const QPointF& preferred, const QVector<QRectF>& occupied,
              const QRectF& bounds = QRectF());

/**
 * Строитель подписей: хранит уже занятые места и при добавлении новой
 * подписи сразу ищет для неё свободное место.
 */
class Planner {
public:
    explicit Planner(const QRectF& bounds = QRectF()) : m_bounds(bounds) {}

    /** Добавляет подпись около предельной точки, разводя её с остальными. */
    void add(const QString& id, const QPointF& preferred, const QString& text, const QColor& color,
             const QColor& background = QColor(255, 255, 255, 245));

    /**
     * Пробует разместить подпись ровно в предельной точке: если там она
     * влезает в границы и не задевает другие подписи, добавляет её и
     * возвращает true. Иначе ничего не добавляет — у вызывающего кода
     * остаётся запасной вариант (например, другая точка той же величины).
     */
    bool tryAdd(const QString& id, const QPointF& preferred, const QString& text, const QColor& color,
                const QColor& background = QColor(255, 255, 255, 245));

    /** Добавляет первую из предельных точек, где подпись умещается без наложений. */
    void addFirstFree(const QString& id, const QVector<QPointF>& candidates, const QString& text,
                      const QColor& color, const QColor& background = QColor(255, 255, 255, 245));

    const QVector<Item>& items() const { return m_items; }
    /** Габариты уже размещённых подписей. */
    QVector<QRectF> occupied() const;

private:
    QRectF m_bounds;
    QVector<Item> m_items;
};

/** Рисует список подписей поверх схемы. */
void drawAll(QPainter* painter, const QVector<Item>& items);

/**
 * Размерная линия без подписи: текст рисует плановщик подписей, поэтому
 * порядок отрисовки один — сначала вся геометрия, потом все тексты.
 */
void drawRulerLine(QPainter* painter, const QPointF& from, const QPointF& to, const QColor& color, double offset);

/** Центр подписи размерной линии: середина отрезка со смещением на вынос. */
QPointF rulerLabelPos(const QPointF& from, const QPointF& to, double offset);

/**
 * Убирает из текста числовые значения с единицами: на чертеже подпись
 * остаётся смысловой («s», «t₁ = h/c», «Опыт завершён: осталось ядер»),
 * а количества смотрят на панелях и графиках. Имена-номера («Теплоход 2»,
 * «Кран 1») функция не трогает — это обозначения, а не величины.
 */
QString stripValues(const QString& text);

}  // namespace lab::labels
