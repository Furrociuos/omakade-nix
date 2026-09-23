#pragma once

#include "sources/ppsspp/PpssppScanner.h"
#include "tracking/PlaySessionStore.h"

#include <QAbstractListModel>
#include <QColor>
#include <QFutureWatcher>
#include <QSqlDatabase>

// PSP games from PPSSPP's remembered paths and explicit PSP folders. The source keeps
// its own cache so a failed or unavailable root never empties the last good library.
class PpssppGameModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(bool ppssppDetected READ ppssppDetected NOTIFY statusChanged)
  Q_PROPERTY(bool scanning READ scanning NOTIFY statusChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)
  Q_PROPERTY(QStringList detectedPaths READ detectedPaths NOTIFY statusChanged)
  Q_PROPERTY(qint64 lastScan READ lastScan NOTIFY statusChanged)

public:
  explicit PpssppGameModel(const QString& omakadeDatabasePath,
                           PlaySessionStore* playSessions = nullptr, QObject* parent = nullptr);
  ~PpssppGameModel() override;

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
  [[nodiscard]] bool ppssppDetected() const;
  [[nodiscard]] bool scanning() const { return m_scanning; }
  [[nodiscard]] QString statusText() const;
  [[nodiscard]] QString errorText() const;
  [[nodiscard]] QStringList detectedPaths() const;
  [[nodiscard]] qint64 lastScan() const;

  Q_INVOKABLE void toggleFavorite(int row);
  Q_INVOKABLE void toggleHidden(int row);
  Q_INVOKABLE void refresh();
  void setConfiguredRomFolders(const QStringList& encoded);
  void refreshFromRoots(const QStringList& roots, const QStringList& userRoots = {});
  [[nodiscard]] static QStringList pspFolders(const QStringList& encoded);

signals:
  void statusChanged();

private:
  struct Game {
    PspGameRecord psp;
    bool favorite = false;
    bool hidden = false;
    QColor accentStart;
    QColor accentEnd;
  };

  bool openDatabase(const QString& path);
  bool ensureSchema();
  void loadDatabase();
  void loadSourceState();
  void applyScan(const PspScanResult& result);
  [[nodiscard]] QVariant valueForRole(const Game& game, int role) const;
  void setStatus(const QString& status, const QString& error = {});

  QVector<Game> m_games;
  QSqlDatabase m_database;
  QString m_connectionName;
  PlaySessionStore* m_playSessions = nullptr;
  QFutureWatcher<PspScanResult> m_scanWatcher;
  bool m_scanning = false;
  bool m_ppssppDetected = false;
  QString m_statusText;
  QString m_errorText;
  QStringList m_detectedPaths;
  QStringList m_configuredFolders;
  qint64 m_lastScan = 0;
};
