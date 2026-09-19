#include "app/AppSettings.h"
#include "library/GameRoles.h"
#include "library/PlayStats.h"
#include "library/UnifiedGameModel.h"
#include "tracking/DiscordPresence.h"
#include "tracking/GameStop.h"
#include "tracking/PlaySessionStore.h"
#include "tracking/SessionDatabase.h"
#include "tracking/SessionJournal.h"
#include "tracking/SessionRecorder.h"
#include "tracking/SessionTitleIndex.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalServer>
#include <QScopeGuard>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QUuid>
#include <QtEndian>
#include <atomic>
#include <memory>

namespace {
class LinkedSource final : public QAbstractListModel {
public:
  int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 2; }
  QVariant data(const QModelIndex& index, int role) const override {
    if (!index.isValid())
      return {};
    switch (role) {
    case GameRoles::Title:
      return QStringLiteral("Linked Game");
    case GameRoles::Source:
      return QStringLiteral("RetroArch");
    case GameRoles::System:
      return QStringLiteral("snes");
    case GameRoles::AppId:
      return QString::number(index.row());
    case GameRoles::InstallPath:
      return QStringLiteral("/games/copy%1.sfc").arg(index.row());
    case GameRoles::Runner:
      return QString{};
    default:
      return {};
    }
  }
  QHash<int, QByteArray> roleNames() const override { return GameRoles::names(); }
};

struct Database {
  QTemporaryDir dir;
  QString path = dir.filePath("library.sqlite3");
  QString name = QUuid::createUuid().toString();
  QSqlDatabase db;
  Database() {
    if (!SessionDatabase::open(db, path, name))
      qFatal("fixture database failed");
  }
  ~Database() {
    db.close();
    db = {};
    QSqlDatabase::removeDatabase(name);
  }
  void sql(const QString& text) {
    QSqlQuery query(db);
    if (!query.exec(text))
      qFatal("%s", qPrintable(query.lastError().text()));
  }
  qint64 add(qint64 start, qint64 seconds, const QString& path = "/games/test.nsp",
             qint64 end = -1) {
    const auto id = SessionDatabase::beginSession(db, path, "Ryujinx", start, 5, 5);
    if (id <= 0 || !SessionDatabase::endSession(db, id, end < 0 ? start + seconds : end, seconds))
      qFatal("fixture session failed");
    return id;
  }
};
qint64 timestamp(int year, int month, int day, int hour, int minute = 0) {
  return QDateTime(QDate(year, month, day), QTime(hour, minute)).toSecsSinceEpoch();
}
qint64 hourTotal(const PlayStats& stats) {
  qint64 result = 0;
  for (const auto& entry : stats.byHour())
    result += entry.toMap().value("seconds").toLongLong();
  return result;
}
void refresh(PlaySessionStore& store) {
  store.setEnabled(false);
  store.setEnabled(true);
}
} // namespace

class ReleaseHardeningTests : public QObject {
  Q_OBJECT
private slots:
  void sameStoreCountsPlayAfterDeletion_data() {
    QTest::addColumn<bool>("all");
    QTest::newRow("single") << false;
    QTest::newRow("all") << true;
  }
  void sameStoreCountsPlayAfterDeletion() {
    QFETCH(bool, all);
    Database data;
    const QString path = "/games/test.nsp";
    PlaySessionStore store(data.path);
    store.observeImportedPlaytime(path, 3600);
    data.add(1000, 600);
    refresh(store);
    store.observeImportedPlaytime(path, 4200);
    if (all)
      QCOMPARE(store.deleteHistoryForPaths({path}), 1);
    else
      QVERIFY(store.deleteSession(
          store.historyForPaths({path}).first().toMap().value("sessionKey").toString(), {path}));
    data.add(5000, 900);
    refresh(store);
    store.observeImportedPlaytime(path, 4200);
    QCOMPARE(store.displaySeconds(path, 4200), qint64(5100));
  }
  void failedWatermarkRollsBackDeletion_data() { sameStoreCountsPlayAfterDeletion_data(); }
  void failedWatermarkRollsBackDeletion() {
    QFETCH(bool, all);
    Database data;
    const QString path = "/games/test.nsp";
    PlaySessionStore store(data.path);
    store.observeImportedPlaytime(path, 3600);
    data.add(1000, 600);
    store.observeImportedPlaytime(path, 4200);
    refresh(store);
    const auto key = store.historyForPaths({path}).first().toMap().value("sessionKey").toString();
    data.sql("CREATE TRIGGER refuse_update BEFORE UPDATE OF observed_seconds ON play_baselines "
             "BEGIN SELECT RAISE(FAIL,'refused'); END");
    if (all)
      QCOMPARE(store.deleteHistoryForPaths({path}), -1);
    else
      QVERIFY(!store.deleteSession(key, {path}));
    QCOMPARE(store.historyForPaths({path}).size(), 1);
    QCOMPARE(SessionDatabase::importWatermarksByPath(data.db).value(path).observedSeconds,
             qint64(600));
    data.sql("DROP TRIGGER refuse_update");
    QCOMPARE(store.deleteHistoryForPaths({path}), 1);
  }
  void statisticsFailClosedAndRecover() {
    Database data;
    PlayStats stats(nullptr, data.path);
    stats.refresh();
    QVERIFY(stats.error().isEmpty());
    data.sql("DROP TABLE play_sessions");
    stats.refresh();
    QVERIFY(!stats.error().isEmpty());
    QVERIFY(stats.headline().isEmpty());
    QVERIFY(stats.topGames().isEmpty());
    QVERIFY(SessionDatabase::ensureSchema(data.db));
    data.add(timestamp(2026, 9, 1, 12), 120);
    stats.setYear(2026);
    stats.refresh();
    QVERIFY(stats.error().isEmpty());
    QCOMPARE(stats.headline().value("recordedSeconds").toLongLong(), qint64(120));
    data.sql("CREATE TABLE achievements(unlocked INTEGER)");
    stats.refresh();
    QVERIFY(!stats.error().isEmpty());
  }
  void corruptDatabaseIsNotZeroPlay() {
    QTemporaryDir dir;
    const auto path = dir.filePath("broken.sqlite3");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not sqlite");
    file.close();
    PlayStats stats(nullptr, path);
    stats.refresh();
    QVERIFY(!stats.error().isEmpty());
    QVERIFY(stats.headline().isEmpty());
  }
  void calendarBoundariesConservePlay_data() {
    QTest::addColumn<qint64>("start");
    QTest::addColumn<int>("seconds");
    QTest::addColumn<int>("days");
    QTest::newRow("midnight") << timestamp(2026, 9, 1, 23, 30) << 7200 << 2;
    QTest::newRow("long-paused-session") << timestamp(2026, 1, 1, 0) << 40 * 86400 << 40;
    QTest::newRow("leap-day") << timestamp(2024, 2, 28, 23, 30) << 90000 << 3;
    QTest::newRow("spring-DST") << timestamp(2026, 3, 8, 0, 30) << 14400 << 1;
    QTest::newRow("fall-DST") << timestamp(2026, 11, 1, 0, 30) << 14400 << 1;
  }
  void calendarBoundariesConservePlay() {
    QFETCH(qint64, start);
    QFETCH(int, seconds);
    QFETCH(int, days);
    Database data;
    data.add(start, seconds);
    PlayStats stats(nullptr, data.path);
    stats.setPeriod("all");
    QCOMPARE(stats.headline().value("recordedSeconds").toLongLong(), qint64(seconds));
    QCOMPARE(hourTotal(stats), qint64(seconds));
    QCOMPARE(stats.headline().value("daysPlayed").toInt(), days);
  }
  void newYearAndPausedSplitsConservePlay() {
    Database data;
    const auto start = timestamp(2025, 12, 31, 23, 30);
    data.add(start, 7200);
    PlayStats stats(nullptr, data.path);
    stats.setYear(2025);
    QCOMPARE(stats.headline().value("recordedSeconds").toLongLong(), qint64(1800));
    QCOMPARE(hourTotal(stats), qint64(1800));
    stats.setYear(2026);
    QCOMPARE(stats.headline().value("recordedSeconds").toLongLong(), qint64(5400));
    QCOMPARE(hourTotal(stats), qint64(5400));
    data.sql("DELETE FROM play_sessions");
    data.add(start, 3600, "/games/test.nsp", start + 7200);
    stats.setYear(2025);
    QCOMPARE(hourTotal(stats), qint64(900));
    QVERIFY(stats.windowNote().contains("estimate"));
    stats.setYear(2026);
    QCOMPARE(hourTotal(stats), qint64(2700));
  }
  void titleTokenDetectsEqualLengthChanges() {
    Database data;
    data.sql("CREATE TABLE ryujinx_games(name TEXT,path TEXT)");
    data.sql("INSERT INTO ryujinx_games VALUES('Alpha Game','/games/a.nsp')");
    SessionTitleIndex index;
    QVERIFY(index.refresh(data.db));
    const auto before = SessionTitleIndex::cacheChangeToken(data.db);
    data.sql("UPDATE ryujinx_games SET name='Bravo Game',path='/games/b.nsp'");
    QVERIFY(before != SessionTitleIndex::cacheChangeToken(data.db));
    QVERIFY(index.refresh(data.db));
    QCOMPARE(index.pathForWindowTitle("Bravo Game", "Ryujinx"), QString("/games/b.nsp"));
    QVERIFY(index.pathForWindowTitle("Alpha Game", "Ryujinx").isEmpty());
  }
  void recoveryPreservesActiveStart() {
    Database data;
    for (auto process : ProcFs::listProcesses()) {
      if (process.pid != QCoreApplication::applicationPid())
        continue;
      process.comm = "audit_emulator";
      process.arguments = {"audit_emulator", "/games/test.nsp"};
      ProcessProfileSet profiles;
      profiles.romExtensions = {"nsp"};
      profiles.emulators.append({"Audit", {"audit_emulator"}, {}});
      QVERIFY(SessionDatabase::beginSession(data.db, "/games/test.nsp", "Audit", 12345, process.pid,
                                            process.procStart) > 0);
      SessionRecorder recorder(data.db, [] { return 0; });
      recorder.recover({process}, profiles, 12355);
      QCOMPARE(recorder.activeCount(), 1);
      QCOMPARE(recorder.activeSessions().first().startedAt, qint64(12345));
      return;
    }
    QFAIL("test process absent from procfs");
  }
  void fullHistoryPagesReachOlderSessions() {
    Database data;
    for (int i = 1; i <= 45; ++i)
      data.add(i * 100, 10);
    PlaySessionStore store(data.path);
    QSet<QString> keys;
    for (int offset = 0; offset < 45; offset += 8)
      for (const auto& row : store.historyForPaths({"/games/test.nsp"}, 8, offset))
        keys.insert(row.toMap().value("sessionKey").toString());
    QCOMPARE(keys.size(), 45);
  }
  void periodSurvivesOtherSettingsAndRestart() {
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.toml");
    {
      AppSettings settings(path);
      settings.setStatsPeriod("all");
      settings.setReducedMotion(true);
    }
    AppSettings reopened(path);
    QCOMPARE(reopened.statsPeriod(), QString("all"));
    QVERIFY(reopened.applyBackupSettings({}, true));
    QCOMPARE(reopened.statsPeriod(), QString("all"));
  }
  void linkedInstallationsShareBacklogIdentity() {
    Database data;
    LinkedSource source;
    UnifiedGameModel games(data.path);
    games.addSourceModel(&source);
    QCOMPARE(games.rowCount(), 2);
    QVERIFY(games.linkGames(0, "RetroArch", "", "1"));
    QCOMPARE(games.rowCount(), 1);
    data.add(timestamp(2026, 1, 1, 12), 600, "/games/copy0.sfc");
    data.add(timestamp(2026, 3, 1, 12), 600, "/games/copy1.sfc");
    PlayStats stats(&games, data.path);
    stats.setPeriod("all");
    QCOMPARE(stats.headline().value("gamesPlayed").toInt(), 1);
    QCOMPARE(stats.backlog().value("firstTimeGames").toInt(), 1);
    QCOMPARE(stats.backlog().value("oneAndDone").toInt(), 0);
    QCOMPARE(stats.backlog().value("returns").toInt(), 1);
    data.add(timestamp(2026, 4, 1, 12), 1000, "/games/unknown.bin");
    data.sql("UPDATE play_sessions SET source='RetroArch'");
    stats.refresh();
    bool unknown = false;
    for (const auto& entry : stats.bySystem()) {
      const auto row = entry.toMap();
      if (row.value("name").toString() == "RetroArch") {
        unknown = true;
        QCOMPARE(row.value("seconds").toLongLong(), qint64(1000));
        QVERIFY(!row.value("console").toBool());
      }
    }
    QVERIFY(unknown);
  }

  void documentsAndIdleScopesAreNotGames() {
    GameStop::GameIdentity game;
    game.installPath = "/games/test";
    for (const QString& program : {"kate", "nautilus", "rsync", "tail", "python3"}) {
      ProcessSnapshot p;
      p.pid = 4242;
      p.procStart = 100;
      p.comm = program;
      p.exePath = "/usr/bin/" + program;
      p.arguments = {program, "/games/test/config.ini"};
      QVERIFY(GameStop::plan(game, {p}, {}).isEmpty());
    }
    game.flatpak = true;
    game.flatpakAppId = "com.valvesoftware.Steam";
    game.winePrefixes = {"/games/test/pfx"};
    QVERIFY(GameStop::plan(game, {}, {}).isEmpty());
  }
  void presenceReconnectsForUnchangedActivity() {
    QTemporaryDir dir;
    const auto path = dir.filePath("discord.sock");
    std::atomic<int> accepted{0};
    std::atomic<bool> ready{false};
    std::atomic<bool> failed{false};
    std::unique_ptr<QThread> peer(QThread::create([&] {
      QLocalServer server;
      if (!server.listen(path)) {
        failed = true;
        ready = true;
        return;
      }
      ready = true;
      for (int connection = 0; connection < 2; ++connection) {
        if (!server.waitForNewConnection(4000)) {
          failed = true;
          return;
        }
        std::unique_ptr<QLocalSocket> socket(server.nextPendingConnection());
        const auto readFrame = [&]() {
          while (socket->bytesAvailable() < 8)
            if (!socket->waitForReadyRead(1500))
              return QByteArray{};
          auto header = socket->read(8);
          const auto size = qFromLittleEndian<quint32>(header.constData() + 4);
          while (socket->bytesAvailable() < size)
            if (!socket->waitForReadyRead(1500))
              return QByteArray{};
          return socket->read(size);
        };
        if (readFrame().isEmpty()) {
          failed = true;
          return;
        }
        socket->write(DiscordPresence::frame(1, R"({"evt":"READY"})"));
        socket->waitForBytesWritten(1000);
        auto command = QJsonDocument::fromJson(readFrame()).object();
        if (command.value("nonce").toString().isEmpty()) {
          failed = true;
          return;
        }
        QJsonObject reply{{"nonce", command.value("nonce")}, {"evt", QJsonValue::Null}};
        socket->write(
            DiscordPresence::frame(1, QJsonDocument(reply).toJson(QJsonDocument::Compact)));
        socket->waitForBytesWritten(1000);
        ++accepted;
        socket->disconnectFromServer();
      }
    }));
    peer->start();
    // Join on every assertion exit, including a failed server setup.
    const auto join = qScopeGuard([&] { peer->wait(); });
    QTRY_VERIFY(ready.load());
    QVERIFY(!failed.load());
    DiscordPresence::Client client("123", {path});
    const QJsonObject activity{{"details", "Test Game"}};
    QVERIFY(client.publishActivity(activity));
    QTRY_COMPARE(accepted.load(), 1);
    QTest::qWait(50);
    QVERIFY(client.publishActivity(activity));
    QTRY_COMPARE(accepted.load(), 2);
    QVERIFY(!failed.load());
  }

  // Scope A: a session refused by the database is written to the durable journal and
  // replayed after a restart, with its original boundaries and exactly once.
  void journalReplaysRefusedSessionAfterRestart() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath("library.sqlite3");
    const QString journalPath = dbPath + QStringLiteral(".journal");
    const QString connection = QStringLiteral("journal-replay");
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      {
        QSqlQuery trigger(database);
        QVERIFY(trigger.exec(QStringLiteral(
            "CREATE TRIGGER deny_insert BEFORE INSERT ON play_sessions BEGIN "
            "SELECT RAISE(ABORT,'denied'); END")));
      }
      qint64 nowMs = 0;
      SessionRecorder recorder(database, [&nowMs] { return nowMs; }, journalPath);
      recorder.setFlushIntervalMs(1);
      const SessionMatch match{.pid = 42,
                               .procStart = 420,
                               .emulator = QStringLiteral("Ryujinx"),
                               .gamePath = QStringLiteral("/games/journaled.nsp")};
      recorder.sync({match}, 1000);
      nowMs = 60000;
      recorder.sync({}, 1060);
      QCOMPARE(recorder.pendingCloseCount(), 1);
      QVERIFY(QFileInfo::exists(journalPath));
      // Storage recovers after the game has gone and before any replay: the trigger is a
      // test stand-in for a database that was full or read-only and is writable again.
      {
        QSqlQuery dropTrigger(database);
        QVERIFY(dropTrigger.exec(QStringLiteral("DROP TRIGGER deny_insert")));
      }
    }
    QSqlDatabase::removeDatabase(connection);
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      {
        SessionRecorder recorder(database, [] { return qint64(0); }, journalPath);
        recorder.recover({}, ProcessProfileSet{}, 2000);
      }
      QSqlQuery query(database);
      QVERIFY(query.exec(QStringLiteral(
          "SELECT game_path, started_at, ended_at, seconds FROM play_sessions")));
      QVERIFY2(query.next(), "the journaled session was not replayed");
      QCOMPARE(query.value(0).toString(), QStringLiteral("/games/journaled.nsp"));
      QCOMPARE(query.value(1).toLongLong(), qint64(1000));
      QCOMPARE(query.value(2).toLongLong(), qint64(1060));
      QCOMPARE(query.value(3).toLongLong(), qint64(60));
      QVERIFY2(!query.next(), "replay wrote the session more than once");
    }
    QSqlDatabase::removeDatabase(connection);
  }

  // A crash between the database commit and the journal acknowledgment must not double a
  // session: the stable key makes the replay a no-op.
  void journalReplayAfterCommitBeforeAckIsIdempotent() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath("library.sqlite3");
    const QString journalPath = dbPath + QStringLiteral(".journal");
    const QString connection = QStringLiteral("journal-ack");
    const QString key = QStringLiteral("11111111-2222-3333-4444-555555555555");
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      QVERIFY(SessionDatabase::insertClosedSession(database, QStringLiteral("/games/idempotent.nsp"),
                                                   QStringLiteral("Ryujinx"), 1000, 1060, 60, 1, 1,
                                                   key));
    }
    QSqlDatabase::removeDatabase(connection);
    {
      SessionJournal journal(journalPath);
      QVERIFY(journal.open());
      SessionJournal::Operation operation;
      operation.key = key;
      operation.gamePath = QStringLiteral("/games/idempotent.nsp");
      operation.source = QStringLiteral("Ryujinx");
      operation.startedAt = 1000;
      operation.endedAt = 1060;
      operation.seconds = 60;
      operation.pid = 1;
      operation.procStart = 1;
      QVERIFY(journal.append(operation));
    }
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      SessionRecorder recorder(database, [] { return qint64(0); }, journalPath);
      recorder.recover({}, ProcessProfileSet{}, 2000);
      QSqlQuery query(database);
      QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM play_sessions")));
      QVERIFY(query.next());
      QCOMPARE(query.value(0).toInt(), 1);
    }
    QSqlDatabase::removeDatabase(connection);
  }

  // Deleting a game's history before a pending record is replayed must stop that record
  // coming back, even when the game had no stored row at all.
  void journalDoesNotResurrectDeletedHistory() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath("library.sqlite3");
    const QString journalPath = dbPath + QStringLiteral(".journal");
    const QString connection = QStringLiteral("journal-delete");
    const QString path = QStringLiteral("/games/deleted.nsp");
    {
      SessionJournal journal(journalPath);
      QVERIFY(journal.open());
      SessionJournal::Operation operation;
      operation.key = QStringLiteral("22222222-3333-4444-5555-666666666666");
      operation.gamePath = path;
      operation.source = QStringLiteral("Ryujinx");
      operation.startedAt = 1000;
      operation.endedAt = 1060;
      operation.seconds = 60;
      QVERIFY(journal.append(operation));
    }
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      // The game has no stored row, but clearing its history still has to invalidate the
      // pending record.
      QCOMPARE(SessionDatabase::deleteSessionsForPaths(database, {path}), 0);
      SessionRecorder recorder(database, [] { return qint64(0); }, journalPath);
      recorder.recover({}, ProcessProfileSet{}, 2000);
      QSqlQuery query(database);
      QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM play_sessions")));
      QVERIFY(query.next());
      QCOMPARE(query.value(0).toInt(), 0);
    }
    QSqlDatabase::removeDatabase(connection);
  }

  // A restart reconciles a session whose process is gone and replays a refused close.
  void restartReconcilesOpenSessionAndReplaysClosed() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath("library.sqlite3");
    const QString journalPath = dbPath + QStringLiteral(".journal");
    const QString connection = QStringLiteral("journal-restart");
    const QString openKey = QStringLiteral("33333333-4444-5555-6666-777777777777");
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      QVERIFY(SessionDatabase::beginSession(database, QStringLiteral("/games/live.nsp"),
                                            QStringLiteral("Ryujinx"), 500, 999999, 999999,
                                            openKey) > 0);
      QSqlQuery update(database);
      update.prepare(QStringLiteral(
          "UPDATE play_sessions SET seconds = 200, heartbeat_at = 700 WHERE session_key = ?"));
      update.addBindValue(openKey);
      QVERIFY(update.exec());
      SessionJournal journal(journalPath);
      QVERIFY(journal.open());
      SessionJournal::Operation operation;
      operation.key = QStringLiteral("44444444-5555-6666-7777-888888888888");
      operation.gamePath = QStringLiteral("/games/done.nsp");
      operation.source = QStringLiteral("Ryujinx");
      operation.startedAt = 1000;
      operation.endedAt = 1060;
      operation.seconds = 60;
      QVERIFY(journal.append(operation));
    }
    QSqlDatabase::removeDatabase(connection);
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      {
        SessionRecorder recorder(database, [] { return qint64(0); }, journalPath);
        recorder.recover({}, ProcessProfileSet{}, 2000);
      }
      QSqlQuery query(database);
      QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM play_sessions WHERE ended_at = 0")));
      QVERIFY(query.next());
      QCOMPARE(query.value(0).toInt(), 0);
      QVERIFY(query.exec(QStringLiteral(
          "SELECT ended_at, seconds FROM play_sessions WHERE game_path = '/games/live.nsp'")));
      QVERIFY(query.next());
      QCOMPARE(query.value(0).toLongLong(), qint64(700));
      QCOMPARE(query.value(1).toLongLong(), qint64(200));
      QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM play_sessions")));
      QVERIFY(query.next());
      QCOMPARE(query.value(0).toInt(), 2);
    }
    QSqlDatabase::removeDatabase(connection);
  }

  // The journal refuses records at its cap and never evicts one it accepted.
  void journalCapacityRefusesWithoutEviction() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString journalPath = dir.filePath("capacity.journal");
    SessionJournal journal(journalPath);
    QVERIFY(journal.open());
    int accepted = 0;
    for (int index = 0; index < 600; ++index) {
      SessionJournal::Operation operation;
      operation.key = QStringLiteral("key-%1").arg(index);
      operation.gamePath = QStringLiteral("/games/g%1.nsp").arg(index);
      operation.source = QStringLiteral("Ryujinx");
      operation.startedAt = 1000;
      operation.endedAt = 1060;
      operation.seconds = 60;
      if (journal.append(operation)) {
        ++accepted;
      }
    }
    QVERIFY2(journal.full(), "the journal did not report that it reached its cap");
    QCOMPARE(accepted, 512);
    const QVector<SessionJournal::Operation> pending = journal.pending(1000, {}, 0);
    QCOMPARE(pending.size(), 512);
    QCOMPARE(pending.first().key, QStringLiteral("key-0"));
  }

  // A malformed journal is set aside once and replaced, so corruption cannot poison every
  // startup.
  void journalMalformedFileIsSetAsideAndReplaced() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString journalPath = dir.filePath("bad.journal");
    {
      QFile file(journalPath);
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write("this is not a journal");
      file.close();
    }
    SessionJournal journal(journalPath);
    QVERIFY(journal.open());
    QVERIFY(journal.recoveredCorrupt());
    QVERIFY(QFileInfo::exists(journalPath + QStringLiteral(".corrupt")));
    SessionJournal::Operation operation;
    operation.key = QStringLiteral("fresh");
    operation.gamePath = QStringLiteral("/games/fresh.nsp");
    operation.source = QStringLiteral("Ryujinx");
    operation.startedAt = 1000;
    operation.endedAt = 1060;
    operation.seconds = 60;
    QVERIFY(journal.append(operation));
    QCOMPARE(journal.pending(10, {}, 0).size(), 1);
  }

  // Storage that cannot hold the journal is reported, not silently accepted.
  void journalUnavailableStorageIsReported() {
    SessionJournal journal(QStringLiteral("/proc/omakade-nonexistent/journal"));
    QVERIFY(!journal.open());
    QVERIFY(!journal.available());
    SessionJournal::Operation operation;
    operation.key = QStringLiteral("x");
    operation.gamePath = QStringLiteral("/games/x.nsp");
    QVERIFY(!journal.append(operation));
  }

  // The stable key makes the first write and a replayed close idempotent on the database.
  void stableSessionKeyMakesWritesIdempotent() {
    const QString connection = QStringLiteral("stable-key");
    QSqlDatabase database;
    QVERIFY(SessionDatabase::open(database, QStringLiteral(":memory:"), connection));
    const QString key = QStringLiteral("55555555-6666-7777-8888-999999999999");
    const qint64 first = SessionDatabase::beginSession(database, QStringLiteral("/games/key.nsp"),
                                                       QStringLiteral("Ryujinx"), 1000, 1, 1, key);
    const qint64 second = SessionDatabase::beginSession(database, QStringLiteral("/games/key.nsp"),
                                                        QStringLiteral("Ryujinx"), 1000, 1, 1, key);
    QCOMPARE(first, second);
    QVERIFY(SessionDatabase::insertClosedSession(database, QStringLiteral("/games/key.nsp"),
                                                 QStringLiteral("Ryujinx"), 1000, 1060, 60, 1, 1,
                                                 key));
    // A second identical write is a no-op rather than a duplicate row.
    QVERIFY(SessionDatabase::insertClosedSession(database, QStringLiteral("/games/key.nsp"),
                                                 QStringLiteral("Ryujinx"), 1000, 1060, 60, 1, 1,
                                                 key));
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*), SUM(seconds) FROM play_sessions")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
    QCOMPARE(query.value(1).toLongLong(), qint64(60));
    database.close();
    database = {};
    QSqlDatabase::removeDatabase(connection);
  }
};
QTEST_GUILESS_MAIN(ReleaseHardeningTests)
#include "ReleaseHardeningTests.moc"
