#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

// Bounded, durable journal for session operations the database refused.
//
// The recorder already tracks sessions in memory, but a refused insert or close lived
// only in a 64 entry queue that a recorder exit lost. The journal gives that work a
// durable home next to the library database. It is intentionally small: it stores one
// record per accepted operation, fsyncs before the operation is acknowledged, and is
// replayed and compacted in bounded work.
//
// It cannot promise zero data loss when no durable destination accepts a write: a full or
// read-only filesystem defeats the database and the journal together. What it guarantees
// is that an operation the journal accepted survives a crash, and that an operation it
// refused is reported rather than silently dropped.
class SessionJournal final {
public:
  // One durable session operation. The stable key is minted before the first database
  // write, so replaying the same operation can never create a second session.
  struct Operation {
    QString key;
    QString gamePath;
    QString source;
    qint64 startedAt = 0;
    qint64 endedAt = 0;
    qint64 seconds = 0;
    qint64 pid = 0;
    qint64 procStart = -1;
    // The journal generation observed for this game when the record was written, plus the
    // global generation at the same moment. Replay drops records older than the current
    // values, so removed history cannot be resurrected by an old journal.
    qint64 gameGeneration = 0;
    qint64 globalGeneration = 0;
  };

  explicit SessionJournal(QString path);
  ~SessionJournal();

  // Creates and opens the journal. A missing or corrupt file is replaced with a fresh
  // one, and the corrupt copy is preserved once beside it for diagnostics. Returns false
  // only when the journal storage itself is unavailable.
  bool open();
  void close();

  [[nodiscard]] bool available() const { return m_available; }
  // True when the journal refused a record because it reached its cap. Accepted records
  // are never evicted to make room.
  [[nodiscard]] bool full() const { return m_full; }
  // True when a corrupt journal was set aside during open, so the recorder can note it
  // once without printing private data.
  [[nodiscard]] bool recoveredCorrupt() const { return m_recoveredCorrupt; }
  [[nodiscard]] QString path() const { return m_path; }
  [[nodiscard]] int recordCount() const;

  // Persists one operation and fsyncs it before returning true. Returns false when the
  // journal is unavailable or at capacity.
  bool append(const Operation& operation);

  // The pending operations, latest record per key in append order, skipping any key that
  // was acknowledged and any record older than currentGameGeneration (per game). Only the
  // first maxRecords are returned, so replay work is bounded.
  [[nodiscard]] QVector<Operation> pending(int maxRecords,
                                           const std::function<qint64(const QString&)>&
                                               currentGameGeneration,
                                           qint64 currentGlobalGeneration) const;

  // Rewrites the journal after a replay: keys that were committed are dropped, records
  // from generations that are no longer current are dropped, and the rest are kept. This
  // also releases a full journal. Returns false on an I/O failure.
  bool compact(const QStringList& committedKeys,
               const std::function<qint64(const QString&)>& currentGameGeneration,
               qint64 currentGlobalGeneration);

private:
  struct Record {
    Operation operation;
    bool isAck = false;
    QString ackKey;
  };

  [[nodiscard]] bool readAll(QVector<Record>& records) const;
  [[nodiscard]] bool writeRecords(const QVector<Record>& records);
  [[nodiscard]] bool ensureDirectory() const;

  QString m_path;
  bool m_available = false;
  bool m_full = false;
  bool m_recoveredCorrupt = false;
  int m_records = 0;
};
