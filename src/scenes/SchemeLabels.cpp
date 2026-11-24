#include "scenes/SchemeLabels.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

#include "scenes/LabScene.h"
#include "scenes/Style.h"

namespace lab::labels {
namespace {

/** Шаг и число колец перебора свободных мест вокруг предельной точки. */
constexpr double kStepPixels = 10.0;
constexpr int kMaxRings = 22;
constexpr int kDirectionsPerRing = 16;

}  // namespace

QSizeF tagSize(const QString& text) {
    static const QFontMetricsF metrics(style::uiFont(9.5, true));
    const QRectF bounds = metrics.boundingRect(text);
    return QSizeF(bounds.width() + 16.0, bounds.height() + 8.0);
}

bool overlaps(const QRectF& a, const QRectF& b) {
    const QRectF intersection = a.intersected(b);
    return intersection.isValid() && intersection.width() > 2.0 && intersection.height() > 2.0;
}

QPointF place(const QSizeF& size, const QPointF& preferred, const QVector<QRectF>& occupied, const QRectF& bounds) {
    const bool bounded = bounds.width() > 0.0 && bounds.height() > 0.0;
    auto fits = [&](const QPointF& center) {
        const QRectF box(center.x() - size.width() / 2.0, center.y() - size.height() / 2.0, size.width(),
                         size.height());
        if (bounded && !bounds.contains(box))
            return false;
        for (const QRectF& other : occupied) {
            if (overlaps(box, other))
                return false;
        }
        return true;
    };

    if (fits(preferred))
        return preferred;
    for (int ring = 1; ring <= kMaxRings; ++ring) {
        for (int index = 0; index < kDirectionsPerRing; ++index) {
            const double angle = 2.0 * M_PI * index / double(kDirectionsPerRing);
            const QPointF candidate = preferred + QPointF(std::cos(angle), std::sin(angle)) * (kStepPixels * ring);
            if (fits(candidate))
                return candidate;
        }
    }
    // Свободного места рядом нет. Если предельная точка вообще лежит за
    // границами чертежа, подпись придвигается к ближайшей допустимой
    // точке: она остаётся у своей величины, но не уходит с рисунка.
    if (bounded) {
        const QPointF clamped(std::clamp(preferred.x(), bounds.left() + size.width() / 2.0,
                                         std::max(bounds.left() + size.width() / 2.0,
                                                  bounds.right() - size.width() / 2.0)),
                              std::clamp(preferred.y(), bounds.top() + size.height() / 2.0,
                                         std::max(bounds.top() + size.height() / 2.0,
                                                  bounds.bottom() - size.height() / 2.0)));
        if (fits(clamped))
            return clamped;
    }
    // Иначе оставляем предельную точку — подпись всё равно останется у
    // своей величины, а не уедет через весь чертёж.
    return preferred;
}

QVector<QRectF> Planner::occupied() const {
    QVector<QRectF> boxes;
    boxes.reserve(m_items.size());
    for (const Item& placed : m_items)
        boxes.push_back(placed.rect());
    return boxes;
}

void Planner::add(const QString& id, const QPointF& preferred, const QString& text, const QColor& color,
                  const QColor& background) {
    Item item;
    item.id = id;
    item.text = text;
    item.size = tagSize(text);
    item.color = color;
    item.background = background;
    item.anchor = place(item.size, preferred, occupied(), m_bounds);
    m_items.push_back(item);
}

bool Planner::tryAdd(const QString& id, const QPointF& preferred, const QString& text, const QColor& color,
                     const QColor& background) {
    Item item;
    item.id = id;
    item.text = text;
    item.size = tagSize(text);
    item.color = color;
    item.background = background;
    const QRectF box(preferred.x() - item.size.width() / 2.0, preferred.y() - item.size.height() / 2.0,
                     item.size.width(), item.size.height());
    if (m_bounds.width() > 0.0 && !m_bounds.contains(box))
        return false;
    for (const QRectF& other : occupied()) {
        if (overlaps(box, other))
            return false;
    }
    item.anchor = preferred;
    m_items.push_back(item);
    return true;
}

void Planner::addFirstFree(const QString& id, const QVector<QPointF>& candidates, const QString& text,
                           const QColor& color, const QColor& background) {
    for (const QPointF& candidate : candidates) {
        if (tryAdd(id, candidate, text, color, background))
            return;
    }
    // Ни одна из точек не подошла — ставим у первой и разводим с остальными.
    if (!candidates.isEmpty())
        add(id, candidates.first(), text, color, background);
}

void drawAll(QPainter* painter, const QVector<Item>& items) {
    for (const Item& item : items)
        LabScene::drawTag(painter, item.anchor, item.text, item.background, item.color);
}

void drawRulerLine(QPainter* painter, const QPointF& from, const QPointF& to, const QColor& color, double offset) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(color, 1.3));

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
    painter->drawLine(a - normal * 5.0, a + normal * 5.0);
    painter->drawLine(b - normal * 5.0, b + normal * 5.0);
    const QPointF head = dir * 7.0;
    painter->drawLine(a, a + head + QPointF(-normal.y(), normal.x()) * 3.4);
    painter->drawLine(a, a + head + QPointF(normal.y(), -normal.x()) * 3.4);
    painter->drawLine(b, b - head + QPointF(-normal.y(), normal.x()) * 3.4);
    painter->drawLine(b, b - head + QPointF(normal.y(), -normal.x()) * 3.4);
    painter->restore();
}

QPointF rulerLabelPos(const QPointF& from, const QPointF& to, double offset) {
    QPointF dir = to - from;
    const double length = std::hypot(dir.x(), dir.y());
    if (length < 1e-6)
        return (from + to) / 2.0;
    dir /= length;
    const QPointF normal(-dir.y(), dir.x());
    return (from + to) / 2.0 + normal * (offset + 12.0);
}

QString stripValues(const QString& text) {
    // Единый разбор для всех сцен: числа с единицами и «= число» уходят,
    // а обозначения (s, t₁ = h/c) и имена с номерами («Теплоход 2») остаются.
    static const QRegularExpression valueWithUnit(QStringLiteral(
        "\\d+(?:[.,]\\d+)?\\s*"
        "(?:км/ч|км/с|м/с|км|дм|мм|м|л|мл|с|мин|ч|К|°С|%|кг|г|Н|кН|Па|кПа|атм|Дж|кДж|Вт|кВт|В|мВ|кВ|А|мА|А/ч"
        "|моль|Кл|Гн|Ф|Ом|Гц|м^3|м^2|л/мин|л/с)"));
    static const QRegularExpression valueAfterEquals(QStringLiteral("(?:=|≈)\\s*[-+−]?\\d+(?:[.,]\\d+)?"));
    static const QRegularExpression bareNumber(QStringLiteral("(?<=\\p{Ll})\\s+\\d+(?:[.,]\\d+)?"));

    QString result = text;
    result.replace(valueWithUnit, QString());
    result.replace(valueAfterEquals, QString());
    result.replace(bareNumber, QString());

    // Подчистка висячих знаков, оставшихся после вычёркивания.
    static const QRegularExpression danglingOperator(QStringLiteral("\\s*[=≈](?=\\s*[,;)]|$)"));
    result.replace(danglingOperator, QString());
    static const QRegularExpression emptyParens(QStringLiteral("\\(\\s*\\)"));
    result.replace(emptyParens, QString());
    static const QRegularExpression doubleSpace(QStringLiteral("\\s{2,}"));
    result.replace(doubleSpace, QStringLiteral(" "));
    static const QRegularExpression spaceBeforePunct(QStringLiteral("\\s+([,;.)])"));
    result.replace(spaceBeforePunct, QStringLiteral("$1"));
    static const QRegularExpression leadingPunct(QStringLiteral("^[\\s]*[,.:—–-]+"));
    result.replace(leadingPunct, QString());
    return result.trimmed();
}

}  // namespace lab::labels
