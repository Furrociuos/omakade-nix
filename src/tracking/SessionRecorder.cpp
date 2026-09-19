#include "tracking/SessionRecorder.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace {
constexpr qint64 kDefaultFlushIntervalMs = 30000;

// How many polls a title-attributed session may go unresolved before it is closed. The
// poll is 5 seconds, so this tolerates a save dialog, a menu or a slow compositor
// answer without inventing a boundary, while still ending the session promptly when the
// game really has gone.
constexpr int kTitleGracePolls = 3;

QElapsedTimer& defaultClock() {
  static QElapsedTimer clock;
  if (!clock.isValid()) {
    clock.start();
  }
  return clock;
}
} // namespace

SessionRecorder::SessionRecorder(const QSqlDatabase& database,
                                 const std::function<qint64()>& elapsedMs,
                                 const QString& journalPath)
    : m_database(database), m_elapsedMs(elapsedMs) {
  if (!m_elapsedMs) {
    m_elapsedMs = [] { return defaultClock().elapsed(); };
  }
  // The journal lives beside the database file. An in-memory database has no file to sit
  // next to, and a test that wants a journal passes an explicit path.
  QString path = journalPath;
  if (path.isEmpty()) {
    const QString name = database.databaseName();
    if (!name.isEmpty() && name != QStringLiteral(":memory:")) {
      path = name + QStringLiteral(".journal");
    }
  }
  if (!path.isEmpty()) {
    m_journal = std::make_unique<SessionJournal>(path);
    if (!m_journal->open()) {
      // Storage that cannot hold the journal is reported through journalAvailable; the
      // in-memory queue and the database keep working exactly as they did.
      m_journal.reset();
    } else if (m_journal->recoveredCorrupt()) {
      m_journalCorrupt = true;
    }
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
  Q_UNUSED(nowWall);
  // Replay anything a previous recorder could not write before adopting the sessions that
  // are still open, so recovered history and live sessions are reconciled against the
  // completed journal rather than the other way round.
  replayJournal();
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
      // A window-title match has no recorded process identity, so it can never
      // be adopted after a restart and is only ever closed by a later poll.
      if (match.procStart <= 0) {
        continue;
      }
      if (match.pid == row.pid && match.procStart == row.procStart &&
          match.gamePath == row.gamePath) {
        adopted = &match;
        break;
      }
    }
    if (adopted != nullptr) {
      ActiveSession session;
      session.id = row.id;
      session.sessionKey = row.sessionKey;
      session.startedAt = row.startedAt;
      session.pid = row.pid;
      session.procStart = row.procStart;
      session.gamePath = row.gamePath;
      session.emulator = adopted->emulator;
      session.rescanSource = adopted->rescanSource;
      session.elapsedMs = row.seconds * 1000;
      session.markMs = nowMs;
      session.lastFlushMs = nowMs;
      m_active.insert(QStringLiteral("%1:%2").arg(row.pid).arg(row.procStart), session);
      continue;
    }
    // The process lives but no longer runs the same game; keep the recorded time
    // and stop where the last heartbeat proved it was still playing.
    if (!SessionDatabase::endSession(m_database, row.id, qMax(row.startedAt, row.heartbeatAt),
                                     row.seconds)) {
      PendingClose pending;
      pending.id = row.id;
      pending.key = row.sessionKey;
      pending.endedAt = qMax(row.startedAt, row.heartbeatAt);
      pending.seconds = row.seconds;
      pending.startedAt = row.startedAt;
      pending.pid = row.pid;
      pending.procStart = row.procStart;
      pending.gamePath = row.gamePath;
      pending.source = row.source;
      queuePending(pending);
      m_lastCloseAttemptMs = nowMs;
      m_storageFailure = true;
    }
  }
}

void SessionRecorder::flush(ActiveSession& session, qint64 nowMs, qint64 nowWall) {
  // A session that has no row yet has nothing to update: its accumulated time is written
  // by retryInsert the moment storage accepts the insert, or by the queued record if the
  // game exits first.
  if (session.id > 0 &&
      !SessionDatabase::updateProgress(m_database, session.id, session.elapsedMs / 1000,
                                       nowWall)) {
    m_storageFailure = true;
  }
  session.lastFlushMs = nowMs;
}

void SessionRecorder::retryInsert(ActiveSession& session, qint64 nowMs, qint64 nowWall) {
  if (session.id > 0) {
    return;
  }
  if (session.sessionKey.isEmpty()) {
    session.sessionKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
  }
  session.id = SessionDatabase::beginSession(m_database, session.gamePath, session.emulator,
                                             session.startedAt, session.pid, session.procStart,
                                             session.sessionKey);
  if (session.id <= 0) {
    m_storageFailure = true;
    return;
  }
  // The row starts at the original start time, so the play that happened while storage was
  // refusing it is written now rather than waiting for the next interval.
  flush(session, nowMs, nowWall);
}

QHash<QString, SessionRecorder::ActiveSession>::Iterator
SessionRecorder::closeSession(QHash<QString, ActiveSession>::Iterator session, qint64 nowMs,
                              qint64 nowWall) {
  // A paused session stopped billing at the last poll, so the span since then is
  // not play time either.
  const qint64 totalMs =
      session->elapsedMs + (session->paused ? 0 : nowMs - session->markMs);
  if (session->sessionKey.isEmpty()) {
    session->sessionKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
  }
  if (session->id > 0) {
    if (!SessionDatabase::endSession(m_database, session->id, nowWall, totalMs / 1000)) {
      PendingClose pending;
      pending.id = session->id;
      pending.key = session->sessionKey;
      pending.endedAt = nowWall;
      pending.seconds = totalMs / 1000;
      pending.startedAt = session->startedAt;
      pending.pid = session->pid;
      pending.procStart = session->procStart;
      pending.gamePath = session->gamePath;
      pending.source = session->emulator;
      queuePending(pending);
      m_lastCloseAttemptMs = nowMs;
      m_storageFailure = true;
    }
  } else {
    // The session never got a row at all. Queue the whole record, so storage recovering
    // after the game has already exited still writes the play that was observed.
    PendingClose pending;
    pending.key = session->sessionKey;
    pending.endedAt = nowWall;
    pending.seconds = totalMs / 1000;
    pending.startedAt = session->startedAt;
    pending.pid = session->pid;
    pending.procStart = session->procStart;
    pending.gamePath = session->gamePath;
    pending.source = session->emulator;
    queuePending(pending);
    m_lastCloseAttemptMs = nowMs;
    m_storageFailure = true;
  }
  if (!session->rescanSource.isEmpty() && !m_rescanRequests.contains(session->rescanSource)) {
    m_rescanRequests.append(session->rescanSource);
  }
  return m_active.erase(session);
}

void SessionRecorder::queuePending(PendingClose pending) {
  m_pendingCloses.append(pending);
  journalPending(pending);
  trimPendingCloses();
}

void SessionRecorder::journalPending(const PendingClose& pending) {
  if (m_journal == nullptr || !m_journal->available() || pending.key.isEmpty()) {
    return;
  }
  SessionJournal::Operation operation;
  operation.key = pending.key;
  operation.gamePath = pending.gamePath;
  operation.source = pending.source;
  operation.startedAt = pending.startedAt;
  operation.endedAt = pending.endedAt;
  operation.seconds = pending.seconds;
  operation.pid = pending.pid;
  operation.procStart = pending.procStart;
  if (!pending.gamePath.isEmpty()) {
    operation.gameGeneration = SessionDatabase::gameGeneration(m_database, pending.gamePath);
  }
  operation.globalGeneration = SessionDatabase::journalGeneration(m_database);
  if (!m_journal->append(operation)) {
    // The record was refused, not evicted: report it so the interface can warn.
    m_journalCapacity = true;
    m_storageFailure = true;
    return;
  }
  if (!m_journal->full()) {
    m_journalCapacity = false;
  }
}

void SessionRecorder::replayJournal() {
  if (m_journal == nullptr || !m_journal->available()) {
    return;
  }
  const qint64 globalGeneration = SessionDatabase::journalGeneration(m_database);
  const auto gameGeneration = [this](const QString& path) {
    return SessionDatabase::gameGeneration(m_database, path);
  };
  const QVector<SessionJournal::Operation> operations =
      m_journal->pending(512, gameGeneration, globalGeneration);
  QStringList committed;
  for (const SessionJournal::Operation& operation : operations) {
    // The stable key makes this idempotent, so a record that was committed before a crash
    // and one that never reached the database both resolve to exactly one session.
    if (SessionDatabase::insertClosedSession(m_database, operation.gamePath, operation.source,
                                             operation.startedAt, operation.endedAt,
                                             operation.seconds, operation.pid, operation.procStart,
                                             operation.key)) {
      committed.append(operation.key);
      continue;
    }
    m_storageFailure = true;
    // Keep it in the in-memory queue so a storage recovery in this run still writes it.
    PendingClose pending;
    pending.key = operation.key;
    pending.endedAt = operation.endedAt;
    pending.seconds = operation.seconds;
    pending.startedAt = operation.startedAt;
    pending.pid = operation.pid;
    pending.procStart = operation.procStart;
    pending.gamePath = operation.gamePath;
    pending.source = operation.source;
    bool queued = false;
    for (const PendingClose& existing : m_pendingCloses) {
      if (existing.key == pending.key) {
        queued = true;
        break;
      }
    }
    if (!queued) {
      m_pendingCloses.append(pending);
    }
  }
  m_journal->compact(committed, gameGeneration, globalGeneration);
  if (!m_journal->full()) {
    m_journalCapacity = false;
  }
  trimPendingCloses();
}

void SessionRecorder::trimPendingCloses() {
  // A storage failure that lasts (a full disk, a read-only database) would otherwise
  // queue one entry per session for the life of the daemon, and every poll would retry
  // all of them. The oldest are dropped once the queue is implausibly long. This is the
  // in-memory retry queue only: the durable journal holds every accepted operation, so a
  // dropped queue entry is delayed until the next recorder start, not lost. One bound
  // covers both kinds of pending write.
  constexpr int kMaxPendingCloses = 64;
  while (m_pendingCloses.size() > kMaxPendingCloses) {
    m_pendingCloses.removeFirst();
  }
}

void SessionRecorder::retryClosed(qint64 nowMs) {
  if (m_pendingCloses.isEmpty() || nowMs - m_lastCloseAttemptMs < m_flushIntervalMs)
    return;
  m_lastCloseAttemptMs = nowMs;
  QStringList committed;
  for (qsizetype i = 0; i < m_pendingCloses.size();) {
    const auto pending = m_pendingCloses.at(i);
    // An entry with no row writes the session whole; one with a row only moves the
    // boundary the open session was already carrying. Both are keyed on the stable session
    // key, so a record that already reached the database is a no-op rather than a duplicate.
    const bool written =
        pending.id > 0
            ? SessionDatabase::endSession(m_database, pending.id, pending.endedAt,
                                          pending.seconds)
            : SessionDatabase::insertClosedSession(m_database, pending.gamePath, pending.source,
                                                   pending.startedAt, pending.endedAt,
                                                   pending.seconds, pending.pid,
                                                   pending.procStart, pending.key);
    if (written) {
      if (!pending.key.isEmpty()) {
        committed.append(pending.key);
      }
      m_pendingCloses.removeAt(i);
    } else {
      m_storageFailure = true;
      ++i;
    }
  }
  if (!committed.isEmpty() && m_journal != nullptr && m_journal->available()) {
    const qint64 globalGeneration = SessionDatabase::journalGeneration(m_database);
    const auto gameGeneration = [this](const QString& path) {
      return SessionDatabase::gameGeneration(m_database, path);
    };
    m_journal->compact(committed, gameGeneration, globalGeneration);
    if (!m_journal->full()) {
      m_journalCapacity = false;
    }
  }
}

void SessionRecorder::sync(const QVector<SessionMatch>& matches, qint64 nowWall,
                           const std::function<bool(qint64)>& unfocused) {
  const qint64 nowMs = m_elapsedMs();
  retryClosed(nowMs);
  const bool pause = m_pauseUnfocused && static_cast<bool>(unfocused);
  QSet<QString> matched;
  matched.reserve(matches.size());
  for (const SessionMatch& match : matches) {
    const QString key = keyFor(match);
    matched.insert(key);
    auto existing = m_active.find(key);
    // The same emulator process can report a different game on a later poll.
    if (existing != m_active.end() && existing->gamePath != match.gamePath) {
      closeSession(existing, nowMs, nowWall);
      existing = m_active.end();
    }
    if (existing == m_active.end()) {
      // The stable identity is minted before the first write, so a refused insert and a
      // later replay both resolve to this one session.
      const QString sessionKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
      const qint64 id = SessionDatabase::beginSession(m_database, match.gamePath, match.emulator,
                                                      nowWall, match.pid, match.procStart,
                                                      sessionKey);
      // A refused insert used to drop the match on the spot, so a session that could not
      // even be created went unrecorded while one that got a row and then failed to close
      // was queued and retried. The game is running either way: the session is tracked with
      // no row yet, which bills its playtime and lets the row be written with its original
      // start once storage accepts writes, or queued as a finished session if the game
      // exits first.
      if (id <= 0) {
        m_storageFailure = true;
      }
      ActiveSession session;
      session.id = id;
      session.sessionKey = sessionKey;
      session.pid = match.pid;
      session.procStart = match.procStart;
      session.gamePath = match.gamePath;
      session.emulator = match.emulator;
      session.rescanSource = match.rescanSource;
      session.startedAt = nowWall;
      session.markMs = nowMs;
      session.lastFlushMs = nowMs;
      // A title match carries no verified process identity (procStart <= 0), which is
      // exactly the case whose title can flicker.
      session.titleMatched = match.procStart <= 0;
      m_active.insert(key, session);
      continue;
    }
    // A session whose row storage has not accepted is retried before it is billed, so the
    // row it eventually writes carries the play that has already happened.
    retryInsert(*existing, nowMs, nowWall);
    // Time is only billed while the game holds the compositor's focus. The mark
    // moves forward either way, so a pause spans exactly the polls where the game
    // was unfocused and is never back-dated when focus returns.
    existing->paused = pause && unfocused(match.pid);
    if (!existing->paused) {
      existing->elapsedMs += nowMs - existing->markMs;
    }
    existing->markMs = nowMs;
    // The heartbeat keeps moving so a crash during a long pause ends the row at the
    // last poll instead of at a boundary reached after the game was put aside.
    if (nowMs - existing->lastFlushMs >= m_flushIntervalMs) {
      flush(*existing, nowMs, nowWall);
    }
  }
  for (auto it = m_active.begin(); it != m_active.end();) {
    if (matched.contains(it.key())) {
      it->missedPolls = 0;
      ++it;
      continue;
    }
    // A title-attributed session whose title did not resolve this poll is tolerated for
    // a short while before it is closed. The title is the only thing identifying the
    // game, and it is not stable: an emulator's window can be a save dialog or a menu
    // for a poll or two, and one missed poll used to end the session and start a new
    // row, splitting a single play session into fragments and rewriting the emulator's
    // last-played at every split.
    //
    // The grace is bounded by the process, not by the poll count alone: while the
    // recorded process is still alive the game really is running, so the time is billed
    // and the session continues. The moment it is gone the session closes, exactly as a
    // verified match would, so no time after the exit is ever billed.
    if (it->titleMatched && ProcFs::processRunning(it->pid) &&
        ++it->missedPolls <= kTitleGracePolls) {
      retryInsert(*it, nowMs, nowWall);
      if (!it->paused) {
        it->elapsedMs += nowMs - it->markMs;
      }
      it->markMs = nowMs;
      if (nowMs - it->lastFlushMs >= m_flushIntervalMs) {
        flush(*it, nowMs, nowWall);
      }
      ++it;
      continue;
    }
    it = closeSession(it, nowMs, nowWall);
  }
}

void SessionRecorder::endAll(qint64 nowWall) {
  const qint64 nowMs = m_elapsedMs();
  retryClosed(nowMs);
  for (auto it = m_active.begin(); it != m_active.end();) {
    it = closeSession(it, nowMs, nowWall);
  }
  // Pending closures retain their original boundary. A blanket close must not
  // replace it with a later toggle/poll time while storage is unavailable.
  if (m_pendingCloses.isEmpty() && !SessionDatabase::endAllSessions(m_database, nowWall))
    m_storageFailure = true;
}

QStringList SessionRecorder::takeRescanRequests() { return std::move(m_rescanRequests); }

QVector<SessionRecorder::ActiveInfo> SessionRecorder::activeSessions() const {
  QVector<ActiveInfo> sessions;
  sessions.reserve(m_active.size());
  for (auto session = m_active.cbegin(); session != m_active.cend(); ++session) {
    sessions.append(ActiveInfo{.gamePath = session->gamePath,
                               .emulator = session->emulator,
                               .startedAt = session->startedAt,
                               .elapsedMs = session->elapsedMs,
                               .paused = session->paused});
  }
  // A long-running game is the one a player is watching, so it leads the list and
  // keeps the presence stable when a second game starts.
  std::sort(sessions.begin(), sessions.end(),
            [](const ActiveInfo& left, const ActiveInfo& right) {
              return left.startedAt < right.startedAt;
            });
  return sessions;
}
