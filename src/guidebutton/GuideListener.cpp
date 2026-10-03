#include "guidebutton/GuideListener.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSocketNotifier>

#include <cerrno>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <utility>

namespace {
constexpr int kRetryIntervalMs = 250;
constexpr int kRetryAttempts = 20;

QString readLine(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QString::fromUtf8(file.readLine()).trimmed();
}

// Asks the kernel to deliver this reader key and d-pad hat events only. The mask is per open
// file, so other readers of the device are unaffected. Empty reports are not delivered either,
// which leaves stick and trigger motion without any wakeup here. Kernels before 4.4 refuse the
// request and deliver everything, which the reader handles the same way.
void restrictEvents(int fd) {
  constexpr size_t kLongBits = sizeof(unsigned long) * 8;
  std::array<unsigned long, (EV_CNT + kLongBits - 1) / kLongBits> types{};
  for (const int type : {EV_KEY, EV_ABS}) {
    types[type / kLongBits] |= 1UL << (type % kLongBits);
  }
  std::array<unsigned long, (ABS_CNT + kLongBits - 1) / kLongBits> axes{};
  for (int axis = ABS_HAT0X; axis <= ABS_HAT3Y; ++axis) {
    axes[axis / kLongBits] |= 1UL << (axis % kLongBits);
  }
  input_mask typeMask{.type = 0,
                      .codes_size = sizeof(types),
                      .codes_ptr = reinterpret_cast<quintptr>(types.data())};
  input_mask axisMask{.type = EV_ABS,
                      .codes_size = sizeof(axes),
                      .codes_ptr = reinterpret_cast<quintptr>(axes.data())};
  ::ioctl(fd, EVIOCSMASK, &axisMask);
  ::ioctl(fd, EVIOCSMASK, &typeMask);
}
} // namespace

GuideListener::GuideListener(QString devDir, QString sysDir, QObject* parent)
    : QObject(parent), m_devDir(std::move(devDir)), m_sysDir(std::move(sysDir)) {
  m_clock.start();
  m_retry.setInterval(kRetryIntervalMs);
  connect(&m_retry, &QTimer::timeout, this, &GuideListener::rescan);
  connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &GuideListener::rescan);
}

GuideListener::~GuideListener() {
  const QStringList nodes = m_devices.keys();
  for (const QString& node : nodes) {
    close(node);
  }
}

void GuideListener::start() {
  m_watcher.addPath(m_devDir);
  rescan();
}

QStringList GuideListener::openNodes() const { return m_devices.keys(); }

QList<GuideListener::Controller> GuideListener::scan(const QString& devDir,
                                                     const QString& sysDir) {
  QList<Controller> controllers;
  const QStringList nodes = QDir(devDir).entryList({QStringLiteral("event*")}, QDir::System);
  for (const QString& node : nodes) {
    const QString device = sysDir + QLatin1Char('/') + node + QStringLiteral("/device");
    if (!GuidePress::isController(readLine(device + QStringLiteral("/capabilities/key")))) {
      continue;
    }
    controllers.append(Controller{
        .node = node,
        .name = readLine(device + QStringLiteral("/name")),
        .virtualDevice = QFileInfo(device).canonicalFilePath().contains(
            QStringLiteral("/devices/virtual/")),
    });
  }
  return controllers;
}

void GuideListener::rescan() {
  const QList<Controller> controllers = scan(m_devDir, m_sysDir);
  QSet<QString> present;
  for (const Controller& controller : controllers) {
    present.insert(controller.node);
  }
  bool changed = false;
  const QStringList openNodes = m_devices.keys();
  for (const QString& node : openNodes) {
    // A node that is gone, or that now belongs to a different device, is closed here even when
    // its read side has not reported the removal yet.
    if (!present.contains(node) || !current(node, m_devices.value(node))) {
      close(node);
      changed = true;
    }
  }
  for (auto waiting = m_waiting.begin(); waiting != m_waiting.end();) {
    waiting = present.contains(waiting.key()) ? std::next(waiting) : m_waiting.erase(waiting);
  }
  for (auto reported = m_reported.begin(); reported != m_reported.end();) {
    reported = present.contains(*reported) ? std::next(reported) : m_reported.erase(reported);
  }
  for (const Controller& controller : controllers) {
    if (m_devices.contains(controller.node) || m_reported.contains(controller.node)) {
      continue;
    }
    const int error = open(controller);
    if (error == 0) {
      m_waiting.remove(controller.node);
      changed = true;
      continue;
    }
    const int left = m_waiting.value(controller.node, kRetryAttempts) - 1;
    if (left > 0) {
      m_waiting.insert(controller.node, left);
      continue;
    }
    m_waiting.remove(controller.node);
    m_reported.insert(controller.node);
    qWarning().noquote() << QStringLiteral("Cannot read %1 (%2): %3")
                                .arg(controller.node, controller.name,
                                     QString::fromLocal8Bit(strerror(error)));
  }
  if (m_waiting.isEmpty()) {
    m_retry.stop();
  } else if (!m_retry.isActive()) {
    m_retry.start();
  }
  if (changed) {
    emit devicesChanged();
  }
}

bool GuideListener::current(const QString& node, const Device& device) const {
  struct stat path {};
  return ::stat(QFile::encodeName(m_devDir + QLatin1Char('/') + node).constData(), &path) == 0 &&
         path.st_rdev == device.rdev && path.st_ino == device.inode;
}

int GuideListener::open(const Controller& controller) {
  const QByteArray path = QFile::encodeName(m_devDir + QLatin1Char('/') + controller.node);
  const int fd = ::open(path.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    return errno;
  }
  struct stat opened {};
  if (::fstat(fd, &opened) != 0) {
    const int error = errno;
    ::close(fd);
    return error;
  }
  restrictEvents(fd);
  Device device{
      .fd = fd, .rdev = opened.st_rdev, .inode = opened.st_ino, .name = controller.name};
  device.notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
  connect(device.notifier, &QSocketNotifier::activated, this,
          [this, node = controller.node] { read(node); });
  m_devices.insert(controller.node, device);
  m_press.opened(controller.node, m_clock.elapsed());
  qInfo().noquote() << QStringLiteral("Watching %1 (%2%3)")
                           .arg(controller.node, controller.name,
                                controller.virtualDevice ? QStringLiteral(", virtual")
                                                         : QString{});
  return 0;
}

void GuideListener::close(const QString& node) {
  Device device = m_devices.take(node);
  if (device.fd < 0) {
    return;
  }
  device.notifier->setEnabled(false);
  device.notifier->deleteLater();
  ::close(device.fd);
  m_press.closed(node);
  qInfo().noquote() << QStringLiteral("Stopped watching %1 (%2)").arg(node, device.name);
}

void GuideListener::read(const QString& node) {
  auto device = m_devices.find(node);
  if (device == m_devices.end()) {
    return;
  }
  // Reported once the device's queue is drained, so nothing a receiver does can change the
  // device table while this loop walks it.
  bool fired = false;
  const QString name = device->name;
  std::array<input_event, 64> events{};
  for (;;) {
    const ssize_t bytes = ::read(device->fd, events.data(), sizeof(events));
    if (bytes < 0 && errno == EINTR) {
      continue;
    }
    if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      break;
    }
    if (bytes <= 0) {
      // ENODEV when the controller disconnects. A read of nothing is end of file, which a
      // device never reports, so it is treated the same way.
      close(node);
      emit devicesChanged();
      break;
    }
    const qint64 now = m_clock.elapsed();
    const auto count = static_cast<size_t>(bytes) / sizeof(input_event);
    for (size_t index = 0; index < count; ++index) {
      const input_event& event = events[index];
      if (device->dropping) {
        device->dropping = !(event.type == EV_SYN && event.code == SYN_REPORT);
        continue;
      }
      if (event.type == EV_SYN && event.code == SYN_DROPPED) {
        device->dropping = true;
        m_press.dropped(node);
        continue;
      }
      fired = m_press.event(node, event.type, event.code, event.value, now) || fired;
    }
  }
  if (fired) {
    emit pressed(node, name);
  }
}
