#include "app/CouchNavigationContract.h"
#include "input/ControllerInput.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QHash>
#include <QKeyEvent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTimer>
#include <SDL3/SDL.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <stdexcept>

namespace {
void settle(int ms = 20) {
  QEventLoop loop;
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
}

void require(bool condition, const QString& message) {
  if (!condition) throw std::runtime_error(message.toStdString());
}

bool until(const std::function<bool()>& ready, int timeout = 2000) {
  QElapsedTimer timer;
  timer.start();
  while (!ready() && timer.elapsed() < timeout) settle();
  return ready();
}

QString name(QQuickItem* item) {
  return item ? item->objectName() : QStringLiteral("no focus");
}

bool within(QQuickItem* item, QQuickItem* container) {
  for (; item; item = item->parentItem()) if (item == container) return true;
  return false;
}

class VirtualPad {
public:
  explicit VirtualPad(ControllerInput& controller, bool startController = true) {
    const int previousCount = controller.controllerCount();
    if (startController) controller.start();
    require(until([] { return SDL_WasInit(SDL_INIT_GAMEPAD) != 0; }),
            QStringLiteral("SDL gamepad initialization failed"));
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.name = "Omakade navigation regression controller";
    // Isolated test runs only allow this VID/PID, so physical pads stay out of the test.
    desc.vendor_id = 0xffff;
    desc.product_id = 0xffff;
    id = SDL_AttachVirtualJoystick(&desc);
    require(id != 0, QString::fromUtf8(SDL_GetError()));
    joystick = SDL_OpenJoystick(id);
    require(joystick != nullptr, QString::fromUtf8(SDL_GetError()));
    require(until([&controller, previousCount] { return controller.controllerCount() == previousCount + 1; }),
            QStringLiteral("Production ControllerInput did not discover the virtual gamepad"));
  }
  ~VirtualPad() {
    if (joystick) SDL_CloseJoystick(joystick);
    if (id) SDL_DetachVirtualJoystick(id);
  }
  void button(SDL_GamepadButton button, int holdMs = 20) {
    require(SDL_SetJoystickVirtualButton(joystick, button, true), QString::fromUtf8(SDL_GetError()));
    SDL_UpdateJoysticks();
    settle(holdMs);
    require(SDL_SetJoystickVirtualButton(joystick, button, false), QString::fromUtf8(SDL_GetError()));
    SDL_UpdateJoysticks();
    settle();
  }
  void direction(int key, bool analog) {
    if (!analog) {
      button(key == Qt::Key_Up ? SDL_GAMEPAD_BUTTON_DPAD_UP
             : key == Qt::Key_Down ? SDL_GAMEPAD_BUTTON_DPAD_DOWN
             : key == Qt::Key_Left ? SDL_GAMEPAD_BUTTON_DPAD_LEFT : SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
      return;
    }
    const int axis = key == Qt::Key_Left || key == Qt::Key_Right
                         ? SDL_GAMEPAD_AXIS_LEFTX : SDL_GAMEPAD_AXIS_LEFTY;
    const Sint16 value = key == Qt::Key_Left || key == Qt::Key_Up ? -30000 : 30000;
    require(SDL_SetJoystickVirtualAxis(joystick, axis, value), QString::fromUtf8(SDL_GetError()));
    SDL_UpdateJoysticks();
    settle();
    require(SDL_SetJoystickVirtualAxis(joystick, axis, 0), QString::fromUtf8(SDL_GetError()));
    SDL_UpdateJoysticks();
    settle();
  }
private:
  SDL_JoystickID id = 0;
  SDL_Joystick* joystick = nullptr;
};

void keyboard(QQuickWindow* window, int key, Qt::KeyboardModifiers mods = Qt::NoModifier) {
  QKeyEvent press(QEvent::KeyPress, key, mods);
  QKeyEvent release(QEvent::KeyRelease, key, mods);
  QCoreApplication::sendEvent(window, &press);
  QCoreApplication::sendEvent(window, &release);
  settle();
}
} // namespace

bool runStartupNavigationContract(QQuickWindow* window, ControllerInput& controller) {
  try {
    // Do not request activation or assign focus here. Startup must do both itself.
    require(until([&] { return window->isActive() && controller.inputEnabled(); }),
            QStringLiteral("Cold launch did not acquire window input ownership"));
    const bool couch = window->property("couchMode").toBool();
    const auto arguments = QCoreApplication::arguments();
    const bool grid = arguments.contains("--startup-grid");
    const bool empty = arguments.contains("--startup-empty") || arguments.contains("--startup-delayed");
    auto* games = window->findChild<QQuickItem*>(couch ? (grid ? "couchGameGrid" : "couchGameStrip") : "libraryGrid");
    auto* initial = empty ? window->findChild<QQuickItem*>(couch ? "couchSettingsButton" : "emptyClearButton") : games;
    require(initial && initial->isVisible() && initial->isEnabled() && initial->hasActiveFocus(),
            QStringLiteral("Cold launch did not focus a usable destination: focus=%1")
                .arg(name(window->activeFocusItem())));
    // This fixture may attach a controller, but it must not start production input.
    // The same first-frame startup connection used by a normal launch must do that.
    require(until([] { return SDL_WasInit(SDL_INIT_GAMEPAD) != 0; }),
            QStringLiteral("Cold launch did not start controller discovery"));
    std::unique_ptr<VirtualPad> pad;
    if (arguments.contains("--startup-controller")) pad = std::make_unique<VirtualPad>(controller, false);
    require(initial->hasActiveFocus(), QStringLiteral("Startup controller discovery lost initial focus"));
    const auto send = [&](int key) {
      if (!pad) keyboard(window, key);
      else if (key == Qt::Key_Return) pad->button(SDL_GAMEPAD_BUTTON_SOUTH);
      else pad->direction(key, false);
    };
    if (!empty) {
      const int before = games->property("currentIndex").toInt();
      send(Qt::Key_Right);
      require(games->property("currentIndex").toInt() == before + 1,
              QStringLiteral("First startup arrow did not select the next game"));
      send(Qt::Key_Return);
      require(window->property("detailOpen").toBool(),
              QStringLiteral("Cold launch could not open the selected game without a mouse"));
    } else if (couch) {
      send(Qt::Key_Left);
      require(name(window->activeFocusItem()) == "couchHomeButton",
              QStringLiteral("First empty-library arrow did not reach Home"));
    } else {
      send(Qt::Key_Return);
      require(until([&] { return games->property("count").toInt() > 0 && games->hasActiveFocus(); }),
              QStringLiteral("Cold launch could not clear filters without a mouse"));
    }
    if (arguments.contains("--startup-delayed") && couch) {
      QObject* library = qmlContext(window)->contextProperty("Library").value<QObject*>();
      library->setProperty("searchText", QString());
      settle();
      require(name(window->activeFocusItem()) == "couchHomeButton",
              QStringLiteral("Delayed results stole the first-input destination"));
      auto* home = window->activeFocusItem();
      send(Qt::Key_Down);
      require(window->activeFocusItem() && window->activeFocusItem() != home
                  && window->activeFocusItem()->isVisible() && window->activeFocusItem()->isEnabled(),
              QStringLiteral("Navigation stopped after delayed results arrived"));
    }
    qInfo() << "Cold launch navigation passed without mouse input or test-assigned focus";
    return true;
  } catch (const std::exception& error) {
    qCritical().noquote() << "Cold launch navigation failed:" << error.what();
    return false;
  }
}

bool runCouchNavigationContract(QQuickWindow* window, ControllerInput& controller) {
  try {
    const auto item = [window](const char* objectName) {
      auto* result = window->findChild<QQuickItem*>(QString::fromLatin1(objectName));
      require(result != nullptr, QStringLiteral("Missing %1").arg(QString::fromLatin1(objectName)));
      return result;
    };
    auto* couch = item("couchLibrary");
    auto* grid = item("couchGameGrid");
    auto* strip = item("couchGameStrip");
    // Views are focus scopes: Qt may focus their current delegate while the view
    // owns navigation. Treat either representation as the same destination.
    const auto focused = [&] {
      if (grid->isVisible() && grid->hasActiveFocus()) return grid;
      if (strip->isVisible() && strip->hasActiveFocus()) return strip;
      return window->activeFocusItem();
    };
    QObject* preferences = qmlContext(window)->contextProperty("Preferences").value<QObject*>();
    QObject* library = qmlContext(window)->contextProperty("Library").value<QObject*>();
    require(preferences && library, QStringLiteral("Missing isolated library fixture"));
    window->setProperty("homeOpen", false);
    window->requestActivate();
    require(until([&] { return window->isActive() && controller.inputEnabled(); }),
            QStringLiteral("Test window never acquired input ownership"));
    VirtualPad pad(controller);
    const QList<QQuickItem*> headers = {item("couchHomeButton"), item("couchSettingsButton"),
                                       item("couchDesktopButton"), item("couchStatsButton")};
    const QList<QQuickItem*> filters = {item("couchConsoleButton"), item("couchShowButton"),
        item("couchSourceButton"), item("couchSortButton"), item("couchConsoleViewButton"),
        item("couchLayoutButton"), item("couchSearchButton"), item("couchFiltersButton")};
    const QList<QQuickItem*> actions = {item("couchViewButton"), item("couchFavoriteButton")};
    item("couchShowButton")->forceActiveFocus();
    require(item("couchShowButton")->hasActiveFocus(),
            QStringLiteral("Cannot focus SHOW: couch visible=%1 enabled=%2, mode=%3 home=%4 stats=%5 details=%6 focus=%7")
                .arg(couch->isVisible()).arg(couch->isEnabled()).arg(window->property("couchMode").toBool())
                .arg(window->property("homeOpen").toBool()).arg(window->property("statsOpen").toBool())
                .arg(window->property("detailOpen").toBool()).arg(name(window->activeFocusItem())));
    pad.direction(Qt::Key_Up, false);
    require(headers.contains(window->activeFocusItem()),
            QStringLiteral("D-pad Up from SHOW does not reach the header; focus=%1").arg(name(window->activeFocusItem())));
    {
      auto* source = item("couchSourceButton");
      source->forceActiveFocus();
      VirtualPad secondPad(controller);
      settle();
      require(source->hasActiveFocus(), QStringLiteral("Controller discovery stole toolbar focus"));
      secondPad.direction(Qt::Key_Up, false);
      require(headers.contains(window->activeFocusItem()), QStringLiteral("Controller switching undid navigation"));
      source->forceActiveFocus();
      pad.direction(Qt::Key_Up, false);
      require(headers.contains(window->activeFocusItem()), QStringLiteral("Returning to the first controller lost navigation"));
    }
    require(until([&] { return controller.controllerCount() == 1; }), QStringLiteral("Virtual controller disconnect was not handled"));
    const auto center = [couch](QQuickItem* control) {
      return control->mapToItem(couch, QPointF(control->width() / 2, control->height() / 2));
    };
    int assertions = 0;
    for (const QString& layout : {QStringLiteral("detail"), QStringLiteral("grid")}) {
      preferences->setProperty("couchLibraryView", layout);
      library->setProperty("expandConsoles", true);
      library->setProperty("mode", 0);
      library->setProperty("sourceFilter", QString());
      settle();
      auto* games = layout == "grid" ? grid : strip;
      for (int state = 0; state < 3; ++state) {
        library->setProperty("mode", state);
        library->setProperty("sourceFilter", state == 1 ? QStringLiteral("Demo") : QString());
        library->setProperty("consoleFilter", state == 1 ? QStringLiteral("nes") : QString());
        library->setProperty("searchText", state == 2 ? QStringLiteral("no-matching-navigation-contract-game") : QString());
        settle(50);
        require(filters.first()->isVisible() == (state == 1), QStringLiteral("Console-return fixture is missing"));
        require((games->property("count").toInt() == 0) == (state == 2), QStringLiteral("Unexpected fixture game count"));
        // Group the actual rendered controls into rows, then independently check every
        // directional edge against their visual order. No QML navigation helper is called.
        QList<QQuickItem*> controls;
        for (auto* control : headers + filters + actions)
          if (control->isVisible() && control->isEnabled()) controls.append(control);
        std::sort(controls.begin(), controls.end(), [&](auto* a, auto* b) {
          const auto ac = center(a), bc = center(b);
          return qAbs(ac.y() - bc.y()) > 3 ? ac.y() < bc.y() : ac.x() < bc.x();
        });
        QList<QList<QQuickItem*>> rows;
        for (auto* control : controls) {
          if (rows.isEmpty() || qAbs(center(rows.last().first()).y() - center(control).y()) > 3)
            rows.append(QList<QQuickItem*>{});
          rows.last().append(control);
        }
        auto tabOrder = controls;
        if (games->property("count").toInt() > 0) tabOrder.append(games);
        tabOrder.first()->forceActiveFocus();
        for (int i = 1; i <= tabOrder.size(); ++i) {
          keyboard(window, Qt::Key_Tab);
          require(focused() == tabOrder[i % tabOrder.size()],
                  QStringLiteral("Tab %1 state=%2 step=%3 expected %4, got %5")
                      .arg(layout).arg(state).arg(i).arg(name(tabOrder[i % tabOrder.size()])).arg(name(window->activeFocusItem())));
        }
        for (int i = tabOrder.size() - 1; i >= 0; --i) {
          keyboard(window, Qt::Key_Backtab, Qt::ShiftModifier);
          require(focused() == tabOrder[i],
                  QStringLiteral("Shift+Tab expected %1, got %2").arg(name(tabOrder[i]), name(window->activeFocusItem())));
        }
        for (int input = 0; input < 3; ++input) {
          const auto send = [&](int key) {
            require(controller.inputEnabled(), QStringLiteral("Input ownership lost during navigation"));
            if (input == 0) keyboard(window, key); else pad.direction(key, input == 2);
          };
          QHash<QQuickItem*, QList<QQuickItem*>> graph;
          for (int r = 0; r < rows.size(); ++r) {
            for (int col = 0; col < rows[r].size(); ++col) {
              auto* source = rows[r][col];
              for (int key : {Qt::Key_Up, Qt::Key_Down, Qt::Key_Left, Qt::Key_Right}) {
                auto* expected = source;
                if (key == Qt::Key_Left && col > 0) expected = rows[r][col - 1];
                if (key == Qt::Key_Right && col + 1 < rows[r].size()) expected = rows[r][col + 1];
                const int adjacent = key == Qt::Key_Up ? r - 1 : r + 1;
                if ((key == Qt::Key_Up || key == Qt::Key_Down) && adjacent >= 0 && adjacent < rows.size()) {
                  expected = *std::min_element(rows[adjacent].begin(), rows[adjacent].end(), [&](auto* a, auto* b) {
                    return qAbs(center(a).x() - center(source).x()) < qAbs(center(b).x() - center(source).x());
                  });
                } else if (key == Qt::Key_Down && r == rows.size() - 1 && games->property("count").toInt() > 0) {
                  expected = games;
                }
                source->forceActiveFocus();
                send(key);
                require(focused() == expected,
                    QStringLiteral("%1 %2 state=%3 input=%4: %5 key=%6 expected %7, got %8")
                        .arg(window->width()).arg(window->height()).arg(state).arg(input)
                        .arg(name(source)).arg(key).arg(name(expected)).arg(name(window->activeFocusItem())));
                graph[source].append(focused());
                ++assertions;
              }
            }
          }
          // Ensure the links form one reachable surface, including the game view.
          QSet<QQuickItem*> reached;
          QList<QQuickItem*> pending{headers.first()};
          while (!pending.isEmpty()) {
            auto* current = pending.takeFirst();
            if (reached.contains(current)) continue;
            reached.insert(current);
            pending.append(graph.value(current));
          }
          for (auto* control : controls)
            require(reached.contains(control), QStringLiteral("Unreachable control: %1").arg(name(control)));
          if (games->property("count").toInt() > 0) {
            require(reached.contains(games), QStringLiteral("Game view unreachable from header"));
            games->setProperty("currentIndex", 0);
            games->forceActiveFocus();
            send(Qt::Key_Up);
            require(controls.contains(window->activeFocusItem()), QStringLiteral("Game view cannot return to controls"));
            auto* returnControl = window->activeFocusItem();
            send(Qt::Key_Down);
            require(games->hasActiveFocus(), QStringLiteral("Game controls cannot return to games"));
            send(Qt::Key_Up);
            require(window->activeFocusItem() == returnControl, QStringLiteral("Game view lost its return control"));
            games->forceActiveFocus();
            games->setProperty("currentIndex", 0);
            const int count = games->property("count").toInt();
            if (count > 1) {
              send(Qt::Key_Right);
              require(games->property("currentIndex").toInt() == 1, QStringLiteral("Game Right did not advance selection"));
              send(Qt::Key_Left);
              require(games->property("currentIndex").toInt() == 0, QStringLiteral("Game Left did not restore selection"));
            }
            if (games == grid && count > grid->property("columnCount").toInt()) {
              send(Qt::Key_Down);
              require(grid->property("currentIndex").toInt() == grid->property("columnCount").toInt(),
                      QStringLiteral("Grid Down did not advance one row"));
              send(Qt::Key_Up);
              require(grid->hasActiveFocus() && grid->property("currentIndex").toInt() == 0,
                      QStringLiteral("Grid Up left the games before reaching the first row"));
            }
          }
        }
      }
      library->setProperty("mode", 0);
      library->setProperty("sourceFilter", QString());
      library->setProperty("searchText", QString());
      settle(50);
      for (auto* start : {games, actions.first()}) {
        if (!start->isVisible() || !start->isEnabled()) continue;
        start->forceActiveFocus();
        library->setProperty("searchText", QStringLiteral("no-matching-navigation-contract-game"));
        settle(50);
        auto* focus = window->activeFocusItem();
        require(focus && focus->isVisible() && focus->isEnabled() && (headers.contains(focus) || filters.contains(focus)),
                QStringLiteral("Empty results stranded focus on %1").arg(name(focus)));
        library->setProperty("searchText", QString());
        settle(50);
      }
    }
    library->setProperty("searchText", QString());
    library->setProperty("consoleFilter", QString());
    library->setProperty("sourceFilter", QString());
    library->setProperty("mode", 0);
    settle(50);
    // Confirm/back also go through SDL. Dialogs must retain focus and restore their opener.
    for (const auto& entry : {std::pair{"couchSearchButton", "searchOpen"},
                             std::pair{"couchFiltersButton", "browseOpen"}}) {
      auto* opener = item(entry.first);
      opener->forceActiveFocus();
      pad.button(SDL_GAMEPAD_BUTTON_SOUTH);
      require(couch->property(entry.second).toBool(), QStringLiteral("Controller did not open %1").arg(entry.second));
      for (int key : {Qt::Key_Up, Qt::Key_Right, Qt::Key_Down, Qt::Key_Left}) {
        pad.direction(key, false);
        auto* focus = window->activeFocusItem();
        require(within(focus, item(entry.second == QStringLiteral("searchOpen") ? "couchKeyboard" : "couchBrowsePanel")),
                QStringLiteral("Dialog navigation escaped into the library"));
      }
      {
        auto* beforeConnection = window->activeFocusItem();
        VirtualPad secondPad(controller);
        settle();
        require(window->activeFocusItem() == beforeConnection, QStringLiteral("Controller connection stole dialog focus"));
      }
      require(until([&] { return controller.controllerCount() == 1; }), QStringLiteral("Dialog controller disconnect was not handled"));
      pad.button(SDL_GAMEPAD_BUTTON_EAST);
      require(!couch->property(entry.second).toBool() && opener->hasActiveFocus(),
              QStringLiteral("Dialog Back did not restore %1").arg(name(opener)));
    }
    // Header destinations and game details are entered and exited using actual pad buttons.
    for (const auto& entry : {std::pair{"couchHomeButton", "homeOpen"},
                             std::pair{"couchSettingsButton", "diagnosticsOpen"},
                             std::pair{"couchStatsButton", "statsOpen"}}) {
      item(entry.first)->forceActiveFocus();
      pad.button(SDL_GAMEPAD_BUTTON_SOUTH);
      require(until([&] { return window->property(entry.second).toBool(); }), QStringLiteral("Destination did not open"));
      pad.direction(Qt::Key_Down, false);
      auto* focus = window->activeFocusItem();
      require(focus && focus->isVisible() && focus->isEnabled() && !headers.contains(focus) && !filters.contains(focus),
              QStringLiteral("Destination retained library focus: %1").arg(entry.second));
      pad.button(SDL_GAMEPAD_BUTTON_EAST);
      require(until([&] { return !window->property(entry.second).toBool() && grid->hasActiveFocus(); }),
              QStringLiteral("Destination Back did not restore game focus: %1").arg(entry.second));
    }
    grid->setProperty("currentIndex", 0);
    grid->forceActiveFocus();
    pad.button(SDL_GAMEPAD_BUTTON_SOUTH);
    require(until([&] { return window->property("detailOpen").toBool(); }), QStringLiteral("Game details did not open"));
    pad.direction(Qt::Key_Right, false);
    require(item("favoriteButton")->hasActiveFocus(), QStringLiteral("Game details action row cannot be traversed"));
    pad.button(SDL_GAMEPAD_BUTTON_EAST);
    require(until([&] { return !window->property("detailOpen").toBool() && grid->hasActiveFocus(); }),
            QStringLiteral("Game details Back did not restore games"));
    // Holding and releasing a controller must leave navigation usable.
    grid->forceActiveFocus();
    grid->setProperty("currentIndex", 0);
    pad.button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 370);
    require(grid->property("currentIndex").toInt() >= 2, QStringLiteral("Held D-pad did not repeat"));
    const int releasedIndex = grid->property("currentIndex").toInt();
    settle(150);
    require(grid->property("currentIndex").toInt() == releasedIndex, QStringLiteral("D-pad repeated after release"));
    pad.button(SDL_GAMEPAD_BUTTON_NORTH);
    auto* layoutControl = item("couchLayoutButton");
    require(layoutControl->hasActiveFocus(), QStringLiteral("Controller toolbar shortcut did not reach the controls"));
    pad.button(SDL_GAMEPAD_BUTTON_NORTH);
    require(grid->hasActiveFocus(), QStringLiteral("Controller toolbar shortcut did not return to games"));
    pad.direction(Qt::Key_Up, false);
    require(layoutControl->hasActiveFocus(), QStringLiteral("Toolbar shortcut lost its return control"));
    keyboard(window, Qt::Key_F6);
    require(grid->hasActiveFocus(), QStringLiteral("Keyboard toolbar shortcut did not return to games"));
    pad.button(SDL_GAMEPAD_BUTTON_START);
    require(until([&] { return !window->property("couchMode").toBool(); }), QStringLiteral("Controller Start did not leave Couch mode"));
    pad.button(SDL_GAMEPAD_BUTTON_START);
    require(until([&] { return window->property("couchMode").toBool() && grid->hasActiveFocus(); }),
            QStringLiteral("Controller Start did not restore Couch game focus"));
    qInfo() << "Couch navigation contract passed:" << assertions << "directional edges, keyboard/D-pad/analog,"
            << "detail/grid, populated/console/empty, dialogs/destinations/details, shortcuts, hotplug, held input";
    return true;
  } catch (const std::exception& error) {
    qCritical().noquote() << "Couch navigation contract:" << error.what();
    return false;
  }
}
