#pragma once

#include "gamemode/GameModePorts.h"

#include <QByteArray>
#include <QJsonObject>
#include <atomic>

// The real desktop behind Game Mode's interfaces. Each call runs one short, bounded
// command, so a stalled compositor or sound server cannot hang a session change.

// Hyprland through `hyprctl`. Outputs and windows are changed with `hyprctl eval`, which
// only exists with Hyprland's Lua configuration; an older Hyprland reports unavailable
// and Game Mode stays on the current display.
class HyprlandGameModeCompositor final : public GameModeCompositor {
public:
  [[nodiscard]] bool available() override;
  [[nodiscard]] QVector<GameModeOutput> outputs(QString* error = nullptr) override;
  bool setOutputEnabled(const QString& name, bool enabled, QString* error = nullptr) override;
  [[nodiscard]] GameModeWindow windowForPid(qint64 pid) override;
  bool placeWindow(const QString& address, const QString& workspace, const QString& output,
                   QString* error = nullptr) override;
  bool returnWindow(const QString& address, const QString& workspace,
                    QString* error = nullptr) override;
  bool focusWorkspace(const QString& workspace, QString* error = nullptr) override;
  bool focusOutput(const QString& name, QString* error = nullptr) override;

  // Parses `hyprctl -j monitors all`.
  [[nodiscard]] static QVector<GameModeOutput> parseOutputs(const QByteArray& json,
                                                            QString* error = nullptr);
  // Finds Omakade's window in `hyprctl -j clients`, preferring its own window class over
  // any other window the process owns.
  [[nodiscard]] static GameModeWindow
  parseWindow(const QByteArray& clientsJson, const QVector<GameModeOutput>& outputs, qint64 pid);
  // The selector a dispatcher accepts for a workspace object: "3", "name:couch" or
  // "special:scratchpad". Empty for the placeholder a disabled output reports.
  [[nodiscard]] static QString workspaceSelector(const QJsonObject& workspace);
  // A double-quoted Lua string literal. Names come from EDID and user configuration, so
  // they are never pasted into a script unescaped.
  [[nodiscard]] static QString luaString(const QString& value);
  [[nodiscard]] static bool validAddress(const QString& address);
  [[nodiscard]] static QString outputScript(const QString& name, bool enabled);
  [[nodiscard]] static QString placeScript(const QString& address, const QString& workspace,
                                           const QString& output);
  [[nodiscard]] static QString returnScript(const QString& address, const QString& workspace);

private:
  bool eval(const QString& script, QString* error);
  // -1 unknown, 0 unavailable, 1 available. Checked once per process.
  std::atomic<int> m_available{-1};
};

// PipeWire or PulseAudio through `pactl`.
class PactlGameModeAudio final : public GameModeAudio {
public:
  [[nodiscard]] bool available() override;
  [[nodiscard]] QVector<GameModeSink> sinks(QString* error = nullptr) override;
  [[nodiscard]] QString defaultSink() override;
  bool setDefaultSink(const QString& name, QString* error = nullptr) override;

  // Parses `pactl -f json list sinks`.
  [[nodiscard]] static QVector<GameModeSink> parseSinks(const QByteArray& json,
                                                        QString* error = nullptr);
};

// Omarchy's do-not-disturb switch through `omarchy-shell`. Absent outside Omarchy, where
// notifications are simply left alone.
class OmarchyGameModeNotifications final : public GameModeNotifications {
public:
  [[nodiscard]] bool available() override;
  [[nodiscard]] bool silenced(bool* silenced) override;
  bool setSilenced(bool silenced) override;
};
