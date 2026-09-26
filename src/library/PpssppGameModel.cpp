#include "library/PpssppGameModel.h"

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

bool looksLikePspSystem(const QString& candidate) {
  return candidate.compare(QStringLiteral("psp"), Qt::CaseInsensitive) == 0 ||
         candidate.compare(QStringLiteral("playstation portable"), Qt::CaseInsensitive) == 0;
}
} // namespace

QStringList PpssppGameModel::pspFolders(const QStringList& encoded) {
  QStringList folders;
  for (const RomFolder& folder : RomFolderScanner::parseEncoded(encoded)) {
    const bool labelled = looksLikePspSystem(folder.system) ||
                          looksLikePspSystem(QFileInfo(folder.path).fileName());
    if (labelled && !folders.contains(folder.path)) {
      folders.append(folder.path);
    }
  }
  return folders;
}

PpssppGameModel::PpssppGameModel(const QString& omakadeDatabasePath,
                                 PlaySessionStore* playSessions, QObject* parent)
    : QAbstractListModel(parent),
      m_connectionName(QStringLiteral("omakade-ppsspp-%1").arg(reinterpret_cast<quintptr>(this))),
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
  connect(&m_scanWatcher, &QFutureWatcher<PspScanResult>::finished, this, [this] {
    m_scanning = false;
    applyScan(m_scanWatcher.result());
    emit statusChanged();
  });
  if (openDatabase(omakadeDatabasePath) && ensureSchema()) {
    loadDatabase();
    loadSourceState();
  }
}

PpssppGameModel::~PpssppGameModel() {
  if (m_scanWatcher.isRunning()) {
    m_scanWatcher.waitForFinished();
  }
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

int PpssppGameModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_games.size());
}

QVariant PpssppGameModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size()) {
    return {};
  }
  return valueForRole(m_games.at(index.row()), role);
}

QHash<int, QByteArray> PpssppGameModel::roleNames() const {
  auto roles = GameRoles::names();
  roles.insert(GameRoles::LaunchTarget, "launchTarget");
  return roles;
}

bool PpssppGameModel::ppssppDetected() const { return m_ppssppDetected; }
QString PpssppGameModel::statusText() const { return m_statusText; }
QString PpssppGameModel::errorText() const { return m_errorText; }
QStringList PpssppGameModel::detectedPaths() const { return m_detectedPaths; }
qint64 PpssppGameModel::lastScan() const { return m_lastScan; }

void PpssppGameModel::toggleFavorite(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.favorite = !game.favorite;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE ppsspp_games SET favorite = ? WHERE game_id = ?"));
  query.addBindValue(game.favorite);
  query.addBindValue(game.psp.gameId);
  if (!query.exec()) {
    game.favorite = !game.favorite;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Favorite});
}

void PpssppGameModel::toggleHidden(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.hidden = !game.hidden;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE ppsspp_games SET hidden = ? WHERE game_id = ?"));
  query.addBindValue(game.hidden);
  query.addBindValue(game.psp.gameId);
  if (!query.exec()) {
    game.hidden = !game.hidden;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Hidden});
}

void PpssppGameModel::setConfiguredRomFolders(const QStringList& encoded) {
  m_configuredFolders = pspFolders(encoded);
}

void PpssppGameModel::refresh() {
  if (m_scanWatcher.isRunning()) {
    return;
  }
  m_scanning = true;
  const QStringList roots = PpssppScanner::discoverRoots();
  const QStringList folders = m_configuredFolders;
  setStatus(QStringLiteral("Scanning PPSSPP games"));
  m_scanWatcher.setFuture(QtConcurrent::run(
      [roots, folders] { return PpssppScanner::scan(roots, folders); }));
}

void PpssppGameModel::refreshFromRoots(const QStringList& roots, const QStringList& userRoots) {
  applyScan(PpssppScanner::scan(roots, userRoots));
}

bool PpssppGameModel::openDatabase(const QString& path) {
  if (path != QStringLiteral(":memory:")) {
    QDir().mkpath(QFileInfo(path).absolutePath());
  }
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(path);
  if (!openTunedDatabase(m_database)) {
    setStatus(QStringLiteral("PPSSPP cache unavailable"), m_database.lastError().text());
    return false;
  }
  return true;
}

bool PpssppGameModel::ensureSchema() {
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS ppsspp_games (game_id TEXT PRIMARY KEY, name TEXT NOT NULL, "
          "path TEXT NOT NULL, disc_id TEXT, disc_version TEXT, region TEXT, cover_path TEXT, "
          "homebrew INTEGER NOT NULL DEFAULT 0, flatpak INTEGER NOT NULL DEFAULT 0, favorite "
          "INTEGER NOT NULL DEFAULT 0, hidden INTEGER NOT NULL DEFAULT 0, observed_at INTEGER NOT "
          "NULL)"))) {
    setStatus(QStringLiteral("Could not initialize PPSSPP cache"), query.lastError().text());
    return false;
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS source_state (source TEXT PRIMARY KEY, last_scan INTEGER, "
          "last_error TEXT, paths TEXT NOT NULL DEFAULT '')"))) {
    setStatus(QStringLiteral("Could not initialize PPSSPP cache"), query.lastError().text());
    return false;
  }
  return true;
}

void PpssppGameModel::loadDatabase() {
  QVector<Game> loaded;
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "SELECT game_id, name, path, disc_id, disc_version, region, cover_path, homebrew, "
          "flatpak, favorite, hidden FROM ppsspp_games WHERE observed_at > 0 ORDER BY name COLLATE "
          "NOCASE"))) {
    setStatus(QStringLiteral("Could not load cached PPSSPP games"), query.lastError().text());
    return;
  }
  while (query.next()) {
    const PspGameRecord record{.gameId = query.value(0).toString(),
                               .title = query.value(1).toString(),
                               .path = query.value(2).toString(),
                               .discId = query.value(3).toString(),
                               .discVersion = query.value(4).toString(),
                               .region = query.value(5).toString(),
                               .coverPath = query.value(6).toString(),
                               .homebrew = query.value(7).toBool(),
                               .flatpak = query.value(8).toBool()};
    loaded.append({.psp = record,
                   .favorite = query.value(9).toBool(),
                   .hidden = query.value(10).toBool(),
                   .accentStart = colorFor(record.gameId, 0),
                   .accentEnd = colorFor(record.gameId, 1)});
  }
  beginResetModel();
  m_games = loaded;
  endResetModel();
}

void PpssppGameModel::loadSourceState() {
  QSqlQuery query(m_database);
  query.prepare(
      QStringLiteral("SELECT last_scan, last_error, paths FROM source_state WHERE source = 'ppsspp'"));
  if (!query.exec() || !query.next()) {
    m_ppssppDetected = PpssppScanner::ppssppInstalled();
    return;
  }
  m_lastScan = query.value(0).toLongLong();
  m_errorText = query.value(1).toString();
  m_detectedPaths = query.value(2).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  m_ppssppDetected = PpssppScanner::ppssppInstalled() || !m_detectedPaths.isEmpty();
  if (m_lastScan > 0) {
    m_statusText = QStringLiteral("Loaded cached PPSSPP games");
  }
}

void PpssppGameModel::applyScan(const PspScanResult& result) {
  m_ppssppDetected = PpssppScanner::ppssppInstalled() || !result.roots.isEmpty();
  if (result.incomplete || (result.roots.isEmpty() && !m_games.isEmpty())) {
    setStatus(QStringLiteral("PPSSPP scan interrupted; kept the cached library"),
              result.warnings.join(QLatin1Char('\n')));
    return;
  }
  if (!m_database.transaction()) {
    setStatus(QStringLiteral("Could not update PPSSPP games"), m_database.lastError().text());
    return;
  }
  const qint64 scanTimestamp = QDateTime::currentSecsSinceEpoch();
  QSqlQuery query(m_database);
  bool okay = query.exec(QStringLiteral("UPDATE ppsspp_games SET observed_at = 0"));
  for (const PspGameRecord& game : result.games) {
    query.prepare(QStringLiteral(
        "INSERT INTO ppsspp_games(game_id, name, path, disc_id, disc_version, region, cover_path, "
        "homebrew, flatpak, observed_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, strftime('%s', 'now')) "
        "ON CONFLICT(game_id) DO UPDATE SET name = excluded.name, path = excluded.path, disc_id = "
        "excluded.disc_id, disc_version = excluded.disc_version, region = excluded.region, "
        "cover_path = excluded.cover_path, homebrew = excluded.homebrew, flatpak = excluded.flatpak, "
        "observed_at = excluded.observed_at"));
    query.addBindValue(game.gameId);
    query.addBindValue(game.title);
    query.addBindValue(game.path);
    query.addBindValue(game.discId);
    query.addBindValue(game.discVersion);
    query.addBindValue(game.region);
    query.addBindValue(game.coverPath);
    query.addBindValue(game.homebrew);
    query.addBindValue(game.flatpak);
    okay = okay && query.exec();
  }
  query.prepare(QStringLiteral(
      "INSERT INTO source_state(source, last_scan, last_error, paths) VALUES('ppsspp', ?, ?, ?) "
      "ON CONFLICT(source) DO UPDATE SET last_scan = excluded.last_scan, last_error = "
      "excluded.last_error, paths = excluded.paths"));
  query.addBindValue(scanTimestamp);
  query.addBindValue(result.warnings.join(QLatin1Char('\n')));
  query.addBindValue(result.roots.isEmpty() ? QStringLiteral("")
                                            : result.roots.join(QLatin1Char('\n')));
  okay = okay && query.exec();
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    setStatus(QStringLiteral("Could not update PPSSPP games"), query.lastError().text());
    return;
  }
  loadDatabase();
  m_detectedPaths = result.roots;
  m_lastScan = scanTimestamp;
  setStatus(m_ppssppDetected ? QStringLiteral("Imported %1 PSP game(s)").arg(result.games.size())
                            : QStringLiteral("PPSSPP was not found"),
            result.warnings.join(QLatin1Char('\n')));
}

QVariant PpssppGameModel::valueForRole(const Game& game, int role) const {
  switch (role) {
  case GameRoles::Title:
    return game.psp.title;
  case GameRoles::Subtitle:
    return game.psp.region.isEmpty() ? QStringLiteral("PPSSPP")
                                     : QStringLiteral("PPSSPP · %1").arg(game.psp.region);
  case GameRoles::Description:
    return game.psp.homebrew ? QStringLiteral("PSP homebrew played through PPSSPP.")
                             : QStringLiteral("PlayStation Portable game played through PPSSPP.");
  case GameRoles::PlaytimeProvenance:
    return PlaySessionStore::provenance(m_playSessions, game.psp.path, -1);
  case GameRoles::PlaytimeSeconds:
    return PlaySessionStore::displayedSeconds(m_playSessions, game.psp.path, 0);
  case GameRoles::Hours:
    return static_cast<int>(PlaySessionStore::displayedSeconds(m_playSessions, game.psp.path, 0) /
                            3600);
  case GameRoles::Progress:
  case GameRoles::AchievementsUnlocked:
  case GameRoles::AchievementsTotal:
    return 0;
  case GameRoles::Favorite:
    return game.favorite;
  case GameRoles::Recent:
    return PlaySessionStore::displayedLastPlayed(m_playSessions, game.psp.path, 0) > 0;
  case GameRoles::LastPlayed:
    return PlaySessionStore::displayedLastPlayed(m_playSessions, game.psp.path, 0);
  case GameRoles::AccentStart:
    return game.accentStart;
  case GameRoles::AccentEnd:
    return game.accentEnd;
  case GameRoles::CoverMark:
    return game.psp.title.left(1).toUpper();
  case GameRoles::Year:
    return 0;
  case GameRoles::AppId:
    return game.psp.gameId;
  case GameRoles::CoverPath:
    return localUrl(game.psp.coverPath);
  case GameRoles::HeroPath:
  case GameRoles::LogoPath:
    return QString{};
  case GameRoles::InstallPath:
    return game.psp.path;
  case GameRoles::Source:
    return QStringLiteral("PPSSPP");
  case GameRoles::Runner:
    return QString{};
  case GameRoles::LaunchTarget:
    return game.psp.path;
  case GameRoles::Flatpak:
    return game.psp.flatpak;
  case GameRoles::Hidden:
    return game.hidden;
  case GameRoles::System:
    return QStringLiteral("psp");
  default:
    return {};
  }
}

void PpssppGameModel::setStatus(const QString& status, const QString& error) {
  m_statusText = status;
  m_errorText = error;
  emit statusChanged();
}
