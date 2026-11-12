#pragma once

#include <QColor>
#include <QRectF>

class QPainter;
class QString;

namespace lab::primitives {

/**
 * Набор простых «учебных» примитивов для визуализации моделей:
 * сосуды с жидкостью, теплоходы, краны, шестерни, звуковые волны.
 * Все функции рисуют локально (в текущей системе координат painter).
 */

/** Контур прямоугольного сосуда и налитая в него жидкость (fraction — доля заполнения). */
void drawVessel(QPainter* painter, const QRectF& rect, double fraction, const QColor& wall,
                const QColor& liquid, double wallThickness = 2.2);

/** Волнистая поверхность жидкости на заданной высоте. */
void drawLiquidSurface(QPainter* painter, double left, double right, double y, const QColor& liquid);

/**
 * Стрелка-поток (струя наполнения, слива, линия перекачки): показывает
 * направление движения воды. Числовых значений не содержит.
 * time — модельное время: по нему движутся пунктирные «капли», поэтому
 * анимация потока остановится на паузе и при сбросе.
 */
void drawFlowArrow(QPainter* painter, const QPointF& from, const QPointF& to, const QString& text,
                   const QColor& color, bool active, double time = 0.0);

/** Схема теплохода: корпус над водой, рубка, труба. Подпись рисует вызывающий код. */
void drawBoat(QPainter* painter, const QPointF& waterlineCenter, const QColor& color,
              bool moving, bool headingRight);

/** Схематичный кран (вентиль): active — открыт. */
void drawTap(QPainter* painter, const QPointF& center, const QColor& color, bool active);

/** Шестерня механизма: radius — радиус, spin — угол поворота (рад), active — вращается. */
void drawGear(QPainter* painter, const QPointF& center, double radius, double spin, const QColor& color,
              bool active, int teeth = 12);

/** Расходящиеся дуги звуковой волны вокруг точки source. */
void drawSoundWaves(QPainter* painter, const QPointF& source, double maxRadius, const QColor& color,
                    double phase = 0.0, int rings = 3);

/** Пристань (пирс) у отметки x на линии воды. */
void drawPier(QPainter* painter, double x, double waterY, double height, const QColor& color);

/** Стрелка течения с подписью. */
void drawCurrentArrow(QPainter* painter, const QRectF& area, const QColor& waterColor, const QString& text);

/** Градуированная шкала объёма/длины справа от прямоугольника. */
void drawScale(QPainter* painter, const QRectF& rect, int majorSteps, const QString& unit,
               double maxValue, const QColor& color);

/**
 * Пружина между точками from и to (работает для любого направления):
 * витки распределяются по фактической длине, поэтому пружина визуально
 * растягивается и сжимается вместе с положением груза.
 * coils — число витков, amplitude — высота витка в пикселях.
 */
void drawSpring(QPainter* painter, const QPointF& from, const QPointF& to, int coils, double amplitude,
                const QColor& color, qreal width = 2.4);

/**
 * Неподвижная опора (стена, потолок, кронштейн): прямоугольник-основание
 * со штриховкой, показывающей, что элемент закреплён.
 * hatchDirection — направление штриховки наружу от основания.
 */
void drawFixedSupport(QPainter* painter, const QRectF& rect, const QPointF& hatchDirection,
                      const QColor& color);

/**
 * Штриховка под поверхностью (грунт под столом, дном сосуда): косые линии
 * под линией контакта — стандартное обозначение твёрдой опоры.
 */
void drawHatchedSurface(QPainter* painter, const QRectF& rect, const QColor& color);

}  // namespace lab::primitives
