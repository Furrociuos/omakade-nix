#pragma once

#include <QDate>
#include <QLocale>
#include <QRegularExpression>
#include <QString>

namespace UserDateFormat {

inline QString format(const QDate& date, const QString& precisionHint = {}) {
  if (!date.isValid()) return {};
  static const QRegularExpression yearOnly(QStringLiteral("^\\d{4}$"));
  static const QRegularExpression monthOnly(QStringLiteral("^\\p{L}+\\s+\\d{4}$"));
  const QString hint = precisionHint.trimmed();
  const QLocale locale = QLocale::system();
  const auto yearMatch = yearOnly.match(hint);
  if (yearMatch.hasMatch())
    return locale.toString(QDate(yearMatch.captured(0).toInt(), 1, 1), QStringLiteral("yyyy"));
  const auto monthMatch = monthOnly.match(hint);
  if (monthMatch.hasMatch()) {
    QDate monthDate = QLocale(QLocale::English).toDate(hint, QStringLiteral("MMMM yyyy"));
    if (!monthDate.isValid())
      monthDate = QLocale(QLocale::English).toDate(hint, QStringLiteral("MMM yyyy"));
    if (monthDate.isValid()) return locale.toString(monthDate, QStringLiteral("MMMM yyyy"));
    return locale.toString(date, QStringLiteral("MMMM yyyy"));
  }
  return locale.toString(date, QStringLiteral("MMM d, yyyy"));
}

} // namespace UserDateFormat
