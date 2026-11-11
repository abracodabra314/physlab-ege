#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <limits>

namespace lab {

/**
 * Тип элемента управления, который интерфейс создаёт для параметра.
 */
enum class ParamKind {
    Number,   // числовое поле со стрелками
    Slider,   // ползунок + числовое поле
    Choice,   // выпадающий список вариантов
    Toggle,   // флажок (вкл/выкл)
};

/**
 * Описание одного исходного параметра модели.
 * Единицы измерения указываются в unit (или прямо в label, если unit пуст).
 */
struct ParamSpec {
    QString id;                                // машинный идентификатор
    QString label;                             // подпись для пользователя
    QString unit;                              // единицы измерения, напр. "км/ч"
    ParamKind kind = ParamKind::Number;
    double minValue = 0.0;                     // физически допустимый диапазон
    double maxValue = 100.0;
    double stepValue = 1.0;                    // шаг изменения
    double decimals = 2.0;                     // количество знаков после запятой (для полей)
    bool allowZero = true;                     // допускает ли значение 0
    double defaultValue = std::numeric_limits<double>::quiet_NaN();  // стартовое значение
    QStringList choices;                       // варианты для ParamKind::Choice
    QString group;                             // группировка в панели параметров
    QString helpText;                          // подсказка
};

/**
 * Текущее (показываемое пользователю) значение физической величины.
 */
struct Readout {
    QString label;
    QString value;
    bool highlight = false;   // выделить (ключевой результат задачи)
};

/**
 * Результат проверки допустимости исходных параметров.
 */
struct ValidationResult {
    bool ok = true;
    QString message;          // текст ошибки/предупреждения на русском языке
    QString hint;             // что сделать, чтобы стало допустимо
};

/**
 * Один шаг математической записи (строка панели «Решение»).
 * kind — заголовок раздела («Дано», «Формула», «Подстановка»...),
 * content — формула в компактной математической разметке (см. utils/MathText.h).
 */
struct DerivationStep {
    QString kind;
    QString content;
};

/**
 * Полная запись решения задачи: от исходных данных до ответа.
 */
struct Derivation {
    bool valid = true;
    QString problemText;              // условие задачи с текущими числами
    QString unknown;                  // что требуется найти
    QString answerTemplate;           // вид ответа (текст с числами, rich-разметка)
    QVector<DerivationStep> steps;
};

/** Состояние проигрывания эксперимента. */
enum class RunState {
    Stopped,   // сброшено, время = 0
    Running,   // идёт анимация
    Paused,    // пауза, можно продолжить
    Finished,  // модель дошла до конца
};

}  // namespace lab
