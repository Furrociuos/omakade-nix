#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// A point-in-time snapshot of one process from procfs, enough to decide whether
// it is an emulator and which game it was started with.
struct ProcessSnapshot {
  qint64 pid = 0;
  // procfs stat field 22, the value GameLauncher also uses to tell a reused pid
  // apart from the process it was tracking.
  qint64 procStart = -1;
  QString comm;
  QStringList arguments;
  // The executable behind /proc/<pid>/exe, resolved. Empty when the link cannot
  // be read. A Wine process reports the wine loader here, not the Windows
  // executable it runs, so this catches native games and launchers.
  QString exePath;
};

namespace ProcFs {

// Lists user-space processes. Kernel threads have an empty cmdline and are skipped.
[[nodiscard]] QVector<ProcessSnapshot> listProcesses();

[[nodiscard]] bool processAlive(qint64 pid, qint64 procStart);

} // namespace ProcFs
