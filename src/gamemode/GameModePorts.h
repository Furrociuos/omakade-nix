#pragma once

#include <QString>
#include <QVector>

// The pieces of the desktop Game Mode touches, behind interfaces so the session logic
// can be exercised without a compositor, a sound server, or a shell.

// One compositor output, enabled or not.
struct GameModeOutput {
  int id = -1;
  QString name;        // connector, such as HDMI-A-2
  QString description; // make, model and serial; stable when connectors renumber
  bool enabled = false;
  bool focused = false;
  int width = 0;
  int height = 0;
  // Selector for the workspace the output currently shows. Empty when disabled.
  QString workspace;
};

struct GameModeSink {
  QString name;
  QString description;
};

// Omakade's own window as the compositor sees it.
struct GameModeWindow {
  QString address;
  QString workspace;
  QString output;
  bool floating = false;
  [[nodiscard]] bool valid() const { return !address.isEmpty(); }
};

class GameModeCompositor {
public:
  virtual ~GameModeCompositor() = default;
  // True when outputs, workspaces and windows can be managed. Without it Game Mode
  // stays on the current display and leaves window placement to the compositor.
  [[nodiscard]] virtual bool available() = 0;
  [[nodiscard]] virtual QVector<GameModeOutput> outputs(QString* error = nullptr) = 0;
  virtual bool setOutputEnabled(const QString& name, bool enabled, QString* error = nullptr) = 0;
  [[nodiscard]] virtual GameModeWindow windowForPid(qint64 pid) = 0;
  // Omakade's placeholder window, which keeps the main window's place in the layout for
  // the session. Invalid until the compositor has mapped it.
  [[nodiscard]] virtual GameModeWindow placeholderForPid(qint64 pid) = 0;
  // Makes the placeholder open out of sight, so mapping it does not disturb the layout.
  virtual bool holdPlaceholder(QString* error = nullptr) = 0;
  // Focuses `output`, moves the window to `workspace` there, and focuses the window. With
  // a `placeholder`, that window first takes the main window's exact place in the layout.
  virtual bool placeWindow(const QString& address, const QString& workspace, const QString& output,
                           const QString& placeholder, QString* error = nullptr) = 0;
  // Moves the window without following it. With a `placeholder`, the window trades places
  // with it instead and so returns to the exact spot it left.
  virtual bool returnWindow(const QString& address, const QString& workspace,
                            const QString& placeholder, QString* error = nullptr) = 0;
  virtual bool focusWindow(const QString& address, QString* error = nullptr) = 0;
  virtual bool focusWorkspace(const QString& workspace, QString* error = nullptr) = 0;
  virtual bool focusOutput(const QString& name, QString* error = nullptr) = 0;
};

class GameModeAudio {
public:
  virtual ~GameModeAudio() = default;
  [[nodiscard]] virtual bool available() = 0;
  [[nodiscard]] virtual QVector<GameModeSink> sinks(QString* error = nullptr) = 0;
  [[nodiscard]] virtual QString defaultSink() = 0;
  virtual bool setDefaultSink(const QString& name, QString* error = nullptr) = 0;
};

class GameModeNotifications {
public:
  virtual ~GameModeNotifications() = default;
  [[nodiscard]] virtual bool available() = 0;
  // Sets `silenced` and returns true when the current state could be read.
  [[nodiscard]] virtual bool silenced(bool* silenced) = 0;
  virtual bool setSilenced(bool silenced) = 0;
};
