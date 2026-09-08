#pragma once

#include <QHash>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include <functional>

// Session storage shared by Omakade and the omakade-sessiond recorder. Both open
// the same library database, so every function takes the caller's connection and
// the schema is created idempotently.
namespace SessionDatabase {

// One recorded play session. Sessions stay open with ended_at = 0 while the game
// process lives; seconds is the wall time accumulated so far and is flushed
// periodically so a crash loses at most one flush interval.
struct SessionRow {
  qint64 id = 0;
  QString gamePath;
  QString source;
  qint64 startedAt = 0;
  qint64 endedAt = 0;
  qint64 seconds = 0;
  qint64 pid = 0;
  qint64 procStart = -1;
  qint64 heartbeatAt = 0;
};

[[nodiscard]] QString defaultDatabasePath();
[[nodiscard]] QString defaultConfigPath();
[[nodiscard]] QString appServerName();

// Opens (or reuses) a tuned connection to the library database and creates the
// session tables. Returns false only when the database cannot be opened.
bool open(QSqlDatabase& database, const QString& path, const QString& connectionName);
void ensureSchema(QSqlDatabase& database);

QVector<SessionRow> openSessions(QSqlDatabase& database);
qint64 beginSession(QSqlDatabase& database, const QString& gamePath, const QString& source,
                    qint64 startedAt, qint64 pid, qint64 procStart);
void updateProgress(QSqlDatabase& database, qint64 id, qint64 seconds, qint64 heartbeatAt);
void endSession(QSqlDatabase& database, qint64 id, qint64 endedAt, qint64 seconds);
void endAllSessions(QSqlDatabase& database, qint64 endedAt);

// Closes open sessions whose tracked process is gone, using the last heartbeat as
// the end time so a dead daemon never invents play time. Returns the survivors.
QVector<SessionRow>
reconcileOpenSessions(QSqlDatabase& database,
                      const std::function<bool(qint64 pid, qint64 procStart)>& processAlive);

[[nodiscard]] QHash<QString, qint64> trackedSecondsByPath(QSqlDatabase& database);
[[nodiscard]] QHash<QString, qint64> lastPlayedByPath(QSqlDatabase& database);

// Capture once, including zero. Subtract already observed time conservatively:
// a late first import may already contain those sessions. Existing baselines
// are never rewritten because historical overlap cannot be inferred reliably.
void captureBaseline(QSqlDatabase& database, const QString& gamePath, qint64 importedSeconds,
                     qint64 capturedAt);
[[nodiscard]] QHash<QString, qint64> baselinesByPath(QSqlDatabase& database);

} // namespace SessionDatabase
