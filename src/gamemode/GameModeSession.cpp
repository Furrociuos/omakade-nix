#include "gamemode/GameModeSession.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QScreen>
#include <QtConcurrentRun>

GameModeSession::GameModeSession(GameModeCompositor* compositor, GameModeAudio* audio,
                                 GameModeNotifications* notifications, const QString& settingsPath,
                                 const QString& statePath, QObject* parent)
    : QObject(parent), m_compositor(compositor), m_audio(audio), m_notifications(notifications),
      m_settingsPath(settingsPath), m_controller(compositor, audio, notifications, statePath),
      m_settings(loadSettings(settingsPath)) {
  connect(&m_refreshWatcher, &QFutureWatcher<Devices>::finished, this,
          &GameModeSession::finishRefresh);
  connect(&m_changeWatcher, &QFutureWatcher<GameModeController::Result>::finished, this,
          &GameModeSession::finishChange);
  if (auto* application = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
    connect(application, &QGuiApplication::screenRemoved, this, &GameModeSession::screenRemoved);
  }
}

GameModeSession::~GameModeSession() {
  m_refreshWatcher.waitForFinished();
  m_changeWatcher.waitForFinished();
}

GameModeSettings GameModeSession::loadSettings(const QString& path) {
  GameModeSettings settings;
  QFile file(path);
  if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
    return settings;
  }
  const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
  settings.outputName = object.value("output_name").toString();
  settings.outputDescription = object.value("output_description").toString();
  settings.sinkName = object.value("sink").toString();
  settings.silenceNotifications = object.value("silence_notifications").toBool(true);
  return settings;
}

bool GameModeSession::saveSettings(const QString& path, const GameModeSettings& settings) {
  if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) {
    return false;
  }
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  file.write(QJsonDocument(QJsonObject{{"output_name", settings.outputName},
                                       {"output_description", settings.outputDescription},
                                       {"sink", settings.sinkName},
                                       {"silence_notifications", settings.silenceNotifications}})
                 .toJson(QJsonDocument::Indented));
  return file.commit();
}

QString GameModeSession::outputLabel(const GameModeOutput& output) {
  if (output.description.isEmpty()) {
    return output.name;
  }
  return QStringLiteral("%1 (%2)").arg(output.description, output.name);
}

int GameModeSession::currentDisplayIndex() const {
  if (m_settings.outputName.isEmpty() && m_settings.outputDescription.isEmpty()) {
    return 0;
  }
  const int index = GameModeController::findOutput(m_outputs, m_settings.outputName,
                                                   m_settings.outputDescription);
  return index < 0 ? -1 : index + 1;
}

int GameModeSession::currentSoundIndex() const {
  if (m_settings.sinkName.isEmpty()) {
    return 0;
  }
  for (int index = 0; index < m_sinks.size(); ++index) {
    if (m_sinks.at(index).name == m_settings.sinkName) {
      return index + 1;
    }
  }
  return -1;
}

QString GameModeSession::displayLabel() const {
  const int index = currentDisplayIndex();
  if (index == 0) {
    return QStringLiteral("Current display");
  }
  if (index < 0) {
    return QStringLiteral("%1 · not connected")
        .arg(m_settings.outputDescription.isEmpty() ? m_settings.outputName
                                                    : m_settings.outputDescription);
  }
  const GameModeOutput& output = m_outputs.at(index - 1);
  return output.enabled ? outputLabel(output)
                        : QStringLiteral("%1 · off until Game Mode").arg(outputLabel(output));
}

QString GameModeSession::soundLabel() const {
  const int index = currentSoundIndex();
  if (index == 0) {
    return QStringLiteral("Current sound output");
  }
  if (index < 0) {
    return QStringLiteral("%1 · not available").arg(m_settings.sinkName);
  }
  const GameModeSink& sink = m_sinks.at(index - 1);
  return sink.description.isEmpty() ? sink.name : sink.description;
}

int GameModeSession::displayChoices() const {
  return m_displayManaged ? static_cast<int>(m_outputs.size()) + 1 : 1;
}

int GameModeSession::soundChoices() const {
  return m_soundManaged ? static_cast<int>(m_sinks.size()) + 1 : 1;
}

void GameModeSession::setSilenceNotifications(bool value) {
  if (m_settings.silenceNotifications == value) {
    return;
  }
  m_settings.silenceNotifications = value;
  persist();
  emit devicesChanged();
}

void GameModeSession::persist() {
  if (!saveSettings(m_settingsPath, m_settings)) {
    emit notice(QStringLiteral("Game Mode settings could not be saved."));
  }
}

void GameModeSession::setBusy(bool busy) {
  m_busy = busy;
  emit stateChanged();
}

void GameModeSession::setStatus(const QString& text) {
  m_statusText = text;
  emit stateChanged();
}

void GameModeSession::refresh() {
  if (m_refreshWatcher.isRunning() || m_changeWatcher.isRunning()) {
    m_refreshPending = true;
    return;
  }
  m_refreshPending = false;
  const bool recover = !m_recoveryChecked;
  m_recoveryChecked = true;
  m_refreshWatcher.setFuture(QtConcurrent::run([this, recover] {
    Devices devices;
    if (recover) {
      devices.recovered = m_controller.recover();
      devices.ranRecovery = true;
    }
    devices.displayManaged = m_compositor != nullptr && m_compositor->available();
    if (devices.displayManaged) {
      devices.outputs = m_compositor->outputs();
    }
    devices.soundManaged = m_audio != nullptr && m_audio->available();
    if (devices.soundManaged) {
      devices.sinks = m_audio->sinks();
    }
    devices.notificationsManaged = m_notifications != nullptr && m_notifications->available();
    return devices;
  }));
}

void GameModeSession::finishRefresh() {
  const Devices devices = m_refreshWatcher.result();
  m_displayManaged = devices.displayManaged;
  m_soundManaged = devices.soundManaged;
  m_notificationsManaged = devices.notificationsManaged;
  m_outputs = devices.outputs;
  m_sinks = devices.sinks;
  emit devicesChanged();
  if (devices.ranRecovery &&
      (!devices.recovered.output.isEmpty() || !devices.recovered.notes.isEmpty())) {
    QStringList message{QStringLiteral("An interrupted Game Mode session was undone.")};
    message.append(devices.recovered.notes);
    emit notice(message.join(QLatin1Char(' ')));
  }
  if (m_refreshPending) {
    refresh();
  }
}

void GameModeSession::cycleDisplay() {
  if (m_busy || m_active) {
    return;
  }
  const int next = (currentDisplayIndex() + 1) % displayChoices();
  if (next == 0) {
    m_settings.outputName.clear();
    m_settings.outputDescription.clear();
  } else {
    m_settings.outputName = m_outputs.at(next - 1).name;
    m_settings.outputDescription = m_outputs.at(next - 1).description;
  }
  persist();
  emit devicesChanged();
}

void GameModeSession::cycleSound() {
  if (m_busy || m_active) {
    return;
  }
  const int next = (currentSoundIndex() + 1) % soundChoices();
  m_settings.sinkName = next == 0 ? QString{} : m_sinks.at(next - 1).name;
  persist();
  emit devicesChanged();
}

void GameModeSession::enter() {
  if (m_busy || m_active) {
    return;
  }
  m_refreshWatcher.waitForFinished();
  m_entering = true;
  m_statusText = QStringLiteral("Starting Game Mode");
  setBusy(true);
  const GameModeSettings settings = m_settings;
  const qint64 pid = QCoreApplication::applicationPid();
  m_changeWatcher.setFuture(
      QtConcurrent::run([this, settings, pid] { return m_controller.enter(settings, pid); }));
}

void GameModeSession::exit() {
  if (m_busy || !m_active) {
    return;
  }
  m_refreshWatcher.waitForFinished();
  m_entering = false;
  m_statusText = QStringLiteral("Leaving Game Mode");
  setBusy(true);
  emit leaving();
  const qint64 pid = QCoreApplication::applicationPid();
  m_changeWatcher.setFuture(QtConcurrent::run([this, pid] { return m_controller.exit(pid); }));
}

void GameModeSession::toggle() {
  if (m_active) {
    exit();
  } else {
    enter();
  }
}

void GameModeSession::finishChange() {
  const GameModeController::Result result = m_changeWatcher.result();
  const QString notes = result.notes.join(QLatin1Char(' '));
  m_busy = false;
  if (m_entering) {
    m_active = result.ok;
    m_output = result.ok ? result.output : QString{};
    m_statusText = result.ok ? QString{} : result.error;
    emit stateChanged();
    if (result.ok) {
      emit entered();
      if (!notes.isEmpty()) {
        emit notice(notes);
      }
    } else {
      emit failed(notes.isEmpty() ? result.error : result.error + QLatin1Char(' ') + notes);
    }
  } else {
    m_active = false;
    m_output.clear();
    m_statusText = result.ok ? QString{} : result.error;
    emit stateChanged();
    emit exited();
    if (!result.ok || !notes.isEmpty()) {
      emit notice(result.ok ? notes : result.error + QLatin1Char(' ') + notes);
    }
  }
  // Entering and leaving both change which displays are on.
  refresh();
}

void GameModeSession::screenRemoved(QScreen* screen) {
  if (!m_active || m_busy || screen == nullptr || m_output.isEmpty() ||
      screen->name() != m_output) {
    return;
  }
  emit notice(QStringLiteral("Game Mode ended because its display was disconnected."));
  exit();
}

void GameModeSession::shutdown() {
  m_refreshWatcher.waitForFinished();
  m_changeWatcher.waitForFinished();
  if (m_controller.active()) {
    (void)m_controller.exit(QCoreApplication::applicationPid());
  }
  m_active = false;
}
