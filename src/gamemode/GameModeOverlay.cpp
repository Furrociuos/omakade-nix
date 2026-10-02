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
  if (m_configured == window) {
    return true;
  }
  // LayerShellQt only means something on a Wayland session. Anywhere else the window would
  // be an ordinary surface, so QML keeps the controls in the main window instead.
  if (!QGuiApplication::platformName().startsWith(QStringLiteral("wayland"),
                                                  Qt::CaseInsensitive)) {
    return false;
  }
  if (window->handle() == nullptr) {
    window->create();
  }
  LayerShellQt::Window* layer = LayerShellQt::Window::get(window);
  if (layer == nullptr) {
    return false;
  }
  layer->setScope(QStringLiteral("omakade"));
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
    layer->setScreen(screen);
  }
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
