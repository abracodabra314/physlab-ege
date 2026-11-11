#pragma once

#include <QApplication>
#include <QIcon>
#include <QPainter>
#include <QPixmap>

namespace lab {

/**
 * Иконка приложения рисуется вектором при запуске — так не требуется
 * внешний файл и иконка одинакова на всех трёх платформах.
 * Эскиз-источник для документации: assets/icons/lab_icon.svg.
 */
inline QIcon appIcon() {
    QPixmap pixmap(128, 128);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Фон-«планшет».
    painter.setPen(QPen(QColor(31, 42, 54), 6));
    painter.setBrush(QColor(247, 246, 242));
    painter.drawRoundedRect(QRectF(8, 8, 112, 112), 22, 22);

    // Дорога.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(86, 90, 97));
    painter.drawRect(QRectF(20, 74, 88, 16));
    QPen dash(QColor(240, 240, 235), 2.4, Qt::DashLine);
    painter.setPen(dash);
    painter.drawLine(QPointF(26, 82), QPointF(102, 82));

    // Автомобиль.
    painter.setPen(QPen(QColor(16, 60, 105), 2));
    painter.setBrush(QColor(33, 118, 198));
    painter.drawRoundedRect(QRectF(34, 52, 34, 14), 4, 4);
    painter.drawRoundedRect(QRectF(41, 43, 19, 10), 3, 3);
    painter.setBrush(QColor(40, 44, 50));
    painter.setPen(QPen(QColor(20, 22, 26), 1.5));
    painter.drawEllipse(QPointF(43, 68), 5.5, 5.5);
    painter.drawEllipse(QPointF(61, 68), 5.5, 5.5);

    // Формула s = vt.
    painter.setPen(QColor(31, 42, 54));
    QFont formula = QApplication::font();
    formula.setPointSizeF(26.0);
    formula.setBold(true);
    formula.setItalic(true);
    painter.setFont(formula);
    painter.drawText(QRectF(18, 12, 92, 34), Qt::AlignCenter, QStringLiteral("s=vt"));

    return QIcon(pixmap);
}

}  // namespace lab
