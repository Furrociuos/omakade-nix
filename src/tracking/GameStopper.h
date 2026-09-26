#pragma once

#include "tracking/GameStop.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

// Sending the signals a plan asks for (issue #53, slice 2). The three levers
// here are the ones that cover most sources: a tracked pid, a Wine prefix, and a
// flatpak app.
namespace GameStop {

enum class LeverResult {
  // The signal was delivered, or the tool reported success.
  Done,
  // Nothing was there to signal: the process is gone, or no wineserver is
  // running for that prefix. This is not an error.
  Missing,
  // The kernel or the tool refused, with a message worth showing.
  Failed,
  // The scope tool is unavailable; verified process signals may be used.
  Unavailable,
  // This pid must never be signalled, whatever the caller asked for.
  Refused,
};

// Every signal Omakade sends goes through this, so a test can substitute a fake
// and no test ever signals a real process.
class SignalSink {
public:
  SignalSink() = default;
  virtual ~SignalSink();
  SignalSink(const SignalSink&) = delete;
  SignalSink& operator=(const SignalSink&) = delete;

  [[nodiscard]] virtual LeverResult terminate(qint64 pid) = 0;
  [[nodiscard]] virtual LeverResult forceTerminate(qint64 pid) = 0;
  // WINEPREFIX=<prefix> wineserver -k, or -k9 when force is set.
  [[nodiscard]] virtual LeverResult stopWinePrefix(const QString& prefix, bool force) = 0;
  // flatpak kill <appId>.
  [[nodiscard]] virtual LeverResult stopFlatpakApp(const QString& appId) = 0;

  // Whatever the last lever's tool said, empty when it said nothing.
  [[nodiscard]] QString lastMessage() const { return m_lastMessage; }

protected:
  QString m_lastMessage;
};

// The real levers: ::kill, wineserver, flatpak. Nothing here guesses a pid or a
// name; it signals what it is handed.
//
// This is NOT the safety boundary. The start-time guard, the protected-pid and
// protected-binary checks, and the escalation order all live in Stopper, so
// anything that calls a sink directly is bypassing every one of them. Take a
// Target through Stopper, or carry the same checks yourself.
class SystemSignalSink final : public SignalSink {
public:
  [[nodiscard]] LeverResult terminate(qint64 pid) override;
  [[nodiscard]] LeverResult forceTerminate(qint64 pid) override;
  [[nodiscard]] LeverResult stopWinePrefix(const QString& prefix, bool force) override;
  [[nodiscard]] LeverResult stopFlatpakApp(const QString& appId) override;

  // Longest a lever waits for its tool before reporting a failure.
  static int commandTimeoutMs();
};

enum class OutcomeKind {
  Signalled,
  AlreadyGone,
  Refused,
  Failed,
};

struct TargetOutcome {
  OutcomeKind kind = OutcomeKind::Refused;
  Target target;
  // A sentence for the log and for the message the user sees.
  QString detail;
  // What the tool said, when it said anything.
  QString message;
};

struct StopReport {
  // True when this report came from the escalate phase.
  bool escalated = false;
  QVector<TargetOutcome> outcomes;

  [[nodiscard]] int signalled() const;
  [[nodiscard]] int refused() const;
  [[nodiscard]] int failed() const;
  // One line per target, in the order they were applied.
  [[nodiscard]] QStringList lines() const;
};

// Applies a plan through a sink. Two phases on purpose: begin() delivers the
// graceful step and escalate() forces whatever is still alive, so the wait
// between them belongs to the caller and nothing here blocks the interface. A
// test drives both phases with no timing at all. The sink belongs to the caller
// and has to outlive the stopper.
class Stopper {
public:
  // Reads the live process start time back, so a pid that was reused between
  // the two phases is never signalled. Defaults to ProcFs::processAlive.
  using LivenessFn = std::function<bool(qint64 pid, qint64 procStart)>;

  explicit Stopper(SignalSink* sink, Guards guards = {}, LivenessFn alive = {});
  ~Stopper();

  // The graceful step: SIGTERM for a process, wineserver -k for a prefix
  // (or verified prefix members when wineserver is unavailable), flatpak kill
  // for an app.
  StopReport begin(const Plan& plan);
  // The forced step, for whatever the caller's grace period left running.
  StopReport escalate();

  // True while the graceful step has targets that were signalled and could
  // still be running, so escalate() has something to do.
  [[nodiscard]] bool pending() const { return !m_pending.isEmpty(); }

  // The wait the caller should leave between the two phases.
  static int gracePeriodMs();

private:
  [[nodiscard]] bool isProtected(const Target& target) const;
  [[nodiscard]] bool alive(const Target& target) const;

  SignalSink* m_sink = nullptr;
  LivenessFn m_alive;
  QVector<qint64> m_protectedPids;
  QStringList m_protectedBinaries;
  QVector<Target> m_pending;
};

[[nodiscard]] QString outcomeText(OutcomeKind kind);

} // namespace GameStop
