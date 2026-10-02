#pragma once

#include "gamemode/GameModePorts.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>

struct GameModeSettings {
  // Empty name and description mean the display Omakade is already on.
  QString outputName;
  QString outputDescription;
  // Empty keeps the current sound output.
  QString sinkName;
  bool silenceNotifications = true;
};

// Everything a session changed, written to disk before each change so an interrupted
// session can still be undone by the next start or by `omakade --game-mode-exit`.
struct GameModeState {
  qint64 ownerPid = 0;
  QString output; // display the session is on; empty when unmanaged
  bool enabledOutput = false;
  QString outputWorkspace; // what that display showed before
  QString focusedOutput;   // display that had focus before
  QString windowWorkspace; // where Omakade's window was
  // Set before the window is moved, so a move that fails halfway still gets focus put back.
  bool windowPlaced = false;
  QString previousSink;
  QString sessionSink;
  bool silencedNotifications = false;

  [[nodiscard]] QJsonObject toJson() const;
  [[nodiscard]] static bool fromJson(const QJsonObject& object, GameModeState* state);
};

// Enters and leaves Game Mode. Every call is synchronous and bounded, so the
// application runs it off the GUI thread.
//
// The promise is narrow on purpose: entering either completes or undoes itself, and
// leaving puts back exactly what entering changed. A display that was already on stays
// on, a sound output the user switched during the session is left alone, and nothing
// outside the session's own display, window and workspace is touched.
class GameModeController {
public:
  struct Result {
    bool ok = false;
    QString error;
    // Things that could not be put back or were skipped, for the status line and the log.
    QStringList notes;
    QString output;
  };
  using Sleep = std::function<void(int milliseconds)>;
  using OwnerAlive = std::function<bool(qint64 pid)>;

  // The workspace Game Mode owns. It exists only while a session is on it.
  [[nodiscard]] static QString workspace();

  GameModeController(GameModeCompositor* compositor, GameModeAudio* audio,
                     GameModeNotifications* notifications, const QString& statePath,
                     Sleep sleep = {}, OwnerAlive ownerAlive = {});

  [[nodiscard]] Result enter(const GameModeSettings& settings, qint64 windowPid);
  [[nodiscard]] Result exit(qint64 windowPid);
  // Undoes a session left behind by a process that is gone. A no-op without one.
  [[nodiscard]] Result recover();

  [[nodiscard]] bool active() const { return m_active; }
  [[nodiscard]] const GameModeState& state() const { return m_state; }

  // Picks the configured display: by description first, since connector names move
  // between ports and reboots, then by connector. Returns -1 when it is not connected.
  [[nodiscard]] static int findOutput(const QVector<GameModeOutput>& outputs, const QString& name,
                                      const QString& description);

private:
  [[nodiscard]] bool managed() const;
  [[nodiscard]] bool save(const GameModeState& state) const;
  [[nodiscard]] bool load(GameModeState* state) const;
  void forget() const;
  // Shared by leaving, by a failed entry, and by recovery. Returns false when something
  // that needed undoing could not be undone.
  bool restore(const GameModeState& state, qint64 windowPid, bool ownerGone,
               QStringList* notes) const;
  [[nodiscard]] bool waitFor(const std::function<bool()>& ready, int timeoutMs) const;

  GameModeCompositor* m_compositor = nullptr;
  GameModeAudio* m_audio = nullptr;
  GameModeNotifications* m_notifications = nullptr;
  QString m_statePath;
  Sleep m_sleep;
  OwnerAlive m_ownerAlive;
  GameModeState m_state;
  bool m_active = false;
};
