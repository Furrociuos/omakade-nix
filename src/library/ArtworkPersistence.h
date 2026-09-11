#pragma once

#include <QHash>
#include <QSqlDatabase>
#include <QSqlQuery>

namespace ArtworkPersistence {
// The statement is supplied by a source model, with path and identity placeholders.
// Retain the complete batch on any failure so a later flush can retry atomically.
inline bool flush(QSqlDatabase& database, const QString& statement,
                  QHash<QString, QString>& pending) {
  if (pending.isEmpty())
    return true;
  if (!database.isOpen() || !database.transaction())
    return false;
  QSqlQuery query(database);
  if (!query.prepare(statement)) {
    database.rollback();
    return false;
  }
  for (auto item = pending.cbegin(); item != pending.cend(); ++item) {
    query.bindValue(0, item.value());
    query.bindValue(1, item.key());
    if (!query.exec()) {
      database.rollback();
      return false;
    }
  }
  if (!database.commit()) {
    database.rollback();
    return false;
  }
  pending.clear();
  return true;
}
} // namespace ArtworkPersistence
