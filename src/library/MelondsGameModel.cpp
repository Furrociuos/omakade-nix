#include "library/MelondsGameModel.h"

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

bool looksLikeDsSystem(const QString& candidate) {
  for (const QString& name : {QStringLiteral("ds"), QStringLiteral("nds"), QStringLiteral("dsi")}) {
    if (candidate.compare(name, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}
} // namespace

QStringList MelondsGameModel::dsFolders(const QStringList& encoded) {
  QStringList folders;
  for (const RomFolder& folder : RomFolderScanner::parseEncoded(encoded)) {
    // A folder counts as a DS folder when the user labelled it as one, or when its own name says
    // so. Anything else belongs to another console's source and is not walked here.
    const bool labelled =
        looksLikeDsSystem(folder.system) || looksLikeDsSystem(QFileInfo(folder.path).fileName());
    if (labelled && !folders.contains(folder.path)) {
      folders.append(folder.path);
    }
  }
  return folders;
}

MelondsGameModel::MelondsGameModel(const QString& omakadeDatabasePath,
                                   PlaySessionStore* playSessions, QObject* parent)
    : QAbstractListModel(parent),
      m_connectionName(QStringLiteral("omakade-melonds-%1").arg(reinterpret_cast<quintptr>(this))),
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
  connect(&m_scanWatcher, &QFutureWatcher<MelondsScanResult>::finished, this, [this] {
    m_scanning = false;
    applyScan(m_scanWatcher.result());
    emit statusChanged();
  });
  if (openDatabase(omakadeDatabasePath) && ensureSchema()) {
    loadDatabase();
    loadSourceState();
  }
}

MelondsGameModel::~MelondsGameModel() {
  if (m_scanWatcher.isRunning()) {
    m_scanWatcher.waitForFinished();
  }
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

int MelondsGameModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_games.size());
}

QVariant MelondsGameModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size()) {
    return {};
  }
  return valueForRole(m_games.at(index.row()), role);
}

QHash<int, QByteArray> MelondsGameModel::roleNames() const {
  auto roles = GameRoles::names();
  roles.insert(GameRoles::LaunchTarget, "launchTarget");
  return roles;
}

bool MelondsGameModel::melondsDetected() const { return m_melondsDetected; }
QString MelondsGameModel::statusText() const { return m_statusText; }
QString MelondsGameModel::errorText() const { return m_errorText; }
QStringList MelondsGameModel::detectedPaths() const { return m_detectedPaths; }
qint64 MelondsGameModel::lastScan() const { return m_lastScan; }

void MelondsGameModel::toggleFavorite(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.favorite = !game.favorite;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE melonds_games SET favorite = ? WHERE game_id = ?"));
  query.addBindValue(game.favorite);
  query.addBindValue(game.melonds.gameId);
  if (!query.exec()) {
    game.favorite = !game.favorite;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Favorite});
}

void MelondsGameModel::toggleHidden(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.hidden = !game.hidden;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE melonds_games SET hidden = ? WHERE game_id = ?"));
  query.addBindValue(game.hidden);
  query.addBindValue(game.melonds.gameId);
  if (!query.exec()) {
    game.hidden = !game.hidden;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Hidden});
}

void MelondsGameModel::setConfiguredRomFolders(const QStringList& encoded) {
  m_configuredFolders = dsFolders(encoded);
}

void MelondsGameModel::refresh() {
  if (m_scanWatcher.isRunning()) {
    return;
  }
  m_scanning = true;
  const QStringList folders = m_configuredFolders;
  const QStringList configRoots = MelondsScanner::discoverConfigRoots();
  setStatus(folders.isEmpty() ? QStringLiteral("No DS ROM folder is configured")
                              : QStringLiteral("Scanning DS ROM folders"));
  m_scanWatcher.setFuture(
      QtConcurrent::run([folders, configRoots] { return MelondsScanner::scan(folders, configRoots); }));
}

void MelondsGameModel::refreshFromFolders(const QStringList& folders) {
  applyScan(MelondsScanner::scan(folders, MelondsScanner::discoverConfigRoots()));
}

bool MelondsGameModel::openDatabase(const QString& path) {
  if (path != QStringLiteral(":memory:")) {
    QDir().mkpath(QFileInfo(path).absolutePath());
  }
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(path);
  if (!openTunedDatabase(m_database)) {
    setStatus(QStringLiteral("melonDS cache unavailable"), m_database.lastError().text());
    return false;
  }
  return true;
}

bool MelondsGameModel::ensureSchema() {
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS melonds_games (game_id TEXT PRIMARY KEY, name TEXT NOT NULL, "
          "path TEXT, game_code TEXT, platform TEXT, cover_path TEXT, dsi INTEGER NOT NULL DEFAULT "
          "0, dsi_ware INTEGER NOT NULL DEFAULT 0, homebrew INTEGER NOT NULL DEFAULT 0, flatpak "
          "INTEGER NOT NULL DEFAULT 0, flatpak_app_id TEXT NOT NULL DEFAULT '', favorite INTEGER "
          "NOT NULL DEFAULT 0, hidden INTEGER NOT NULL DEFAULT 0, observed_at INTEGER NOT NULL)"))) {
    setStatus(QStringLiteral("Could not initialize melonDS cache"), query.lastError().text());
    return false;
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS source_state (source TEXT PRIMARY KEY, last_scan INTEGER, "
          "last_error TEXT, paths TEXT NOT NULL DEFAULT '')"))) {
    setStatus(QStringLiteral("Could not initialize melonDS cache"), query.lastError().text());
    return false;
  }
  return true;
}

void MelondsGameModel::loadDatabase() {
  QVector<Game> loaded;
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "SELECT game_id, name, path, game_code, platform, cover_path, dsi, dsi_ware, homebrew, "
          "flatpak, flatpak_app_id, favorite, hidden FROM melonds_games WHERE observed_at > 0 "
          "ORDER BY name COLLATE NOCASE"))) {
    setStatus(QStringLiteral("Could not load cached DS games"), query.lastError().text());
    return;
  }
  while (query.next()) {
    const MelondsGameRecord record{
        .gameId = query.value(0).toString(),
        .gameCode = query.value(3).toString(),
        .title = query.value(1).toString(),
        .path = query.value(2).toString(),
        .platform = query.value(4).toString(),
        .coverPath = query.value(5).toString(),
        .dsi = query.value(6).toBool(),
        .dsiWare = query.value(7).toBool(),
        .homebrew = query.value(8).toBool(),
        .flatpak = query.value(9).toBool(),
        .flatpakAppId = query.value(10).toString()};
    loaded.append({.melonds = record,
                   .favorite = query.value(11).toBool(),
                   .hidden = query.value(12).toBool(),
                   .accentStart = colorFor(record.gameId, 0),
                   .accentEnd = colorFor(record.gameId, 1)});
  }
  beginResetModel();
  m_games = loaded;
  endResetModel();
}

void MelondsGameModel::loadSourceState() {
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "SELECT last_scan, last_error, paths FROM source_state WHERE source = 'melonds'"));
  if (!query.exec() || !query.next()) {
    return;
  }
  m_lastScan = query.value(0).toLongLong();
  m_errorText = query.value(1).toString();
  m_detectedPaths = query.value(2).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  m_melondsDetected = MelondsScanner::melondsInstalled() || !m_detectedPaths.isEmpty();
  if (m_lastScan > 0) {
    m_statusText = QStringLiteral("Loaded cached DS games");
  }
}

void MelondsGameModel::applyScan(const MelondsScanResult& result) {
  m_melondsDetected = MelondsScanner::melondsInstalled() || !result.folders.isEmpty();
  if (result.incomplete || (result.folders.isEmpty() && !m_games.isEmpty())) {
    // A folder that is not there right now, or a scan that stopped early, must not empty the
    // library: the games stay, with the reason reported.
    setStatus(QStringLiteral("DS scan interrupted; kept the cached library"),
              result.warnings.join(QLatin1Char('\n')));
    return;
  }
  if (!m_database.transaction()) {
    setStatus(QStringLiteral("Could not update DS games"), m_database.lastError().text());
    return;
  }
  const qint64 scanTimestamp = QDateTime::currentSecsSinceEpoch();
  QSqlQuery query(m_database);
  bool okay = query.exec(QStringLiteral("UPDATE melonds_games SET observed_at = 0"));
  for (const MelondsGameRecord& game : result.games) {
    query.prepare(QStringLiteral(
        "INSERT INTO melonds_games(game_id, name, path, game_code, platform, cover_path, dsi, "
        "dsi_ware, homebrew, flatpak, flatpak_app_id, observed_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, strftime('%s', 'now')) ON CONFLICT(game_id) DO UPDATE SET name = excluded.name, "
        "path = excluded.path, game_code = excluded.game_code, platform = excluded.platform, "
        "cover_path = excluded.cover_path, dsi = excluded.dsi, dsi_ware = excluded.dsi_ware, "
        "homebrew = excluded.homebrew, flatpak = excluded.flatpak, flatpak_app_id = "
        "excluded.flatpak_app_id, observed_at = excluded.observed_at"));
    query.addBindValue(game.gameId);
    query.addBindValue(game.title);
    query.addBindValue(game.path);
    query.addBindValue(game.gameCode);
    query.addBindValue(game.platform);
    query.addBindValue(game.coverPath);
    query.addBindValue(game.dsi);
    query.addBindValue(game.dsiWare);
    query.addBindValue(game.homebrew);
    query.addBindValue(game.flatpak);
    query.addBindValue(game.flatpakAppId.isNull() ? QStringLiteral("") : game.flatpakAppId);
    okay = okay && query.exec();
  }
  query.prepare(QStringLiteral(
      "INSERT INTO source_state(source, last_scan, last_error, paths) VALUES('melonds', ?, ?, ?) "
      "ON CONFLICT(source) DO UPDATE SET last_scan = excluded.last_scan, last_error = "
      "excluded.last_error, paths = excluded.paths"));
  query.addBindValue(scanTimestamp);
  query.addBindValue(result.warnings.join(QLatin1Char('\n')));
  query.addBindValue(result.folders.isEmpty() ? QStringLiteral("")
                                              : result.folders.join(QLatin1Char('\n')));
  okay = okay && query.exec();
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    setStatus(QStringLiteral("Could not update DS games"), query.lastError().text());
    return;
  }
  loadDatabase();
  m_detectedPaths = result.folders;
  m_lastScan = scanTimestamp;
  setStatus(m_melondsDetected
                ? QStringLiteral("Imported %1 DS game(s)").arg(result.games.size())
                : QStringLiteral("No DS ROM folder is configured"),
            result.warnings.join(QLatin1Char('\n')));
}

QVariant MelondsGameModel::valueForRole(const Game& game, int role) const {
  switch (role) {
  case GameRoles::Title:
    return game.melonds.title;
  case GameRoles::Subtitle:
    return QStringLiteral("melonDS");
  case GameRoles::Description:
    // The console and the kind of dump are the header's own answers, so the description says what
    // melonDS will actually run rather than assuming every file is a plain DS release.
    return game.melonds.homebrew
               ? QStringLiteral("Homebrew %1 dump played through melonDS.").arg(game.melonds.platform)
               : QStringLiteral("Nintendo %1 game played through melonDS.")
                     .arg(game.melonds.platform);
  case GameRoles::PlaytimeProvenance:
    return PlaySessionStore::provenance(m_playSessions, game.melonds.path, -1);
  case GameRoles::PlaytimeSeconds:
    return PlaySessionStore::displayedSeconds(m_playSessions, game.melonds.path, 0);
  case GameRoles::Hours:
    return static_cast<int>(
        PlaySessionStore::displayedSeconds(m_playSessions, game.melonds.path, 0) / 3600);
  case GameRoles::Progress:
  case GameRoles::AchievementsUnlocked:
  case GameRoles::AchievementsTotal:
    return 0;
  case GameRoles::Favorite:
    return game.favorite;
  case GameRoles::Recent:
    return PlaySessionStore::displayedLastPlayed(m_playSessions, game.melonds.path, 0) > 0;
  case GameRoles::LastPlayed:
    return PlaySessionStore::displayedLastPlayed(m_playSessions, game.melonds.path, 0);
  case GameRoles::AccentStart:
    return game.accentStart;
  case GameRoles::AccentEnd:
    return game.accentEnd;
  case GameRoles::CoverMark:
    return game.melonds.title.left(1).toUpper();
  case GameRoles::Year:
    return 0;
  case GameRoles::AppId:
    return game.melonds.gameId;
  case GameRoles::CoverPath:
    return localUrl(game.melonds.coverPath);
  case GameRoles::HeroPath:
  case GameRoles::LogoPath:
    return QString{};
  case GameRoles::InstallPath:
    return game.melonds.path;
  case GameRoles::Source:
    return QStringLiteral("melonDS");
  case GameRoles::Runner:
    return game.melonds.flatpakAppId;
  case GameRoles::LaunchTarget:
    return game.melonds.path;
  case GameRoles::Flatpak:
    return game.melonds.flatpak;
  case GameRoles::Hidden:
    return game.hidden;
  case GameRoles::System:
    return QStringLiteral("ds");
  default:
    return {};
  }
}

void MelondsGameModel::setStatus(const QString& status, const QString& error) {
  m_statusText = status;
  m_errorText = error;
  emit statusChanged();
}
