#pragma once

#include <QObject>
#include <QString>

class QQuickWindow;

// Game Mode's controls on a Wayland layer-shell surface. A fullscreen game holds keyboard
// focus with the compositor, so focusing Omakade's main window loses that fight and the
// controls stay hidden behind the game. An overlay layer surface is drawn above the game
// and takes exclusive keyboard focus for the menu instead.
class GameModeOverlay final : public QObject {
  Q_OBJECT

public:
  explicit GameModeOverlay(QObject* parent = nullptr);

  // Configures `window` as an overlay layer surface on the screen named `outputName`,
  // falling back to the window's own screen when the name is empty or not connected.
  // Returns false when LayerShellQt cannot be used, which leaves QML to keep the controls
  // in the main window. Safe to call before every show: a window is configured once.
  Q_INVOKABLE bool prepare(QQuickWindow* window, const QString& outputName);

private:
  QQuickWindow* m_configured = nullptr;
};
