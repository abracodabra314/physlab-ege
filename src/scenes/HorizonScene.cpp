#include "scenes/HorizonScene.h"

#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>
#include <cmath>

#include "models/HorizonModel.h"
#include "scenes/Style.h"
#include "utils/MathText.h"

namespace lab::scenes {
namespace {

/**
 * Центральный угол α мал даже при больших высотах (для Земли и h = 2 км это
 * 1,4°), поэтому на чертеже он увеличен: иначе прямоугольный треугольник OAB
 * слился бы в линию. Множитель подбирается по максимальной высоте и один для
 * всех моментов подъёма, поэтому форма зависимости сохраняется: угол на
 * чертеже растёт пропорционально настоящему. Величина увеличения указана на
 * схеме, а все числа берутся из формул модели, а не с линейки рисунка.
 */
constexpr double kTargetAngleRadians = 0.80;      // ≈ 46° при h = h_max
constexpr double kMaxDrawnAngleRadians = 1.35;    // предел искажения чертежа
constexpr double kMinExaggeration = 1.0;
constexpr double kMaxExaggeration = 3000.0;

/**
 * Где на чертеже размещать подписи — доли радиуса планеты. Метки угла и
 * длины дуги стоят внутри круга: линия взгляда и её размерная линия проходят
 * снаружи, касаясь окружности, и свободных мест там нет.
 */
constexpr double kAlphaArcFraction = 0.50;        // дуга, отмечающая угол α
constexpr double kAlphaLabelFraction = 0.76;      // подпись α — внутри угла
constexpr double kArcLabelFraction = 0.80;        // подпись длины дуги s
constexpr double kArcLabelAngleFactor = 1.15;     // подпись s — ниже радиуса OB
constexpr double kHorizonLabelFraction = 0.68;    // метка точки B
constexpr double kRadiusLabelFraction = 0.50;     // подпись R на радиусе
/** Вынос размерной линии дальности за линию взгляда, доля радиуса. */
constexpr double kDistanceOffsetFraction = 0.30;
/** Вынос размерной линии высоты от вертикали OA, пиксели. */
constexpr double kHeightOffsetPixels = 34.0;

/**
 * Размеры шара на чертеже. Значения параметра различаются на порядки
 * (Луна — 1737 км, Солнце — 696000 км), поэтому размер зависит от радиуса
 * логарифмически: маленькая планета остаётся различимой, крупная — внутри
 * чертежа.
 */
constexpr double kPlanetSizeMin = 0.86;
constexpr double kPlanetSizeSpan = 0.14;

/** Перевод километров в метры: радиус планеты в модели задан в километрах. */
constexpr double kMetersPerKilometer = 1000.0;
/** Радиус Земли в километрах — чертёж по умолчанию, если постановка неверна. */
constexpr double kEarthRadiusKm = 6371.0;

/** Число делений вертикальной шкалы высот. */
constexpr int kHeightTicks = 4;

/** Отметки линейки радиусов: спутники и планеты Солнечной системы. */
const double kRadiusMarks[] = {100.0, 1737.0, 3390.0, 6371.0, 69600.0, 605100.0};
/** Границы логарифмической линейки радиусов — те же, что у параметра модели. */
constexpr double kRadiusScaleMin = 100.0;
constexpr double kRadiusScaleMax = 605100.0;

/**
 * Масштаб стрелки скорости. Он задан не от текущей скорости (иначе любая
 * скорость рисовалась бы стрелкой одного размера), а от опорной: тогда
 * 5 м/с — короткая стрелка, 250 м/с — длинная, длина пропорциональна величине.
 */
constexpr double kReferenceAscentSpeed = 120.0;   // м/с
constexpr double kArrowMinPixels = 6.0;
constexpr double kArrowMaxPixels = 90.0;

/** Поля чертежа: шкала высот, зазор до шара, линейка радиусов, верхний отступ. */
constexpr double kRulerWidth = 96.0;
constexpr double kGapToPlanet = 34.0;
constexpr double kRadiusBarHeight = 34.0;
constexpr double kRightReserved = 120.0;
constexpr double kTopReserved = 40.0;
/** Зазор между верхней точкой чертежа и полем схемы. */
constexpr double kTopGap = 10.0;

/**
 * Размерная линия без подписи: подпись рисует вызывающий код через реестр
 * надписей, поэтому порядок отрисовки kontrollируется один — сначала вся
 * геометрия, потом все тексты.
 */
void drawDimensionLine(QPainter* painter, const QPointF& from, const QPointF& to, const QColor& color,
                       double offset) {
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

/** Центр подписи размерной линии: середина отрезка со смещением на вынос. */
QPointF dimensionLabelPos(const QPointF& from, const QPointF& to, double offset) {
    QPointF dir = to - from;
    const double length = std::hypot(dir.x(), dir.y());
    if (length < 1e-6)
        return (from + to) / 2.0;
    dir /= length;
    const QPointF normal(-dir.y(), dir.x());
    return (from + to) / 2.0 + normal * (offset + 12.0);
}

/** Габарит «пилюли» с текстом: те же припуски, что у LabScene::drawTag. */
QSizeF tagSize(const QString& text) {
    static const QFontMetricsF metrics(style::uiFont(9.5, true));
    const QRectF bounds = metrics.boundingRect(text);
    return QSizeF(bounds.width() + 16.0, bounds.height() + 8.0);
}

/** Пересекаются ли две подписи (пересечение меньше пикселя — не считается). */
bool labelsOverlap(const QRectF& a, const QRectF& b) {
    const QRectF overlap = a.intersected(b);
    return overlap.isValid() && overlap.width() > 2.0 && overlap.height() > 2.0;
}

/**
 * Позиция центра подписи, которая не перекрывается с уже размещёнными
 * надписями и целиком попадает в границы чертежа. Если свободное место не
 * найдено сразу, подпись сдвигается по спирали от своей геометрической
 * точки: она остаётся рядом с величиной, которую описывает, но не
 * наползает на соседей. Перебор детерминирован, поэтому картинка
 * стабильна от кадра к кадру.
 */
QPointF resolveLabelPosition(const QSizeF& size, const QPointF& preferred, const QVector<QRectF>& occupied,
                             const QRectF& bounds) {
    const auto fits = [&](const QPointF& center) {
        const QRectF box(center.x() - size.width() / 2.0, center.y() - size.height() / 2.0, size.width(),
                         size.height());
        if (bounds.isValid() && !bounds.contains(box))
            return false;
        for (const QRectF& other : occupied) {
            if (labelsOverlap(box, other))
                return false;
        }
        return true;
    };

    if (fits(preferred))
        return preferred;
    constexpr double kStep = 10.0;
    constexpr int kRings = 20;
    constexpr int kDirections = 16;
    for (int ring = 1; ring <= kRings; ++ring) {
        for (int index = 0; index < kDirections; ++index) {
            const double angle = 2.0 * M_PI * index / double(kDirections);
            const QPointF candidate = preferred + QPointF(std::cos(angle), std::sin(angle)) * (kStep * ring);
            if (fits(candidate))
                return candidate;
        }
    }
    return preferred;
}

}  // namespace

HorizonScene::HorizonScene(Simulation* model, QObject* parent) : LabScene(model, parent) {}

// ------------------------------------------------------------------
// Геометрия чертежа
// ------------------------------------------------------------------

void HorizonScene::reconfigure(const QRectF& area) {
    auto* model = qobject_cast<models::HorizonModel*>(m_model);

    // Рамка схемы — верхняя часть области просмотра: под ней графики,
    // ещё ниже общая ось времени.
    m_schemeRect = QRectF(area.left() + 24.0, area.top() + 18.0, area.width() - 48.0,
                          std::max(230.0, area.height() * 0.60));
    m_worldRect = m_schemeRect.adjusted(18.0, 16.0, -18.0, -14.0);

    // По вертикали чертёж занимает радиус шара ниже центра, сам шар и
    // подъём наблюдателя: OA = R/cos α, поэтому высота равна R(1 + 1/cos α).
    const double liftFactor = 1.0 / std::cos(kTargetAngleRadians);
    const double byHeight =
        (m_worldRect.height() - kRadiusBarHeight - kTopReserved - kTopGap) / (1.0 + liftFactor);
    const double byWidth = (m_worldRect.width() - kRulerWidth - kGapToPlanet - kRightReserved) / 2.0;
    const double maxRadiusPx = std::clamp(std::min(byHeight, byWidth), 34.0, 240.0);

    const double radiusKilometers = model && model->validate().ok ? model->radiusKilometers() : kEarthRadiusKm;
    const double lowLog = std::log10(kRadiusScaleMin);
    const double highLog = std::log10(kRadiusScaleMax);
    const double fraction =
        (std::log10(std::clamp(radiusKilometers, kRadiusScaleMin, kRadiusScaleMax)) - lowLog) / (highLog - lowLog);
    m_planetRadiusPx = maxRadiusPx * (kPlanetSizeMin + kPlanetSizeSpan * fraction);

    m_rulerX = m_worldRect.left() + kRulerWidth;
    // Шар опирается нижней точкой на одну и ту же линию: при изменении
    // радиуса он растёт вверх и не «уезжает» за край чертежа.
    const double barTop = m_worldRect.bottom() - kRadiusBarHeight;
    m_center = QPointF(m_rulerX + kGapToPlanet + m_planetRadiusPx, barTop - kTopGap - m_planetRadiusPx);

    // Стрелка скорости подъёма.
    m_velocityScale = kArrowMaxPixels / kReferenceAscentSpeed;

    setRanges(area, 0.0, std::max(1.0, model && model->validate().ok ? model->maxHeight() : 1.0), 0.0, 1.0);
}

QPointF HorizonScene::planetPoint(double angleRadians, double radiusPx) const {
    // Угол отсчитывается от вертикали вверх, в сторону точки горизонта.
    return QPointF(m_center.x() + std::sin(angleRadians) * radiusPx,
                   m_center.y() - std::cos(angleRadians) * radiusPx);
}

double HorizonScene::angleExaggeration() const {
    auto* model = qobject_cast<models::HorizonModel*>(m_model);
    if (!model || !model->validate().ok)
        return 1.0;
    const double alphaMax = model->centralAngle(model->maxHeight());
    if (alphaMax <= 0.0)
        return 1.0;
    return std::clamp(kTargetAngleRadians / alphaMax, kMinExaggeration, kMaxExaggeration);
}

double HorizonScene::drawnCentralAngle() const {
    auto* model = qobject_cast<models::HorizonModel*>(m_model);
    if (!model || !model->validate().ok)
        return 0.0;
    const double alpha = model->centralAngle(model->heightAt(m_model->time()));
    return std::min(angleExaggeration() * alpha, kMaxDrawnAngleRadians);
}

double HorizonScene::liftForHeight(double height) const {
    auto* model = qobject_cast<models::HorizonModel*>(m_model);
    if (!model || !model->validate().ok)
        return 0.0;
    const double alpha = std::min(angleExaggeration() * model->centralAngle(height), kMaxDrawnAngleRadians);
    // Прямой угол в точке касания: cos α = R/(R + h), значит OA = R/cos α.
    return m_planetRadiusPx * (1.0 / std::max(1e-9, std::cos(alpha)) - 1.0);
}

double HorizonScene::heightForLift(double liftPx) const {
    auto* model = qobject_cast<models::HorizonModel*>(m_model);
    if (!model || !model->validate().ok)
        return 0.0;
    const double radiusPlusLift = m_planetRadiusPx + std::max(0.0, liftPx);
    const double drawnAngle = std::acos(std::clamp(m_planetRadiusPx / radiusPlusLift, 0.0, 1.0));
    const double trueAngle = std::min(drawnAngle / angleExaggeration(), M_PI_2 - 1e-9);
    const double cosAlpha = std::cos(trueAngle);
    if (cosAlpha <= 1e-12)
        return model->maxHeight();
    // Обратная формула к cos α = R/(R + h):  h = R(1/cos α − 1).
    return model->radiusMeters() * (1.0 / cosAlpha - 1.0);
}

QPointF HorizonScene::observerScenePos() const {
    const double height = m_model && m_model->validate().ok
                              ? static_cast<models::HorizonModel*>(m_model)->heightAt(m_model->time())
                              : 0.0;
    return planetPoint(0.0, m_planetRadiusPx + liftForHeight(height));
}

QPointF HorizonScene::horizonScenePos() const { return planetPoint(drawnCentralAngle(), m_planetRadiusPx); }

QPointF HorizonScene::subsatelliteScenePos() const { return planetPoint(0.0, m_planetRadiusPx); }

HorizonScene::SchemeGeometry HorizonScene::schemeGeometry() const {
    SchemeGeometry geometry;
    geometry.center = m_center;
    geometry.planetRadiusPx = m_planetRadiusPx;
    geometry.subsatellite = subsatelliteScenePos();
    geometry.observer = observerScenePos();
    geometry.horizon = horizonScenePos();
    geometry.drawnAlpha = drawnCentralAngle();
    return geometry;
}

// ------------------------------------------------------------------
// Реестр подписей чертежа
// ------------------------------------------------------------------

QVector<HorizonScene::Annotation> HorizonScene::annotations() const {
    QVector<Annotation> list;
    auto* model = qobject_cast<models::HorizonModel*>(m_model);
    if (!model || !model->validate().ok)
        return list;

    const double angle = drawnCentralAngle();
    const QPointF observer = observerScenePos();
    const double radius = m_planetRadiusPx;
    // Подписи остаются в области чертежа: за её краем их рисовать нельзя.
    const QRectF bounds = m_worldRect;

    // Сначала размещаются подписи величин, затем пояснения: приоритет у
    // того, что читается из формул.
    struct Preferred {
        QString id;
        QPointF at;
        QString text;
        QColor color;
        QColor background;
    };
    QVector<Preferred> wanted;

    auto want = [&wanted](const QString& id, const QPointF& at, const QString& text, const QColor& color,
                          const QColor& background = QColor(255, 255, 255, 245)) {
        wanted.push_back({id, at, text, color, background});
    };

    if (angle > 1e-6) {
        // Расстояние по прямой — размерная линия над линией взгляда.
        want(QStringLiteral("distance"),
             dimensionLabelPos(observer, horizonScenePos(), -radius * kDistanceOffsetFraction),
             QStringLiteral("d = %1 км")
                 .arg(utils::formatNumber(model->horizonDistanceAt(m_model->time()) / kMetersPerKilometer, 3)),
             style::bodyA().darker(150));
        // Длина дуги поверхности — внутри круга, где нет линий.
        want(QStringLiteral("arc"), planetPoint(angle * kArcLabelAngleFactor, radius * kArcLabelFraction),
             QStringLiteral("s = %1 км")
                 .arg(utils::formatNumber(model->arcDistanceAt(m_model->time()) / kMetersPerKilometer, 3)),
             style::bodyD().darker(150));
        // Центральный угол он же угол понижения горизонта.
        want(QStringLiteral("alpha"), planetPoint(angle * 0.5, radius * kAlphaLabelFraction),
             QStringLiteral("α = %1°").arg(utils::formatNumber(model->dipAngleAt(m_model->time()), 3)),
             style::accentWarm().darker(150));
        // Высота наблюдателя — размерная линия слева от вертикали OA.
        want(QStringLiteral("height"), dimensionLabelPos(subsatelliteScenePos(), observer, -kHeightOffsetPixels),
             QStringLiteral("h = %1 м").arg(utils::formatNumber(model->heightAt(m_model->time()), 1)),
             style::bodyB().darker(160));
        // Точка горизонта.
        want(QStringLiteral("horizon"), planetPoint(angle, radius * kHorizonLabelFraction), QStringLiteral("B"),
             style::bodyA().darker(170));
    }
    // Масштаб планеты и её центр.
    want(QStringLiteral("radius"), QPointF(m_center.x() - radius * kRadiusLabelFraction, m_center.y() - 18.0),
         QStringLiteral("R = %1 км").arg(utils::formatNumber(model->radiusKilometers(), 0)), style::ink());
    want(QStringLiteral("center"), m_center + QPointF(-24.0, 22.0), QStringLiteral("O"), style::ink(),
         QColor(255, 255, 255, 240));
    want(QStringLiteral("observer"), observer + QPointF(-28.0, -16.0), QStringLiteral("A"),
         style::bodyB().darker(170));

    // Пояснения к чертежу: кто есть кто и как читать увеличенный угол.
    want(QStringLiteral("legend"), QPointF(m_schemeRect.right() - 200.0, m_schemeRect.top() + 12.0),
         QStringLiteral("A — наблюдатель, B — горизонт, O — центр"), style::inkSoft(), QColor(255, 255, 255, 205));
    const double exaggeration = angleExaggeration();
    if (exaggeration > 1.01) {
        want(QStringLiteral("exaggeration"), QPointF(m_schemeRect.right() - 215.0, m_schemeRect.top() + 34.0),
             QStringLiteral("угол α на чертеже увеличен в %1 раз").arg(utils::formatNumber(exaggeration, 1)),
             style::inkSoft(), QColor(255, 255, 255, 205));
    }

    // Итог задачи: подъём завершён, дальность горизонта — как в ответе.
    if (m_model->time() >= model->ascentTime() - 1e-9) {
        want(QStringLiteral("result"), QPointF(m_schemeRect.center().x(), m_worldRect.bottom() - 4.0),
             QStringLiteral("h = %1 м: d = %2 км, s = %3 км, видно %4 % поверхности")
                 .arg(utils::formatNumber(model->maxHeight(), 0),
                      utils::formatNumber(model->horizonDistance(model->maxHeight()) / kMetersPerKilometer, 3),
                      utils::formatNumber(model->arcDistance(model->maxHeight()) / kMetersPerKilometer, 3),
                      utils::formatNumber(100.0 * model->visibleFraction(model->maxHeight()), 5)),
             style::success().darker(130), QColor(234, 247, 239, 240));
    }

    QVector<QRectF> occupied;
    for (const Preferred& item : wanted) {
        Annotation note;
        note.id = item.id;
        note.text = item.text;
        note.size = tagSize(item.text);
        note.color = item.color;
        note.background = item.background;
        note.anchor = resolveLabelPosition(note.size, item.at, occupied, bounds);
        const QRectF box(note.anchor.x() - note.size.width() / 2.0, note.anchor.y() - note.size.height() / 2.0,
                         note.size.width(), note.size.height());
        occupied.push_back(box);
        list.push_back(note);
    }
    return list;
}

// ------------------------------------------------------------------
// Логарифмическая линейка радиусов
// ------------------------------------------------------------------

QRectF HorizonScene::radiusBarRect() const {
    const double height = 14.0;
    const double top = m_center.y() + m_planetRadiusPx + 8.0;
    return QRectF(m_rulerX, top, m_worldRect.right() - m_rulerX - 10.0, height);
}

double HorizonScene::radiusBarX(double radiusKilometers) const {
    const QRectF bar = radiusBarRect();
    const double lowLog = std::log10(kRadiusScaleMin);
    const double highLog = std::log10(kRadiusScaleMax);
    const double valueLog = std::log10(std::clamp(radiusKilometers, kRadiusScaleMin, kRadiusScaleMax));
    return bar.left() + (valueLog - lowLog) / (highLog - lowLog) * bar.width();
}

double HorizonScene::radiusForBarX(double sceneX) const {
    const QRectF bar = radiusBarRect();
    const double lowLog = std::log10(kRadiusScaleMin);
    const double highLog = std::log10(kRadiusScaleMax);
    const double fraction = std::clamp((sceneX - bar.left()) / std::max(1e-9, bar.width()), 0.0, 1.0);
    return std::pow(10.0, lowLog + fraction * (highLog - lowLog));
}

// ------------------------------------------------------------------
// Перетаскивание мышью: высота наблюдателя и радиус планеты
// ------------------------------------------------------------------

double HorizonScene::mapHandlePosition(const HandleInfo& info, const QPointF& scenePos) const {
    auto* model = qobject_cast<models::HorizonModel*>(m_model);
    if (!model)
        return LabScene::mapHandlePosition(info, scenePos);

    if (info.id == QLatin1String("height"))
        return heightForLift(subsatelliteScenePos().y() - scenePos.y());
    if (info.id == QLatin1String("radius"))
        return radiusForBarX(scenePos.x());
    return LabScene::mapHandlePosition(info, scenePos);
}

double HorizonScene::handleScenePosition(const HandleInfo& info) const {
    auto* model = qobject_cast<models::HorizonModel*>(m_model);
    if (!model)
        return LabScene::handleScenePosition(info);

    if (info.id == QLatin1String("height"))
        return observerScenePos().y();
    if (info.id == QLatin1String("radius"))
        return radiusBarX(model->radiusKilometers());
    return LabScene::handleScenePosition(info);
}

void HorizonScene::positionHandles() {
    auto* model = qobject_cast<models::HorizonModel*>(m_model);
    if (!model)
        return;

    for (DragHandleItem* handle : m_handles) {
        if (!handle)
            continue;
        const HandleInfo info = handle->info();
        if (info.id == QLatin1String("height")) {
            // Ручка на вертикали OA: её перемещение — это подъём наблюдателя.
            handle->setAxis(DragHandleItem::Axis::Vertical);
            handle->setAnchor(QPointF(m_center.x(), observerScenePos().y()));
        } else if (info.id == QLatin1String("radius")) {
            handle->setAxis(DragHandleItem::Axis::Horizontal);
            handle->setAnchor(QPointF(radiusBarX(model->radiusKilometers()), radiusBarRect().center().y()));
        } else {
            handle->setAnchor(observerScenePos());
        }
    }
}

// ------------------------------------------------------------------
// Векторы: скорость подъёма (сил и ускорений в модели нет)
// ------------------------------------------------------------------

QVector<vectors::Arrow> HorizonScene::vectorsFor(double t) const {
    auto* model = qobject_cast<models::HorizonModel*>(m_model);
    QVector<vectors::Arrow> arrows;
    if (!model || !model->validate().ok)
        return arrows;

    const double speed = model->verticalVelocityAt(t);
    if (!m_model->showsVelocities() || speed <= 0.0)
        return arrows;

    const QPointF observer = observerScenePos();
    vectors::Arrow lift;
    lift.from = observer;
    lift.to = observer + QPointF(0.0, -vectors::visualLength(speed, m_velocityScale, kArrowMinPixels, kArrowMaxPixels));
    lift.symbol = QStringLiteral("v");
    lift.kind = vectors::Kind::Velocity;
    lift.labelOffset = QPointF(22.0, -4.0);
    arrows.push_back(lift);
    return arrows;
}

void HorizonScene::drawVectors(QPainter* painter, const QVector<vectors::Arrow>& arrows) const {
    for (const vectors::Arrow& arrow : arrows)
        vectors::drawArrow(painter, arrow);
}

// ------------------------------------------------------------------
// Геометрия чертежа
// ------------------------------------------------------------------

void HorizonScene::drawPlanet(QPainter* painter) const {
    auto* model = qobject_cast<models::HorizonModel*>(m_model);
    if (!model)
        return;

    const QRectF circle(m_center.x() - m_planetRadiusPx, m_center.y() - m_planetRadiusPx,
                        m_planetRadiusPx * 2.0, m_planetRadiusPx * 2.0);

    // Планета — шар, на чертеже его большой круг.
    painter->setPen(QPen(style::success().darker(150), 2.0));
    painter->setBrush(QColor(214, 233, 219, 235));
    painter->drawEllipse(circle);

    // Радиус, отложенный влево от центра: масштаб планеты на чертеже.
    painter->setPen(QPen(style::inkSoft(), 1.3, Qt::DashLine));
    painter->drawLine(m_center, QPointF(m_center.x() - m_planetRadiusPx, m_center.y()));

    // Центр O.
    painter->setPen(Qt::NoPen);
    painter->setBrush(style::ink());
    painter->drawEllipse(m_center, 4.0, 4.0);

    // Поверхность в зените наблюдателя — начало отсчёта высоты и дуги.
    painter->setPen(Qt::NoPen);
    painter->setBrush(style::bodyD());
    painter->drawEllipse(subsatelliteScenePos(), 4.5, 4.5);
}

void HorizonScene::drawSightLines(QPainter* painter) const {
    auto* model = qobject_cast<models::HorizonModel*>(m_model);
    if (!model || !model->validate().ok)
        return;

    const double angle = drawnCentralAngle();
    const QPointF observer = observerScenePos();
    const QPointF horizon = horizonScenePos();

    // OA — отрезок от центра к наблюдателю, гипотенуза прямоугольного треугольника.
    painter->setPen(QPen(style::ink(), 1.5));
    painter->drawLine(m_center, observer);
    if (angle <= 1e-6)
        return;

    // OB — радиус, проведённый в точку касания.
    painter->setPen(QPen(style::ink(), 1.5));
    painter->drawLine(m_center, horizon);

    // AB — линия взгляда; её продолжают за точку горизонта, как на учебных чертежах.
    const QPointF direction = (horizon - observer) / std::max(1e-9, std::hypot(horizon.x() - observer.x(),
                                                                              horizon.y() - observer.y()));
    painter->setPen(QPen(style::bodyA(), 2.4));
    painter->drawLine(observer, horizon);
    painter->setPen(QPen(style::bodyA(), 1.2, Qt::DashLine));
    painter->drawLine(horizon, horizon + direction * std::max(20.0, m_planetRadiusPx * 0.28));

    drawRightAngleMark(painter, horizon);

    // Угол α у центра: между направлением на наблюдателя и на точку горизонта.
    const double arcRadius = std::max(26.0, m_planetRadiusPx * kAlphaArcFraction);
    const QRectF arcBox(m_center.x() - arcRadius, m_center.y() - arcRadius, arcRadius * 2.0, arcRadius * 2.0);
    painter->setPen(QPen(style::accentWarm(), 1.7));
    painter->setBrush(Qt::NoBrush);
    const int startDegrees = int(std::round((90.0 - angle * 180.0 / M_PI) * 16.0));
    painter->drawArc(arcBox, startDegrees, int(std::round(angle * 180.0 / M_PI * 16.0)));

    // Расстояние по прямой — размерная линия вне круга, над линией взгляда.
    drawDimensionLine(painter, observer, horizon, style::bodyA(), -m_planetRadiusPx * kDistanceOffsetFraction);

    // Точка горизонта B.
    painter->setPen(Qt::NoPen);
    painter->setBrush(style::bodyA());
    painter->drawEllipse(horizon, 5.0, 5.0);
}

void HorizonScene::drawRightAngleMark(QPainter* painter, const QPointF& at) const {
    // Квадратик в точке касания: OB ⊥ AB, поэтому треугольник OAB — прямоугольный,
    // и к нему применима теорема Пифагора.
    const QPointF observer = observerScenePos();
    QPointF toObserver = observer - at;
    toObserver /= std::max(1e-9, std::hypot(toObserver.x(), toObserver.y()));
    QPointF toCenter = m_center - at;
    toCenter /= std::max(1e-9, std::hypot(toCenter.x(), toCenter.y()));

    const double size = std::max(7.0, std::min(16.0, m_planetRadiusPx * 0.13));
    const QPointF p1 = at + toObserver * size;
    const QPointF p2 = at + toCenter * size;
    const QPointF corner = p1 + toCenter * size;
    painter->setPen(QPen(style::ink(), 1.4));
    painter->setBrush(Qt::NoBrush);
    painter->drawLine(p1, corner);
    painter->drawLine(p2, corner);
}

void HorizonScene::drawArcPath(QPainter* painter) const {
    auto* model = qobject_cast<models::HorizonModel*>(m_model);
    if (!model || !model->validate().ok)
        return;

    const double angle = drawnCentralAngle();
    if (angle <= 1e-6)
        return;

    // Дуга поверхности от точки под наблюдателем до точки горизонта: путь
    // s = R·α вдоль кривой. Прямой отрезок взгляда длиннее — касательная
    // отходит от поверхности.
    const QRectF circle(m_center.x() - m_planetRadiusPx, m_center.y() - m_planetRadiusPx,
                        m_planetRadiusPx * 2.0, m_planetRadiusPx * 2.0);
    painter->setPen(QPen(style::bodyD(), std::max(3.0, m_planetRadiusPx * 0.045), Qt::SolidLine, Qt::RoundCap));
    painter->setBrush(Qt::NoBrush);
    // Qt отсчитывает углы от оси X против часовой стрелки; вертикаль вверх — 90°.
    const int startDegrees = int(std::round((90.0 - angle * 180.0 / M_PI) * 16.0));
    const int spanDegrees = int(std::round(angle * 180.0 / M_PI * 16.0));
    painter->drawArc(circle, startDegrees, spanDegrees);
}

void HorizonScene::drawHeightScale(QPainter* painter) const {
    auto* model = qobject_cast<models::HorizonModel*>(m_model);
    if (!model || !model->validate().ok)
        return;

    // Вертикальная шкала высот: отметки стоят там же, где их даёт чертежная
    // зависимость h -> подъём, поэтому шкала согласована с перетаскиванием.
    const double surfaceY = m_center.y() - m_planetRadiusPx;
    const double topY = surfaceY - liftForHeight(model->maxHeight());
    const double rulerX = m_rulerX;

    painter->setPen(QPen(style::inkSoft(), 1.3));
    painter->drawLine(QPointF(rulerX, surfaceY), QPointF(rulerX, topY));
    painter->setFont(style::uiFont(7.5));
    for (int i = 0; i <= kHeightTicks; ++i) {
        const double height = model->maxHeight() * i / double(kHeightTicks);
        const double y = surfaceY - liftForHeight(height);
        painter->setPen(QPen(style::inkSoft(), i == 0 ? 1.5 : 1.0));
        painter->drawLine(QPointF(rulerX - 6.0, y), QPointF(rulerX + 6.0, y));
        painter->setPen(style::inkSoft());
        painter->drawText(QRectF(m_worldRect.left(), y - 7.0, rulerX - m_worldRect.left() - 10.0, 14),
                          Qt::AlignRight | Qt::AlignVCenter,
                          QStringLiteral("%1 м").arg(utils::formatNumber(height, height < 10.0 ? 1 : 0)));
    }
    painter->setPen(style::inkSoft());
    painter->drawText(QRectF(m_worldRect.left(), topY - 18.0, rulerX - m_worldRect.left() + 8.0, 14), Qt::AlignRight,
                      QStringLiteral("h, м"));

    // Текущая высота наблюдателя: пунктир от шкалы и размерная линия
    // слева от вертикали OA.
    const QPointF observer = observerScenePos();
    painter->setPen(QPen(style::bodyB(), 1.2, Qt::DashLine));
    painter->drawLine(QPointF(rulerX, observer.y()), QPointF(observer.x() - m_planetRadiusPx * 0.42, observer.y()));
    drawDimensionLine(painter, subsatelliteScenePos(), observer, style::bodyB(), -kHeightOffsetPixels);
}

void HorizonScene::drawRadiusScale(QPainter* painter) const {
    auto* model = qobject_cast<models::HorizonModel*>(m_model);
    if (!model || !model->validate().ok)
        return;

    // Линейка радиусов: перетаскивание ручки по ней меняет радиус планеты,
    // а вместе с ним и все формулы задачи.
    const QRectF bar = radiusBarRect();
    painter->setPen(QPen(style::inkSoft(), 1.2));
    painter->setBrush(QColor(255, 255, 255, 215));
    painter->drawRoundedRect(bar, 4, 4);
    painter->setFont(style::uiFont(7.5));
    for (const double value : kRadiusMarks) {
        const double x = radiusBarX(value);
        painter->setPen(QPen(style::inkSoft(), 1.0));
        painter->drawLine(QPointF(x, bar.bottom()), QPointF(x, bar.bottom() + 5.0));
        painter->setPen(style::inkSoft());
        painter->drawText(QRectF(x - 44.0, bar.bottom() + 5.0, 88.0, 13), Qt::AlignHCenter,
                          utils::formatNumber(value, 0));
    }

    const double currentX = radiusBarX(model->radiusKilometers());
    painter->setPen(QPen(style::danger(), 1.6));
    painter->setBrush(style::paper());
    painter->drawEllipse(QPointF(currentX, bar.center().y()), 6.0, 6.0);
}

void HorizonScene::drawObserver(QPainter* painter) const {
    const QPointF observer = observerScenePos();
    painter->setPen(QPen(style::bodyB().darker(170), 1.6));
    painter->setBrush(style::bodyB());
    painter->drawEllipse(observer, 8.0, 8.0);
}

// ------------------------------------------------------------------
// Схема целиком
// ------------------------------------------------------------------

void HorizonScene::paintScheme(QPainter* painter) {
    auto* model = qobject_cast<models::HorizonModel*>(m_model);
    if (!model)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    drawPlanet(painter);
    drawSightLines(painter);
    drawArcPath(painter);
    drawHeightScale(painter);
    drawRadiusScale(painter);
    drawObserver(painter);
    drawVectors(painter, vectorsFor(m_model->time()));

    // Все тексты — последними и по одному реестру: так подписи не набегают
    // друг на друга и не затираются линиями, которые рисуются позже.
    for (const Annotation& note : annotations())
        drawTag(painter, note.anchor, note.text, note.background, note.color);

    drawPlots(painter);
    painter->restore();
}

// ------------------------------------------------------------------
// Графики: два от времени и две не временные зависимости
// ------------------------------------------------------------------

void HorizonScene::drawPlots(QPainter* painter) const {
    auto* model = qobject_cast<models::HorizonModel*>(m_model);
    if (!model || !model->validate().ok)
        return;

    // Полоса между схемой и общей осью времени.
    const double plotsTop = m_schemeRect.bottom() + 14.0;
    const double plotsBottom = timeAxisRect().top() - 10.0;
    const double available = plotsBottom - plotsTop;
    if (available < 90.0)
        return;

    constexpr double kGap = 10.0;
    const double panelWidth = (m_schemeRect.width() - 3.0 * kGap) / 4.0;
    const double height = std::min(available, 210.0);
    const double top = plotsTop + std::max(0.0, available - height) * 0.5;
    const double duration = std::max(1e-6, m_model->duration());
    const double currentT = m_model->time();
    const double eventT = m_model->eventTime();

    // ---- 1. Закон подъёма ----
    {
        PlotSeries lift;
        lift.points = model->heightSeries();
        lift.color = style::bodyB();
        lift.label = QStringLiteral("h(t)");
        drawPlot(painter, QRectF(m_schemeRect.left(), top, panelWidth, height),
                 QStringLiteral("Высота h(t), м"), timeUnitLabel(), {lift}, duration, currentT, eventT);
    }

    // ---- 2. Дальность по прямой и путь по поверхности ----
    {
        PlotSeries distance;
        distance.points = model->distanceSeries();
        distance.color = style::bodyA();
        distance.label = QStringLiteral("d(t)");
        PlotSeries arc;
        arc.points = model->arcSeries();
        arc.color = style::bodyD();
        arc.dashed = true;
        arc.label = QStringLiteral("s(t)");
        drawPlot(painter, QRectF(m_schemeRect.left() + panelWidth + kGap, top, panelWidth, height),
                 QStringLiteral("Горизонт d(t) и путь s(t), км"), timeUnitLabel(), {distance, arc}, duration,
                 currentT, eventT);
    }

    // ---- 3. Зависимость дальности от высоты ----
    {
        PlotSeries distance;
        distance.points = model->distanceVersusHeight();
        distance.color = style::bodyA();
        distance.label = QStringLiteral("d(h)");
        const QPointF current(model->heightAt(currentT), model->horizonDistanceAt(currentT) / kMetersPerKilometer);
        drawGraph(painter, QRectF(m_schemeRect.left() + 2.0 * (panelWidth + kGap), top, panelWidth, height),
                  QStringLiteral("Дальность d(h), км"), QStringLiteral("высота h, м"), QStringLiteral("d, км"),
                  {distance}, current);
    }

    // ---- 4. Зависимость пути по поверхности от высоты ----
    {
        PlotSeries arc;
        arc.points = model->arcVersusHeight();
        arc.color = style::bodyD();
        arc.label = QStringLiteral("s(h)");
        const QPointF current(model->heightAt(currentT), model->arcDistanceAt(currentT) / kMetersPerKilometer);
        drawGraph(painter, QRectF(m_schemeRect.left() + 3.0 * (panelWidth + kGap), top, panelWidth, height),
                  QStringLiteral("Путь s(h), км"), QStringLiteral("высота h, м"), QStringLiteral("s, км"), {arc},
                  current);
    }
}

}  // namespace lab::scenes
