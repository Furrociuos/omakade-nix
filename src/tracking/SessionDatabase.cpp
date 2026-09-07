#include "tracking/SessionDatabase.h"

#include "library/DatabaseTuning.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QtGlobal>

#include <functional>
#include <unistd.h>

namespace {
constexpr int kCurrentSchema = 1;
} // namespace

namespace SessionDatabase {

QString defaultDatabasePath() {
  const QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                            QStringLiteral("/omakade");
  return directory + QStringLiteral("/library.sqlite3");
}

QString defaultConfigPath() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
         QStringLiteral("/omakade/config.toml");
}

QString appServerName() { return QStringLiteral("omakade-%1").arg(getuid()); }

bool open(QSqlDatabase& database, const QString& path, const QString& connectionName) {
  if (path != QStringLiteral(":memory:")) {
    QDir().mkpath(QFileInfo(path).absolutePath());
  }
  database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
  database.setDatabaseName(path);
  if (!openTunedDatabase(database)) {
    return false;
  }
  ensureSchema(database);
  return true;
}

void ensureSchema(QSqlDatabase& database) {
  QSqlQuery query(database);
  query.exec(QStringLiteral(
      "CREATE TABLE IF NOT EXISTS play_sessions (id INTEGER PRIMARY KEY, game_path TEXT NOT "
      "NULL, source TEXT NOT NULL DEFAULT '', started_at INTEGER NOT NULL, ended_at INTEGER NOT "
      "NULL DEFAULT 0, seconds INTEGER NOT NULL DEFAULT 0, pid INTEGER NOT NULL DEFAULT 0, "
      "proc_start INTEGER NOT NULL DEFAULT -1, heartbeat_at INTEGER NOT NULL DEFAULT 0)"));
  query.exec(
      QStringLiteral("CREATE INDEX IF NOT EXISTS play_sessions_path ON play_sessions(game_path)"));
  query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS play_baselines (game_path TEXT PRIMARY "
                            "KEY, baseline_seconds INTEGER NOT NULL DEFAULT 0, captured_at "
                            "INTEGER NOT NULL, schema INTEGER NOT NULL DEFAULT %1)")
                 .arg(kCurrentSchema));
}

QVector<SessionRow> openSessions(QSqlDatabase& database) {
  QVector<SessionRow> rows;
  QSqlQuery query(database);
  if (!query.exec(QStringLiteral(
          "SELECT id, game_path, source, started_at, ended_at, seconds, pid, proc_start, "
          "heartbeat_at FROM play_sessions WHERE ended_at = 0 ORDER BY id"))) {
    return rows;
  }
  while (query.next()) {
    rows.append({.id = query.value(0).toLongLong(),
                 .gamePath = query.value(1).toString(),
                 .source = query.value(2).toString(),
                 .startedAt = query.value(3).toLongLong(),
                 .endedAt = query.value(4).toLongLong(),
                 .seconds = query.value(5).toLongLong(),
                 .pid = query.value(6).toLongLong(),
                 .procStart = query.value(7).toLongLong(),
                 .heartbeatAt = query.value(8).toLongLong()});
  }
  return rows;
}

qint64 beginSession(QSqlDatabase& database, const QString& gamePath, const QString& source,
                    qint64 startedAt, qint64 pid, qint64 procStart) {
  QSqlQuery query(database);
  query.prepare(QStringLiteral("INSERT INTO play_sessions(game_path, source, started_at, pid, "
                               "proc_start, heartbeat_at) VALUES(?, ?, ?, ?, ?, ?)"));
  query.addBindValue(gamePath);
  query.addBindValue(source);
  query.addBindValue(startedAt);
  query.addBindValue(pid);
  query.addBindValue(procStart);
  query.addBindValue(startedAt);
  if (!query.exec()) {
    return 0;
  }
  return query.lastInsertId().toLongLong();
}

void updateProgress(QSqlDatabase& database, qint64 id, qint64 seconds, qint64 heartbeatAt) {
  QSqlQuery query(database);
  query.prepare(
      QStringLiteral("UPDATE play_sessions SET seconds = ?, heartbeat_at = ? WHERE id = ?"));
  query.addBindValue(seconds);
  query.addBindValue(heartbeatAt);
  query.addBindValue(id);
  query.exec();
}

void endSession(QSqlDatabase& database, qint64 id, qint64 endedAt, qint64 seconds) {
  QSqlQuery query(database);
  query.prepare(QStringLiteral("UPDATE play_sessions SET ended_at = ?, seconds = ? WHERE id = ?"));
  query.addBindValue(endedAt);
  query.addBindValue(seconds);
  query.addBindValue(id);
  query.exec();
}

void endAllSessions(QSqlDatabase& database, qint64 endedAt) {
  QSqlQuery query(database);
  query.prepare(QStringLiteral(
      "UPDATE play_sessions SET ended_at = ?, seconds = CASE WHEN heartbeat_at > started_at "
      "THEN seconds ELSE 0 END WHERE ended_at = 0"));
  query.addBindValue(endedAt);
  query.exec();
}

QVector<SessionRow> reconcileOpenSessions(QSqlDatabase& database,
                                          const std::function<bool(qint64, qint64)>& processAlive) {
  QVector<SessionRow> survivors;
  const QVector<SessionRow> open = openSessions(database);
  for (const SessionRow& row : open) {
    const bool alive =
        processAlive && processAlive(row.pid, row.procStart) && row.pid > 0 && row.procStart > 0;
    if (alive) {
      survivors.append(row);
      continue;
    }
    const qint64 endedAt = row.heartbeatAt > row.startedAt ? row.heartbeatAt : row.startedAt;
    endSession(database, row.id, endedAt, row.seconds);
  }
  return survivors;
}

QHash<QString, qint64> trackedSecondsByPath(QSqlDatabase& database) {
  QHash<QString, qint64> totals;
  QSqlQuery query(database);
  if (!query.exec(
          QStringLiteral("SELECT game_path, SUM(seconds) FROM play_sessions GROUP BY game_path"))) {
    return totals;
  }
  while (query.next()) {
    totals.insert(query.value(0).toString(), query.value(1).toLongLong());
  }
  return totals;
}

QHash<QString, qint64> lastPlayedByPath(QSqlDatabase& database) {
  QHash<QString, qint64> lastPlayed;
  QSqlQuery query(database);
  if (!query.exec(QStringLiteral(
          "SELECT game_path, MAX(COALESCE(NULLIF(ended_at, 0), started_at)) FROM play_sessions "
          "GROUP BY game_path"))) {
    return lastPlayed;
  }
  while (query.next()) {
    lastPlayed.insert(query.value(0).toString(), query.value(1).toLongLong());
  }
  return lastPlayed;
}

void captureBaseline(QSqlDatabase& database, const QString& gamePath, qint64 importedSeconds,
                     qint64 capturedAt) {
  if (gamePath.isEmpty() || importedSeconds <= 0) {
    return;
  }
  QSqlQuery query(database);
  query.prepare(QStringLiteral("INSERT OR IGNORE INTO play_baselines(game_path, baseline_seconds, "
                               "captured_at) VALUES(?, ?, ?)"));
  query.addBindValue(gamePath);
  query.addBindValue(importedSeconds);
  query.addBindValue(capturedAt);
  query.exec();
}

QHash<QString, qint64> baselinesByPath(QSqlDatabase& database) {
  QHash<QString, qint64> baselines;
  QSqlQuery query(database);
  if (!query.exec(QStringLiteral("SELECT game_path, baseline_seconds FROM play_baselines"))) {
    return baselines;
  }
  while (query.next()) {
    baselines.insert(query.value(0).toString(), query.value(1).toLongLong());
  }
  return baselines;
}

} // namespace SessionDatabase
