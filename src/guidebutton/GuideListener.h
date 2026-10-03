#pragma once

#include "guidebutton/GuidePress.h"

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <sys/types.h>

class QSocketNotifier;

// Watches every controller's Guide or Home button through evdev and reports presses. It only
// reads: no device is grabbed, written to, remapped or created, so games, Steam Input and
// other readers see exactly what they did before. Only devices sysfs describes as controllers
// are opened, and each reader asks the kernel for button and d-pad events alone, so stick and
// trigger motion never wakes this process.
class GuideListener final : public QObject {
  Q_OBJECT

public:
  struct Controller {
    QString node;
    QString name;
    // Made by software, such as Steam Input's pad, rather than a driver for hardware.
    bool virtualDevice = false;
  };

  explicit GuideListener(QString devDir = QStringLiteral("/dev/input"),
                         QString sysDir = QStringLiteral("/sys/class/input"),
                         QObject* parent = nullptr);
  ~GuideListener() override;

  void start();
  // The controllers sysfs lists now, opened or not.
  [[nodiscard]] static QList<Controller> scan(const QString& devDir, const QString& sysDir);
  [[nodiscard]] QStringList openNodes() const;

signals:
  void pressed(const QString& node, const QString& name);
  void devicesChanged();

private:
  struct Device {
    int fd = -1;
    dev_t rdev = 0;
    ino_t inode = 0;
    QString name;
    QSocketNotifier* notifier = nullptr;
    bool dropping = false;
  };

  void rescan();
  // Zero once the controller is open, otherwise the errno that refused it.
  int open(const Controller& controller);
  void close(const QString& node);
  void read(const QString& node);
  [[nodiscard]] bool current(const QString& node, const Device& device) const;

  QString m_devDir;
  QString m_sysDir;
  QHash<QString, Device> m_devices;
  // Controllers that could not be opened yet, with the attempts left. udev grants access a
  // moment after a node appears, so a refusal right after a hotplug is retried briefly.
  QHash<QString, int> m_waiting;
  QSet<QString> m_reported;
  QFileSystemWatcher m_watcher;
  QTimer m_retry;
  QElapsedTimer m_clock;
  GuidePress m_press;
};
