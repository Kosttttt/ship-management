#include "core/SqlStatementSplitter.h"

QStringList SqlStatementSplitter::split(const QString& script)
{
    QStringList statements;
    QString     current;
    bool        inLineComment  = false;
    bool        inBlockComment = false;
    bool        inQuotedText   = false;

    for (int i = 0; i < script.size(); ++i) {
        const QChar ch   = script.at(i);
        const QChar next = (i + 1 < script.size()) ? script.at(i + 1) : QChar();

        if (inLineComment) {
            if (ch == QLatin1Char('\n')) {
                inLineComment = false;
                current.append(ch);
            }
        } else if (inBlockComment) {
            if (ch == QLatin1Char('*') && next == QLatin1Char('/')) {
                inBlockComment = false;
                ++i;
            }
        } else if (inQuotedText) {
            current.append(ch);
            if (ch == QLatin1Char('\'')) {
                inQuotedText = false;
            }
        } else if (ch == QLatin1Char('-') && next == QLatin1Char('-')) {
            inLineComment = true;
            ++i;
        } else if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
            inBlockComment = true;
            ++i;
        } else if (ch == QLatin1Char(';')) {
            statements.append(current.trimmed());
            current.clear();
        } else {
            if (ch == QLatin1Char('\'')) {
                inQuotedText = true;
            }
            current.append(ch);
        }
    }
    statements.append(current.trimmed());

    statements.removeAll(QString());
    return statements;
}
