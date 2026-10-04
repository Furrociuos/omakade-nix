#include "gamemode/GameModeOverlay.h"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>

#include <LayerShellQt/Window>

GameModeOverlay::GameModeOverlay(QObject* parent) : QObject(parent) {}

bool GameModeOverlay::prepare(QQuickWindow* window, const QString& outputName) {
  if (window == nullptr) {
    return false;
  }
  // LayerShellQt only means something on a Wayland session, and LayerShellQt cannot tell
  // whether the compositor offers layer shell at all, so the overlay is kept to Hyprland,
  // which Game Mode already relies on. Anywhere else QML keeps the controls in the main
  // window instead.
  if (!QGuiApplication::platformName().startsWith(QStringLiteral("wayland"),
                                                  Qt::CaseInsensitive) ||
      qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")) {
    return false;
  }
  if (window->handle() == nullptr) {
    window->create();
  }
  LayerShellQt::Window* layer = LayerShellQt::Window::get(window);
  if (layer == nullptr) {
    return false;
  }
  // The surface binds to its output when it is shown, so the screen is set on every call:
  // a later session can be on another display.
  QScreen* screen = window->screen();
  if (!outputName.isEmpty()) {
    const QList<QScreen*> screens = QGuiApplication::screens();
    for (QScreen* candidate : screens) {
      if (candidate->name() == outputName) {
        screen = candidate;
        break;
      }
    }
  }
  if (screen != nullptr) {
    window->setScreen(screen);
    layer->setScreen(screen);
  }
  if (m_configured == window) {
    return true;
  }
  layer->setScope(QStringLiteral("omakade"));
  layer->setLayer(LayerShellQt::Window::LayerOverlay);
  layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop) |
                    LayerShellQt::Window::AnchorBottom |
                    LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight);
  layer->setExclusiveZone(-1);
  layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
  layer->setActivateOnShow(true);
  m_configured = window;
  return true;
}
