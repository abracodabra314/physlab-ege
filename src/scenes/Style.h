#pragma once

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QPen>

namespace lab::style {

/**
 * Единая палитра «учебной схемы»: спокойные цвета, тонкие линии,
 * геометрическая точность важнее фотореализма.
 */
inline QColor background() { return QColor(247, 246, 242); }
inline QColor paper() { return QColor(255, 255, 255); }
inline QColor ink() { return QColor(38, 44, 52); }
inline QColor inkSoft() { return QColor(92, 102, 115); }
inline QColor grid() { return QColor(222, 226, 232); }
inline QColor accent() { return QColor(33, 118, 198); }
inline QColor accentWarm() { return QColor(214, 122, 34); }
inline QColor danger() { return QColor(198, 61, 52); }
inline QColor success() { return QColor(38, 132, 84); }

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

inline QColor water() { return QColor(96, 165, 220, 190); }
inline QColor waterDeep() { return QColor(43, 111, 168, 210); }
inline QColor sand() { return QColor(226, 210, 172); }

inline QPen thinPen() { return QPen(ink(), 1.2); }
inline QPen dashPen() {
    QPen pen(inkSoft(), 1.0, Qt::DashLine);
    return pen;
}

/**
 * Шрифт подписей на сцене: берётся системный интерфейс-шрифт (он корректно
 * подобран на Windows/macOS/Linux), задаётся только размер и насыщенность.
 */
inline QFont uiFont(qreal pointSize = 10.5, bool bold = false) {
    QFont font = QApplication::font();
    font.setPointSizeF(pointSize);
    font.setBold(bold);
    return font;
}

}  // namespace lab::style
