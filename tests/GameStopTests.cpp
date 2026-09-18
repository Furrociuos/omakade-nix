#include "tracking/GameStop.h"
#include "tracking/ProcFs.h"
#include "tracking/ProcessMatcher.h"
#include "sources/steam/SteamScanner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

namespace {

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

// Proton hands the executable to wine as a Z: drive path, so a fixture has to
// carry the same form or it proves nothing about the real command line.
QString zPath(const QString& unixPath) {
  QString windows = unixPath;
  windows.replace(QLatin1Char('/'), QLatin1Char('\\'));
  return QStringLiteral("Z:") + windows;
}

ProcessProfileSet cemuProfiles() {
  ProcessProfileSet profiles;
  profiles.emulators.append({.name = QStringLiteral("Cemu"),
                             .binaries = {QStringLiteral("cemu")},
                             .rescanSource = QStringLiteral("Cemu")});
  profiles.romExtensions = {QStringLiteral("wua")};
  return profiles;
}

bool hasNote(const GameStop::Plan& plan, const QString& fragment) {
  for (const QString& note : plan.notes) {
    if (note.contains(fragment)) {
      return true;
    }
  }
  return false;
}

const GameStop::Skipped* findSkipped(const GameStop::Plan& plan, qint64 pid) {
  for (const GameStop::Skipped& skipped : plan.skipped) {
    if (skipped.pid == pid) {
      return &skipped;
    }
  }
  return nullptr;
}

} // namespace

class GameStopTests : public QObject {
  Q_OBJECT

private slots:
  // The install-path rung reads the resolved executable and the command line, so
  // the snapshot has to carry a real one.
  void snapshotResolvesExecutablePath() {
    const QVector<ProcessSnapshot> processes = ProcFs::listProcesses();
    QVERIFY2(!processes.isEmpty(), "the process snapshot is empty");
    const ProcessSnapshot* self = nullptr;
    for (const ProcessSnapshot& candidate : processes) {
      if (candidate.pid == QCoreApplication::applicationPid()) {
        self = &candidate;
      }
    }
    QVERIFY2(self != nullptr, "the test process is not in its own snapshot");
    QVERIFY2(!self->exePath.isEmpty(), "exePath was not resolved for a live process");
    QVERIFY2(QFileInfo::exists(self->exePath), qPrintable(self->exePath));
  }

  void ownedProcessIsExactAndRecycledPidIsRefused() {
    GameStop::GameIdentity game;
    game.title = QStringLiteral("Frostpunk");
    game.ownedProcesses.append({.pid = 555, .procStart = 5555});
    const QVector<ProcessSnapshot> processes = {
        process(555, 5555, QStringLiteral("Frostpunk"), {QStringLiteral("/games/Frostpunk")})};
    const GameStop::Plan plan = GameStop::plan(game, processes, {});
    QCOMPARE(plan.targets.size(), 1);
    QCOMPARE(plan.targets.first().kind, GameStop::TargetKind::TerminateProcess);
    QCOMPARE(plan.targets.first().confidence, GameStop::Confidence::Exact);
    QCOMPARE(plan.targets.first().pid, qint64(555));
    QVERIFY(findSkipped(plan, 555) == nullptr);
  }

  void recycledPidProducesNoTarget() {
    GameStop::GameIdentity game;
    game.ownedProcesses.append({.pid = 4242, .procStart = 100});
    const QVector<ProcessSnapshot> processes = {
        process(4242, 999, QStringLiteral("chromium"), {QStringLiteral("/usr/bin/chromium")})};
    const GameStop::Plan plan = GameStop::plan(game, processes, {});
    QCOMPARE(plan.targets.size(), 0);
    QCOMPARE(plan.skipped.size(), 1);
    const GameStop::Skipped& skipped = plan.skipped.first();
    QCOMPARE(skipped.pid, qint64(4242));
    QCOMPARE(skipped.reason, GameStop::SkipReason::RecycledPid);
    QVERIFY2(skipped.detail.contains(QStringLiteral("chromium")), qPrintable(skipped.detail));
  }

  void steamProtonGameAttributesInstallPath() {
    const QString installPath = QDir::homePath() + QStringLiteral("/.steam/steamapps/common/Frostpunk");
    GameStop::GameIdentity game;
    game.title = QStringLiteral("Frostpunk");
    game.appId = QStringLiteral("323190");
    game.source = QStringLiteral("Steam");
    game.installPath = installPath;

    const QVector<ProcessSnapshot> processes = {
        // The Proton wrapper chain names the executable as a unix path.
        process(500, 5000, QStringLiteral("python3"),
                {QStringLiteral("/home/user/.steam/steamapps/common/Proton - Experimental/proton"),
                 QStringLiteral("run"), installPath + QStringLiteral("/Frostpunk.exe")},
                QStringLiteral("/home/user/.steam/steamapps/common/Proton - Experimental/proton")),
        // The wine process itself carries the Z: form.
        process(501, 5001, QStringLiteral("wine64"),
                {QStringLiteral("/home/user/.steam/steamapps/common/Proton - Experimental/files/bin/wine64"),
                 zPath(installPath + QStringLiteral("/Frostpunk.exe"))}),
        // The Steam client is not part of the game and must stay out of it.
        process(700, 7000, QStringLiteral("steam"), {QStringLiteral("/usr/bin/steam")},
                QStringLiteral("/usr/bin/steam"))};
    const GameStop::Plan plan = GameStop::plan(game, processes, {});
    QCOMPARE(plan.targets.size(), 2);
    QCOMPARE(plan.targets.at(0).pid, qint64(500));
    QCOMPARE(plan.targets.at(1).pid, qint64(501));
    for (const GameStop::Target& target : plan.targets) {
      QCOMPARE(target.kind, GameStop::TargetKind::TerminateProcess);
      QCOMPARE(target.confidence, GameStop::Confidence::Good);
      QVERIFY2(target.reason.contains(installPath), qPrintable(target.reason));
    }
    QVERIFY(hasNote(plan, QStringLiteral("anti-cheat")));
  }

  void battleNetGameClosesItsSharedPrefix() {
    const QString prefix = QDir::homePath() + QStringLiteral("/.local/share/bottles/bottles/battlenet");
    GameStop::GameIdentity game;
    game.title = QStringLiteral("World of Warcraft");
    game.source = QStringLiteral("Battle.net");
    game.installPath = prefix + QStringLiteral("/drive_c/Program Files (x86)/World of Warcraft");
    game.winePrefixes = {prefix};

    // Both processes report only Windows paths, which is what a Wine process
    // actually shows: the prefix target is what covers them.
    const QVector<ProcessSnapshot> processes = {
        process(800, 8000, QStringLiteral("Battle.net.exe"),
                {QStringLiteral("C:\\Program Files (x86)\\Battle.net\\Battle.net.exe")}),
        process(801, 8001, QStringLiteral("Wow.exe"),
                {QStringLiteral("C:\\Program Files (x86)\\World of Warcraft\\Wow.exe")})};
    const GameStop::Plan plan = GameStop::plan(game, processes, {});
    QCOMPARE(plan.targets.size(), 1);
    const GameStop::Target& target = plan.targets.first();
    QCOMPARE(target.kind, GameStop::TargetKind::WinePrefix);
    QCOMPARE(target.confidence, GameStop::Confidence::Exact);
    QCOMPARE(target.prefix, prefix);
    QCOMPARE(plan.processTargets(), 0);
    QVERIFY2(GameStop::describe(target).contains(prefix), qPrintable(GameStop::describe(target)));
  }

  void installPathProcessInsideAPrefixIsNotListedTwice() {
    const QString prefix = QDir::homePath() + QStringLiteral("/.wine/games");
    GameStop::GameIdentity game;
    game.winePrefixes = {prefix};
    game.installPath = prefix + QStringLiteral("/drive_c/Games/Thing");
    const QVector<ProcessSnapshot> processes = {
        process(42, 4200, QStringLiteral("thing.exe"),
                {QStringLiteral("thing.exe")}, game.installPath + QStringLiteral("/thing.exe"))};
    const GameStop::Plan plan = GameStop::plan(game, processes, {});
    QCOMPARE(plan.targets.size(), 1);
    QCOMPARE(plan.targets.first().kind, GameStop::TargetKind::WinePrefix);
    const GameStop::Skipped* skipped = findSkipped(plan, 42);
    QVERIFY(skipped != nullptr);
    QCOMPARE(skipped->reason, GameStop::SkipReason::CoveredByPrefix);
  }

  void flatpakGameTargetsTheAppId() {
    GameStop::GameIdentity game;
    game.title = QStringLiteral("RetroArch");
    game.flatpak = true;
    game.flatpakAppId = QStringLiteral("org.libretro.RetroArch");
    const QVector<ProcessSnapshot> processes = {
        process(910, 9100, QStringLiteral("retroarch"),
                {QStringLiteral("retroarch"), QStringLiteral("-L"),
                 QStringLiteral("/games/cores/snes9x_libretro.so")})};
    const GameStop::Plan plan = GameStop::plan(game, processes, {});
    QCOMPARE(plan.targets.size(), 1);
    const GameStop::Target& target = plan.targets.first();
    QCOMPARE(target.kind, GameStop::TargetKind::FlatpakApp);
    QCOMPARE(target.appId, QStringLiteral("org.libretro.RetroArch"));
    QCOMPARE(plan.scopeTargets(), 1);
  }

  void emulatorSessionIsClaimedByItsContentPath() {
    const QString game = QStringLiteral("/games/wiiu/Zelda.wua");
    GameStop::GameIdentity identity;
    identity.title = QStringLiteral("Zelda");
    identity.emulator = QStringLiteral("Cemu");
    identity.gamePaths = {game};
    const QVector<ProcessSnapshot> processes = {
        process(900, 9000, QStringLiteral("cemu"),
                {QStringLiteral("/usr/bin/cemu"), QStringLiteral("-g"), game}),
        // A different game in the same emulator is not this game's process.
        process(901, 9001, QStringLiteral("cemu"),
                {QStringLiteral("/usr/bin/cemu"), QStringLiteral("-g"),
                 QStringLiteral("/games/wiiu/Other.wua")})};
    const GameStop::Plan plan = GameStop::plan(identity, processes, cemuProfiles());
    QCOMPARE(plan.targets.size(), 1);
    const GameStop::Target& target = plan.targets.first();
    QCOMPARE(target.pid, qint64(900));
    QCOMPARE(target.confidence, GameStop::Confidence::Exact);
    QVERIFY2(target.reason.contains(game), qPrintable(target.reason));
  }

  void emulatorWithoutAContentPathClaimsNothing() {
    GameStop::GameIdentity identity;
    identity.emulator = QStringLiteral("Cemu");
    const QVector<ProcessSnapshot> processes = {
        process(900, 9000, QStringLiteral("cemu"),
                {QStringLiteral("/usr/bin/cemu"), QStringLiteral("-g"),
                 QStringLiteral("/games/wiiu/Zelda.wua")})};
    const GameStop::Plan plan = GameStop::plan(identity, processes, cemuProfiles());
    QCOMPARE(plan.targets.size(), 0);
    QVERIFY(hasNote(plan, QStringLiteral("no recorded content path")));
  }

  void nothingRunningIsReportedRatherThanAnEmptyPlan() {
    GameStop::GameIdentity game;
    game.installPath = QStringLiteral("/games/native/Thing");
    const GameStop::Plan plan = GameStop::plan(game, {}, {});
    QVERIFY(plan.isEmpty());
    QVERIFY(hasNote(plan, QStringLiteral("Nothing attributable")));
  }

  // A scan that yields a whole system folder for one game must not turn into a
  // session-wide kill list.
  void broadRootsAreRefused() {
    GameStop::GameIdentity game;
    game.installPath = QStringLiteral("/");
    game.winePrefixes = {QDir::homePath()};
    const QVector<ProcessSnapshot> processes = {
        process(60, 6000, QStringLiteral("game"), {QStringLiteral("game")},
                QStringLiteral("/games/native/Thing/game")),
        process(61, 6100, QStringLiteral("browser"), {QStringLiteral("browser")},
                QStringLiteral("/usr/bin/browser"))};
    const GameStop::Plan plan = GameStop::plan(game, processes, {});
    QVERIFY(plan.isEmpty());
    QVERIFY(hasNote(plan, QStringLiteral("system folder")));
    QVERIFY(hasNote(plan, QStringLiteral("not a Wine prefix")));
  }

  // A manual library entry stores the program's own path as its install path,
  // and a Heroic sideload with no folder falls back to the program's directory.
  // Neither is one game's folder, and attributing everything running from there
  // would close the session instead of the game.
  void anInstallPathThatNamesAProgramOrSharedBinariesIsRefused() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString program = temporary.path() + QStringLiteral("/wine");
    QFile file(program);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("#!/bin/sh\n"), 10);
    file.close();

    GameStop::GameIdentity game;
    game.installPath = program;
    const QVector<ProcessSnapshot> processes = {
        process(70, 7000, QStringLiteral("wine"), {program, QStringLiteral("game.exe")},
                QStringLiteral("/usr/bin/wine"))};
    const GameStop::Plan plan = GameStop::plan(game, processes, {});
    QVERIFY2(plan.isEmpty(), "a path that names a program was treated as an install folder");
    QVERIFY(hasNote(plan, QStringLiteral("the program itself")));

    GameStop::GameIdentity sideload;
    sideload.installPath = QStringLiteral("/usr/bin");
    const GameStop::Plan sideloadPlan = GameStop::plan(sideload, processes, {});
    QVERIFY2(sideloadPlan.isEmpty(), "a shared program directory was treated as an install folder");
    QVERIFY(hasNote(sideloadPlan, QStringLiteral("shared system folder")));

    // A game installed in a directory that happens to be called bin is a miss,
    // not a wrong signal, and the note says so rather than staying silent.
    GameStop::GameIdentity nested;
    nested.installPath = temporary.path() + QStringLiteral("/MyGame/bin");
    QVERIFY(QDir().mkpath(nested.installPath));
    const GameStop::Plan nestedPlan = GameStop::plan(nested, processes, {});
    QVERIFY(nestedPlan.isEmpty());
    QVERIFY(hasNote(nestedPlan, QStringLiteral("shared system folder")));
  }

  void aRelativeWinePrefixIsRefused() {
    GameStop::GameIdentity game;
    game.winePrefixes = {QStringLiteral("~/.wine/thing"), QStringLiteral("relative/prefix")};
    const QVector<ProcessSnapshot> processes = {
        process(80, 8000, QStringLiteral("game.exe"),
                {QStringLiteral("relative/prefix/drive_c/game.exe")})};
    const GameStop::Plan plan = GameStop::plan(game, processes, {});
    QVERIFY2(plan.isEmpty(), "a relative Wine prefix was used");
    QVERIFY(hasNote(plan, QStringLiteral("not an absolute path")));
  }

  void protectedProcessesAreNeverTargeted() {
    const QString installPath = QStringLiteral("/games/native/Thing");
    GameStop::GameIdentity game;
    game.installPath = installPath;
    game.ownedProcesses.append({.pid = 1, .procStart = 10});
    const QVector<ProcessSnapshot> processes = {
        // pid 1 is never a target, even with an empty guard list.
        process(1, 10, QStringLiteral("init"), {QStringLiteral("/sbin/init")}),
        // Omakade itself and a shell are on the built-in list.
        process(2, 20, QStringLiteral("omakade"), {QStringLiteral("/usr/bin/omakade")},
                installPath + QStringLiteral("/omakade")),
        process(3, 30, QStringLiteral("bash"), {QStringLiteral("/usr/bin/bash")},
                installPath + QStringLiteral("/run.sh")),
        // A caller-supplied pid is protected as well.
        process(4, 40, QStringLiteral("thing"), {QStringLiteral("/games/native/Thing/thing.exe")},
                installPath + QStringLiteral("/thing.exe")),
        // The real game, which is the only thing that should be stopped.
        process(5, 50, QStringLiteral("thing"), {QStringLiteral("thing")},
                installPath + QStringLiteral("/bin/thing"))};
    GameStop::Guards guards = GameStop::defaultGuards();
    guards.protectedPids = {4};
    const GameStop::Plan plan = GameStop::plan(game, processes, {}, guards);
    QCOMPARE(plan.targets.size(), 1);
    QCOMPARE(plan.targets.first().pid, qint64(5));
    for (qint64 pid : {qint64(1), qint64(2), qint64(3), qint64(4)}) {
      const GameStop::Skipped* skipped = findSkipped(plan, pid);
      QVERIFY2(skipped != nullptr, qPrintable(QString::number(pid)));
      QCOMPARE(skipped->reason, GameStop::SkipReason::Protected);
    }
  }

  // The shipped profile file is what the recorder attributes sessions with, so
  // attribution has to work off the same data rather than a fixture that only
  // agrees with itself.
  void shippedProfilesClaimAnEmulatorSession() {
    QString error;
    const ProcessProfileSet profiles = ProcessMatcher::load(
        QStringLiteral(OMAKADE_FIXTURE_DIR "/../../resources/sessiond-profiles.json"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(!profiles.emulators.isEmpty(), "the shipped profile file loaded no emulators");

    const QString game = QStringLiteral("/games/Breath of the Wild.wua");
    GameStop::GameIdentity identity;
    identity.emulator = QStringLiteral("Cemu");
    identity.gamePaths = {game};
    const QVector<ProcessSnapshot> processes = {
        process(950, 9500, QStringLiteral("cemu"),
                {QStringLiteral("/usr/bin/cemu"), QStringLiteral("-g"), game})};
    const GameStop::Plan plan = GameStop::plan(identity, processes, profiles);
    QCOMPARE(plan.targets.size(), 1);
    QCOMPARE(plan.targets.first().pid, qint64(950));
  }

  void installedSteamGameTargetsTheAppId() {
    const QString installPath =
        QDir::homePath() + QStringLiteral("/.steam/steam/steamapps/common/Portal 2");
    GameStop::GameIdentity game;
    game.title = QStringLiteral("Portal 2");
    game.appId = QStringLiteral("620");
    game.source = QStringLiteral("Steam");
    game.installPath = installPath;
    game.flatpak = true;
    game.flatpakAppId = QStringLiteral("com.valvesoftware.Steam");
    const GameStop::Plan plan = GameStop::plan(game, {}, {});
    QCOMPARE(plan.scopeTargets(), 1);
    QCOMPARE(plan.targets.first().kind, GameStop::TargetKind::FlatpakApp);
  }

  // The slice 3 path end to end: derive the Proton prefix from what the Steam
  // row holds, hand it to attribution, and get a prefix target that covers the
  // wine-side children the install-path rung cannot see.
  void derivedProtonPrefixBecomesAPrefixTarget() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString library = temporary.path() + QStringLiteral("/Library");
    const QString installPath = library + QStringLiteral("/steamapps/common/Frostpunk");
    const QString prefix = library + QStringLiteral("/steamapps/compatdata/323190/pfx");
    QVERIFY(QDir().mkpath(installPath));
    QVERIFY(QDir().mkpath(prefix));
    QVERIFY2(SteamScanner::protonPrefix(installPath, QStringLiteral("323190")) == prefix,
             "the prefix was not derived from the install path");

    GameStop::GameIdentity game;
    game.title = QStringLiteral("Frostpunk");
    game.appId = QStringLiteral("323190");
    game.source = QStringLiteral("Steam");
    game.installPath = installPath;
    const QString derived = SteamScanner::protonPrefix(installPath, game.appId);
    if (!derived.isEmpty()) {
      game.winePrefixes.append(derived);
    }

    const QVector<ProcessSnapshot> processes = {
        // The Proton wrapper chain, which names the game as a unix path and does
        // not live inside the prefix, so it is its own target.
        process(501, 5001, QStringLiteral("python3"),
                {QStringLiteral("/home/user/.steam/steamapps/common/Proton - Experimental/proton"),
                 QStringLiteral("run"), zPath(installPath + QStringLiteral("/Frostpunk.exe"))}),
        // A wine-side child that shows only a Windows path. It cannot be
        // attributed by path at all, which is exactly why the derived prefix
        // matters: the prefix target is what closes it.
        process(502, 5002, QStringLiteral("SteamService.exe"),
                {QStringLiteral("C:\\Program Files (x86)\\Steam\\SteamService.exe")})};
    const GameStop::Plan plan = GameStop::plan(game, processes, {});
    QCOMPARE(plan.targets.size(), 2);
    QCOMPARE(plan.targets.at(0).kind, GameStop::TargetKind::WinePrefix);
    QCOMPARE(plan.targets.at(0).prefix, prefix);
    QCOMPARE(plan.targets.at(0).confidence, GameStop::Confidence::Exact);
    QVERIFY2(plan.targets.at(0).reason.contains(QStringLiteral("Every process in this Wine prefix")),
             qPrintable(plan.targets.at(0).reason));
    QCOMPARE(plan.targets.at(1).kind, GameStop::TargetKind::TerminateProcess);
    QCOMPARE(plan.targets.at(1).pid, qint64(501));
    QCOMPARE(plan.targets.at(1).confidence, GameStop::Confidence::Good);
    for (const GameStop::Target& target : plan.targets) {
      QVERIFY(target.pid != 502);
    }
  }

  void planDescribesEveryTargetInOneLine() {
    const QString prefix = QStringLiteral("/home/user/.wine/thing");
    GameStop::GameIdentity game;
    game.winePrefixes = {prefix};
    game.ownedProcesses.append({.pid = 12, .procStart = 120});
    const QVector<ProcessSnapshot> processes = {
        process(12, 120, QStringLiteral("thing"), {QStringLiteral("/games/thing/thing.exe")})};
    const GameStop::Plan plan = GameStop::plan(game, processes, {});
    const QStringList lines = GameStop::describe(plan);
    QCOMPARE(lines.size(), plan.targets.size());
    QCOMPARE(lines.size(), 2);
    QVERIFY2(lines.first().contains(QStringLiteral("pid 12")), qPrintable(lines.first()));
    QVERIFY2(lines.at(1).contains(prefix), qPrintable(lines.at(1)));
  }
};

QTEST_GUILESS_MAIN(GameStopTests)
#include "GameStopTests.moc"
