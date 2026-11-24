#pragma once

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QPen>

namespace lab::style {

/**
 * Единая палитра «чертёж в тетради»: белая бумага в синюю клетку, чернильные
 * линии, бледные тела и цвет только там, где он несёт смысл (векторы,
 * выделения). Геометрическая точность важнее фотореализма.
 */
inline QColor background() { return QColor(253, 253, 250); }
inline QColor paper() { return QColor(255, 255, 255); }
inline QColor ink() { return QColor(30, 38, 50); }
inline QColor inkSoft() { return QColor(92, 102, 115); }
// Клетка, как в тетради по физике: синие линии, светлее основного контура.
inline QColor grid() { return QColor(196, 213, 235); }
inline QColor accent() { return QColor(33, 118, 198); }
inline QColor accentWarm() { return QColor(214, 122, 34); }
inline QColor danger() { return QColor(198, 61, 52); }
inline QColor success() { return QColor(38, 132, 84); }

// Тела на чертеже — обычным «цветным карандашом»: контур чёрный, заливка
// средняя, чтобы не сливалась с бумагой и не спорила с векторами.
inline QColor bodyA() { return QColor(33, 118, 198); }
inline QColor bodyB() { return QColor(198, 61, 52); }
inline QColor bodyC() { return QColor(38, 132, 84); }
inline QColor bodyD() { return QColor(142, 84, 176); }

/** Цвет объекта по его «варианту» (0..3). */
inline QColor variantColor(int variant) {
    switch (variant % 4) {
        case 0: return bodyA();
        case 1: return bodyB();
        case 2: return bodyC();
        default: return bodyD();
    }
}

inline QColor water() { return QColor(96, 165, 220, 170); }
inline QColor waterDeep() { return QColor(43, 111, 168, 200); }
inline QColor sand() { return QColor(226, 210, 172); }

inline QPen thinPen() { return QPen(ink(), 1.2); }
inline QPen dashPen() {
    QPen pen(inkSoft(), 1.0, Qt::DashLine);
    return pen;
}

/**
 * Шрифт подписей на сцене: берётся системный интерфейс-шрифт (он корректно
 * подобран на Windows/macOS/Linux), задаётся только размер и насыщенность.
 * Физические величины на чертеже принято писать курсивом.
 */
inline QFont uiFont(qreal pointSize = 10.5, bool bold = false, bool italic = false) {
    QFont font = QApplication::font();
    font.setPointSizeF(pointSize);
    font.setBold(bold);
    font.setItalic(italic);
    return font;
}

}  // namespace lab::style
