#include "gamemode/GameModeController.h"
#include "gamemode/GameModeDesktop.h"
#include "gamemode/GameModeSession.h"
#include "gamemode/GameModeShortcut.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QSemaphore>
#include <QElapsedTimer>
#include <functional>
#include <atomic>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <QTemporaryDir>
#include <QTest>

namespace {

GameModeOutput output(int id, const QString& name, const QString& description, bool enabled,
                      bool focused, const QString& workspace) {
  GameModeOutput result;
  result.id = id;
  result.name = name;
  result.description = description;
  result.enabled = enabled;
  result.focused = focused;
  result.workspace = enabled ? workspace : QString{};
  return result;
}

const QString kPlaceholderAddress = QStringLiteral("0x55d0c0ffee99");

// A compositor that behaves the way the real one was observed to: enabling an output
// takes a few polls to go live and gives it a workspace, placing a window creates the
// target workspace on the output that was focused first.
class FakeCompositor final : public GameModeCompositor {
public:
  // A window owned by another process, such as a game or its launcher.
  struct OtherWindow {
    QString address;
    QString workspace;
  };

  std::function<void()> beforeOutputs;
  std::function<void()> beforePlace;
  bool usable = true;
  bool enableFails = false;
  // -1 never goes live.
  int livePolls = 2;
  bool placeFails = false;
  bool holdFails = false;
  bool tradeFails = false;
  // Set by the controller's placeholder callback.
  bool placeholderShown = false;
  int placeholderPolls = 2;
  GameModeWindow placeholder;
  QVector<GameModeOutput> list;
  GameModeWindow window;
  QVector<OtherWindow> others;
  QStringList log;
  int pendingPolls = -1;
  QString pendingOutput;

  void tick() {
    if (pendingPolls > 0 && --pendingPolls == 0) {
      for (GameModeOutput& entry : list) {
        if (entry.name == pendingOutput) {
          entry.enabled = true;
          entry.workspace = QStringLiteral("9");
        }
      }
    }
  }

  bool available() override { return usable; }
  QVector<GameModeOutput> outputs(QString*) override {
    if (beforeOutputs) beforeOutputs();
    return list;
  }
  bool setOutputEnabled(const QString& name, bool enabled, QString*) override {
    log.append(QStringLiteral("%1 %2").arg(enabled ? "enable" : "disable", name));
    if (enabled && enableFails) {
      return false;
    }
    if (enabled) {
      pendingOutput = name;
      pendingPolls = livePolls;
    } else {
      pendingPolls = -1;
      for (GameModeOutput& entry : list) {
        if (entry.name == name) {
          entry.enabled = false;
          entry.workspace.clear();
        }
      }
    }
    return true;
  }
  GameModeWindow windowForPid(qint64) override { return window; }
  int otherWindowsOn(const QString& workspace, qint64) override {
    return otherWindowAddressesOn(workspace, 0).size();
  }
  QStringList otherWindowAddressesOn(const QString& workspace, qint64) override {
    QStringList addresses;
    for (const OtherWindow& entry : others) {
      if (entry.workspace == workspace) {
        addresses.append(entry.address);
      }
    }
    return addresses;
  }
  GameModeWindow placeholderForPid(qint64) override {
    // Mapping takes a poll, as it does for a real window.
    if (placeholderShown && placeholderPolls > 0 && --placeholderPolls == 0) {
      placeholder = {kPlaceholderAddress, QStringLiteral("special:omakade"), {}};
    }
    return placeholderShown ? placeholder : GameModeWindow{};
  }
  bool holdPlaceholder(QString*) override {
    log.append(QStringLiteral("hold"));
    return !holdFails;
  }
  bool placeWindow(const QString& address, const QString& workspace, const QString& target,
                   const QString& held, QString*) override {
    if (beforePlace) beforePlace();
    log.append(held.isEmpty()
                   ? QStringLiteral("place %1 %2 %3").arg(address, workspace, target)
                   : QStringLiteral("place %1 %2 %3 holding %4")
                         .arg(address, workspace, target, held));
    if (placeFails) {
      return false;
    }
    if (!held.isEmpty()) {
      placeholder.workspace = window.workspace;
    }
    window.workspace = workspace;
    window.output = target;
    for (GameModeOutput& entry : list) {
      if (entry.name == target) {
        entry.workspace = workspace;
      }
    }
    return true;
  }
  bool returnWindow(const QString& address, const QString& workspace, const QString& held,
                    QString*) override {
    if (!held.isEmpty()) {
      log.append(QStringLiteral("trade %1 %2").arg(address, held));
      if (tradeFails) {
        return false;
      }
      window.workspace = placeholder.workspace;
      return true;
    }
    log.append(QStringLiteral("return %1 %2").arg(address, workspace));
    for (OtherWindow& entry : others) {
      if (entry.address == address) {
        entry.workspace = workspace;
        return true;
      }
    }
    window.workspace = workspace;
    return true;
  }
  bool focusWindow(const QString& address, QString*) override {
    log.append(QStringLiteral("focus-window %1").arg(address));
    return true;
  }
  bool focusWorkspace(const QString& workspace, QString*) override {
    log.append(QStringLiteral("focus-workspace %1").arg(workspace));
    return true;
  }
  bool focusOutput(const QString& name, QString*) override {
    log.append(QStringLiteral("focus-output %1").arg(name));
    return true;
  }
};

class FakeAudio final : public GameModeAudio {
public:
  bool usable = true;
  bool setFails = false;
  QVector<GameModeSink> list{{QStringLiteral("headset"), QStringLiteral("Headset")}};
  QString current = QStringLiteral("headset");
  QStringList log;
  // A sink that only exists once this many polls have passed, like HDMI audio.
  GameModeSink late;
  int latePolls = -1;

  void tick() {
    if (latePolls > 0 && --latePolls == 0) {
      list.append(late);
    }
  }
  bool available() override { return usable; }
  QVector<GameModeSink> sinks(QString*) override { return list; }
  QString defaultSink() override { return current; }
  bool setDefaultSink(const QString& name, QString*) override {
    log.append(QStringLiteral("default %1").arg(name));
    if (setFails) {
      return false;
    }
    current = name;
    return true;
  }
};

class FakeNotifications final : public GameModeNotifications {
public:
  bool usable = true;
  bool quiet = false;
  bool setFails = false;
  std::function<void()> beforeSet;
  QStringList log;
  bool available() override { return usable; }
  bool silenced(bool* silenced) override {
    *silenced = quiet;
    return true;
  }
  bool setSilenced(bool silenced) override {
    if (beforeSet) beforeSet();
    log.append(silenced ? QStringLiteral("silence") : QStringLiteral("unsilence"));
    if (setFails) return false;
    quiet = silenced;
    return true;
  }
};

const QString kDesk = QStringLiteral("DP-2");
const QString kTv = QStringLiteral("HDMI-A-2");
const QString kTvDescription = QStringLiteral("Samsung Electric Company QBQ90 0x01000E00");
const QString kTvSink = QStringLiteral("alsa_output.hdmi-stereo");
const QString kAddress = QStringLiteral("0x55d0c0ffee00");

} // namespace

class GameModeTests final : public QObject {
  Q_OBJECT

private:
  QTemporaryDir m_directory;
  FakeCompositor m_compositor;
  FakeAudio m_audio;
  FakeNotifications m_notifications;
  int m_slept = 0;

  [[nodiscard]] QString statePath() const { return m_directory.filePath("game-mode-state.json"); }

  GameModeController controller(bool ownerAlive = false) {
    return GameModeController(
        &m_compositor, &m_audio, &m_notifications, statePath(),
        [this](int milliseconds) {
          m_slept += milliseconds;
          m_compositor.tick();
          m_audio.tick();
        },
        [ownerAlive](qint64) { return ownerAlive; });
  }

  // A desk monitor with Omakade on workspace 3, and a television that is off.
  void deskAndTv(bool tvEnabled) {
    m_compositor.list = {
        output(0, kDesk, QStringLiteral("Dell S2721DGF"), true, true, QStringLiteral("3")),
        output(1, kTv, kTvDescription, tvEnabled, false, QStringLiteral("5"))};
    m_compositor.window = {kAddress, QStringLiteral("3"), kDesk};
  }

  [[nodiscard]] static GameModeSettings tvSettings(const QString& sink = {}) {
    GameModeSettings settings;
    settings.outputName = kTv;
    settings.outputDescription = kTvDescription;
    settings.sinkName = sink;
    return settings;
  }

private slots:
  void init() {
    m_compositor = FakeCompositor{};
    m_audio = FakeAudio{};
    m_notifications = FakeNotifications{};
    m_slept = 0;
    QFile::remove(statePath());
  }

  void currentDisplayEntersAndRestores() {
    m_compositor.list = {
        output(0, kDesk, QStringLiteral("Dell S2721DGF"), true, true, QStringLiteral("3"))};
    m_compositor.window = {kAddress, QStringLiteral("3"), kDesk};
    GameModeController game = controller();

    const auto entered = game.enter({}, 100);
    QVERIFY2(entered.ok, qPrintable(entered.error));
    QCOMPARE(entered.output, kDesk);
    QVERIFY(game.active());
    QCOMPARE(m_compositor.window.workspace, GameModeController::workspace());
    QVERIFY(QFile::exists(statePath()));
    QVERIFY(m_audio.log.isEmpty());
    QCOMPARE(m_notifications.log, QStringList{"silence"});

    const auto left = game.exit(100);
    QVERIFY2(left.ok, qPrintable(left.error));
    QVERIFY(!game.active());
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QVERIFY(m_compositor.log.contains("focus-workspace 3"));
    QVERIFY(!m_compositor.log.join(' ').contains("able "));
    QCOMPARE(m_notifications.log, (QStringList{"silence", "unsilence"}));
    QVERIFY(!QFile::exists(statePath()));
  }

  // With other windows tiled beside it, Omakade has to come back to the same place.
  void placeholderKeepsTheWindowsPlaceInTheLayout() {
    deskAndTv(true);
    GameModeController game = controller();
    QList<bool> shown;
    game.setPlaceholder([&](bool visible) {
      shown.append(visible);
      m_compositor.placeholderShown = visible;
    });

    const auto entered = game.enter({}, 100);
    QVERIFY2(entered.ok, qPrintable(entered.error));
    QVERIFY(game.state().placeholder);
    QVERIFY(m_compositor.log.contains(
        QStringLiteral("place %1 name:omakade %2 holding %3").arg(kAddress, kDesk,
                                                                 kPlaceholderAddress)));
    // The placeholder now sits where Omakade was.
    QCOMPARE(m_compositor.placeholder.workspace, QStringLiteral("3"));

    const auto left = game.exit(100);
    QVERIFY2(left.ok, qPrintable(left.error));
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QVERIFY(m_compositor.log.contains(
        QStringLiteral("trade %1 %2").arg(kAddress, kPlaceholderAddress)));
    QVERIFY(m_compositor.log.contains(QStringLiteral("focus-window %1").arg(kAddress)));
    QVERIFY(!m_compositor.log.contains(QStringLiteral("return %1 3").arg(kAddress)));
    QCOMPARE(shown, (QList<bool>{true, false}));
  }

  void placeholderThatNeverMapsFallsBackToMovingTheWindow() {
    deskAndTv(true);
    GameModeController game = controller();
    QList<bool> shown;
    game.setPlaceholder([&](bool visible) { shown.append(visible); });

    const auto entered = game.enter({}, 100);
    QVERIFY2(entered.ok, qPrintable(entered.error));
    QVERIFY(!game.state().placeholder);
    QVERIFY(m_compositor.log.contains(
        QStringLiteral("place %1 name:omakade %2").arg(kAddress, kDesk)));
    QCOMPARE(shown, (QList<bool>{true, false}));

    QVERIFY(game.exit(100).ok);
    QVERIFY(m_compositor.log.contains(QStringLiteral("return %1 3").arg(kAddress)));
  }

  void closedPlaceholderOrFailedTradeFallsBackToMovingTheWindow() {
    deskAndTv(true);
    GameModeController game = controller();
    game.setPlaceholder([&](bool visible) { m_compositor.placeholderShown = visible; });
    QVERIFY(game.enter({}, 100).ok);
    QVERIFY(game.state().placeholder);
    m_compositor.tradeFails = true;

    const auto left = game.exit(100);
    QVERIFY2(left.ok, qPrintable(left.error));
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QVERIFY(m_compositor.log.contains(QStringLiteral("return %1 3").arg(kAddress)));
  }

  void floatingWindowNeedsNoPlaceholder() {
    deskAndTv(true);
    m_compositor.window.floating = true;
    GameModeController game = controller();
    bool asked = false;
    game.setPlaceholder([&](bool) { asked = true; });
    QVERIFY(game.enter({}, 100).ok);
    QVERIFY(!asked);
    QVERIFY(!game.state().placeholder);
  }

  // Couch Mode on the desktop is fullscreen, and Hyprland refuses to swap such a window.
  void fullscreenWindowNeedsNoPlaceholder() {
    deskAndTv(true);
    m_compositor.window.fullscreen = true;
    GameModeController game = controller();
    bool asked = false;
    game.setPlaceholder([&](bool) { asked = true; });
    QVERIFY(game.enter({}, 100).ok);
    QVERIFY(!asked);
    QVERIFY(!game.state().placeholder);
    QVERIFY(game.exit(100).ok);
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
  }

  void televisionIsTurnedOnAndOffAgain() {
    deskAndTv(false);
    m_audio.late = {kTvSink, QStringLiteral("QBQ90 HDMI")};
    m_audio.latePolls = 4;
    GameModeController game = controller();

    const auto entered = game.enter(tvSettings(kTvSink), 100);
    QVERIFY2(entered.ok, qPrintable(entered.error));
    QCOMPARE(entered.output, kTv);
    QCOMPARE(m_compositor.window.output, kTv);
    QCOMPARE(m_audio.current, kTvSink);
    QVERIFY(game.state().enabledOutput);
    QCOMPARE(game.state().previousSink, QStringLiteral("headset"));

    const auto left = game.exit(100);
    QVERIFY2(left.ok, qPrintable(left.error));
    QCOMPARE(m_audio.current, QStringLiteral("headset"));
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QVERIFY(!m_compositor.list.at(1).enabled);
    // The window comes back and focus returns to the desk before the television goes off.
    const QStringList tail = m_compositor.log.mid(m_compositor.log.size() - 3);
    QCOMPARE(tail, (QStringList{QStringLiteral("return %1 3").arg(kAddress),
                                QStringLiteral("focus-output %1").arg(kDesk),
                                QStringLiteral("disable %1").arg(kTv)}));
    QVERIFY(!QFile::exists(statePath()));
  }

  void televisionThatWasOnStaysOn() {
    deskAndTv(true);
    GameModeController game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    QVERIFY(!game.state().enabledOutput);
    QVERIFY(game.exit(100).ok);
    QVERIFY(m_compositor.list.at(1).enabled);
    QVERIFY(!m_compositor.log.contains(QStringLiteral("disable %1").arg(kTv)));
    // It shows the workspace it had before, and focus goes back to the desk.
    QVERIFY(m_compositor.log.contains("focus-workspace 5"));
    QCOMPARE(m_compositor.log.last(), QStringLiteral("focus-output %1").arg(kDesk));
  }

  void televisionThatNeverTurnsOnIsUndone() {
    deskAndTv(false);
    m_compositor.livePolls = -1;
    GameModeController game = controller();
    const auto entered = game.enter(tvSettings(kTvSink), 100);
    QVERIFY(!entered.ok);
    QVERIFY(entered.error.contains("did not turn on"));
    QVERIFY(!game.active());
    QCOMPARE(m_compositor.log, (QStringList{QStringLiteral("enable %1").arg(kTv),
                                            QStringLiteral("disable %1").arg(kTv)}));
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QVERIFY(m_audio.log.isEmpty());
    QVERIFY(m_notifications.log.isEmpty());
    QVERIFY(!QFile::exists(statePath()));
    QVERIFY(m_slept >= 10000);
  }

  void missingSoundOutputTurnsTheTelevisionBackOff() {
    deskAndTv(false);
    GameModeController game = controller();
    const auto entered = game.enter(tvSettings(kTvSink), 100);
    QVERIFY(!entered.ok);
    QVERIFY(entered.error.contains("sound output"));
    QVERIFY(!m_compositor.list.at(1).enabled);
    QCOMPARE(m_audio.current, QStringLiteral("headset"));
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QVERIFY(!QFile::exists(statePath()));
  }

  void failedSoundSwitchLeavesTheDefaultAlone() {
    deskAndTv(true);
    m_audio.list.append({kTvSink, QStringLiteral("QBQ90 HDMI")});
    m_audio.setFails = true;
    GameModeController game = controller();
    QVERIFY(!game.enter(tvSettings(kTvSink), 100).ok);
    QCOMPARE(m_audio.current, QStringLiteral("headset"));
    QCOMPARE(m_audio.log, QStringList{QStringLiteral("default %1").arg(kTvSink)});
    QVERIFY(!QFile::exists(statePath()));
  }

  void failedWindowMoveUndoesSoundAndDisplay() {
    deskAndTv(false);
    m_audio.list.append({kTvSink, QStringLiteral("QBQ90 HDMI")});
    m_compositor.placeFails = true;
    GameModeController game = controller();
    QVERIFY(!game.enter(tvSettings(kTvSink), 100).ok);
    QCOMPARE(m_audio.current, QStringLiteral("headset"));
    QVERIFY(!m_compositor.list.at(1).enabled);
    QVERIFY(m_notifications.log.isEmpty());
    QVERIFY(!QFile::exists(statePath()));
  }

  void disconnectedDisplayChangesNothing() {
    m_compositor.list = {
        output(0, kDesk, QStringLiteral("Dell S2721DGF"), true, true, QStringLiteral("3"))};
    m_compositor.window = {kAddress, QStringLiteral("3"), kDesk};
    GameModeController game = controller();
    const auto entered = game.enter(tvSettings(kTvSink), 100);
    QVERIFY(!entered.ok);
    QVERIFY(entered.error.contains("not connected"));
    QVERIFY(m_compositor.log.isEmpty());
    QVERIFY(m_audio.log.isEmpty());
    QVERIFY(m_notifications.log.isEmpty());
  }

  void soundOutputChosenDuringTheSessionIsKept() {
    deskAndTv(true);
    m_audio.list.append({kTvSink, QStringLiteral("QBQ90 HDMI")});
    m_audio.list.append({QStringLiteral("speakers"), QStringLiteral("Speakers")});
    GameModeController game = controller();
    QVERIFY(game.enter(tvSettings(kTvSink), 100).ok);
    m_audio.current = QStringLiteral("speakers");
    QVERIFY(game.exit(100).ok);
    QCOMPARE(m_audio.current, QStringLiteral("speakers"));
  }

  void vanishedSessionSinkFallsBackToThePreviousOne() {
    deskAndTv(true);
    m_audio.list.append({kTvSink, QStringLiteral("QBQ90 HDMI")});
    m_audio.list.append({QStringLiteral("speakers"), QStringLiteral("Speakers")});
    GameModeController game = controller();
    QVERIFY(game.enter(tvSettings(kTvSink), 100).ok);
    // The television went to standby: its sink is gone and the sound server fell back.
    m_audio.list.removeAt(1);
    m_audio.current = QStringLiteral("speakers");
    QVERIFY(game.exit(100).ok);
    QCOMPARE(m_audio.current, QStringLiteral("headset"));
  }

  void chosenDisplayRequiresMappedWindow() {
    deskAndTv(false);
    m_compositor.window = {};
    GameModeController game = controller();
    const auto entered = game.enter(tvSettings(kTvSink), 100);
    QVERIFY(!entered.ok);
    QVERIFY(entered.error.contains("window"));
    QVERIFY(m_compositor.log.isEmpty());
    QVERIFY(m_audio.log.isEmpty());
    QVERIFY(m_notifications.log.isEmpty());
    QVERIFY(!QFile::exists(statePath()));
    QVERIFY(m_slept >= 3000);
  }

  void windowAndNotificationChangesAreJournaledFirst() {
    deskAndTv(true);
    m_compositor.beforePlace = [this] {
      QFile record(statePath());
      QVERIFY(record.open(QIODevice::ReadOnly));
      const auto state = QJsonDocument::fromJson(record.readAll()).object();
      QVERIFY(state.value("window_placed").toBool());
      QCOMPARE(state.value("window_workspace").toString(), QStringLiteral("3"));
    };
    m_notifications.beforeSet = [this] {
      QFile record(statePath());
      QVERIFY(record.open(QIODevice::ReadOnly));
      QVERIFY(QJsonDocument::fromJson(record.readAll()).object()
                  .value("silenced_notifications").toBool());
    };
    GameModeController game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    m_notifications.beforeSet = {};
    QVERIFY(game.exit(100).ok);
  }

  void notificationsStayOnWithoutRecoveryStorage() {
    const QString blocked = m_directory.filePath("blocked-notification-state");
    QFile file(blocked);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    GameModeController game(nullptr, nullptr, &m_notifications, blocked + "/state.json");
    const auto entered = game.enter({}, 100);
    QVERIFY(entered.ok);
    QVERIFY(!entered.notes.isEmpty());
    QVERIFY(m_notifications.log.isEmpty());
    QVERIFY(!m_notifications.quiet);
    QVERIFY(game.exit(100).ok);
    QFile::remove(blocked);
  }

  void failedNotificationChangeClearsRecoveryFlag() {
    deskAndTv(true);
    m_notifications.setFails = true;
    GameModeController game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    QFile record(statePath());
    QVERIFY(record.open(QIODevice::ReadOnly));
    QVERIFY(!QJsonDocument::fromJson(record.readAll()).object()
                 .value("silenced_notifications").toBool());
    record.close();
    QVERIFY(game.exit(100).ok);
    QCOMPARE(m_notifications.log, QStringList{"silence"});
  }

  void notificationsAlreadySilencedStaySilenced() {
    deskAndTv(true);
    m_notifications.quiet = true;
    GameModeController game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    QVERIFY(game.exit(100).ok);
    QVERIFY(m_notifications.log.isEmpty());
    QVERIFY(m_notifications.quiet);
  }

  void notificationsCanBeLeftAlone() {
    deskAndTv(true);
    GameModeSettings settings = tvSettings();
    settings.silenceNotifications = false;
    GameModeController game = controller();
    QVERIFY(game.enter(settings, 100).ok);
    QVERIFY(m_notifications.log.isEmpty());
    QVERIFY(game.exit(100).ok);
  }

  void interruptedSessionIsUndoneWithoutTouchingWindows() {
    deskAndTv(true);
    m_audio.list.append({kTvSink, QStringLiteral("QBQ90 HDMI")});
    m_audio.current = kTvSink;
    m_notifications.quiet = true;
    GameModeState state;
    state.ownerPid = 4242;
    state.output = kTv;
    state.enabledOutput = true;
    state.focusedOutput = kDesk;
    state.windowWorkspace = QStringLiteral("3");
    state.previousSink = QStringLiteral("headset");
    state.sessionSink = kTvSink;
    state.silencedNotifications = true;
    QFile file(statePath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(state.toJson()).toJson());
    file.close();

    GameModeController game = controller(false);
    const auto recovered = game.recover();
    QVERIFY2(recovered.ok, qPrintable(recovered.error));
    QCOMPARE(recovered.output, kTv);
    QCOMPARE(m_audio.current, QStringLiteral("headset"));
    QVERIFY(!m_notifications.quiet);
    QCOMPARE(m_compositor.log, QStringList{QStringLiteral("disable %1").arg(kTv)});
    QVERIFY(!QFile::exists(statePath()));
    // Nothing left to undo the second time.
    QVERIFY(game.recover().ok);
    QCOMPARE(m_compositor.log.size(), 1);
  }

  void exitedOwnerDoesNotBlockRecoveryBeforeItsParentReapsIt() {
    const pid_t child = fork();
    QVERIFY(child >= 0);
    if (child == 0) {
      prctl(PR_SET_NAME, "omakade");
      _exit(0);
    }
    struct ReapChild {
      pid_t pid;
      ~ReapChild() { waitpid(pid, nullptr, 0); }
    } reap{child};
    siginfo_t info{};
    QCOMPARE(waitid(P_PID, child, &info, WEXITED | WNOWAIT), 0);
    QFile comm(QStringLiteral("/proc/%1/comm").arg(child));
    QVERIFY(comm.open(QIODevice::ReadOnly));
    QCOMPARE(comm.readAll().trimmed(), QByteArray("omakade"));
    GameModeState state;
    state.ownerPid = child;
    state.silencedNotifications = true;
    m_notifications.quiet = true;
    QFile file(statePath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(state.toJson()).toJson());
    file.close();
    GameModeController game(&m_compositor, &m_audio, &m_notifications, statePath());
    QVERIFY(game.recover().ok);
    QVERIFY(!m_notifications.quiet);
    QVERIFY(!QFile::exists(statePath()));
  }

  void sessionOwnedByARunningOmakadeIsLeftAlone() {
    deskAndTv(true);
    GameModeState state;
    state.ownerPid = 4242;
    state.output = kTv;
    state.enabledOutput = true;
    QFile file(statePath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(state.toJson()).toJson());
    file.close();

    GameModeController game = controller(true);
    QVERIFY(!game.recover().ok);
    QVERIFY(!game.enter(tvSettings(), 100).ok);
    QVERIFY(m_compositor.log.isEmpty());
    QVERIFY(QFile::exists(statePath()));
  }

  void unreadableStateDoesNotBlockTheNextSession() {
    deskAndTv(true);
    QFile file(statePath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{ not json");
    file.close();
    GameModeController game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    QVERIFY(game.exit(100).ok);
  }

  void withoutACompositorOnlySoundAndNotificationsChange() {
    m_compositor.usable = false;
    m_audio.list.append({kTvSink, QStringLiteral("QBQ90 HDMI")});
    GameModeController game = controller();

    const auto chosen = game.enter(tvSettings(kTvSink), 100);
    QVERIFY(!chosen.ok);
    QVERIFY(chosen.error.contains("Hyprland"));
    QVERIFY(m_audio.log.isEmpty());

    GameModeSettings settings;
    settings.sinkName = kTvSink;
    const auto entered = game.enter(settings, 100);
    QVERIFY2(entered.ok, qPrintable(entered.error));
    QVERIFY(entered.output.isEmpty());
    QCOMPARE(m_audio.current, kTvSink);
    QVERIFY(game.exit(100).ok);
    QCOMPARE(m_audio.current, QStringLiteral("headset"));
    QVERIFY(m_compositor.log.isEmpty());
  }

  void enteringTwiceIsRefused() {
    deskAndTv(true);
    GameModeController game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    QVERIFY(!game.enter(tvSettings(), 100).ok);
    QVERIFY(game.exit(100).ok);
  }

  void displayIsFoundByDescriptionBeforeConnector() {
    const QVector<GameModeOutput> outputs{
        output(0, kDesk, QStringLiteral("Dell S2721DGF"), true, true, "1"),
        output(1, QStringLiteral("HDMI-A-1"), kTvDescription, false, false, {})};
    // The television moved to another port.
    QCOMPARE(GameModeController::findOutput(outputs, kTv, kTvDescription), 1);
    // A connector that now carries a different display is not the chosen one.
    QCOMPARE(GameModeController::findOutput(outputs, kDesk, kTvDescription), 1);
    QCOMPARE(GameModeController::findOutput(outputs, kDesk, QStringLiteral("LG OLED")), -1);
    // A display with no description is matched by connector alone.
    QCOMPARE(GameModeController::findOutput(outputs, kDesk, {}), 0);
    QCOMPARE(GameModeController::findOutput(outputs, QStringLiteral("DP-9"), {}), -1);
    // Two identical displays are told apart by connector.
    const QVector<GameModeOutput> twins{output(0, "DP-1", "Twin", true, true, "1"),
                                        output(1, "DP-2", "Twin", true, false, "2")};
    QCOMPARE(GameModeController::findOutput(twins, "DP-2", "Twin"), 1);
    QCOMPARE(GameModeController::findOutput(twins, "DP-7", "Twin"), 0);
  }

  void monitorsAreParsed() {
    const QByteArray json = R"([
      {"id":0,"name":"DP-2","description":"Dell S2721DGF","width":2560,"height":1440,
       "disabled":false,"focused":true,"activeWorkspace":{"id":3,"name":"3"}},
      {"id":1,"name":"HDMI-A-2","description":"Samsung QBQ90","width":0,"height":0,
       "disabled":true,"focused":false,"activeWorkspace":{"id":0,"name":""}},
      {"id":2,"name":"DP-3","description":"","width":1920,"height":1080,
       "disabled":false,"focused":false,"activeWorkspace":{"id":-1337,"name":"omakade"}},
      {"description":"nameless"}])";
    const auto outputs = HyprlandGameModeCompositor::parseOutputs(json);
    QCOMPARE(outputs.size(), 3);
    QCOMPARE(outputs.at(0).workspace, QStringLiteral("3"));
    QVERIFY(outputs.at(0).enabled && outputs.at(0).focused);
    QVERIFY(!outputs.at(1).enabled);
    QVERIFY(outputs.at(1).workspace.isEmpty());
    QCOMPARE(outputs.at(2).workspace, GameModeController::workspace());

    QString error;
    QVERIFY(HyprlandGameModeCompositor::parseOutputs("{", &error).isEmpty());
    QVERIFY(!error.isEmpty());
  }

  void ownWindowIsFoundAmongClients() {
    const auto outputs = HyprlandGameModeCompositor::parseOutputs(
        R"([{"id":0,"name":"DP-2","disabled":false,"activeWorkspace":{"id":3,"name":"3"}},
            {"id":1,"name":"HDMI-A-2","disabled":false,"activeWorkspace":{"id":5,"name":"5"}}])");
    const QByteArray clients = R"([
      {"address":"0xaaa1","mapped":true,"pid":77,"class":"steam","monitor":0,
       "workspace":{"id":3,"name":"3"}},
      {"address":"0xbbb2","mapped":true,"pid":100,"class":"omakade-file-dialog","monitor":0,
       "workspace":{"id":3,"name":"3"}},
      {"address":"0xccc3","mapped":false,"pid":100,"class":"io.github.tsouth89.Omakade",
       "monitor":0,"workspace":{"id":3,"name":"3"}},
      {"address":"0xddd4","mapped":true,"pid":100,"class":"io.github.tsouth89.Omakade",
       "monitor":1,"workspace":{"id":-1337,"name":"omakade"}},
      {"address":"not-an-address","mapped":true,"pid":100,"class":"io.github.tsouth89.Omakade",
       "monitor":1,"workspace":{"id":5,"name":"5"}}])";
    const GameModeWindow window = HyprlandGameModeCompositor::parseWindow(clients, outputs, 100);
    QCOMPARE(window.address, QStringLiteral("0xddd4"));
    QCOMPARE(window.output, kTv);
    QCOMPARE(window.workspace, GameModeController::workspace());
    QVERIFY(!HyprlandGameModeCompositor::parseWindow(clients, outputs, 5).valid());
    QVERIFY(!HyprlandGameModeCompositor::parseWindow("nope", outputs, 100).valid());

    // The placeholder shares the window class and is told apart by its title.
    const QByteArray withPlaceholder = R"([
      {"address":"0xeee5","mapped":true,"pid":100,"class":"io.github.tsouth89.Omakade",
       "title":"Omakade Game Mode Placeholder — Omakade","fullscreen":2,"monitor":0,
       "workspace":{"id":-98,"name":"special:omakade"}},
      {"address":"0xddd4","mapped":true,"pid":100,"class":"io.github.tsouth89.Omakade",
       "title":"Omakade","floating":true,"monitor":0,"workspace":{"id":3,"name":"3"}}])";
    const GameModeWindow main =
        HyprlandGameModeCompositor::parseWindow(withPlaceholder, outputs, 100);
    QCOMPARE(main.address, QStringLiteral("0xddd4"));
    QVERIFY(main.floating);
    QVERIFY(!main.fullscreen);
    const GameModeWindow held =
        HyprlandGameModeCompositor::parseWindow(withPlaceholder, outputs, 100, true);
    QCOMPARE(held.address, QStringLiteral("0xeee5"));
    QCOMPARE(held.workspace, QStringLiteral("special:omakade"));
    QVERIFY(held.fullscreen);
    QVERIFY(!HyprlandGameModeCompositor::parseWindow(clients, outputs, 100, true).valid());
  }

  void scriptsQuoteEveryName() {
    QCOMPARE(HyprlandGameModeCompositor::luaString("HDMI-A-2"), QStringLiteral("\"HDMI-A-2\""));
    QCOMPARE(HyprlandGameModeCompositor::luaString("a\"b\\c\nd"),
             QStringLiteral("\"a\\\"b\\\\c\\010d\""));
    QCOMPARE(HyprlandGameModeCompositor::outputScript("HDMI-A-2", true),
             QStringLiteral("hl.monitor({ output = \"HDMI-A-2\", disabled = false })"));
    QCOMPARE(HyprlandGameModeCompositor::outputScript("x\" }) os.exit() --", false),
             QStringLiteral("hl.monitor({ output = \"x\\\" }) os.exit() --\", disabled = true })"));
    // Hyprland matches a rule against the whole title, which Qt ends with " — Omakade".
    QVERIFY(HyprlandGameModeCompositor::holdScript().contains(
        QStringLiteral("title = \"^Omakade Game Mode Placeholder.*\"")));
    const QString place =
        HyprlandGameModeCompositor::placeScript("0xddd4", "name:omakade", "HDMI-A-2");
    QVERIFY(place.startsWith("hl.dispatch(hl.dsp.focus({ monitor = \"HDMI-A-2\" }))"));
    QVERIFY(place.contains(
        "hl.dsp.window.move({ window = \"address:0xddd4\", workspace = \"name:omakade\" })"));
    QVERIFY(place.endsWith("hl.dispatch(hl.dsp.focus({ window = \"address:0xddd4\" }))"));
    QVERIFY(HyprlandGameModeCompositor::returnScript("0xddd4", "3").contains("follow = false"));
    QVERIFY(HyprlandGameModeCompositor::validAddress("0x55d0c0ffee00"));
    QVERIFY(!HyprlandGameModeCompositor::validAddress("0x55\" })"));
    QVERIFY(!HyprlandGameModeCompositor::validAddress(""));
  }

  void shortcutIsOneLineThatCanBeAddedAndRemoved() {
    const QString stock = QStringLiteral(
        "-- Add a new binding.\n"
        "-- o.bind(\"SUPER + CTRL + G\", \"Game Mode\", \"omakade --game-mode-toggle\")\n"
        "o.bind(\"SUPER + H\", nil, \"voxtype record toggle\")\n");
    // A commented example is not a binding.
    QVERIFY(GameModeShortcut::boundKey(stock).isEmpty());

    const QString added = GameModeShortcut::withBinding(stock);
    QVERIFY(added.startsWith(stock));
    QVERIFY(added.endsWith(GameModeShortcut::bindingLine() + QLatin1Char('\n')));
    QCOMPARE(GameModeShortcut::boundKey(added), QStringLiteral("SUPER + CTRL + G"));
    // Adding twice changes nothing, and removing gives back the file as it was.
    QCOMPARE(GameModeShortcut::withBinding(added), added);
    QCOMPARE(GameModeShortcut::withoutBinding(added), stock);

    // A binding the user wrote on another key counts, and only that line is removed.
    const QString own = stock + QStringLiteral(
        "  o.bind(\"SUPER + F9\", \"Couch\", \"omakade --game-mode\")\n"
        "o.bind(\"SUPER + F10\", \"Leave\", \"omakade --game-mode-exit\")\n");
    QCOMPARE(GameModeShortcut::boundKey(own), QStringLiteral("SUPER + F9"));
    QCOMPARE(GameModeShortcut::withBinding(own), own);
    const QString removed = GameModeShortcut::withoutBinding(own);
    QVERIFY(!removed.contains("SUPER + F9"));
    QVERIFY(removed.contains("SUPER + F10"));
    QVERIFY(removed.contains("SUPER + H"));
    const QString multiple = own + QStringLiteral(
        "o.bind(\"SUPER + F8\", \"Game Mode\", \"omakade --game-mode-toggle\")\n");
    QCOMPARE(GameModeShortcut::removalScript(multiple),
             QStringLiteral("hl.unbind(\"SUPER + F9\")\nhl.unbind(\"SUPER + F8\")"));
    QVERIFY(GameModeShortcut::removalScript(stock).isEmpty());

    QCOMPARE(GameModeShortcut::withBinding({}),
             QStringLiteral("-- Omakade Game Mode. Press it again to leave. Added by Omakade.\n") +
                 GameModeShortcut::bindingLine() + QLatin1Char('\n'));
    // Hyprland may also reload the edited file on its own; clearing the key first keeps
    // that from leaving two bindings that each toggle Game Mode.
    QCOMPARE(GameModeShortcut::liveBindingScript(),
             QStringLiteral("hl.unbind(\"SUPER + CTRL + G\")\n") + GameModeShortcut::bindingLine());
    QCOMPARE(GameModeShortcut::displayKey("SUPER + CTRL + G"), QStringLiteral("Super + Ctrl + G"));
    QCOMPARE(GameModeShortcut::displayKey("SUPER+F9"), QStringLiteral("Super + F9"));
  }

  void shortcutKeyInUseIsReported() {
    const QByteArray binds = R"([
      {"modmask":65,"key":"G","description":"Signal"},
      {"modmask":64,"key":"G","description":"Toggle window grouping"}])";
    QVERIFY(GameModeShortcut::takenBy(binds).isEmpty());
    QCOMPARE(GameModeShortcut::takenBy(R"([{"modmask":68,"key":"g","description":"Herdr"}])"),
             QStringLiteral("Herdr"));
    QCOMPARE(GameModeShortcut::takenBy(R"([{"modmask":68,"key":"G","description":""}])"),
             QStringLiteral("another shortcut"));
    // A matching description is not evidence that Omakade owns the binding.
    QCOMPARE(GameModeShortcut::takenBy(
                 R"([{"modmask":68,"key":"G","description":"Game Mode","dispatcher":"exec","arg":"steam -gamepadui"}])"),
             QStringLiteral("Game Mode"));
    QCOMPARE(GameModeShortcut::takenBy(
                 R"([{"modmask":68,"key":"G","description":"Game Mode","dispatcher":"__lua","arg":"210"}])"),
             QStringLiteral("Game Mode"));
    QVERIFY(GameModeShortcut::takenBy(
                R"([{"modmask":68,"key":"G","description":"Game Mode","dispatcher":"exec","arg":"omakade --game-mode-toggle"}])")
                .isEmpty());
    QVERIFY(GameModeShortcut::takenBy("nope").isEmpty());
  }

  // A Steam game is started by Steam, not Omakade, so the window is what shows it is there.
  void otherWindowsOnTheGameModeWorkspaceAreCounted() {
    const QByteArray clients = R"([
      {"address":"0xa1","mapped":true,"pid":100,"workspace":{"id":-1337,"name":"omakade"}},
      {"address":"0xa2","mapped":true,"pid":200,"workspace":{"id":-1337,"name":"omakade"}},
      {"address":"0xa3","mapped":false,"pid":300,"workspace":{"id":-1337,"name":"omakade"}},
      {"address":"0xa4","mapped":true,"pid":400,"workspace":{"id":1,"name":"1"}}])";
    QCOMPARE(HyprlandGameModeCompositor::countOtherWindows(clients,
                                                           GameModeController::workspace(), 100),
             1);
    QCOMPARE(HyprlandGameModeCompositor::countOtherWindows(clients, QStringLiteral("1"), 100), 1);
    QCOMPARE(HyprlandGameModeCompositor::countOtherWindows("nope", QStringLiteral("1"), 100), 0);

    // The addresses are the ones the count comes from, and only mapped, other-pid,
    // valid-address windows on that workspace qualify.
    const QByteArray withJunk = R"([
      {"address":"0xa1","mapped":true,"pid":100,"workspace":{"id":-1337,"name":"omakade"}},
      {"address":"0xa2","mapped":true,"pid":200,"workspace":{"id":-1337,"name":"omakade"}},
      {"address":"0xa3","mapped":false,"pid":300,"workspace":{"id":-1337,"name":"omakade"}},
      {"address":"0xa4","mapped":true,"pid":400,"workspace":{"id":1,"name":"1"}},
      {"address":"nope","mapped":true,"pid":500,"workspace":{"id":-1337,"name":"omakade"}}])";
    QCOMPARE(HyprlandGameModeCompositor::otherWindowAddresses(
                 withJunk, GameModeController::workspace(), 100),
             QStringList{QStringLiteral("0xa2")});
    QCOMPARE(HyprlandGameModeCompositor::otherWindowAddresses(withJunk, QStringLiteral("1"), 100),
             QStringList{QStringLiteral("0xa4")});
    QVERIFY(HyprlandGameModeCompositor::otherWindowAddresses("nope",
                                                             GameModeController::workspace(), 100)
                .isEmpty());
  }

  // A game left running when Game Mode is left must come home with Omakade instead of
  // being stranded on the workspace that goes away.
  void leavingGameModeBringsLeftBehindWindowsToTheDesktop() {
    deskAndTv(true);
    m_compositor.others = {{QStringLiteral("0x9a01"), GameModeController::workspace()}};
    GameModeController game = controller();
    QVERIFY(game.enter({}, 100).ok);

    const auto left = game.exit(100);
    QVERIFY2(left.ok, qPrintable(left.error));
    QCOMPARE(m_compositor.others.at(0).workspace, QStringLiteral("3"));
    QVERIFY(m_compositor.log.contains(QStringLiteral("return 0x9a01 3")));
  }

  // An interrupted session leaves the same windows behind, and recovery brings them home.
  void recoveryBringsLeftBehindWindowsToTheDesktop() {
    deskAndTv(true);
    m_compositor.others = {{QStringLiteral("0x9a01"), GameModeController::workspace()}};
    GameModeState state;
    state.ownerPid = 4242;
    state.windowWorkspace = QStringLiteral("3");
    state.windowPlaced = true;
    QFile file(statePath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(state.toJson()).toJson());
    file.close();

    GameModeController game = controller(false);
    const auto recovered = game.recover();
    QVERIFY2(recovered.ok, qPrintable(recovered.error));
    QCOMPARE(m_compositor.others.at(0).workspace, QStringLiteral("3"));
    QVERIFY(m_compositor.log.contains(QStringLiteral("return 0x9a01 3")));
  }

  void workspaceSelectorsMatchDispatchers() {
    using Compositor = HyprlandGameModeCompositor;
    QCOMPARE(Compositor::workspaceSelector({{"id", 3}, {"name", "3"}}), QStringLiteral("3"));
    QCOMPARE(Compositor::workspaceSelector({{"id", -1337}, {"name", "omakade"}}),
             QStringLiteral("name:omakade"));
    QCOMPARE(Compositor::workspaceSelector({{"id", -98}, {"name", "special:scratchpad"}}),
             QStringLiteral("special:scratchpad"));
    QVERIFY(Compositor::workspaceSelector({{"id", 0}, {"name", ""}}).isEmpty());
  }

  void sinksAreParsed() {
    const auto sinks = PactlGameModeAudio::parseSinks(
        R"([{"index":51,"name":"alsa_output.hdmi-stereo","description":"QBQ90 HDMI"},
            {"index":52,"name":"headset","description":""},{"index":53}])");
    QCOMPARE(sinks.size(), 2);
    QCOMPARE(sinks.at(0).description, QStringLiteral("QBQ90 HDMI"));
    QCOMPARE(sinks.at(1).name, QStringLiteral("headset"));
    QVERIFY(PactlGameModeAudio::parseSinks("[").isEmpty());
  }

  void sessionCyclesChoicesAndKeepsThem() {
    deskAndTv(false);
    m_audio.list.append({kTvSink, QStringLiteral("QBQ90 HDMI")});
    const QString settingsPath = m_directory.filePath("cycle/game-mode.json");
    GameModeSession session(&m_compositor, &m_audio, &m_notifications, settingsPath, statePath());
    QCOMPARE(session.displayChoices(), 1);
    session.refresh();
    QTRY_VERIFY(session.displayManaged() && session.soundManaged());
    QCOMPARE(session.displayChoices(), 3);
    QCOMPARE(session.soundChoices(), 3);
    QCOMPARE(session.displayLabel(), QStringLiteral("Current display"));
    QCOMPARE(session.soundLabel(), QStringLiteral("Current sound output"));

    session.cycleDisplay();
    QCOMPARE(session.displayLabel(), QStringLiteral("Dell S2721DGF (DP-2)"));
    session.cycleDisplay();
    QCOMPARE(session.displayLabel(),
             QStringLiteral("%1 (%2) · off until Game Mode").arg(kTvDescription, kTv));
    session.cycleSound();
    QCOMPARE(session.soundLabel(), QStringLiteral("Headset"));
    session.cycleSound();
    QCOMPARE(session.soundLabel(), QStringLiteral("QBQ90 HDMI"));
    const GameModeSettings saved = GameModeSession::loadSettings(settingsPath);
    QCOMPARE(saved.outputName, kTv);
    QCOMPARE(saved.outputDescription, kTvDescription);
    QCOMPARE(saved.sinkName, kTvSink);

    // An unplugged choice is shown as missing, not silently replaced.
    m_compositor.list.removeLast();
    m_audio.list.removeLast();
    session.refresh();
    QTRY_COMPARE(session.displayChoices(), 2);
    QCOMPARE(session.displayLabel(), QStringLiteral("%1 · not connected").arg(kTvDescription));
    QCOMPARE(session.soundLabel(), QStringLiteral("%1 · not available").arg(kTvSink));
    session.cycleDisplay();
    QCOMPARE(session.displayLabel(), QStringLiteral("Current display"));
    session.cycleSound();
    QCOMPARE(session.soundLabel(), QStringLiteral("Current sound output"));
  }

  void sessionListsAndSelectsDevicesWithoutLosingMissingChoices() {
    deskAndTv(false);
    m_audio.list.append({kTvSink, QStringLiteral("QBQ90 HDMI")});
    const QString settingsPath = m_directory.filePath("select/game-mode.json");
    GameModeSession session(&m_compositor, &m_audio, &m_notifications, settingsPath, statePath());
    session.refresh();
    QTRY_COMPARE(session.displayOptions().size(), 3);
    QCOMPARE(session.displayIndex(), 0);
    session.selectDisplay(2);
    session.selectSound(2);
    QCOMPARE(session.displayIndex(), 2);
    QCOMPARE(session.soundIndex(), 2);
    QVERIFY(session.displayOptions().at(2).toMap().value("label").toString().contains("off until"));
    session.selectDisplay(-1);
    session.selectSound(99);
    QCOMPARE(session.settings().outputName, kTv);
    QCOMPARE(session.settings().sinkName, kTvSink);

    m_compositor.list.removeLast();
    m_audio.list.removeLast();
    session.refresh();
    QTRY_VERIFY(!session.displayOptions().last().toMap().value("available").toBool());
    QCOMPARE(session.displayIndex(), 2);
    QCOMPARE(session.soundIndex(), 2);
    QVERIFY(!session.soundOptions().last().toMap().value("available").toBool());
    QCOMPARE(GameModeSession::loadSettings(settingsPath).outputName, kTv);
    session.selectDisplay(0);
    session.selectSound(0);
    QVERIFY(session.settings().outputName.isEmpty());
    QVERIFY(session.settings().sinkName.isEmpty());
  }

  void sessionStartAndLeaveDoNotBlockOnDiscovery() {
    m_compositor.list = {output(0, kDesk, "Desk", true, true, "3")};
    m_compositor.window = {kAddress, "3", kDesk};
    QSemaphore discoveryStarted, continueDiscovery;
    std::atomic_bool pauseDiscovery{false};
    m_compositor.beforeOutputs = [&] {
      if (pauseDiscovery.exchange(false)) {
        discoveryStarted.release();
        continueDiscovery.tryAcquire(1, 1000);
      }
    };
    GameModeSession session(&m_compositor, &m_audio, &m_notifications,
                            m_directory.filePath("responsive-settings.json"), statePath());
    for (bool entering : {true, false}) {
      // Wait until any refresh from the previous transition has settled.
      QSignalSpy devices(&session, &GameModeSession::devicesChanged);
      session.refresh();
      QTRY_VERIFY(!devices.isEmpty());
      pauseDiscovery = true;
      session.refresh();
      QVERIFY(discoveryStarted.tryAcquire(1, 1000));
      QElapsedTimer elapsed;
      elapsed.start();
      if (entering) session.enter(); else session.exit();
      const auto duration = elapsed.elapsed();
      continueDiscovery.release();
      QVERIFY2(duration < 200, "Device discovery blocked the UI transition");
      QTRY_VERIFY(!session.busy());
      QCOMPARE(session.active(), entering);
    }
  }

  void sessionSignalsTheWindowInOrder() {
    deskAndTv(true);
    const QString settingsPath = m_directory.filePath("order/game-mode.json");
    QVERIFY(GameModeSession::saveSettings(settingsPath, tvSettings()));
    GameModeSession session(&m_compositor, &m_audio, &m_notifications, settingsPath, statePath());
    QStringList order;
    connect(&session, &GameModeSession::entered, this, [&order] { order.append("entered"); });
    connect(&session, &GameModeSession::leaving, this, [&order] { order.append("leaving"); });
    connect(&session, &GameModeSession::exited, this, [&order] { order.append("exited"); });
    connect(&session, &GameModeSession::failed, this, [&order] { order.append("failed"); });

    session.enter();
    QVERIFY(session.busy());
    QTRY_VERIFY(session.active() && !session.busy());
    QCOMPARE(m_compositor.window.output, kTv);
    // Choices are fixed while a session is using them.
    session.cycleDisplay();
    session.selectDisplay(0);
    session.selectSound(0);
    session.setSilenceNotifications(false);
    QCOMPARE(session.settings().outputName, kTv);
    QVERIFY(session.silenceNotifications());

    session.exit();
    QTRY_VERIFY(!session.active() && !session.busy());
    QCOMPARE(order, (QStringList{"entered", "leaving", "exited"}));
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QVERIFY(!QFile::exists(statePath()));
  }

  void sessionSaysWhyItCouldNotStart() {
    m_compositor.list = {
        output(0, kDesk, QStringLiteral("Dell S2721DGF"), true, true, QStringLiteral("3"))};
    m_compositor.window = {kAddress, QStringLiteral("3"), kDesk};
    const QString settingsPath = m_directory.filePath("missing/game-mode.json");
    QVERIFY(GameModeSession::saveSettings(settingsPath, tvSettings()));
    GameModeSession session(&m_compositor, &m_audio, &m_notifications, settingsPath, statePath());
    QSignalSpy failed(&session, &GameModeSession::failed);
    QSignalSpy entered(&session, &GameModeSession::entered);
    session.enter();
    QTRY_COMPARE(failed.size(), 1);
    QVERIFY(failed.first().first().toString().contains("not connected"));
    QVERIFY(!session.active() && !session.busy());
    QVERIFY(session.statusText().contains("not connected"));
    QCOMPARE(entered.size(), 0);
  }

  void closingOmakadeLeavesGameMode() {
    deskAndTv(true);
    m_audio.list.append({kTvSink, QStringLiteral("QBQ90 HDMI")});
    const QString settingsPath = m_directory.filePath("shutdown/game-mode.json");
    QVERIFY(GameModeSession::saveSettings(settingsPath, tvSettings(kTvSink)));
    GameModeSession session(&m_compositor, &m_audio, &m_notifications, settingsPath, statePath());
    session.enter();
    QTRY_VERIFY(session.active());
    QCOMPARE(m_audio.current, kTvSink);
    session.shutdown();
    QVERIFY(!session.active());
    QCOMPARE(m_audio.current, QStringLiteral("headset"));
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QVERIFY(!m_notifications.quiet);
    QVERIFY(!QFile::exists(statePath()));
  }

  void choicesSurviveARestart() {
    const QString path = m_directory.filePath("nested/game-mode.json");
    const GameModeSettings defaults = GameModeSession::loadSettings(path);
    QVERIFY(defaults.outputName.isEmpty() && defaults.sinkName.isEmpty());
    QVERIFY(defaults.silenceNotifications);

    GameModeSettings chosen = tvSettings(kTvSink);
    chosen.silenceNotifications = false;
    QVERIFY(GameModeSession::saveSettings(path, chosen));
    const GameModeSettings loaded = GameModeSession::loadSettings(path);
    QCOMPARE(loaded.outputName, kTv);
    QCOMPARE(loaded.outputDescription, kTvDescription);
    QCOMPARE(loaded.sinkName, kTvSink);
    QVERIFY(!loaded.silenceNotifications);
  }
};

QTEST_GUILESS_MAIN(GameModeTests)
#include "GameModeTests.moc"
