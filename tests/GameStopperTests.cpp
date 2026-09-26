#include "tracking/GameStop.h"
#include "tracking/GameStopper.h"
#include "tracking/ProcFs.h"

#include <QSet>
#include <QTest>

namespace {

// Every signal in these tests goes through this fake, so nothing here can touch
// a real process. It records what it was asked to do.
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
  GameStop::LeverResult prefixResult = GameStop::LeverResult::Done;
  GameStop::LeverResult prefixForceResult = GameStop::LeverResult::Done;
  GameStop::LeverResult flatpakResult = GameStop::LeverResult::Done;

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
    return force ? prefixForceResult : prefixResult;
  }
  GameStop::LeverResult stopFlatpakApp(const QString& appId) override {
    calls.append({QStringLiteral("flatpak"), 0, appId, false});
    return flatpakResult;
  }

  [[nodiscard]] QStringList levers() const {
    QStringList names;
    for (const Call& call : calls) {
      names.append(call.forced ? call.lever + QStringLiteral("-forced") : call.lever);
    }
    return names;
  }

  // The sink reports its tool's words through lastMessage(); this is how a fake
  // supplies them.
  void setMessage(const QString& message) { m_lastMessage = message; }
};

GameStop::Target processTarget(qint64 pid, qint64 procStart, const QString& comm) {
  GameStop::Target target;
  target.kind = GameStop::TargetKind::TerminateProcess;
  target.pid = pid;
  target.procStart = procStart;
  target.comm = comm;
  target.label = QStringLiteral("%1 (pid %2)").arg(comm).arg(pid);
  target.reason = QStringLiteral("test");
  return target;
}

GameStop::Target prefixTarget(const QString& prefix) {
  GameStop::Target target;
  target.kind = GameStop::TargetKind::WinePrefix;
  target.prefix = prefix;
  target.witnesses.append({42, 4200});
  target.label = QStringLiteral("Wine prefix %1").arg(prefix);
  return target;
}

GameStop::Target flatpakTarget(const QString& appId) {
  GameStop::Target target;
  target.kind = GameStop::TargetKind::FlatpakApp;
  target.appId = appId;
  target.witnesses.append({42, 4200});
  target.label = QStringLiteral("flatpak app %1").arg(appId);
  return target;
}

GameStop::Plan planOf(const QVector<GameStop::Target>& targets) {
  GameStop::Plan plan;
  plan.targets = targets;
  return plan;
}

// What the stopper logs, captured so the "log what was signalled and why" rule
// is asserted rather than assumed.
QStringList g_logged;

void captureMessage(QtMsgType, const QMessageLogContext&, const QString& message) {
  g_logged.append(message);
}

} // namespace

class GameStopperTests : public QObject {
  Q_OBJECT

private slots:
  // The graceful step asks, and the forced step only reaches what survived it.
  void gracefulThenForcedForAProcessThatIgnoresSigterm() {
    FakeSink sink;
    QSet<qint64> alive{42};
    GameStop::Stopper stopper(&sink, {}, [&alive](qint64 pid, qint64) {
      return alive.contains(pid);
    });
    const GameStop::Plan plan = planOf({processTarget(42, 4200, QStringLiteral("game"))});

    const GameStop::StopReport first = stopper.begin(plan);
    QCOMPARE(first.outcomes.size(), 1);
    QCOMPARE(first.outcomes.first().kind, GameStop::OutcomeKind::Signalled);
    QCOMPARE(sink.levers(), QStringList{QStringLiteral("terminate")});
    QVERIFY(stopper.pending());

    // The game ignored SIGTERM, so the forced step reaches it.
    const GameStop::StopReport second = stopper.escalate();
    QVERIFY(second.escalated);
    QCOMPARE(second.outcomes.size(), 1);
    QCOMPARE(second.outcomes.first().kind, GameStop::OutcomeKind::Signalled);
    QCOMPARE(sink.levers(),
             (QStringList{QStringLiteral("terminate"), QStringLiteral("force")}));
    QVERIFY(!stopper.pending());
    QVERIFY2(second.outcomes.first().detail.contains(QStringLiteral("forced")),
             qPrintable(second.outcomes.first().detail));
  }

  void aProcessThatClosesIsNotForced() {
    FakeSink sink;
    QSet<qint64> alive{42};
    GameStop::Stopper stopper(&sink, {}, [&alive](qint64 pid, qint64) {
      return alive.contains(pid);
    });
    const GameStop::Plan plan = planOf({processTarget(42, 4200, QStringLiteral("game"))});
    QCOMPARE(stopper.begin(plan).signalled(), 1);

    alive.clear();  // it obeyed the first signal
    const GameStop::StopReport second = stopper.escalate();
    QCOMPARE(second.outcomes.size(), 1);
    QCOMPARE(second.outcomes.first().kind, GameStop::OutcomeKind::AlreadyGone);
    QCOMPARE(sink.levers(), QStringList{QStringLiteral("terminate")});
  }

  // A pid that was reused between the two phases names somebody else by then.
  void aRecycledPidIsNotForced() {
    FakeSink sink;
    bool stillAlive = true;
    GameStop::Stopper stopper(&sink, {}, [&stillAlive](qint64, qint64) { return stillAlive; });
    const GameStop::Plan plan = planOf({processTarget(42, 4200, QStringLiteral("game"))});
    QCOMPARE(stopper.begin(plan).signalled(), 1);

    // The start time no longer matches, so liveness answers false.
    stillAlive = false;
    const GameStop::StopReport second = stopper.escalate();
    QCOMPARE(second.outcomes.size(), 1);
    QCOMPARE(second.outcomes.first().kind, GameStop::OutcomeKind::AlreadyGone);
    QCOMPARE(sink.levers(), QStringList{QStringLiteral("terminate")});
  }

  void aGoneProcessIsNeverSignalled() {
    FakeSink sink;
    GameStop::Stopper stopper(&sink, {}, [](qint64, qint64) { return false; });
    const GameStop::Plan plan = planOf({processTarget(42, 4200, QStringLiteral("game"))});
    const GameStop::StopReport report = stopper.begin(plan);
    QCOMPARE(report.outcomes.size(), 1);
    QCOMPARE(report.outcomes.first().kind, GameStop::OutcomeKind::AlreadyGone);
    QVERIFY(sink.calls.isEmpty());
    QVERIFY(!stopper.pending());
  }

  // No start time means no way to tell the attributed process from whatever
  // holds that pid now, so it is refused rather than signalled.
  void aTargetWithoutAStartTimeIsRefused() {
    FakeSink sink;
    GameStop::Stopper stopper(&sink, {}, [](qint64, qint64) { return true; });
    const GameStop::Plan plan = planOf({processTarget(42, -1, QStringLiteral("game"))});
    const GameStop::StopReport report = stopper.begin(plan);
    QCOMPARE(report.outcomes.first().kind, GameStop::OutcomeKind::Refused);
    QVERIFY2(report.outcomes.first().detail.contains(QStringLiteral("start time")),
             qPrintable(report.outcomes.first().detail));
    QVERIFY(sink.calls.isEmpty());
  }

  void protectedProcessesAreRefusedEvenIfAPlanNamesThem() {
    FakeSink sink;
    QVector<GameStop::Target> targets = {
        processTarget(1, 10, QStringLiteral("init")),
        processTarget(2, 20, QStringLiteral("omakade")),
        processTarget(3, 30, QStringLiteral("bash")),
        processTarget(4, 40, QStringLiteral("thing")),
        processTarget(QCoreApplication::applicationPid(), 50, QStringLiteral("test")),
    };
    GameStop::Guards guards = GameStop::defaultGuards();
    guards.protectedPids = {4};
    GameStop::Stopper stopper(&sink, guards, [](qint64, qint64) { return true; });
    const GameStop::StopReport report = stopper.begin(planOf(targets));
    QCOMPARE(report.refused(), targets.size());
    QCOMPARE(report.signalled(), 0);
    QVERIFY(sink.calls.isEmpty());
  }

  void aPrefixClosesGracefullyAndCanBeForced() {
    FakeSink sink;
    GameStop::Stopper stopper(&sink, {}, [](qint64, qint64) { return true; });
    const GameStop::Plan plan = planOf({prefixTarget(QStringLiteral("/games/prefix"))});
    const GameStop::StopReport first = stopper.begin(plan);
    QCOMPARE(first.outcomes.first().kind, GameStop::OutcomeKind::Signalled);
    QCOMPARE(sink.calls.size(), 1);
    QCOMPARE(sink.calls.first().text, QStringLiteral("/games/prefix"));
    QVERIFY(!sink.calls.first().forced);

    QCOMPARE(stopper.escalate().outcomes.first().kind, GameStop::OutcomeKind::Signalled);
    QCOMPARE(sink.calls.size(), 2);
    QCOMPARE(sink.calls.at(1).lever, QStringLiteral("force"));
    QCOMPARE(sink.calls.at(1).pid, qint64(42));
  }

  // Wine reports exit 1 when no wineserver is running for the prefix. That is
  // nothing to do, not a failure the user needs to see.
  void anExitedScopeIsNeverRetargeted() {
    FakeSink sink;
    bool originalAlive = true;
    GameStop::Stopper stopper(&sink, {}, [&](qint64 pid, qint64 start) {
      return originalAlive && pid == 42 && start == 4200;
    });
    const auto plan = planOf({prefixTarget(QStringLiteral("/games/prefix"))});
    QCOMPARE(stopper.begin(plan).signalled(), 1);
    originalAlive = false;
    QCOMPARE(stopper.escalate().outcomes.first().kind, GameStop::OutcomeKind::AlreadyGone);
    QCOMPARE(sink.calls.size(), 1);
    QCOMPARE(stopper.begin(plan).outcomes.first().kind, GameStop::OutcomeKind::AlreadyGone);
    QCOMPARE(sink.calls.size(), 1);
    auto missing = flatpakTarget(QStringLiteral("org.example.Game"));
    missing.witnesses.clear();
    QCOMPARE(stopper.begin(planOf({missing})).refused(), 1);
    QCOMPARE(sink.calls.size(), 1);
  }

  void aPrefixWithNoWineserverIsNothingToDo() {
    FakeSink sink;
    sink.prefixResult = GameStop::LeverResult::Missing;
    GameStop::Stopper stopper(&sink, {}, [](qint64, qint64) { return true; });
    const GameStop::StopReport report =
        stopper.begin(planOf({prefixTarget(QStringLiteral("/games/prefix"))}));
    QCOMPARE(report.outcomes.first().kind, GameStop::OutcomeKind::AlreadyGone);
    QVERIFY2(report.outcomes.first().detail.contains(QStringLiteral("no wineserver")),
             qPrintable(report.outcomes.first().detail));
    QVERIFY(!stopper.pending());
  }

  void unavailableWineserverFallsBackToVerifiedPrefixMembers() {
    FakeSink sink;
    sink.prefixResult = GameStop::LeverResult::Unavailable;
    auto target = prefixTarget(QStringLiteral("/games/prefix"));
    target.witnesses.append({43, 4300});
    QSet<qint64> live{42, 43};
    GameStop::Stopper stopper(&sink, {}, [&live](qint64 pid, qint64 start) {
      return live.contains(pid) && start == (pid == 42 ? 4200 : 4300);
    });

    const GameStop::StopReport graceful = stopper.begin(planOf({target}));
    QCOMPARE(graceful.signalled(), 1);
    QVERIFY(stopper.pending());
    QCOMPARE(sink.levers(),
             (QStringList{QStringLiteral("prefix"), QStringLiteral("terminate"),
                          QStringLiteral("terminate")}));
    QCOMPARE(sink.calls.at(1).pid, qint64(42));
    QCOMPARE(sink.calls.at(2).pid, qint64(43));

    live.remove(43);
    const GameStop::StopReport forced = stopper.escalate();
    QCOMPARE(forced.signalled(), 1);
    QCOMPARE(sink.levers().last(), QStringLiteral("force"));
    QCOMPARE(sink.calls.last().pid, qint64(42));
  }

  void unavailableWineserverSkipsReusedAndProtectedPids() {
    FakeSink sink;
    sink.prefixResult = GameStop::LeverResult::Unavailable;
    auto target = prefixTarget(QStringLiteral("/games/prefix"));
    target.witnesses.append({43, 4300});
    target.witnesses.append({44, 4400});
    GameStop::Guards guards;
    guards.protectedPids = {43};
    GameStop::Stopper stopper(&sink, guards, [](qint64 pid, qint64 start) {
      // PID 44 was reused after the preview; PID 43 is protected.
      return (pid == 42 && start == 4200) || (pid == 43 && start == 4300);
    });

    const GameStop::StopReport report = stopper.begin(planOf({target}));
    QCOMPARE(report.refused(), 1);
    QVERIFY(stopper.pending());
    QCOMPARE(sink.levers(), (QStringList{QStringLiteral("prefix"), QStringLiteral("terminate")}));
    QCOMPARE(sink.calls.last().pid, qint64(42));
  }

  void aFlatpakAppIsStoppedByItsAppId() {
    FakeSink sink;
    GameStop::Stopper stopper(&sink, {}, [](qint64, qint64) { return true; });
    const GameStop::StopReport report =
        stopper.begin(planOf({flatpakTarget(QStringLiteral("org.libretro.RetroArch"))}));
    QCOMPARE(report.outcomes.first().kind, GameStop::OutcomeKind::Signalled);
    QCOMPARE(sink.calls.size(), 1);
    QCOMPARE(sink.calls.first().lever, QStringLiteral("flatpak"));
    QCOMPARE(sink.calls.first().text, QStringLiteral("org.libretro.RetroArch"));
    // flatpak kill has no stronger step, so there is nothing to escalate.
    QVERIFY(!stopper.pending());
    QVERIFY(stopper.escalate().outcomes.isEmpty());
  }

  void aFailedLeverIsReportedWithTheToolsWords() {
    FakeSink sink;
    sink.terminateResult = GameStop::LeverResult::Failed;
    sink.setMessage(QStringLiteral("Operation not permitted"));
    GameStop::Stopper stopper(&sink, {}, [](qint64, qint64) { return true; });
    const GameStop::StopReport report =
        stopper.begin(planOf({processTarget(42, 4200, QStringLiteral("game"))}));
    QCOMPARE(report.failed(), 1);
    QCOMPARE(report.outcomes.first().message, QStringLiteral("Operation not permitted"));
    QVERIFY2(report.lines().first().contains(QStringLiteral("Operation not permitted")),
             qPrintable(report.lines().first()));
    // A signal the kernel refused will not be forced either, and the report
    // says so instead of retrying quietly.
    QVERIFY(!stopper.pending());
  }

  void theReportNamesEveryTargetAndWhatHappened() {
    FakeSink sink;
    sink.prefixResult = GameStop::LeverResult::Missing;
    sink.flatpakResult = GameStop::LeverResult::Failed;
    sink.setMessage(QStringLiteral("error: app is not running"));
    GameStop::Stopper stopper(&sink, {}, [](qint64, qint64) { return true; });
    const GameStop::Plan plan = planOf({processTarget(42, 4200, QStringLiteral("game")),
                                        prefixTarget(QStringLiteral("/p")),
                                        flatpakTarget(QStringLiteral("org.example.Game"))});
    const GameStop::StopReport report = stopper.begin(plan);
    const QStringList lines = report.lines();
    QCOMPARE(lines.size(), 3);
    QVERIFY2(lines.at(0).contains(QStringLiteral("pid 42")), qPrintable(lines.at(0)));
    QVERIFY2(lines.at(1).contains(QStringLiteral("/p")), qPrintable(lines.at(1)));
    QVERIFY2(lines.at(2).contains(QStringLiteral("org.example.Game")), qPrintable(lines.at(2)));
    QCOMPARE(report.signalled(), 1);
    QCOMPARE(report.failed(), 1);
  }

  void escalatingTwiceDoesNothingTheSecondTime() {
    FakeSink sink;
    GameStop::Stopper stopper(&sink, {}, [](qint64, qint64) { return true; });
    const GameStop::Plan plan = planOf({processTarget(42, 4200, QStringLiteral("game"))});
    QCOMPARE(stopper.begin(plan).signalled(), 1);
    QCOMPARE(stopper.escalate().signalled(), 1);
    const int callsBefore = sink.calls.size();
    QVERIFY(stopper.escalate().outcomes.isEmpty());
    QCOMPARE(sink.calls.size(), callsBefore);
  }

  void theGracePeriodIsAStatedNumber() {
    QCOMPARE(GameStop::Stopper::gracePeriodMs(), 5000);
    QVERIFY(GameStop::SystemSignalSink::commandTimeoutMs() > 0);
  }

  void theLogRecordsWhatWasSignalledAndWhy() {
    FakeSink sink;
    GameStop::Stopper stopper(&sink, {}, [](qint64, qint64) { return true; });
    GameStop::Target target = processTarget(42, 4200, QStringLiteral("game"));
    target.reason = QStringLiteral("Omakade started this process and it is still running.");

    g_logged.clear();
    QtMessageHandler previous = qInstallMessageHandler(captureMessage);
    stopper.begin(planOf({target}));
    const GameStop::StopReport forced = stopper.escalate();
    qInstallMessageHandler(previous);

    QCOMPARE(forced.signalled(), 1);
    QCOMPARE(g_logged.size(), 2);
    QVERIFY2(g_logged.first().contains(QStringLiteral("pid 42")), qPrintable(g_logged.first()));
    QVERIFY2(g_logged.first().contains(QStringLiteral("it is still running")),
             qPrintable(g_logged.first()));
    QVERIFY2(g_logged.at(1).contains(QStringLiteral("forced")), qPrintable(g_logged.at(1)));
  }

  // The real sink is the last line of defence, and these assertions never reach
  // the kernel: pid 1, this process, and a pid that cannot exist are refused.
  void theSystemSinkRefusesPidsItMustNeverSignal() {
    GameStop::SystemSignalSink sink;
    QCOMPARE(sink.terminate(1), GameStop::LeverResult::Refused);
    QCOMPARE(sink.forceTerminate(1), GameStop::LeverResult::Refused);
    QCOMPARE(sink.terminate(0), GameStop::LeverResult::Refused);
    QCOMPARE(sink.forceTerminate(-9), GameStop::LeverResult::Refused);
    QCOMPARE(sink.terminate(QCoreApplication::applicationPid()), GameStop::LeverResult::Refused);
    QCOMPARE(sink.stopWinePrefix(QString(), false), GameStop::LeverResult::Refused);
    QCOMPARE(sink.stopFlatpakApp(QString()), GameStop::LeverResult::Refused);
  }

  // A pid nothing holds answers ESRCH, which is "nothing to do". pid 2^31 is
  // outside the kernel's pid range, so this stays a hypothetical target.
  void theSystemSinkReportsAMissingProcessAsNothingToDo() {
    GameStop::SystemSignalSink sink;
    QCOMPARE(sink.terminate(0x7fffffff), GameStop::LeverResult::Missing);
  }

  // The whole path, attribution then stop, against processes that are not
  // there: the plan comes out of a snapshot fixture and every lever answers
  // that there is nothing left to signal.
  void attributedTargetsReachTheSink() {
    GameStop::GameIdentity identity;
    identity.installPath = QStringLiteral("/games/native/Thing");
    const QVector<ProcessSnapshot> processes = {
        {.pid = 77,
         .procStart = 7700,
         .comm = QStringLiteral("thing"),
         .arguments = {QStringLiteral("thing")},
         .exePath = QStringLiteral("/games/native/Thing/thing")}};
    const GameStop::Plan plan = GameStop::plan(identity, processes, {});
    QCOMPARE(plan.targets.size(), 1);

    FakeSink sink;
    sink.terminateResult = GameStop::LeverResult::Missing;
    GameStop::Stopper stopper(&sink, {}, [](qint64, qint64) { return true; });
    const GameStop::StopReport report = stopper.begin(plan);
    QCOMPARE(sink.calls.size(), 1);
    QCOMPARE(sink.calls.first().pid, qint64(77));
    QCOMPARE(report.outcomes.first().kind, GameStop::OutcomeKind::AlreadyGone);
  }
};

QTEST_GUILESS_MAIN(GameStopperTests)
#include "GameStopperTests.moc"
