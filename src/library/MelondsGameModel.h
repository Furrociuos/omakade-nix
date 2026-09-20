#pragma once

#include "sources/melonds/MelondsScanner.h"
#include "tracking/PlaySessionStore.h"

#include <QAbstractListModel>
#include <QColor>
#include <QFutureWatcher>
#include <QSqlDatabase>

// The DS games melonDS can play, cached beside the other sources so a scan that fails never empties
// the library, and so the user's own favourite and hidden choices survive a rescan.
//
// The games come from the folders the user keeps ROMs in rather than from melonDS, which keeps no
// library of its own, and each one is named from the ROM header melonDS itself reads.
class MelondsGameModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(bool melondsDetected READ melondsDetected NOTIFY statusChanged)
  Q_PROPERTY(bool scanning READ scanning NOTIFY statusChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)
  Q_PROPERTY(QStringList detectedPaths READ detectedPaths NOTIFY statusChanged)
  Q_PROPERTY(qint64 lastScan READ lastScan NOTIFY statusChanged)

public:
  explicit MelondsGameModel(const QString& omakadeDatabasePath,
                            PlaySessionStore* playSessions = nullptr, QObject* parent = nullptr);
  ~MelondsGameModel() override;

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
  [[nodiscard]] bool melondsDetected() const;
  [[nodiscard]] bool scanning() const { return m_scanning; }
  [[nodiscard]] QString statusText() const;
  [[nodiscard]] QString errorText() const;
  [[nodiscard]] QStringList detectedPaths() const;
  [[nodiscard]] qint64 lastScan() const;

  Q_INVOKABLE void toggleFavorite(int row);
  Q_INVOKABLE void toggleHidden(int row);
  Q_INVOKABLE void refresh();
  // The ROM folders the user configured, in the encoded form the settings store them in. Only the
  // folders that hold DS games are walked, so a folder for another console is not rescanned here.
  void setConfiguredRomFolders(const QStringList& encoded);
  // Scans an explicit list of folders, which is what the tests and an already decoded source use.
  void refreshFromFolders(const QStringList& folders);
  [[nodiscard]] static QStringList dsFolders(const QStringList& encoded);

signals:
  void statusChanged();

private:
  struct Game {
    MelondsGameRecord melonds;
    bool favorite = false;
    bool hidden = false;
    QColor accentStart;
    QColor accentEnd;
  };

  bool openDatabase(const QString& path);
  bool ensureSchema();
  void loadDatabase();
  void loadSourceState();
  void applyScan(const MelondsScanResult& result);
  [[nodiscard]] QVariant valueForRole(const Game& game, int role) const;
  void setStatus(const QString& status, const QString& error = {});

  QVector<Game> m_games;
  QSqlDatabase m_database;
  QString m_connectionName;
  PlaySessionStore* m_playSessions = nullptr;
  QFutureWatcher<MelondsScanResult> m_scanWatcher;
  bool m_scanning = false;
  bool m_melondsDetected = false;
  QString m_statusText;
  QString m_errorText;
  QStringList m_detectedPaths;
  QStringList m_configuredFolders;
  qint64 m_lastScan = 0;
};
