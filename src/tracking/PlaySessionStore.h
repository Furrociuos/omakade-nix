#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <QSqlDatabase>

class QTimer;

// Aggregates the sessions recorded by omakade-sessiond and merges them with the
// playtime each source imports from its own emulator. The displayed total is
// max(imported, baseline + tracked): the baseline is the imported playtime the
// first time a game was seen with sessions enabled and is never raised, so the
// emulator's own growing counter and the recorded sessions never double count.
// When the daemon is off, or a period ran without it, the imported value is
// larger and simply wins.
class PlaySessionStore final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
  explicit PlaySessionStore(const QString& databasePath, QObject* parent = nullptr);

  [[nodiscard]] bool enabled() const;
  void setEnabled(bool value);

  // Models report the playtime their emulator imports so the first sighting is
  // remembered. Later sightings are ignored by design.
  void captureBaseline(const QString& gamePath, qint64 importedSeconds);

  [[nodiscard]] qint64 displaySeconds(const QString& gamePath, qint64 importedSeconds) const;
  [[nodiscard]] qint64 sessionLastPlayed(const QString& gamePath) const;

  [[nodiscard]] static qint64 merge(qint64 importedSeconds, qint64 baselineSeconds,
                                    qint64 trackedSeconds);

  // Model helpers: they read through the store when present and fall back to the
  // imported values otherwise, so sources built without a store behave exactly
  // as they did before session tracking existed.
  [[nodiscard]] static qint64 displayedSeconds(const PlaySessionStore* store,
                                               const QString& gamePath, qint64 importedSeconds);
  [[nodiscard]] static qint64 displayedLastPlayed(const PlaySessionStore* store,
                                                  const QString& gamePath,
                                                  qint64 importedLastPlayed);

signals:
  void enabledChanged();
  void totalsChanged();

private:
  void refresh();

  QSqlDatabase m_database;
  QString m_connectionName;
  bool m_enabled = true;
  bool m_valid = false;
  QHash<QString, qint64> m_trackedSeconds;
  QHash<QString, qint64> m_baselines;
  QHash<QString, qint64> m_lastPlayed;
  QTimer* m_refreshTimer = nullptr;
};
