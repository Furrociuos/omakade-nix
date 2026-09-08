#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QVariantList>
class UnifiedGameModel;
class HomeModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool active READ active WRITE setActive)
  Q_PROPERTY(QVariantList recent READ recent NOTIFY changed)
  Q_PROPERTY(QVariantList queue READ queue NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
public:
  HomeModel(UnifiedGameModel* games, const QString& databasePath, QObject* parent = nullptr);
  ~HomeModel() override;
  bool active() const { return m_active; }
  void setActive(bool value) {
    m_active = value;
    if (value)
      refresh();
  }
  QVariantList recent() const { return m_recent; }
  QVariantList queue() const { return m_queue; }
  QString error() const { return m_error; }
  Q_INVOKABLE bool enqueue(const QString& source, const QString& runner, const QString& appId);
  Q_INVOKABLE bool remove(const QString& key);
  Q_INVOKABLE bool move(const QString& key, int direction);
  Q_INVOKABLE void refresh();

signals:
  void changed();

private:
  static QString keyFor(const QVariantMap& game);
  QHash<QString, QVariantMap> gamesByIdentity() const;
  QVariantList stored(bool* okay = nullptr) const;
  bool write(const QVariantList& rows);
  void scheduleRefresh();
  UnifiedGameModel* m_games;
  QSqlDatabase m_database;
  QString m_connection, m_error;
  QVariantList m_recent, m_queue;
  bool m_refreshPending = false;
  bool m_active = false;
};
