#include "library/HomeModel.h"
#include "library/GameRoles.h"
#include "library/UnifiedGameModel.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QTimer>
#include <QUuid>
#include <algorithm>

QString HomeModel::keyFor(const QVariantMap& game) {
  return QString::fromUtf8(
      QJsonDocument(QJsonArray{game.value("source").toString(), game.value("runner").toString(),
                               game.value("appId").toString()})
          .toJson(QJsonDocument::Compact));
}
HomeModel::HomeModel(UnifiedGameModel* games, const QString& path, QObject* parent)
    : QObject(parent), m_games(games), m_connection(QUuid::createUuid().toString()) {
  m_database = QSqlDatabase::addDatabase("QSQLITE", m_connection);
  m_database.setDatabaseName(path.isEmpty() ? ":memory:" : path);
  if (!m_database.open())
    m_error = "Could not open Up next storage.";
  else {
    QSqlQuery query(m_database);
    if (!query.exec(
            "CREATE TABLE IF NOT EXISTS play_queue (source TEXT NOT NULL, runner TEXT NOT NULL, "
            "app_id TEXT NOT NULL, title TEXT NOT NULL, position INTEGER NOT NULL, "
            "PRIMARY KEY(source,runner,app_id))"))
      m_error = "Could not prepare Up next storage.";
  }
  connect(games, &QAbstractItemModel::modelReset, this, &HomeModel::scheduleRefresh);
  connect(games, &QAbstractItemModel::rowsInserted, this, &HomeModel::scheduleRefresh);
  connect(games, &QAbstractItemModel::rowsRemoved, this, &HomeModel::scheduleRefresh);
  connect(games, &QAbstractItemModel::dataChanged, this, &HomeModel::scheduleRefresh);
}
HomeModel::~HomeModel() {
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connection);
}
void HomeModel::scheduleRefresh() {
  if (!m_active || m_refreshPending)
    return;
  m_refreshPending = true;
  QTimer::singleShot(0, this, [this] {
    m_refreshPending = false;
    refresh();
  });
}
QHash<QString, QVariantMap> HomeModel::gamesByIdentity() const {
  QHash<QString, QVariantMap> result;
  for (int row = 0; row < m_games->rowCount(); ++row) {
    const auto index = m_games->index(row);
    if (index.data(GameRoles::IsPortal).toBool())
      continue;
    QVariantMap game;
    const auto roles = m_games->roleNames();
    for (auto role = roles.begin(); role != roles.end(); ++role)
      game.insert(QString::fromUtf8(role.value()), index.data(role.key()));
    game["available"] = m_games->preferredInstallation(row).value("launchAvailable").toBool();
    game["identity"] = keyFor(game);
    for (const auto& member : m_games->installations(row))
      result.insert(keyFor(member.toMap()), game);
    result.insert(keyFor(game), game);
  }
  return result;
}
QVariantList HomeModel::stored(bool* okay) const {
  QVariantList rows;
  if (okay)
    *okay = false;
  QSqlQuery query(m_database);
  if (!query.exec("SELECT source,runner,app_id,title FROM play_queue ORDER BY "
                  "position,source,runner,app_id"))
    return rows;
  if (okay)
    *okay = true;
  while (query.next())
    rows.append(QVariantMap{{"source", query.value(0)},
                            {"runner", query.value(1)},
                            {"appId", query.value(2)},
                            {"title", query.value(3)}});
  return rows;
}
void HomeModel::refresh() {
  bool readOkay = false;
  const auto saved = stored(&readOkay);
  if (!readOkay) {
    m_error = "Could not read Up next. Your queue has not been changed.";
    emit changed();
    return;
  }
  const auto games = gamesByIdentity();
  QSet<QString> seen;
  QVariantList recent, queue;
  for (const auto& game : games) {
    const auto id = game.value("identity").toString();
    if (seen.contains(id) || game.value("hidden").toBool() || !game.value("available").toBool() ||
        game.value("lastPlayed").toLongLong() <= 0)
      continue;
    seen.insert(id);
    recent.append(game);
  }
  std::sort(recent.begin(), recent.end(), [](const QVariant& a, const QVariant& b) {
    const auto x = a.toMap(), y = b.toMap();
    const auto xt = x.value("lastPlayed").toLongLong(), yt = y.value("lastPlayed").toLongLong();
    return xt != yt ? xt > yt : x.value("identity").toString() < y.value("identity").toString();
  });
  while (recent.size() > 8)
    recent.removeLast();
  QHash<QString, int> positions;
  for (const auto& value : saved) {
    const auto original = value.toMap();
    const auto key = keyFor(original);
    auto game = games.value(key, original);
    if (game.value("hidden").toBool())
      continue;
    const auto group = game.value("identity", key).toString();
    if (positions.contains(group)) {
      auto existing = queue.at(positions[group]).toMap();
      auto keys = existing.value("queueKeys").toStringList();
      keys.append(key);
      existing["queueKeys"] = keys;
      queue[positions[group]] = existing;
      continue;
    }
    game["queueKey"] = key;
    game["queueKeys"] = QStringList{key};
    game["available"] = game.value("available").toBool();
    positions.insert(group, queue.size());
    queue.append(game);
  }
  if (m_recent != recent || m_queue != queue) {
    m_recent = recent;
    m_queue = queue;
    emit changed();
  }
}
bool HomeModel::write(const QVariantList& rows) {
  if (!m_database.transaction()) {
    m_error = "Could not save Up next. Check available storage.";
    emit changed();
    return false;
  }
  QSqlQuery query(m_database);
  bool okay = query.exec("DELETE FROM play_queue");
  for (int i = 0; okay && i < rows.size(); ++i) {
    const auto row = rows[i].toMap();
    query.prepare("INSERT INTO play_queue(source,runner,app_id,title,position) VALUES(?,?,?,?,?)");
    query.addBindValue(row.value("source"));
    query.addBindValue(row.value("runner").toString());
    query.addBindValue(row.value("appId"));
    query.addBindValue(row.value("title"));
    query.addBindValue(i);
    okay = query.exec();
  }
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    m_error = "Could not save Up next. Your queue was kept.";
    emit changed();
    return false;
  }
  const bool hadError = !m_error.isEmpty();
  m_error.clear();
  refresh();
  if (hadError)
    emit changed();
  return true;
}
bool HomeModel::enqueue(const QString& source, const QString& runner, const QString& appId) {
  const auto key = keyFor({{"source", source}, {"runner", runner}, {"appId", appId}});
  const auto games = gamesByIdentity();
  if (!games.contains(key) || games[key].value("hidden").toBool())
    return false;
  const auto identity = games[key].value("identity").toString();
  bool readOkay = false;
  auto rows = stored(&readOkay);
  if (!readOkay) {
    m_error = "Could not read Up next. Your queue has not been changed.";
    emit changed();
    return false;
  }
  for (const auto& row : rows) {
    const auto existing = keyFor(row.toMap());
    if (existing == key || games.value(existing).value("identity").toString() == identity)
      return true;
  }
  if (rows.size() >= 100) {
    m_error = "Up next holds up to 100 games.";
    emit changed();
    return false;
  }
  rows.append(QVariantMap{{"source", source},
                          {"runner", runner},
                          {"appId", appId},
                          {"title", games[key].value("title")}});
  return write(rows);
}
bool HomeModel::remove(const QString& key) {
  refresh();
  QStringList keys;
  for (const auto& item : m_queue)
    if (item.toMap().value("queueKey").toString() == key)
      keys = item.toMap().value("queueKeys").toStringList();
  if (keys.isEmpty())
    return false;
  bool readOkay = false;
  auto rows = stored(&readOkay);
  if (!readOkay) {
    m_error = "Could not read Up next. Your queue has not been changed.";
    emit changed();
    return false;
  }
  for (int i = rows.size() - 1; i >= 0; --i)
    if (keys.contains(keyFor(rows[i].toMap())))
      rows.removeAt(i);
  return write(rows);
}
bool HomeModel::move(const QString& key, int direction) {
  if (direction != -1 && direction != 1)
    return false;
  refresh();
  int index = -1;
  for (int i = 0; i < m_queue.size(); ++i)
    if (m_queue[i].toMap().value("queueKey").toString() == key)
      index = i;
  if (index < 0 || index + direction < 0 || index + direction >= m_queue.size())
    return false;
  auto groups = m_queue;
  groups.swapItemsAt(index, index + direction);
  bool readOkay = false;
  const auto original = stored(&readOkay);
  if (!readOkay) {
    m_error = "Could not read Up next. Your queue has not been changed.";
    emit changed();
    return false;
  }
  QHash<QString, QVariant> byKey;
  for (const auto& row : original)
    byKey.insert(keyFor(row.toMap()), row);
  QVariantList ordered;
  QSet<QString> used;
  for (const auto& group : groups)
    for (const auto& id : group.toMap().value("queueKeys").toStringList()) {
      ordered.append(byKey.value(id));
      used.insert(id);
    }
  // Hidden entries remain saved and keep their relative order.
  for (const auto& row : original)
    if (!used.contains(keyFor(row.toMap())))
      ordered.append(row);
  return write(ordered);
}
