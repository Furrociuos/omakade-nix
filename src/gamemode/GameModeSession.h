#pragma once

#include "gamemode/GameModeController.h"

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

class QScreen;

// Game Mode as the interface sees it: one switch that puts Couch Mode on a chosen display
// with its sound output, and puts the desktop back when it is switched off.
//
// Desktop changes run on a worker thread through GameModeController. This object keeps
// the choices, reports progress, and tells the window when to enter and leave Couch Mode.
class GameModeSession final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool active READ active NOTIFY stateChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
  // False when the compositor cannot be managed, which leaves only the current display.
  Q_PROPERTY(bool displayManaged READ displayManaged NOTIFY devicesChanged)
  Q_PROPERTY(bool soundManaged READ soundManaged NOTIFY devicesChanged)
  Q_PROPERTY(bool notificationsManaged READ notificationsManaged NOTIFY devicesChanged)
  Q_PROPERTY(QString displayLabel READ displayLabel NOTIFY devicesChanged)
  Q_PROPERTY(QString soundLabel READ soundLabel NOTIFY devicesChanged)
  Q_PROPERTY(int displayChoices READ displayChoices NOTIFY devicesChanged)
  Q_PROPERTY(int soundChoices READ soundChoices NOTIFY devicesChanged)
  Q_PROPERTY(QVariantList displayOptions READ displayOptions NOTIFY devicesChanged)
  Q_PROPERTY(QVariantList soundOptions READ soundOptions NOTIFY devicesChanged)
  Q_PROPERTY(int displayIndex READ displayIndex NOTIFY devicesChanged)
  Q_PROPERTY(int soundIndex READ soundIndex NOTIFY devicesChanged)
  Q_PROPERTY(QString sessionDisplayLabel READ sessionDisplayLabel NOTIFY devicesChanged)
  Q_PROPERTY(QString sessionSoundLabel READ sessionSoundLabel NOTIFY devicesChanged)
  Q_PROPERTY(bool silenceNotifications READ silenceNotifications WRITE setSilenceNotifications
                 NOTIFY devicesChanged)

public:
  // Null ports leave that part of the desktop alone. `settingsPath` holds the choices and
  // `statePath` the record of what a running session changed.
  GameModeSession(GameModeCompositor* compositor, GameModeAudio* audio,
                  GameModeNotifications* notifications, const QString& settingsPath,
                  const QString& statePath, QObject* parent = nullptr);
  ~GameModeSession() override;

  [[nodiscard]] bool active() const { return m_active; }
  [[nodiscard]] bool busy() const { return m_busy; }
  [[nodiscard]] QString statusText() const { return m_statusText; }
  [[nodiscard]] bool displayManaged() const { return m_displayManaged; }
  [[nodiscard]] bool soundManaged() const { return m_soundManaged; }
  [[nodiscard]] bool notificationsManaged() const { return m_notificationsManaged; }
  [[nodiscard]] QString displayLabel() const;
  [[nodiscard]] QString soundLabel() const;
  [[nodiscard]] int displayChoices() const;
  [[nodiscard]] int soundChoices() const;
  [[nodiscard]] QVariantList displayOptions() const;
  [[nodiscard]] QVariantList soundOptions() const;
  [[nodiscard]] int displayIndex() const;
  [[nodiscard]] int soundIndex() const;
  [[nodiscard]] QString sessionDisplayLabel() const;
  [[nodiscard]] QString sessionSoundLabel() const;
  [[nodiscard]] bool silenceNotifications() const { return m_settings.silenceNotifications; }
  void setSilenceNotifications(bool value);
  [[nodiscard]] const GameModeSettings& settings() const { return m_settings; }

  // Rereads the connected displays and sound outputs. Also undoes a session that an
  // earlier run left behind, the first time it is called.
  Q_INVOKABLE void refresh();
  // Steps to the next display or sound output. The first choice is always the one that
  // changes nothing: the current display, the current sound output.
  Q_INVOKABLE void cycleDisplay();
  Q_INVOKABLE void cycleSound();
  Q_INVOKABLE void selectDisplay(int index);
  Q_INVOKABLE void selectSound(int index);
  Q_INVOKABLE void enter();
  Q_INVOKABLE void exit();
  Q_INVOKABLE void toggle();
  // Leaves Game Mode before the process ends. Blocks until the desktop is put back.
  void shutdown();

  [[nodiscard]] static QString outputLabel(const GameModeOutput& output);
  [[nodiscard]] static GameModeSettings loadSettings(const QString& path);
  [[nodiscard]] static bool saveSettings(const QString& path, const GameModeSettings& settings);

signals:
  void stateChanged();
  void devicesChanged();
  // The desktop is ready: the window should enter Couch Mode now.
  void entered();
  // The window should leave Couch Mode; the desktop is put back right after.
  void leaving();
  void exited();
  void failed(const QString& message);
  // Something worth a toast that is not a failure to start.
  void notice(const QString& message);

private:
  struct Devices {
    bool displayManaged = false;
    bool soundManaged = false;
    bool notificationsManaged = false;
    QVector<GameModeOutput> outputs;
    QVector<GameModeSink> sinks;
    QString defaultSink;
    GameModeController::Result recovered;
    bool ranRecovery = false;
  };
  void setBusy(bool busy);
  void setStatus(const QString& text);
  void persist();
  void finishRefresh();
  void finishChange();
  void startChange();
  void screenRemoved(QScreen* screen);
  [[nodiscard]] int currentDisplayIndex() const;
  [[nodiscard]] int currentSoundIndex() const;

  GameModeCompositor* m_compositor = nullptr;
  GameModeAudio* m_audio = nullptr;
  GameModeNotifications* m_notifications = nullptr;
  QString m_settingsPath;
  GameModeController m_controller;
  GameModeSettings m_settings;
  QVector<GameModeOutput> m_outputs;
  QVector<GameModeSink> m_sinks;
  QFutureWatcher<Devices> m_refreshWatcher;
  QFutureWatcher<GameModeController::Result> m_changeWatcher;
  QString m_statusText;
  QString m_output;
  QString m_defaultSink;
  bool m_displayManaged = false;
  bool m_soundManaged = false;
  bool m_notificationsManaged = false;
  bool m_active = false;
  bool m_busy = false;
  bool m_entering = false;
  bool m_recoveryChecked = false;
  bool m_refreshPending = false;
  bool m_changePending = false;
};
