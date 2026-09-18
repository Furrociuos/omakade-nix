#include "tracking/GameStopper.h"

#include <QCoreApplication>
#include <QDebug>
#include <QIODevice>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <signal.h>
#include <sys/types.h>

#include <cerrno>
#include <cstring>

namespace {

constexpr int kGracePeriodMs = 5000;
constexpr int kCommandTimeoutMs = 15000;

// "Close game (pid 42): asked it to close (message)".
QString outcomeLine(const GameStop::TargetOutcome& outcome) {
  QString line = QStringLiteral("%1: %2").arg(GameStop::describe(outcome.target), outcome.detail);
  if (!outcome.message.isEmpty()) {
    line += QStringLiteral(" (%1)").arg(outcome.message);
  }
  return line;
}

// The same line with the attribution reason on it, for the log. The user-facing
// lines leave the reason to the confirmation that was shown beforehand.
QString outcomeLogLine(const GameStop::TargetOutcome& outcome) {
  QString line = outcomeLine(outcome);
  if (!outcome.target.reason.isEmpty()) {
    line += QStringLiteral(", because %1").arg(outcome.target.reason);
  }
  return line;
}

// What was signalled and why goes to the log, and a refusal or a failure goes in
// as a warning so it is not lost among routine output.
void logOutcomes(const GameStop::StopReport& report) {
  for (const GameStop::TargetOutcome& outcome : report.outcomes) {
    const QString line = outcomeLogLine(outcome);
    if (outcome.kind == GameStop::OutcomeKind::Failed) {
      qWarning().noquote() << line;
    } else {
      qInfo().noquote() << line;
    }
  }
}

// Runs a tool to completion. A wait here is bounded by the timeout, which is
// what keeps an unresponsive flatpak or wineserver from hanging the caller.
bool runCommand(const QString& program, const QStringList& arguments,
                const QProcessEnvironment& environment, int* exitCode, QString* message) {
  QProcess process;
  process.setProcessEnvironment(environment);
  process.start(program, arguments, QIODevice::ReadOnly);
  if (!process.waitForStarted(kCommandTimeoutMs)) {
    *message = QStringLiteral("%1 could not be started.").arg(program);
    return false;
  }
  if (!process.waitForFinished(kCommandTimeoutMs)) {
    process.kill();
    process.waitForFinished();
    *message = QStringLiteral("%1 did not finish within %2 seconds.")
                   .arg(program)
                   .arg(kCommandTimeoutMs / 1000);
    return false;
  }
  *exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
  const QByteArray standardError = process.readAllStandardError().trimmed();
  const QByteArray standardOutput = process.readAllStandardOutput().trimmed();
  *message = QString::fromLocal8Bit(standardError.isEmpty() ? standardOutput : standardError);
  return true;
}

GameStop::LeverResult processSignalResult(qint64 pid, int signal) {
  if (pid <= 1 || pid == QCoreApplication::applicationPid()) {
    return GameStop::LeverResult::Refused;
  }
  errno = 0;
  if (::kill(static_cast<pid_t>(pid), signal) == 0) {
    return GameStop::LeverResult::Done;
  }
  if (errno == ESRCH) {
    return GameStop::LeverResult::Missing;
  }
  return GameStop::LeverResult::Failed;
}

QString signalFailureMessage(qint64 pid, int signal) {
  return QStringLiteral("kill(%1, %2) failed: %3")
      .arg(pid)
      .arg(signal)
      .arg(QString::fromLocal8Bit(std::strerror(errno)));
}

} // namespace

namespace GameStop {

SignalSink::~SignalSink() = default;

int SystemSignalSink::commandTimeoutMs() { return kCommandTimeoutMs; }

LeverResult SystemSignalSink::terminate(qint64 pid) {
  m_lastMessage.clear();
  const LeverResult result = processSignalResult(pid, SIGTERM);
  if (result == LeverResult::Failed) {
    m_lastMessage = signalFailureMessage(pid, SIGTERM);
  }
  return result;
}

LeverResult SystemSignalSink::forceTerminate(qint64 pid) {
  m_lastMessage.clear();
  const LeverResult result = processSignalResult(pid, SIGKILL);
  if (result == LeverResult::Failed) {
    m_lastMessage = signalFailureMessage(pid, SIGKILL);
  }
  return result;
}

// wineserver -k kills the server for WINEPREFIX, sending SIGINT and then
// SIGKILL; -k9 sends SIGKILL straight away (wine's server/main.c: `-k` reaches
// kill_lock_owner and the process exits with !ret, so exit 1 means no server was
// running for that prefix, which is nothing to do rather than a failure).
LeverResult SystemSignalSink::stopWinePrefix(const QString& prefix, bool force) {
  m_lastMessage.clear();
  if (prefix.isEmpty()) {
    m_lastMessage = QStringLiteral("No Wine prefix was given.");
    return LeverResult::Refused;
  }
  const QString wineserver = QStandardPaths::findExecutable(QStringLiteral("wineserver"));
  if (wineserver.isEmpty()) {
    m_lastMessage = QStringLiteral("wineserver is not installed.");
    return LeverResult::Failed;
  }
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("WINEPREFIX"), prefix);
  int exitCode = -1;
  QString message;
  if (!runCommand(wineserver, {force ? QStringLiteral("-k9") : QStringLiteral("-k")}, environment,
                  &exitCode, &message)) {
    m_lastMessage = message;
    return LeverResult::Failed;
  }
  m_lastMessage = message;
  if (exitCode == 0) {
    return LeverResult::Done;
  }
  if (exitCode == 1) {
    return LeverResult::Missing;
  }
  return LeverResult::Failed;
}

// flatpak kill stops a running instance by application id. What flatpak does
// when the app is not running is not documented, so its own message is passed
// through instead of being classified.
LeverResult SystemSignalSink::stopFlatpakApp(const QString& appId) {
  m_lastMessage.clear();
  if (appId.isEmpty()) {
    m_lastMessage = QStringLiteral("No flatpak application id was given.");
    return LeverResult::Refused;
  }
  const QString flatpak = QStandardPaths::findExecutable(QStringLiteral("flatpak"));
  if (flatpak.isEmpty()) {
    m_lastMessage = QStringLiteral("flatpak is not installed.");
    return LeverResult::Failed;
  }
  int exitCode = -1;
  QString message;
  if (!runCommand(flatpak, {QStringLiteral("kill"), appId}, QProcessEnvironment::systemEnvironment(),
                  &exitCode, &message)) {
    m_lastMessage = message;
    return LeverResult::Failed;
  }
  m_lastMessage = message;
  return exitCode == 0 ? LeverResult::Done : LeverResult::Failed;
}

int StopReport::signalled() const {
  int count = 0;
  for (const TargetOutcome& outcome : outcomes) {
    if (outcome.kind == OutcomeKind::Signalled) {
      ++count;
    }
  }
  return count;
}

int StopReport::refused() const {
  int count = 0;
  for (const TargetOutcome& outcome : outcomes) {
    if (outcome.kind == OutcomeKind::Refused) {
      ++count;
    }
  }
  return count;
}

int StopReport::failed() const {
  int count = 0;
  for (const TargetOutcome& outcome : outcomes) {
    if (outcome.kind == OutcomeKind::Failed) {
      ++count;
    }
  }
  return count;
}

QStringList StopReport::lines() const {
  QStringList lines;
  lines.reserve(outcomes.size());
  for (const TargetOutcome& outcome : outcomes) {
    lines.append(outcomeLine(outcome));
  }
  return lines;
}

QString outcomeText(OutcomeKind kind) {
  switch (kind) {
  case OutcomeKind::Signalled:
    return QStringLiteral("signalled");
  case OutcomeKind::AlreadyGone:
    return QStringLiteral("already gone");
  case OutcomeKind::Refused:
    return QStringLiteral("refused");
  case OutcomeKind::Failed:
    return QStringLiteral("failed");
  }
  return QStringLiteral("unknown");
}

int Stopper::gracePeriodMs() { return kGracePeriodMs; }

Stopper::Stopper(SignalSink* sink, Guards guards, LivenessFn alive)
    : m_sink(sink), m_alive(std::move(alive)) {
  m_protectedPids = guards.protectedPids;
  if (!m_protectedPids.contains(1)) {
    m_protectedPids.append(1);
  }
  m_protectedBinaries = guards.protectedBinaries;
  for (const QString& name : defaultGuards().protectedBinaries) {
    if (!m_protectedBinaries.contains(name, Qt::CaseInsensitive)) {
      m_protectedBinaries.append(name);
    }
  }
  if (!m_alive) {
    m_alive = [](qint64 pid, qint64 procStart) { return ProcFs::processAlive(pid, procStart); };
  }
}

Stopper::~Stopper() = default;

bool Stopper::isProtected(const Target& target) const {
  if (target.pid <= 1 || target.pid == QCoreApplication::applicationPid()) {
    return true;
  }
  if (m_protectedPids.contains(target.pid)) {
    return true;
  }
  for (const QString& name : m_protectedBinaries) {
    if (target.comm.compare(name, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}

bool Stopper::alive(const Target& target) const {
  // The start-time guard is mandatory: without a start time there is no way to
  // tell the process we attributed from whatever holds that pid now.
  return target.procStart > 0 && m_alive(target.pid, target.procStart);
}

StopReport Stopper::begin(const Plan& plan) {
  StopReport report;
  report.escalated = false;
  m_pending.clear();

  for (const Target& target : plan.targets) {
    TargetOutcome outcome;
    outcome.target = target;

    if (target.kind == TargetKind::WinePrefix) {
      const LeverResult result = m_sink->stopWinePrefix(target.prefix, false);
      outcome.message = m_sink->lastMessage();
      switch (result) {
      case LeverResult::Done:
        outcome.kind = OutcomeKind::Signalled;
        outcome.detail = QStringLiteral("asked wineserver to close this prefix");
        m_pending.append(target);
        break;
      case LeverResult::Missing:
        outcome.kind = OutcomeKind::AlreadyGone;
        outcome.detail = QStringLiteral("no wineserver was reachable for this prefix");
        break;
      case LeverResult::Failed:
        outcome.kind = OutcomeKind::Failed;
        outcome.detail = QStringLiteral("wineserver could not close this prefix");
        break;
      case LeverResult::Refused:
        outcome.kind = OutcomeKind::Refused;
        outcome.detail = QStringLiteral("refused");
        break;
      }
      report.outcomes.append(outcome);
      continue;
    }

    if (target.kind == TargetKind::FlatpakApp) {
      const LeverResult result = m_sink->stopFlatpakApp(target.appId);
      outcome.message = m_sink->lastMessage();
      switch (result) {
      case LeverResult::Done:
        outcome.kind = OutcomeKind::Signalled;
        outcome.detail = QStringLiteral("asked flatpak to stop this app");
        break;
      case LeverResult::Missing:
        outcome.kind = OutcomeKind::AlreadyGone;
        outcome.detail = QStringLiteral("flatpak reported nothing running for this app");
        break;
      case LeverResult::Failed:
        outcome.kind = OutcomeKind::Failed;
        outcome.detail = QStringLiteral("flatpak could not stop this app");
        break;
      case LeverResult::Refused:
        outcome.kind = OutcomeKind::Refused;
        outcome.detail = QStringLiteral("refused");
        break;
      }
      report.outcomes.append(outcome);
      continue;
    }

    if (isProtected(target)) {
      outcome.kind = OutcomeKind::Refused;
      outcome.detail = QStringLiteral("this process is never signalled");
      report.outcomes.append(outcome);
      continue;
    }
    if (target.procStart <= 0) {
      outcome.kind = OutcomeKind::Refused;
      outcome.detail = QStringLiteral("no process start time, so this pid could belong to "
                                      "anything");
      report.outcomes.append(outcome);
      continue;
    }
    if (!alive(target)) {
      outcome.kind = OutcomeKind::AlreadyGone;
      outcome.detail = QStringLiteral("already gone");
      report.outcomes.append(outcome);
      continue;
    }
    const LeverResult result = m_sink->terminate(target.pid);
    outcome.message = m_sink->lastMessage();
    switch (result) {
    case LeverResult::Done:
      outcome.kind = OutcomeKind::Signalled;
      outcome.detail = QStringLiteral("asked it to close");
      m_pending.append(target);
      break;
    case LeverResult::Missing:
      outcome.kind = OutcomeKind::AlreadyGone;
      outcome.detail = QStringLiteral("already gone");
      break;
    case LeverResult::Refused:
      outcome.kind = OutcomeKind::Refused;
      outcome.detail = QStringLiteral("this process is never signalled");
      break;
    case LeverResult::Failed:
      outcome.kind = OutcomeKind::Failed;
      outcome.detail = QStringLiteral("the signal was refused");
      break;
    }
    report.outcomes.append(outcome);
  }
  logOutcomes(report);
  return report;
}

StopReport Stopper::escalate() {
  StopReport report;
  report.escalated = true;
  const QVector<Target> pending = m_pending;
  m_pending.clear();

  for (const Target& target : pending) {
    TargetOutcome outcome;
    outcome.target = target;

    if (target.kind == TargetKind::WinePrefix) {
      const LeverResult result = m_sink->stopWinePrefix(target.prefix, true);
      outcome.message = m_sink->lastMessage();
      switch (result) {
      case LeverResult::Done:
        outcome.kind = OutcomeKind::Signalled;
        outcome.detail = QStringLiteral("forced the prefix closed");
        break;
      case LeverResult::Missing:
        outcome.kind = OutcomeKind::AlreadyGone;
        outcome.detail = QStringLiteral("closed by the graceful step");
        break;
      case LeverResult::Failed:
        outcome.kind = OutcomeKind::Failed;
        outcome.detail = QStringLiteral("wineserver could not force this prefix closed");
        break;
      case LeverResult::Refused:
        outcome.kind = OutcomeKind::Refused;
        outcome.detail = QStringLiteral("refused");
        break;
      }
      report.outcomes.append(outcome);
      continue;
    }

    if (target.kind != TargetKind::TerminateProcess) {
      continue;
    }
    if (!alive(target)) {
      outcome.kind = OutcomeKind::AlreadyGone;
      outcome.detail = QStringLiteral("closed by the graceful signal");
      report.outcomes.append(outcome);
      continue;
    }
    if (isProtected(target)) {
      outcome.kind = OutcomeKind::Refused;
      outcome.detail = QStringLiteral("this process is never signalled");
      report.outcomes.append(outcome);
      continue;
    }
    const LeverResult result = m_sink->forceTerminate(target.pid);
    outcome.message = m_sink->lastMessage();
    switch (result) {
    case LeverResult::Done:
      outcome.kind = OutcomeKind::Signalled;
      outcome.detail = QStringLiteral("forced it closed");
      break;
    case LeverResult::Missing:
      outcome.kind = OutcomeKind::AlreadyGone;
      outcome.detail = QStringLiteral("closed by the graceful signal");
      break;
    case LeverResult::Refused:
      outcome.kind = OutcomeKind::Refused;
      outcome.detail = QStringLiteral("this process is never signalled");
      break;
    case LeverResult::Failed:
      outcome.kind = OutcomeKind::Failed;
      outcome.detail = QStringLiteral("the forced signal was refused");
      break;
    }
    report.outcomes.append(outcome);
  }
  logOutcomes(report);
  return report;
}

} // namespace GameStop