#pragma once
#include <QRegularExpression>
#include <QString>

inline QString SanitizeQtLogMessage(QString text) {
    text.truncate(2048);
    text.replace(QRegularExpression("(password|passwd|token|secret|authorization|api[_-]?key)\\s*[:=]\\s*[^\\s&]+",
        QRegularExpression::CaseInsensitiveOption), QStringLiteral("\\1=[REDACTED]"));
    text.replace(QRegularExpression("\\bBearer\\s+[^\\s&]+", QRegularExpression::CaseInsensitiveOption),
        QStringLiteral("Bearer [REDACTED]"));
    text.replace(QRegularExpression("(://)[^/@\\s:]+:[^/@\\s]+@"), QStringLiteral("\\1[REDACTED]@"));
    return text;
}
