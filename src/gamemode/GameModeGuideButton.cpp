#include "gamemode/GameModeGuideButton.h"

#include <QProcess>
#include <QStandardPaths>
#include <QtConcurrentRun>

namespace {
constexpr int kCommandTimeoutMs = 10000;

GameModeGuideButton::Result systemctlUser(const QStringList& arguments) {
  const QString executable = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
  if (executable.isEmpty()) {
    return {};
  }
  QProcess process;
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(executable, QStringList{QStringLiteral("--user")} + arguments);
  if (!process.waitForStarted(kCommandTimeoutMs)) {
    return {};
  }
  if (!process.waitForFinished(kCommandTimeoutMs)) {
    process.kill();
    process.waitForFinished(kCommandTimeoutMs);
    return {.ok = false, .output = QStringLiteral("systemctl did not finish")};
  }
  return {.ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
          .output = QString::fromUtf8(process.readAll()).trimmed()};
}

QString lastLine(const QString& text) {
  return text.section(QLatin1Char('\n'), -1).trimmed();
}
} // namespace

GameModeGuideButton::GameModeGuideButton(bool managed, QObject* parent)
    : QObject(parent), m_managed(managed) {
  connect(&m_watcher, &QFutureWatcher<Outcome>::finished, this, [this] {
    m_outcome = m_watcher.result();
    emit changed();
  });
}

GameModeGuideButton::~GameModeGuideButton() { m_watcher.waitForFinished(); }

void GameModeGuideButton::refresh() { start(Action::Refresh); }

void GameModeGuideButton::enable() { start(Action::Enable); }

void GameModeGuideButton::disable() { start(Action::Disable); }

void GameModeGuideButton::start(Action action) {
  if (!m_managed || m_watcher.isRunning()) {
    return;
  }
  m_watcher.setFuture(QtConcurrent::run([action] { return run(systemctlUser, action); }));
  emit changed();
}

GameModeGuideButton::Outcome GameModeGuideButton::run(const Systemctl& systemctl,
                                                      Action action) {
  Outcome outcome;
  const QString unit = QString::fromLatin1(kUnit);
  const Result load = systemctl(
      {QStringLiteral("show"), QStringLiteral("--property=LoadState"), QStringLiteral("--value"),
       unit});
  if (!load.ok || load.output != QStringLiteral("loaded")) {
    return outcome;
  }
  outcome.available = true;

  QString failure;
  if (action != Action::Refresh) {
    const bool on = action == Action::Enable;
    const Result changed = systemctl({on ? QStringLiteral("enable") : QStringLiteral("disable"),
                                      QStringLiteral("--now"), unit});
    if (!changed.ok) {
      failure = (on ? QStringLiteral("Could not turn on the controller button")
                    : QStringLiteral("Could not turn off the controller button")) +
                (lastLine(changed.output).isEmpty()
                     ? QStringLiteral(".")
                     : QStringLiteral(": ") + lastLine(changed.output));
    }
  }
  // Read back what systemd now holds rather than assuming the change took.
  outcome.enabled = systemctl({QStringLiteral("is-enabled"), unit}).output ==
                    QStringLiteral("enabled");
  outcome.running = systemctl({QStringLiteral("is-active"), unit}).output ==
                    QStringLiteral("active");
  if (!failure.isEmpty()) {
    outcome.statusText = failure;
  } else if (outcome.enabled && !outcome.running) {
    outcome.statusText = QStringLiteral(
        "The controller button is on but its service is not running. See journalctl --user "
        "-u omakade-guide-button.");
  }
  return outcome;
}
