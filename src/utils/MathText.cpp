#include "utils/MathText.h"

#include <QHash>
#include <QStringList>

#include <cmath>

namespace lab::utils {
namespace {

// ------------------------------------------------------------------
// Вспомогательный рекурсивный преобразователь компактной формулы в HTML.
// ------------------------------------------------------------------
class Parser {
public:
    explicit Parser(const QString& src) : m_src(src) {}

    QString parseUntil(char terminator) {
        QString out;
        while (m_pos < m_src.size()) {
            const QChar ch = m_src.at(m_pos);
            if (terminator != 0 && ch == QLatin1Char(terminator)) {
                ++m_pos;  // закрывающую скобкуconsumirует вызывающий код
                return out;
            }
            out += parseAtom();
        }
        if (terminator != 0)
            --m_pos;  // не нашли закрывающую скобку — возвращаемся
        return out;
    }

private:
    static QString escape(QChar ch) {
        switch (ch.unicode()) {
            case u'<': return QStringLiteral("&lt;");
            case u'>': return QStringLiteral("&gt;");
            case u'&': return QStringLiteral("&amp;");
            default: return QString(ch);
        }
    }

    static QString escapeText(const QString& text) {
        QString out;
        out.reserve(text.size());
        for (QChar ch : text)
            out += escape(ch);
        return out;
    }

    // Символьные команды вида \cdot, \approx и т. п.
    static const QHash<QString, QString>& symbolMap() {
        static const QHash<QString, QString> symbols = {
            {QStringLiteral("cdot"), QStringLiteral("&#8901;")},      // ⋅
            {QStringLiteral("times"), QStringLiteral("&times;")},      // ×
            {QStringLiteral("div"), QStringLiteral("&divide;")},       // ÷
            {QStringLiteral("approx"), QStringLiteral("&approx;")},    // ≈
            {QStringLiteral("le"), QStringLiteral("&le;")},            // ≤
            {QStringLiteral("ge"), QStringLiteral("&ge;")},            // ≥
            {QStringLiteral("ne"), QStringLiteral("&ne;")},            // ≠
            {QStringLiteral("equiv"), QStringLiteral("&equiv;")},      // ≡
            {QStringLiteral("rightarrow"), QStringLiteral("&rarr;")},  // →
            {QStringLiteral("Rightarrow"), QStringLiteral("&#8658;")}, // ⇒
            {QStringLiteral("leftarrow"), QStringLiteral("&larr;")},
            {QStringLiteral("pm"), QStringLiteral("&plusmn;")},        // ±
            {QStringLiteral("pi"), QStringLiteral("&pi;")},
            {QStringLiteral("theta"), QStringLiteral("&theta;")},
            {QStringLiteral("mu"), QStringLiteral("&mu;")},
            {QStringLiteral("varphi"), QStringLiteral("&phi;")},
            {QStringLiteral("omega"), QStringLiteral("&omega;")},
            {QStringLiteral("Delta"), QStringLiteral("&Delta;")},      // Δ
            {QStringLiteral("alpha"), QStringLiteral("&alpha;")},
            {QStringLiteral("beta"), QStringLiteral("&beta;")},
            {QStringLiteral("gamma"), QStringLiteral("&gamma;")},
            {QStringLiteral("lambda"), QStringLiteral("&lambda;")},
            {QStringLiteral("int"), QStringLiteral("&#8747;")},       // ∫
            {QStringLiteral("infty"), QStringLiteral("&infin;")},
            {QStringLiteral("percent"), QStringLiteral("%")},
            {QStringLiteral("degree"), QStringLiteral("&deg;")},
            // Штрих у буквы: t′, T′ — обозначение «то же, но измеренное
            // наблюдателем».
            {QStringLiteral("prime"), QStringLiteral("&#8242;")},      // ′
            {QStringLiteral("circ"), QStringLiteral("&deg;")},
            {QStringLiteral("in"), QStringLiteral("&isin;")},
            // Названия функций выводятся прямыми буквами (как в школьной записи).
            {QStringLiteral("sin"), QStringLiteral("sin")},
            {QStringLiteral("cos"), QStringLiteral("cos")},
            {QStringLiteral("tan"), QStringLiteral("tan")},
            {QStringLiteral("max"), QStringLiteral("max")},
            {QStringLiteral("min"), QStringLiteral("min")},
            {QStringLiteral("ll"), QStringLiteral("&#8810;")},         // ≪
            {QStringLiteral("gg"), QStringLiteral("&#8811;")},         // ≫
            {QStringLiteral("perp"), QStringLiteral("&#8869;")},       // ⊥
            {QStringLiteral("angle"), QStringLiteral("&#8736;")},      // ∠
            // \left( ... \right) — «тянущиеся» скобки: самой скобке
            // размера не задаём, команды просто пропускаются.
            {QStringLiteral("left"), QStringLiteral("")},
            {QStringLiteral("right"), QStringLiteral("")},
            // Теплота, работа и символ состояния системы: Q и A пишутся
            // прямыми буквами, чтобы не путаться с обозначением площади.
            {QStringLiteral("nu"), QStringLiteral("&#957;")},        // ν
            {QStringLiteral("tau"), QStringLiteral("&tau;")},
            {QStringLiteral("uparrow"), QStringLiteral("&#8593;")},
            {QStringLiteral("downarrow"), QStringLiteral("&#8595;")},
            {QStringLiteral("pdot"), QStringLiteral("p&#775;")},      // p с точкой: скорость изменения p
            {QStringLiteral("bar"), QStringLiteral("")},              // содержимое группы печатается как есть
            {QStringLiteral("arcsin"), QStringLiteral("arcsin")},
            {QStringLiteral("arccos"), QStringLiteral("arccos")},
            {QStringLiteral("arctan"), QStringLiteral("arctan")},
            {QStringLiteral("ln"), QStringLiteral("ln")},
            {QStringLiteral("log"), QStringLiteral("log")},
            {QStringLiteral("sum"), QStringLiteral("&#8721;")},        // ∑
            {QStringLiteral("quad"), QStringLiteral("&nbsp;&nbsp;&nbsp;")},
            {QStringLiteral("qquad"), QStringLiteral("&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;")},
            {QStringLiteral("sim"), QStringLiteral("&sim;")},          // ∼
            {QStringLiteral("propto"), QStringLiteral("&prop;")},      // ∝
            {QStringLiteral(";"), QStringLiteral("&nbsp;")},
            {QStringLiteral(" "), QStringLiteral("&nbsp;")},
        };
        return symbols;
    }

    static QString style() { return QStringLiteral("vertical-align:middle"); }

    // Разбирает аргумент-группу: {..} или одиночный символ/число.
    QString parseGroup() {
        skipSpaces();
        if (m_pos >= m_src.size())
            return {};
        if (m_src.at(m_pos) == QLatin1Char('{')) {
            ++m_pos;
            return parseUntil('}');
        }
        if (m_src.at(m_pos) == QLatin1Char('\\')) {
            ++m_pos;
            return parseCommandName();
        }
        const QChar ch = m_src.at(m_pos);
        ++m_pos;
        return escape(ch);
    }

    void skipSpaces() {
        while (m_pos < m_src.size() && m_src.at(m_pos).isSpace())
            ++m_pos;
    }

    QString makeFraction(const QString& numerator, const QString& denominator) {
        // Двухстрочная таблица: черта дроби рисуется верхней границей ячейки
        // знаменателя — такой приём надёжно поддерживается QTextDocument в Qt.
        return QStringLiteral(
                   "<table cellspacing=0 cellpadding=0 style=\"%1\">"
                   "<tr><td align=center>%2</td></tr>"
                   "<tr><td align=center style=\"border-top:1px solid #202020\">%3</td></tr>"
                   "</table>")
            .arg(style(), numerator, denominator);
    }

    QString parseCommandName() {
        int start = m_pos;
        while (m_pos < m_src.size() && m_src.at(m_pos).isLetter())
            ++m_pos;
        if (start == m_pos) {  // \*, \{, \, и т. п.
            if (m_pos < m_src.size()) {
                const QChar single = m_src.at(m_pos);
                // Односимвольные команды вида «\,» (тонкий пробел между
                // множителем и дифференциалом: p\,dV).
                const auto shortIt = symbolMap().constFind(QString(single));
                if (shortIt != symbolMap().constEnd()) {
                    ++m_pos;
                    return shortIt.value();
                }
                const QString html = escape(single);
                ++m_pos;
                return html;
            }
            return {};
        }
        const QString name = m_src.mid(start, m_pos - start);

        if (name == QLatin1String("frac")) {
            const QString num = parseGroup();
            const QString den = parseGroup();
            return makeFraction(num, den);
        }
        if (name == QLatin1String("sqrt")) {
            const QString body = parseGroup();
            return QStringLiteral("&#8730;<span style=\"text-decoration:overline\">%1</span>").arg(body);
        }
        if (name == QLatin1String("text")) {
            skipSpaces();
            QString body;
            if (m_pos < m_src.size() && m_src.at(m_pos) == QLatin1Char('{')) {
                ++m_pos;
                body = parseUntil('}');
            } else {
                body = parseGroup();
            }
            return QStringLiteral("<span>%1</span>").arg(body);
        }
        const auto it = symbolMap().constFind(name);
        if (it != symbolMap().constEnd())
            return it.value();
        return QStringLiteral("\\%1").arg(name);  // неизвестная команда — оставляем как есть
    }

    QString parseAtom() {
        const QChar ch = m_src.at(m_pos);
        if (ch == QLatin1Char('\\')) {
            ++m_pos;
            return parseCommandName();
        }
        if (ch == QLatin1Char('{')) {
            ++m_pos;
            return parseUntil('}');
        }
        if (ch == QLatin1Char('_') || ch == QLatin1Char('^')) {
            ++m_pos;
            QString group = parseGroup();
            // Перед индексом нужна «база» — берём предыдущий символ (или закрывающий тег).
            const char* tag = ch == QLatin1Char('_') ? "sub" : "sup";
            return QStringLiteral("<%1>%2</%1>").arg(QLatin1String(tag), group);
        }
        ++m_pos;
        return escape(ch);
    }

    QString m_src;
    int m_pos = 0;
};

}  // namespace

QString toRichMath(const QString& source) {
    Parser parser(source);
    return parser.parseUntil(0);
}

QString toRichMathLine(const QString& source) {
    return QStringLiteral("<div align=\"center\">%1</div>").arg(toRichMath(source));
}

QString formatNumber(double value, int precision) {
    if (!std::isfinite(value))
        return QStringLiteral("—");
    QString text = QString::number(value, 'f', precision);
    // Убираем лишние нули в дробной части.
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0')))
            text.chop(1);
        if (text.endsWith(QLatin1Char('.')))
            text.chop(1);
    }
    // Нормализация вида «-0» и «-0.000».
    if (text == QStringLiteral("-0"))
        text = QStringLiteral("0");
    text.replace(QLatin1Char('.'), QLatin1Char(','));
    return text;
}

QString formatDuration(double amount, const QString& unit) {
    auto trim = [](double v) { return formatNumber(v, 2); };
    if (unit == QLatin1String("h")) {
        const int wholeHours = static_cast<int>(amount);
        const double minutes = (amount - wholeHours) * 60.0;
        if (wholeHours == 0)
            return QStringLiteral("%1 мин").arg(trim(minutes));
        if (qFuzzyCompare(minutes, std::round(minutes)))
            return QStringLiteral("%1 ч %2 мин").arg(trim(wholeHours)).arg(trim(std::round(minutes)));
        return QStringLiteral("%1 ч").arg(trim(amount));
    }
    if (unit == QLatin1String("min")) {
        const int wholeMinutes = static_cast<int>(amount);
        const double seconds = (amount - wholeMinutes) * 60.0;
        if (wholeMinutes == 0)
            return QStringLiteral("%1 с").arg(trim(seconds));
        return QStringLiteral("%1 мин %2 с").arg(trim(wholeMinutes)).arg(trim(seconds));
    }
    return QStringLiteral("%1 с").arg(trim(amount));
}

}  // namespace lab::utils
