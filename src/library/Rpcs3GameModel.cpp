#include "library/Rpcs3GameModel.h"

#include "library/DatabaseTuning.h"
#include "library/GameRoles.h"
#include "sources/retro/RomFolderScanner.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUrl>
#include <QtConcurrent>

namespace {
QColor colorFor(const QString& id, int offset) {
  const QByteArray hash = QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256);
  return QColor::fromHsl((static_cast<unsigned char>(hash.at(offset)) * 359) / 255, 115,
                         offset == 0 ? 105 : 72);
}

QString localUrl(const QString& path) {
  return path.isEmpty() ? QString{} : QUrl::fromLocalFile(path).toString();
}

bool looksLikePs3System(const QString& candidate) {
  return candidate.compare(QStringLiteral("ps3"), Qt::CaseInsensitive) == 0 ||
         candidate.compare(QStringLiteral("playstation 3"), Qt::CaseInsensitive) == 0;
}
} // namespace

QStringList Rpcs3GameModel::ps3Folders(const QStringList& encoded) {
  QStringList folders;
  for (const RomFolder& folder : RomFolderScanner::parseEncoded(encoded)) {
    const bool labelled = looksLikePs3System(folder.system) ||
                          looksLikePs3System(QFileInfo(folder.path).fileName());
    if (labelled && !folders.contains(folder.path)) {
      folders.append(folder.path);
    }
  }
  return folders;
}

Rpcs3GameModel::Rpcs3GameModel(const QString& omakadeDatabasePath,
                               PlaySessionStore* playSessions, QObject* parent)
    : QAbstractListModel(parent),
      m_connectionName(QStringLiteral("omakade-rpcs3-%1").arg(reinterpret_cast<quintptr>(this))),
      m_playSessions(playSessions) {
  if (m_playSessions != nullptr) {
    connect(m_playSessions, &PlaySessionStore::totalsChanged, this, [this] {
      if (!m_games.isEmpty()) {
        emit dataChanged(index(0), index(static_cast<int>(m_games.size()) - 1),
                         {GameRoles::Hours, GameRoles::PlaytimeSeconds, GameRoles::PlaytimeText,
                          GameRoles::PlaytimeProvenance, GameRoles::LastPlayed});
      }
    });
  }
  connect(&m_scanWatcher, &QFutureWatcher<Rpcs3ScanResult>::finished, this, [this] {
    m_scanning = false;
    applyScan(m_scanWatcher.result());
    emit statusChanged();
  });
  if (openDatabase(omakadeDatabasePath) && ensureSchema()) {
    loadDatabase();
    loadSourceState();
  }
}

Rpcs3GameModel::~Rpcs3GameModel() {
  if (m_scanWatcher.isRunning()) {
    m_scanWatcher.waitForFinished();
  }
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

int Rpcs3GameModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_games.size());
}

QVariant Rpcs3GameModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size()) {
    return {};
  }
  return valueForRole(m_games.at(index.row()), role);
}

QHash<int, QByteArray> Rpcs3GameModel::roleNames() const {
  auto roles = GameRoles::names();
  roles.insert(GameRoles::LaunchTarget, "launchTarget");
  return roles;
}

bool Rpcs3GameModel::rpcs3Detected() const { return m_rpcs3Detected; }
QString Rpcs3GameModel::statusText() const { return m_statusText; }
QString Rpcs3GameModel::errorText() const { return m_errorText; }
QStringList Rpcs3GameModel::detectedPaths() const { return m_detectedPaths; }
qint64 Rpcs3GameModel::lastScan() const { return m_lastScan; }

void Rpcs3GameModel::toggleFavorite(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.favorite = !game.favorite;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE rpcs3_games SET favorite = ? WHERE game_id = ?"));
  query.addBindValue(game.favorite);
  query.addBindValue(game.rpcs3.gameId);
  if (!query.exec()) {
    game.favorite = !game.favorite;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Favorite});
}

void Rpcs3GameModel::toggleHidden(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.hidden = !game.hidden;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE rpcs3_games SET hidden = ? WHERE game_id = ?"));
  query.addBindValue(game.hidden);
  query.addBindValue(game.rpcs3.gameId);
  if (!query.exec()) {
    game.hidden = !game.hidden;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Hidden});
}

void Rpcs3GameModel::setConfiguredRomFolders(const QStringList& encoded) {
  m_configuredFolders = ps3Folders(encoded);
}

void Rpcs3GameModel::refresh() {
  if (m_scanWatcher.isRunning()) {
    return;
  }
  m_scanning = true;
  const QStringList roots = Rpcs3Scanner::discoverConfigRoots();
  const QStringList folders = m_configuredFolders;
  setStatus(QStringLiteral("Scanning RPCS3 library"));
  m_scanWatcher.setFuture(QtConcurrent::run(
      [roots, folders] { return Rpcs3Scanner::scan(roots, folders); }));
}

void Rpcs3GameModel::refreshFromRoots(const QStringList& roots, const QStringList& userRoots) {
  applyScan(Rpcs3Scanner::scan(roots, userRoots));
}

bool Rpcs3GameModel::openDatabase(const QString& path) {
  if (path != QStringLiteral(":memory:")) {
    QDir().mkpath(QFileInfo(path).absolutePath());
  }
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(path);
  if (!openTunedDatabase(m_database)) {
    setStatus(QStringLiteral("RPCS3 cache unavailable"), m_database.lastError().text());
    return false;
  }
  return true;
}

bool Rpcs3GameModel::ensureSchema() {
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS rpcs3_games (game_id TEXT PRIMARY KEY, name TEXT NOT NULL, "
          "path TEXT NOT NULL, launch_target TEXT NOT NULL, title_id TEXT, category TEXT, "
          "cover_path TEXT, installed INTEGER NOT NULL DEFAULT 0, flatpak INTEGER NOT NULL "
          "DEFAULT 0, favorite INTEGER NOT NULL DEFAULT 0, hidden INTEGER NOT NULL DEFAULT 0, "
          "observed_at INTEGER NOT NULL)"))) {
    setStatus(QStringLiteral("Could not initialize RPCS3 cache"), query.lastError().text());
    return false;
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS source_state (source TEXT PRIMARY KEY, last_scan INTEGER, "
          "last_error TEXT, paths TEXT NOT NULL DEFAULT '')"))) {
    setStatus(QStringLiteral("Could not initialize RPCS3 cache"), query.lastError().text());
    return false;
  }
  return true;
}

void Rpcs3GameModel::loadDatabase() {
  QVector<Game> loaded;
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "SELECT game_id, name, path, launch_target, title_id, category, cover_path, installed, "
          "flatpak, favorite, hidden FROM rpcs3_games WHERE observed_at > 0 ORDER BY name COLLATE "
          "NOCASE"))) {
    setStatus(QStringLiteral("Could not load cached RPCS3 games"), query.lastError().text());
    return;
  }
  while (query.next()) {
    const Rpcs3GameRecord record{.gameId = query.value(0).toString(),
                                 .titleId = query.value(4).toString(),
                                 .title = query.value(1).toString(),
                                 .path = query.value(2).toString(),
                                 .launchTarget = query.value(3).toString(),
                                 .category = query.value(5).toString(),
                                 .coverPath = query.value(6).toString(),
                                 .installed = query.value(7).toBool(),
                                 .flatpak = query.value(8).toBool()};
    loaded.append({.rpcs3 = record,
                   .favorite = query.value(9).toBool(),
                   .hidden = query.value(10).toBool(),
                   .accentStart = colorFor(record.gameId, 0),
                   .accentEnd = colorFor(record.gameId, 1)});
  }
  beginResetModel();
  m_games = loaded;
  endResetModel();
}

void Rpcs3GameModel::loadSourceState() {
  QSqlQuery query(m_database);
  query.prepare(
      QStringLiteral("SELECT last_scan, last_error, paths FROM source_state WHERE source = 'rpcs3'"));
  if (!query.exec() || !query.next()) {
    m_rpcs3Detected = Rpcs3Scanner::rpcs3Installed();
    return;
  }
  m_lastScan = query.value(0).toLongLong();
  m_errorText = query.value(1).toString();
  m_detectedPaths = query.value(2).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  m_rpcs3Detected = Rpcs3Scanner::rpcs3Installed() || !m_detectedPaths.isEmpty();
  if (m_lastScan > 0) {
    m_statusText = QStringLiteral("Loaded cached RPCS3 games");
  }
}

void Rpcs3GameModel::applyScan(const Rpcs3ScanResult& result) {
  m_rpcs3Detected = Rpcs3Scanner::rpcs3Installed() || !result.roots.isEmpty();
  if (result.incomplete || (result.roots.isEmpty() && !m_games.isEmpty())) {
    setStatus(QStringLiteral("RPCS3 scan interrupted; kept the cached library"),
              result.warnings.join(QLatin1Char('\n')));
    return;
  }
  if (!m_database.transaction()) {
    setStatus(QStringLiteral("Could not update RPCS3 games"), m_database.lastError().text());
    return;
  }
  const qint64 scanTimestamp = QDateTime::currentSecsSinceEpoch();
  QSqlQuery query(m_database);
  bool okay = query.exec(QStringLiteral("UPDATE rpcs3_games SET observed_at = 0"));
  for (const Rpcs3GameRecord& game : result.games) {
    query.prepare(QStringLiteral(
        "INSERT INTO rpcs3_games(game_id, name, path, launch_target, title_id, category, "
        "cover_path, installed, flatpak, observed_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "strftime('%s', 'now')) ON CONFLICT(game_id) DO UPDATE SET name = excluded.name, path = "
        "excluded.path, launch_target = excluded.launch_target, title_id = excluded.title_id, "
        "category = excluded.category, cover_path = excluded.cover_path, installed = "
        "excluded.installed, flatpak = excluded.flatpak, observed_at = excluded.observed_at"));
    query.addBindValue(game.gameId);
    query.addBindValue(game.title);
    query.addBindValue(game.path);
    query.addBindValue(game.launchTarget);
    query.addBindValue(game.titleId);
    query.addBindValue(game.category);
    query.addBindValue(game.coverPath);
    query.addBindValue(game.installed);
    query.addBindValue(game.flatpak);
    okay = okay && query.exec();
  }
  query.prepare(QStringLiteral(
      "INSERT INTO source_state(source, last_scan, last_error, paths) VALUES('rpcs3', ?, ?, ?) "
      "ON CONFLICT(source) DO UPDATE SET last_scan = excluded.last_scan, last_error = "
      "excluded.last_error, paths = excluded.paths"));
  query.addBindValue(scanTimestamp);
  query.addBindValue(result.warnings.join(QLatin1Char('\n')));
  query.addBindValue(result.roots.isEmpty() ? QStringLiteral("")
                                            : result.roots.join(QLatin1Char('\n')));
  okay = okay && query.exec();
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    setStatus(QStringLiteral("Could not update RPCS3 games"), query.lastError().text());
    return;
  }
  loadDatabase();
  m_detectedPaths = result.roots;
  m_lastScan = scanTimestamp;
  setStatus(m_rpcs3Detected ? QStringLiteral("Imported %1 RPCS3 game(s)").arg(result.games.size())
                           : QStringLiteral("RPCS3 was not found"),
            result.warnings.join(QLatin1Char('\n')));
}

QVariant Rpcs3GameModel::valueForRole(const Game& game, int role) const {
  switch (role) {
  case GameRoles::Title:
    return game.rpcs3.title;
  case GameRoles::Subtitle:
    return game.rpcs3.category.isEmpty()
               ? QStringLiteral("RPCS3")
               : QStringLiteral("RPCS3 · %1").arg(game.rpcs3.category);
  case GameRoles::Description:
    return QStringLiteral("PlayStation 3 game launched through RPCS3.");
  case GameRoles::PlaytimeProvenance:
    return PlaySessionStore::provenance(m_playSessions, game.rpcs3.path, -1);
  case GameRoles::PlaytimeSeconds:
    return PlaySessionStore::displayedSeconds(m_playSessions, game.rpcs3.path, 0);
  case GameRoles::Hours:
    return static_cast<int>(PlaySessionStore::displayedSeconds(m_playSessions, game.rpcs3.path, 0) /
                            3600);
  case GameRoles::Progress:
  case GameRoles::AchievementsUnlocked:
  case GameRoles::AchievementsTotal:
    return 0;
  case GameRoles::Favorite:
    return game.favorite;
  case GameRoles::Recent:
    return PlaySessionStore::displayedLastPlayed(m_playSessions, game.rpcs3.path, 0) > 0;
  case GameRoles::LastPlayed:
    return PlaySessionStore::displayedLastPlayed(m_playSessions, game.rpcs3.path, 0);
  case GameRoles::AccentStart:
    return game.accentStart;
  case GameRoles::AccentEnd:
    return game.accentEnd;
  case GameRoles::CoverMark:
    return game.rpcs3.title.left(1).toUpper();
  case GameRoles::Year:
    return 0;
  case GameRoles::AppId:
    return game.rpcs3.titleId.isEmpty() ? game.rpcs3.gameId : game.rpcs3.titleId;
  case GameRoles::CoverPath:
    return localUrl(game.rpcs3.coverPath);
  case GameRoles::HeroPath:
  case GameRoles::LogoPath:
    return QString{};
  case GameRoles::InstallPath:
    return game.rpcs3.path;
  case GameRoles::Source:
    return QStringLiteral("RPCS3");
  case GameRoles::Runner:
    return QString{};
  case GameRoles::LaunchTarget:
    return game.rpcs3.launchTarget;
  case GameRoles::Flatpak:
    return game.rpcs3.flatpak;
  case GameRoles::Hidden:
    return game.hidden;
  case GameRoles::System:
    return QStringLiteral("ps3");
  default:
    return {};
  }
}

void Rpcs3GameModel::setStatus(const QString& status, const QString& error) {
  m_statusText = status;
  m_errorText = error;
  emit statusChanged();
}
