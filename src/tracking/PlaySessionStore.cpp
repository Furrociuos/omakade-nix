#include "tracking/PlaySessionStore.h"

#include "tracking/SessionDatabase.h"

#include <QDateTime>
#include <QTimer>
#include <QUuid>

namespace {
constexpr int kRefreshIntervalMs = 20000;
} // namespace

PlaySessionStore::PlaySessionStore(const QString& databasePath, QObject* parent)
    : QObject(parent),
      m_connectionName(QStringLiteral("omakade-sessions-%1").arg(QUuid::createUuid().toString())) {
  m_valid = SessionDatabase::open(m_database, databasePath, m_connectionName);
  refresh();
  m_baselines = SessionDatabase::baselinesByPath(m_database);
  m_refreshTimer = new QTimer(this);
  m_refreshTimer->setInterval(kRefreshIntervalMs);
  connect(m_refreshTimer, &QTimer::timeout, this, &PlaySessionStore::refresh);
  m_refreshTimer->start();
}

PlaySessionStore::~PlaySessionStore() {
  m_refreshTimer->stop();
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

bool PlaySessionStore::enabled() const { return m_enabled; }

void PlaySessionStore::setEnabled(bool value) {
  if (m_enabled == value) {
    return;
  }
  m_enabled = value;
  emit enabledChanged();
  refresh();
}

void PlaySessionStore::captureBaseline(const QString& gamePath, qint64 importedSeconds) {
  if (!m_valid || !m_enabled || m_baselines.contains(gamePath)) {
    return;
  }
  SessionDatabase::captureBaseline(m_database, gamePath, importedSeconds,
                                   QDateTime::currentSecsSinceEpoch());
  m_baselines = SessionDatabase::baselinesByPath(m_database);
}

qint64 PlaySessionStore::displaySeconds(const QString& gamePath, qint64 importedSeconds) const {
  if (!m_valid || !m_enabled || gamePath.isEmpty()) {
    return importedSeconds;
  }
  return merge(importedSeconds, m_baselines.value(gamePath, 0),
               m_trackedSeconds.value(gamePath, 0));
}

qint64 PlaySessionStore::sessionLastPlayed(const QString& gamePath) const {
  if (!m_valid || !m_enabled || gamePath.isEmpty()) {
    return 0;
  }
  return m_lastPlayed.value(gamePath, 0);
}

qint64 PlaySessionStore::merge(qint64 importedSeconds, qint64 baselineSeconds,
                               qint64 trackedSeconds) {
  return qMax(importedSeconds, baselineSeconds + trackedSeconds);
}

qint64 PlaySessionStore::displayedSeconds(const PlaySessionStore* store, const QString& gamePath,
                                          qint64 importedSeconds) {
  return store == nullptr ? importedSeconds : store->displaySeconds(gamePath, importedSeconds);
}

qint64 PlaySessionStore::displayedLastPlayed(const PlaySessionStore* store, const QString& gamePath,
                                             qint64 importedLastPlayed) {
  return store == nullptr ? importedLastPlayed
                          : qMax(importedLastPlayed, store->sessionLastPlayed(gamePath));
}

void PlaySessionStore::refresh() {
  if (!m_valid) {
    return;
  }
  const QHash<QString, qint64> tracked =
      m_enabled ? SessionDatabase::trackedSecondsByPath(m_database) : QHash<QString, qint64>{};
  const QHash<QString, qint64> lastPlayed =
      m_enabled ? SessionDatabase::lastPlayedByPath(m_database) : QHash<QString, qint64>{};
  if (tracked == m_trackedSeconds && lastPlayed == m_lastPlayed) {
    return;
  }
  m_trackedSeconds = tracked;
  m_lastPlayed = lastPlayed;
  emit totalsChanged();
}
