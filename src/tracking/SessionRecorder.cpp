#include "tracking/SessionRecorder.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QSet>

#include <utility>

namespace {
constexpr qint64 kDefaultFlushIntervalMs = 30000;

QElapsedTimer& defaultClock() {
  static QElapsedTimer clock;
  if (!clock.isValid()) {
    clock.start();
  }
  return clock;
}
} // namespace

SessionRecorder::SessionRecorder(const QSqlDatabase& database,
                                 const std::function<qint64()>& elapsedMs)
    : m_database(database), m_elapsedMs(elapsedMs) {
  if (!m_elapsedMs) {
    m_elapsedMs = [] { return defaultClock().elapsed(); };
  }
}

SessionRecorder::~SessionRecorder() = default;

void SessionRecorder::setFlushIntervalMs(int intervalMs) {
  if (intervalMs > 0) {
    m_flushIntervalMs = intervalMs;
  }
}

QString SessionRecorder::keyFor(const SessionMatch& match) const {
  return QStringLiteral("%1:%2").arg(match.pid).arg(match.procStart);
}

void SessionRecorder::recover(const QVector<ProcessSnapshot>& processes,
                              const ProcessProfileSet& profiles, qint64 nowWall) {
  const QVector<SessionDatabase::SessionRow> survivors =
      SessionDatabase::reconcileOpenSessions(m_database, &ProcFs::processAlive);
  if (survivors.isEmpty()) {
    return;
  }
  const QVector<SessionMatch> matches = ProcessMatcher::match(processes, profiles);
  const qint64 nowMs = m_elapsedMs();
  for (const SessionDatabase::SessionRow& row : survivors) {
    const SessionMatch* adopted = nullptr;
    for (const SessionMatch& match : matches) {
      if (match.pid == row.pid && match.procStart == row.procStart &&
          match.gamePath == row.gamePath) {
        adopted = &match;
        break;
      }
    }
    if (adopted != nullptr) {
      ActiveSession session;
      session.id = row.id;
      session.gamePath = row.gamePath;
      session.emulator = adopted->emulator;
      session.rescanSource = adopted->rescanSource;
      session.elapsedMs = 0;
      session.markMs = nowMs;
      session.lastFlushMs = nowMs;
      m_active.insert(QStringLiteral("%1:%2").arg(row.pid).arg(row.procStart), session);
      continue;
    }
    // The process lives but no longer runs the same game; keep the recorded time
    // and stop where the last heartbeat proved it was still playing.
    SessionDatabase::endSession(m_database, row.id, nowWall, row.seconds);
  }
}

void SessionRecorder::flush(ActiveSession& session, qint64 nowMs, qint64 nowWall) {
  SessionDatabase::updateProgress(m_database, session.id, session.elapsedMs / 1000, nowWall);
  session.lastFlushMs = nowMs;
}

QHash<QString, SessionRecorder::ActiveSession>::Iterator
SessionRecorder::closeSession(QHash<QString, ActiveSession>::Iterator session, qint64 nowMs,
                              qint64 nowWall) {
  const qint64 totalMs = session->elapsedMs + (nowMs - session->markMs);
  SessionDatabase::endSession(m_database, session->id, nowWall, totalMs / 1000);
  if (!session->rescanSource.isEmpty() && !m_rescanRequests.contains(session->rescanSource)) {
    m_rescanRequests.append(session->rescanSource);
  }
  return m_active.erase(session);
}

void SessionRecorder::sync(const QVector<SessionMatch>& matches, qint64 nowWall) {
  const qint64 nowMs = m_elapsedMs();
  QSet<QString> matched;
  matched.reserve(matches.size());
  for (const SessionMatch& match : matches) {
    const QString key = keyFor(match);
    matched.insert(key);
    auto existing = m_active.find(key);
    if (existing == m_active.end()) {
      const qint64 id = SessionDatabase::beginSession(m_database, match.gamePath, match.emulator,
                                                      nowWall, match.pid, match.procStart);
      if (id <= 0) {
        continue;
      }
      ActiveSession session;
      session.id = id;
      session.gamePath = match.gamePath;
      session.emulator = match.emulator;
      session.rescanSource = match.rescanSource;
      session.markMs = nowMs;
      session.lastFlushMs = nowMs;
      m_active.insert(key, session);
      continue;
    }
    existing->elapsedMs += nowMs - existing->markMs;
    existing->markMs = nowMs;
    if (nowMs - existing->lastFlushMs >= m_flushIntervalMs) {
      flush(*existing, nowMs, nowWall);
    }
  }
  for (auto it = m_active.begin(); it != m_active.end();) {
    if (!matched.contains(it.key())) {
      it = closeSession(it, nowMs, nowWall);
    } else {
      ++it;
    }
  }
}

void SessionRecorder::endAll(qint64 nowWall) {
  const qint64 nowMs = m_elapsedMs();
  for (auto it = m_active.begin(); it != m_active.end();) {
    it = closeSession(it, nowMs, nowWall);
  }
  SessionDatabase::endAllSessions(m_database, nowWall);
}

QStringList SessionRecorder::takeRescanRequests() { return std::move(m_rescanRequests); }
