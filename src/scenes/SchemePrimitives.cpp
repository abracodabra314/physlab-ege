#include "scenes/SchemePrimitives.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

#include "scenes/Style.h"
#include "utils/MathText.h"

namespace lab::primitives {
namespace {

constexpr double kPi = 3.14159265358979323846;

}  // namespace

void drawVessel(QPainter* painter, const QRectF& rect, double fraction, const QColor& wall,
                const QColor& liquid, double wallThickness) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const double clamped = std::clamp(fraction, 0.0, 1.0);

    if (clamped > 0.0) {
        const double liquidHeight = (rect.height() - wallThickness) * clamped;
        QRectF inner(rect.left() + wallThickness, rect.bottom() - liquidHeight,
                     rect.width() - 2 * wallThickness, liquidHeight);
        if (inner.height() > 1.0) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(liquid);
            painter->drawRect(inner);
            drawLiquidSurface(painter, inner.left(), inner.right(), inner.top(), liquid);
        }
    }

    // Стенки и дно (сосуд открыт сверху).
    painter->setPen(QPen(wall.darker(160), wallThickness));
    painter->setBrush(QColor(wall.red(), wall.green(), wall.blue(), 40));
    QPainterPath path;
    path.moveTo(rect.left(), rect.top());
    path.lineTo(rect.left(), rect.bottom());
    path.lineTo(rect.right(), rect.bottom());
    path.lineTo(rect.right(), rect.top());
    painter->drawPath(path);
    painter->restore();
}

void drawLiquidSurface(QPainter* painter, double left, double right, double y, const QColor& liquid) {
    if (right - left < 2.0)
        return;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const double wavelength = std::max(18.0, (right - left) / 4.0);
    const double amplitude = 2.6;

    auto buildSurface = [&](QPainterPath& path) {
        path.moveTo(left, y);
        double x = left;
        bool up = true;
        while (x < right - 0.5) {
            const double next = std::min(x + wavelength, right);
            const double mid = (x + next) / 2.0;
            path.quadTo(mid, y + (up ? -amplitude : amplitude) * 2.0, next, y);
            x = next;
            up = !up;
        }
    };

    QPainterPath surface;
    buildSurface(surface);
    surface.lineTo(right, y + 12.0);
    surface.lineTo(left, y + 12.0);
    surface.closeSubpath();
    painter->setPen(Qt::NoPen);
    painter->setBrush(liquid);
    painter->drawPath(surface);

    QPainterPath line;
    buildSurface(line);
    painter->setPen(QPen(liquid.darker(150), 1.4));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(line);
    painter->restore();
}

void drawFlowArrow(QPainter* painter, const QPointF& from, const QPointF& to, const QString& text,
                   const QColor& color, bool active) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QColor penColor = active ? color : QColor(163, 167, 173);
    painter->setPen(QPen(penColor, active ? 5.0 : 2.4, active ? Qt::SolidLine : Qt::DashLine));
    painter->drawLine(from, to);

    QPointF dir = to - from;
    const double length = std::hypot(dir.x(), dir.y());
    if (length > 1e-6) {
        dir /= length;
        const QPointF normal(-dir.y(), dir.x());
        painter->setBrush(penColor);
        painter->setPen(Qt::NoPen);
        QPolygonF head;
        head << to << to - dir * 14.0 + normal * 7.0 << to - dir * 14.0 - normal * 7.0;
        painter->drawPolygon(head);
    }

    if (!text.isEmpty()) {
        painter->setFont(style::uiFont(8.5, active));
        painter->setPen(active ? penColor.darker(140) : QColor(150, 154, 160));
        const QPointF labelPos = (from + to) / 2.0 + QPointF(0, -12.0);
        painter->drawText(QRectF(labelPos.x() - 80, labelPos.y() - 9, 160, 18), Qt::AlignCenter, text);
    }
    painter->restore();
}

void drawBoat(QPainter* painter, const QPointF& waterlineCenter, const QColor& color,
              bool moving, bool headingRight) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->translate(waterlineCenter);

    const double bowSign = headingRight ? 1.0 : -1.0;
    const double half = 46.0;

    // Корпус.
    QPainterPath hull;
    hull.moveTo(-half, -14.0);
    hull.lineTo(half * 0.70, -14.0);
    hull.lineTo(half, -8.0);
    hull.lineTo(half * 0.55, 2.0);
    hull.lineTo(-half * 0.82, 2.0);
    hull.closeSubpath();
    hull.translate(0, 0);
    if (bowSign < 0)
        hull.translate(0, 0);  // зеркало не требуется: схема симметрична по форме

    painter->setPen(QPen(color.darker(170), 1.4));
    painter->setBrush(color);
    painter->drawPath(hull);

    // Надстройка и труба.
    painter->setPen(QPen(color.darker(190), 1.2));
    painter->setBrush(QColor(248, 247, 243));
    painter->drawRoundedRect(QRectF(-20.0, -30.0, 30.0, 16.0), 3, 3);
    painter->setBrush(QColor(62, 66, 72));
    painter->setPen(Qt::NoPen);
    painter->drawRect(QRectF(6.0, -40.0, 6.0, 11.0));

    // Иллюминаторы.
    painter->setBrush(QColor(226, 238, 248));
    for (int i = -1; i <= 1; ++i)
        painter->drawEllipse(QPointF(-8.0 + i * 12.0, -22.0), 2.4, 2.4);

    // Бурун при движении.
    if (moving) {
        painter->setPen(QPen(QColor(255, 255, 255, 215), 1.6));
        painter->setBrush(Qt::NoBrush);
        QPainterPath wake;
        const double bow = half * bowSign;
        wake.moveTo(bow, -6.0);
        wake.cubicTo(bow + 12.0 * bowSign, -9.0, bow + 18.0 * bowSign, 1.0, bow + 26.0 * bowSign, 4.0);
        painter->drawPath(wake);
    }
    painter->restore();
}

void drawTap(QPainter* painter, const QPointF& center, const QColor& color, bool active) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->translate(center);
    const QColor pen = active ? color.darker(130) : QColor(158, 162, 168);
    painter->setPen(QPen(pen, 2.0));
    painter->setBrush(active ? color : QColor(226, 227, 229));
    painter->drawRect(QRectF(-7, -7, 14, 14));
    // Маховик.
    painter->drawLine(QPointF(0, -7), QPointF(0, -14));
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(QPointF(0, -17.5), 6.5, 3.4);
    painter->restore();
}

void drawGear(QPainter* painter, const QPointF& center, double radius, double spin, const QColor& color,
              bool active, int teeth) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->translate(center);
    painter->rotate(active ? spin * 180.0 / kPi : 0.0);

    const double inner = radius * 0.82;
    const double step = 2.0 * kPi / teeth;
    const double halfTooth = step * 0.30;

    QPainterPath gear;
    gear.moveTo(inner, 0.0);
    for (int i = 0; i < teeth; ++i) {
        const double a = i * step;
        gear.lineTo(std::cos(a - halfTooth) * inner, std::sin(a - halfTooth) * inner);
        gear.lineTo(std::cos(a - halfTooth * 0.55) * radius, std::sin(a - halfTooth * 0.55) * radius);
        gear.lineTo(std::cos(a + halfTooth * 0.55) * radius, std::sin(a + halfTooth * 0.55) * radius);
        gear.lineTo(std::cos(a + halfTooth) * inner, std::sin(a + halfTooth) * inner);
    }
    gear.closeSubpath();

    const QColor fill = active ? color : QColor(206, 209, 213);
    painter->setPen(QPen(fill.darker(150), 1.4));
    painter->setBrush(fill);
    painter->drawPath(gear);
    painter->setBrush(QColor(247, 246, 242));
    painter->drawEllipse(QPointF(0, 0), radius * 0.26, radius * 0.26);
    painter->restore();
}

void drawSoundWaves(QPainter* painter, const QPointF& source, double maxRadius, const QColor& color,
                    double phase, int rings) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(Qt::NoBrush);
    for (int i = 0; i < rings; ++i) {
        const double t = std::fmod(phase + double(i) / double(rings), 1.0);
        const double r = maxRadius * t;
        if (r < 3.0)
            continue;
        const int alpha = static_cast<int>(200 * (1.0 - t));
        painter->setPen(QPen(QColor(color.red(), color.green(), color.blue(), alpha), 1.8));
        // Дуги направлены вверх: от воды к устью колодца.
        painter->drawArc(QRectF(source.x() - r, source.y() - r, r * 2, r * 2), 25 * 16, 130 * 16);
    }
    painter->restore();
}

void drawPier(QPainter* painter, double x, double waterY, double height, const QColor& color) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    // Сваи.
    painter->setPen(QPen(color.darker(150), 1.4));
    painter->setBrush(color);
    painter->drawRect(QRectF(x - 8, waterY - height * 0.35, 5, height));
    painter->drawRect(QRectF(x + 3, waterY - height * 0.35, 5, height));
    // Настил причала.
    painter->setBrush(color.darker(120));
    painter->drawRect(QRectF(x - 26, waterY - height * 0.35 - 7, 52, 7));
    painter->restore();
}

void drawCurrentArrow(QPainter* painter, const QRectF& area, const QColor& waterColor, const QString& text) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    for (int row = 0; row < 3; ++row) {
        const double y = area.top() + area.height() * (0.30 + 0.20 * row);
        painter->setPen(QPen(waterColor.darker(130), 1.8, Qt::DashLine));
        painter->drawLine(QPointF(area.left() + 24, y), QPointF(area.right() - 34, y));
        painter->setBrush(waterColor.darker(130));
        painter->setPen(Qt::NoPen);
        QPolygonF head;
        head << QPointF(area.right() - 24, y) << QPointF(area.right() - 36, y - 5.5)
             << QPointF(area.right() - 36, y + 5.5);
        painter->drawPolygon(head);
    }
    if (!text.isEmpty()) {
        painter->setFont(style::uiFont(9.0, true));
        painter->setPen(waterColor.darker(160));
        painter->drawText(QRectF(area.center().x() - 140, area.center().y() - 9, 280, 18), Qt::AlignCenter, text);
    }
    painter->restore();
}

void drawScale(QPainter* painter, const QRectF& rect, int majorSteps, const QString& unit,
               double maxValue, const QColor& color) {
    painter->save();
    painter->setPen(QPen(color, 1.0));
    painter->setFont(style::uiFont(8.0));
    const int labelEvery = majorSteps >= 8 ? majorSteps / 4 : 1;
    for (int i = 0; i <= majorSteps; ++i) {
        const double y = rect.bottom() - rect.height() * i / majorSteps;
        const bool labeled = (i % labelEvery) == 0;
        painter->drawLine(QPointF(rect.right(), y), QPointF(rect.right() + (labeled ? 9.0 : 4.0), y));
        if (labeled) {
            painter->drawText(QRectF(rect.right() + 12, y - 7, 84, 14), Qt::AlignLeft | Qt::AlignVCenter,
                              QStringLiteral("%1 %2").arg(utils::formatNumber(maxValue * i / majorSteps, 0), unit));
        }
    }
    painter->restore();
}

}  // namespace lab::primitives
