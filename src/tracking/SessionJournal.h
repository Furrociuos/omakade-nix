#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// Bounded, durable journal for session operations the database refused.
//
// The recorder tracks sessions in memory, but a refused insert, close or active checkpoint
// used to live only in a 64 entry queue that a recorder exit lost. The journal gives that
// work a durable home next to the library database. It fsyncs a record before the operation
// is acknowledged, keeps the verified prefix of a file whose last append was torn by a
// crash, and replays and compacts in bounded work.
//
// It cannot promise zero loss when no durable destination accepts a write: a full or
// read-only filesystem defeats the database and the journal together. What it guarantees is
// that an operation the journal accepted survives a crash, and that an operation it refused
// is reported rather than silently dropped.
class SessionJournal final {
public:
  // One durable operation. A finished session carries an end time; an active checkpoint
  // (open) carries the elapsed seconds observed so far so a killed recorder can reconstruct
  // the session up to its last checkpoint. The stable key is minted before the first
  // database write, so replaying the same operation can never create a second session.
  struct Operation {
    QString key;
    QString gamePath;
    QString source;
    qint64 startedAt = 0;
    qint64 endedAt = 0;
    qint64 seconds = 0;
    qint64 pid = 0;
    qint64 procStart = -1;
    // The database incarnation this record was written under, and the game's clear epoch at
    // that moment. A record from a different incarnation, or older than the current clear
    // epoch for its game, belongs to history the user has since removed or replaced.
    QString incarnation;
    qint64 clearEpoch = 0;
    bool open = false;
  };

  explicit SessionJournal(QString path);
  ~SessionJournal();

  // Creates and opens the journal. A file whose last append was torn is truncated back to its
  // verified prefix, so already accepted records still replay. A file with interior damage is
  // set aside once and replaced. Returns false only when the journal storage is unavailable.
  bool open();
  void close();

  [[nodiscard]] bool available() const { return m_available; }
  // True when the journal refused a record because it reached its cap. Accepted records are
  // never evicted to make room.
  [[nodiscard]] bool full() const { return m_full; }
  // True when a damaged journal was set aside during open.
  [[nodiscard]] bool recoveredCorrupt() const { return m_recoveredCorrupt; }
  // True when a torn final append was truncated away during open, keeping the prefix.
  [[nodiscard]] bool recoveredTornTail() const { return m_recoveredTornTail; }
  [[nodiscard]] QString path() const { return m_path; }
  [[nodiscard]] int recordCount() const;

  // Persists one operation and fsyncs it before returning true. Returns false when the
  // journal is unavailable or at capacity.
  bool append(const Operation& operation);

  // The pending operations, latest record per key in append order, skipping acknowledged
  // keys. Only the first maxRecords are returned, so replay work is bounded. Staleness is
  // decided by the caller at write time, inside the database transaction, not filtered here.
  [[nodiscard]] QVector<Operation> pending(int maxRecords) const;

  // Rewrites the journal after a replay. Keys the caller finished with, whether they were
  // written or dropped as stale, are removed; the rest are kept and a full journal is
  // released. Returns false on an I/O failure.
  bool compact(const QStringList& dropKeys);

  // Read-only probe for status display: how many records are pending, or -1 when a journal
  // file exists but is damaged. Missing means nothing to report. It never creates,
  // quarantines or rewrites the file, so the interface can poll it safely.
  [[nodiscard]] static int pendingCount(const QString& path);

private:
  struct Record {
    Operation operation;
    bool isAck = false;
    QString ackKey;
  };

  // Returns false only for interior corruption. A partial final frame sets tornTail and keeps
  // the valid prefix in records.
  [[nodiscard]] bool readAll(QVector<Record>& records, bool& tornTail) const;
  [[nodiscard]] bool writeRecords(const QVector<Record>& records);
  [[nodiscard]] bool ensureDirectory() const;

  QString m_path;
  bool m_available = false;
  bool m_full = false;
  bool m_recoveredCorrupt = false;
  bool m_recoveredTornTail = false;
  int m_records = 0;
};
