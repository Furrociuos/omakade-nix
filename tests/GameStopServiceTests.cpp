#include "library/GameStopService.h"
#include "tracking/GameStop.h"
#include "tracking/ProcFs.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

namespace {

class FakeSink final : public GameStop::SignalSink {
public:
  struct Call {
    QString lever;
    qint64 pid = 0;
    QString text;
    bool forced = false;
  };

  QVector<Call> calls;
  GameStop::LeverResult terminateResult = GameStop::LeverResult::Done;
  GameStop::LeverResult forceResult = GameStop::LeverResult::Done;

  GameStop::LeverResult terminate(qint64 pid) override {
    calls.append({QStringLiteral("terminate"), pid, {}, false});
    return terminateResult;
  }
  GameStop::LeverResult forceTerminate(qint64 pid) override {
    calls.append({QStringLiteral("force"), pid, {}, false});
    return forceResult;
  }
  GameStop::LeverResult stopWinePrefix(const QString& prefix, bool force) override {
    calls.append({QStringLiteral("prefix"), 0, prefix, force});
    return GameStop::LeverResult::Done;
  }
  GameStop::LeverResult stopFlatpakApp(const QString& appId) override {
    calls.append({QStringLiteral("flatpak"), 0, appId, false});
    return GameStop::LeverResult::Done;
  }

  [[nodiscard]] QStringList levers() const {
    QStringList names;
    for (const Call& call : calls) {
      names.append(call.forced ? call.lever + QStringLiteral("-forced") : call.lever);
    }
    return names;
  }
};

ProcessSnapshot process(qint64 pid, qint64 procStart, const QString& comm,
                        const QStringList& arguments, const QString& exePath = {}) {
  ProcessSnapshot snapshot;
  snapshot.pid = pid;
  snapshot.procStart = procStart;
  snapshot.comm = comm;
  snapshot.arguments = arguments;
  snapshot.exePath = exePath;
  return snapshot;
}

QVariantMap row(const QString& source, const QString& appId, const QString& title) {
  return QVariantMap{{QStringLiteral("source"), source},
                     {QStringLiteral("appId"), appId},
                     {QStringLiteral("title"), title}};
}

bool anyLineContains(const QVariantList& lines, const QString& fragment) {
  for (const QVariant& line : lines) {
    if (line.toString().contains(fragment)) {
      return true;
    }
  }
  return false;
}

bool anyStringContains(const QStringList& strings, const QString& fragment) {
  for (const QString& value : strings) {
    if (value.contains(fragment)) {
      return true;
    }
  }
  return false;
}

} // namespace

class GameStopServiceTests : public QObject {
  Q_OBJECT

private slots:
  // The mapping from a library row to an identity is the part that can be
  // wrong, so each source is pinned.
  void identityForDerivesThePrefixPerSource() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString library = temporary.path() + QStringLiteral("/Library");
    const QString steamInstall = library + QStringLiteral("/steamapps/common/Frostpunk");
    const QString prefix = library + QStringLiteral("/steamapps/compatdata/323190/pfx");
    QVERIFY(QDir().mkpath(steamInstall));
    QVERIFY(QDir().mkpath(prefix));

    QVariantMap steam = row(QStringLiteral("Steam"), QStringLiteral("323190"),
                            QStringLiteral("Frostpunk"));
    steam.insert(QStringLiteral("installPath"), steamInstall);
    const GameStop::GameIdentity steamIdentity = GameStopService::identityFor(steam);
    QCOMPARE(steamIdentity.winePrefixes, QStringList{prefix});
    QVERIFY(steamIdentity.flatpakAppId.isEmpty());
    QCOMPARE(steamIdentity.installPath, steamInstall);

    // A Battle.net row stores its prefix in the launch target.
    QVariantMap battleNet = row(QStringLiteral("Battle.net"), QStringLiteral("wow"),
                                QStringLiteral("World of Warcraft"));
    battleNet.insert(QStringLiteral("launchTarget"),
                     QStringLiteral("/home/user/.local/share/bottles/bottles/battlenet"));
    const GameStop::GameIdentity battleNetIdentity = GameStopService::identityFor(battleNet);
    QCOMPARE(battleNetIdentity.winePrefixes,
             QStringList{QStringLiteral("/home/user/.local/share/bottles/bottles/battlenet")});

    // An emulator row's content path is what a session is matched on.
    QVariantMap retroArch = row(QStringLiteral("RetroArch"), QStringLiteral("snes"),
                                QStringLiteral("Chrono Trigger"));
    retroArch.insert(QStringLiteral("installPath"), QStringLiteral("/games/snes/Chrono.sfc"));
    retroArch.insert(QStringLiteral("launchTarget"), QStringLiteral("/cores/snes9x_lr.so"));
    const GameStop::GameIdentity emulatorIdentity = GameStopService::identityFor(retroArch);
    QCOMPARE(emulatorIdentity.gamePaths,
             (QStringList{QStringLiteral("/games/snes/Chrono.sfc"),
                          QStringLiteral("/cores/snes9x_lr.so")}));
    QCOMPARE(emulatorIdentity.emulator, QStringLiteral("RetroArch"));

    // A flatpak emulator keeps its app id in the runner role, and a wine runner
    // name is not an app id.
    QVariantMap ryujinx = row(QStringLiteral("Ryujinx"), QStringLiteral("zelda"),
                              QStringLiteral("Zelda"));
    ryujinx.insert(QStringLiteral("flatpak"), true);
    ryujinx.insert(QStringLiteral("runner"), QStringLiteral("io.github.ryubing.Ryujinx"));
    QCOMPARE(GameStopService::identityFor(ryujinx).flatpakAppId,
             QStringLiteral("io.github.ryubing.Ryujinx"));
    ryujinx.insert(QStringLiteral("runner"), QStringLiteral("proton"));
    QCOMPARE(GameStopService::identityFor(ryujinx).flatpakAppId,
             QStringLiteral("io.github.ryubing.Ryujinx"));
    // Without the flatpak flag the app id is not used at all.
    ryujinx.insert(QStringLiteral("flatpak"), false);
    QVERIFY(GameStopService::identityFor(ryujinx).flatpakAppId.isEmpty());
  }

  void previewNamesTheTargetsAndNothingElse() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString library = temporary.path() + QStringLiteral("/Library");
    const QString steamInstall = library + QStringLiteral("/steamapps/common/Frostpunk");
    const QString prefix = library + QStringLiteral("/steamapps/compatdata/323190/pfx");
    QVERIFY(QDir().mkpath(steamInstall));
    QVERIFY(QDir().mkpath(prefix));

    QVariantMap steam = row(QStringLiteral("Steam"), QStringLiteral("323190"),
                            QStringLiteral("Frostpunk"));
    steam.insert(QStringLiteral("installPath"), steamInstall);

    GameStopService service;
    service.setSnapshotProvider([] { return QVector<ProcessSnapshot>{}; });
    const QVariantList lines = service.preview(steam);
    QCOMPARE(lines.size(), 1);
    QVERIFY2(lines.first().toString().contains(prefix), qPrintable(lines.first().toString()));

    // A row with nothing to attribute says so rather than showing an empty list.
    QVariantMap nothing = row(QStringLiteral("Manual"), QStringLiteral("thing"),
                              QStringLiteral("Thing"));
    nothing.insert(QStringLiteral("installPath"), QStringLiteral("/usr/bin/wine"));
    QVERIFY(service.preview(nothing).isEmpty());
    QVERIFY(anyStringContains(service.notesFor(nothing), QStringLiteral("Nothing attributable")));
  }

  void liveGamesKeepsOnlyWhatHasATarget() {
    GameStopService service;
    service.setSnapshotProvider([] {
      return QVector<ProcessSnapshot>{process(90, 9000, QStringLiteral("game"),
                                             {QStringLiteral("game")},
                                             QStringLiteral("/games/native/Thing/game"))};
    });
    QVariantMap live = row(QStringLiteral("Manual"), QStringLiteral("live"),
                           QStringLiteral("Live Game"));
    live.insert(QStringLiteral("installPath"), QStringLiteral("/games/native/Thing"));
    QVariantMap idle = row(QStringLiteral("Manual"), QStringLiteral("idle"),
                           QStringLiteral("Not Running"));
    idle.insert(QStringLiteral("installPath"), QStringLiteral("/games/native/Other"));
    service.setRowsProvider([live, idle] { return QVariantList{live, idle}; });

    const QVariantList games = service.liveGames();
    QCOMPARE(games.size(), 1);
    const QVariantMap found = games.first().toMap();
    QCOMPARE(found.value(QStringLiteral("title")).toString(), QStringLiteral("Live Game"));
    QVERIFY(anyLineContains(found.value(QStringLiteral("lines")).toList(),
                            QStringLiteral("pid 90")));
  }

  // The point of the two-phase stop in the service: the interface thread is not
  // the one that runs the levers, and the report says what actually happened.
  void stopRunsOffTheCallingThreadAndReportsWhatClosed() {
    GameStopService service;
    FakeSink sink;
    service.setSignalSink(&sink);
    service.setGracePeriodMs(0);
    // It ignores the graceful signal and dies to the forced one: the first two
    // reads see it alive (the graceful step and the forced step), and the read
    // the post-check makes sees it gone.
    int reads = 0;
    QThread* livenessThread = nullptr;
    service.setLiveness([&reads, &livenessThread](qint64, qint64) {
      livenessThread = QThread::currentThread();
      return ++reads <= 2;
    });
    service.setSnapshotProvider([] {
      return QVector<ProcessSnapshot>{
          process(91, 9100, QStringLiteral("game"), {QStringLiteral("game")},
                  QStringLiteral("/games/native/Thing/game"))};
    });
    QVariantMap game = row(QStringLiteral("Manual"), QStringLiteral("thing"),
                           QStringLiteral("Thing"));
    game.insert(QStringLiteral("installPath"), QStringLiteral("/games/native/Thing"));

    QSignalSpy finished(&service, &GameStopService::finished);
    QVERIFY(service.stop(game));
    // The call returned before the levers ran, so the interface stays responsive.
    QVERIFY(service.busy());
    // The process ignores the graceful signal, so the forced step reaches it.
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.count(), 1);
    QVERIFY2(livenessThread != nullptr, "the levers never checked liveness");
    QVERIFY2(livenessThread != QThread::currentThread(),
             "the levers ran on the calling thread");
    QCOMPARE(sink.levers(), (QStringList{QStringLiteral("terminate"), QStringLiteral("force")}));
    QCOMPARE(finished.first().at(0).toBool(), true);
    QVERIFY2(anyLineContains(finished.first().at(2).toList(), QStringLiteral("closed")),
             "a process that died to the forced signal was not reported as closed");
    QVERIFY2(!anyLineContains(finished.first().at(2).toList(), QStringLiteral("still running")),
             "a process that died was reported as still running");
  }

  void aProcessThatSurvivesIsReportedRatherThanCalledDone() {
    GameStopService service;
    FakeSink sink;
    service.setSignalSink(&sink);
    service.setGracePeriodMs(0);
    service.setLiveness([](qint64, qint64) { return true; });  // it never dies
    service.setSnapshotProvider([] {
      return QVector<ProcessSnapshot>{
          process(92, 9200, QStringLiteral("game"), {QStringLiteral("game")},
                  QStringLiteral("/games/native/Thing/game"))};
    });
    QVariantMap game = row(QStringLiteral("Manual"), QStringLiteral("thing"),
                           QStringLiteral("Thing"));
    game.insert(QStringLiteral("installPath"), QStringLiteral("/games/native/Thing"));

    QSignalSpy finished(&service, &GameStopService::finished);
    QVERIFY(service.stop(game));
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.first().at(0).toBool(), false);
    const QVariantList lines = finished.first().at(2).toList();
    QVERIFY2(anyLineContains(lines, QStringLiteral("still running")),
             "a surviving process was reported as closed");
    QCOMPARE(sink.levers(), (QStringList{QStringLiteral("terminate"), QStringLiteral("force")}));
  }

  void aScopeTargetGoesToTheMatchingLever() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString library = temporary.path() + QStringLiteral("/Library");
    const QString steamInstall = library + QStringLiteral("/steamapps/common/Frostpunk");
    const QString prefix = library + QStringLiteral("/steamapps/compatdata/323190/pfx");
    QVERIFY(QDir().mkpath(steamInstall));
    QVERIFY(QDir().mkpath(prefix));

    GameStopService service;
    FakeSink sink;
    service.setSignalSink(&sink);
    service.setGracePeriodMs(0);
    service.setLiveness([](qint64, qint64) { return false; });
    service.setSnapshotProvider([] { return QVector<ProcessSnapshot>{}; });
    QVariantMap steam = row(QStringLiteral("Steam"), QStringLiteral("323190"),
                            QStringLiteral("Frostpunk"));
    steam.insert(QStringLiteral("installPath"), steamInstall);

    QSignalSpy finished(&service, &GameStopService::finished);
    QVERIFY(service.stop(steam));
    QVERIFY(finished.wait(5000));
    // The prefix takes the graceful step and, since the wineserver reported
    // success and a prefix is still pending, the forced one after the wait.
    QCOMPARE(sink.calls.size(), 2);
    QCOMPARE(sink.calls.first().lever, QStringLiteral("prefix"));
    QCOMPARE(sink.calls.first().text, prefix);
    QCOMPARE(sink.calls.first().forced, false);
    QCOMPARE(sink.calls.at(1).forced, true);
    QCOMPARE(finished.first().at(0).toBool(), true);
  }

  void stopRefusesWhenNothingIsAttributable() {
    GameStopService service;
    FakeSink sink;
    service.setSignalSink(&sink);
    service.setSnapshotProvider([] { return QVector<ProcessSnapshot>{}; });
    QVariantMap idle = row(QStringLiteral("Manual"), QStringLiteral("idle"),
                           QStringLiteral("Not Running"));
    idle.insert(QStringLiteral("installPath"), QStringLiteral("/games/native/Other"));
    QSignalSpy finished(&service, &GameStopService::finished);
    QVERIFY(!service.stop(idle));
    QVERIFY(!service.busy());
    QVERIFY(sink.calls.isEmpty());
    QCOMPARE(finished.count(), 0);
  }

  void stopAllReportsEveryLiveGameOnce() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString first = temporary.path() + QStringLiteral("/One");
    const QString second = temporary.path() + QStringLiteral("/Two");
    QVERIFY(QDir().mkpath(first));
    QVERIFY(QDir().mkpath(second));

    GameStopService service;
    FakeSink sink;
    service.setSignalSink(&sink);
    service.setGracePeriodMs(0);
    // Each game is read three times, in this order: the graceful step, the
    // forced step, and the post-check that asks whether it is gone.
    int reads = 0;
    service.setLiveness([&reads](qint64, qint64) { return ++reads % 3 != 0; });
    service.setSnapshotProvider([first, second] {
      return QVector<ProcessSnapshot>{
          process(93, 9300, QStringLiteral("one"), {QStringLiteral("one")}, first + "/one"),
          process(94, 9400, QStringLiteral("two"), {QStringLiteral("two")}, second + "/two")};
    });
    QVariantMap one = row(QStringLiteral("Manual"), QStringLiteral("one"), QStringLiteral("One"));
    one.insert(QStringLiteral("installPath"), first);
    QVariantMap two = row(QStringLiteral("Manual"), QStringLiteral("two"), QStringLiteral("Two"));
    two.insert(QStringLiteral("installPath"), second);
    // The same game seen twice, as a linked row would be, must not double count.
    service.setRowsProvider([one, two] { return QVariantList{one, two, one}; });

    QSignalSpy finished(&service, &GameStopService::finished);
    QVERIFY(service.stopAll());
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.first().at(0).toBool(), true);
    // Each live game takes the graceful step and then the forced one, because
    // the stub only lets go at the post-check.
    QCOMPARE(sink.levers(),
             (QStringList{QStringLiteral("terminate"), QStringLiteral("force"),
                          QStringLiteral("terminate"), QStringLiteral("force")}));
    int titles = 0;
    for (const QVariant& line : finished.first().at(2).toList()) {
      if (line.toString() == QStringLiteral("One")) {
        ++titles;
      }
    }
    QCOMPARE(titles, 1);
  }

  void stopRefusesToStartTwiceAtOnce() {
    GameStopService service;
    FakeSink sink;
    service.setSignalSink(&sink);
    service.setGracePeriodMs(400);
    service.setLiveness([](qint64, qint64) { return false; });
    service.setSnapshotProvider([] {
      return QVector<ProcessSnapshot>{
          process(95, 9500, QStringLiteral("game"), {QStringLiteral("game")},
                  QStringLiteral("/games/native/Thing/game"))};
    });
    QVariantMap game = row(QStringLiteral("Manual"), QStringLiteral("thing"),
                           QStringLiteral("Thing"));
    game.insert(QStringLiteral("installPath"), QStringLiteral("/games/native/Thing"));

    QSignalSpy finished(&service, &GameStopService::finished);
    QVERIFY(service.stop(game));
    QVERIFY(!service.stop(game));
    QVERIFY(finished.wait(5000));
    QVERIFY(!service.busy());
  }
};

QTEST_GUILESS_MAIN(GameStopServiceTests)
#include "GameStopServiceTests.moc"