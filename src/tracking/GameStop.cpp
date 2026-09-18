#include "tracking/GameStop.h"

#include <QDir>
#include <QSet>

namespace {

// Processes that are never a game: the recorder, the launcher's own window, the
// session's shells. A guard list is a backstop, not the attribution mechanism,
// so it stays short and name-exact.
const QStringList& defaultProtectedBinaries() {
  static const QStringList names = {
      QStringLiteral("omakade"),     QStringLiteral("omakade-sessiond"),
      QStringLiteral("systemd"),     QStringLiteral("init"),
      QStringLiteral("dbus-daemon"), QStringLiteral("sh"),
      QStringLiteral("bash"),        QStringLiteral("zsh"),
      QStringLiteral("fish"),        QStringLiteral("dash"),
  };
  return names;
}

// Windows Path form is how a Wine process reports the game it runs, and Proton
// passes the executable as Z:\home\... . Dropping the drive prefix lets those
// compare against the unix install path the library holds.
QString normalizedPath(const QString& value) {
  QString text = value;
  text.replace(QLatin1Char('\\'), QLatin1Char('/'));
  if (text.size() > 2 && text.at(1) == QLatin1Char(':') && text.at(2) == QLatin1Char('/')) {
    text = text.mid(2);
  }
  return QDir::cleanPath(text);
}

// A root this broad is never one game's install folder or Wine prefix, and
// treating it as one would attribute the whole session to the game.
bool rootIsTooBroad(const QString& root) {
  const QString cleaned = normalizedPath(root);
  static const QStringList neverGameRoots = {
      QStringLiteral("/"),     QStringLiteral("/home"), QStringLiteral("/root"),
      QStringLiteral("/usr"),  QStringLiteral("/var"),  QStringLiteral("/tmp"),
      QStringLiteral("/run"),  QStringLiteral("/boot"), QStringLiteral("/etc"),
      QStringLiteral("/opt"),  QStringLiteral("/dev"),  QStringLiteral("/proc"),
      QStringLiteral("/sys"),
  };
  if (neverGameRoots.contains(cleaned) || cleaned == QDir::homePath()) {
    return true;
  }
  // Directories that hold shared programs rather than one game. Storing one of
  // these as a game's folder is real: a Heroic sideload with no folder_name
  // falls back to the directory of its executable, which is /usr/bin or
  // ~/.local/bin when the entry points at a launcher. Attributing everything
  // running from there would close the session, not the game. A game genuinely
  // installed in a directory called bin is not attributed, which is the safe
  // direction to be wrong in.
  static const QStringList sharedDirectories = {
      QStringLiteral("bin"),     QStringLiteral("sbin"), QStringLiteral("lib"),
      QStringLiteral("lib64"),   QStringLiteral("libexec"), QStringLiteral("include"),
      QStringLiteral("share"),   QStringLiteral("local/bin"), QStringLiteral("local/lib"),
  };
  const QString name = QFileInfo(cleaned).fileName();
  if (sharedDirectories.contains(name)) {
    return true;
  }
  return cleaned.isEmpty();
}

// One game's install folder is a folder. A path that names a program is the
// executable of a game rather than where it lives, which is what a manual entry
// stores: refusing it keeps "everything running from this file's directory" out
// of the plan. A folder that does not exist is left alone here, because the
// library holds paths for games that live on an unmounted disk.
bool rootNamesAProgram(const QString& root) {
  const QString cleaned = normalizedPath(root);
  return !cleaned.isEmpty() && QFileInfo(cleaned).isFile();
}

// True when candidate is root itself or sits under it. Case-sensitive, because
// this is a Linux path check.
bool pathInside(const QString& candidate, const QString& root) {
  if (candidate.isEmpty() || root.isEmpty()) {
    return false;
  }
  const QString path = normalizedPath(candidate);
  QString clean = normalizedPath(root);
  if (clean.isEmpty() || clean == QLatin1String("/")) {
    return false;
  }
  if (path == clean) {
    return true;
  }
  if (!clean.endsWith(QLatin1Char('/'))) {
    clean += QLatin1Char('/');
  }
  return path.startsWith(clean);
}

// A process counts as inside a root when its own executable or any argument it
// was started with sits under it.
bool processInside(const ProcessSnapshot& process, const QString& root) {
  if (pathInside(process.exePath, root)) {
    return true;
  }
  for (const QString& argument : process.arguments) {
    if (pathInside(argument, root)) {
      return true;
    }
  }
  return false;
}

bool pathMatchesAny(const QString& path, const QStringList& candidates) {
  const QString target = normalizedPath(path);
  for (const QString& candidate : candidates) {
    if (!candidate.isEmpty() && normalizedPath(candidate) == target) {
      return true;
    }
  }
  return false;
}

QString processKey(qint64 pid, qint64 procStart) {
  return QStringLiteral("%1:%2").arg(pid).arg(procStart);
}

QString processLabel(const ProcessSnapshot& process) {
  const QString name = process.comm.isEmpty() ? QStringLiteral("process") : process.comm;
  return QStringLiteral("%1 (pid %2)").arg(name).arg(process.pid);
}

} // namespace

namespace GameStop {

int Plan::processTargets() const {
  int count = 0;
  for (const Target& target : targets) {
    if (target.kind == TargetKind::TerminateProcess) {
      ++count;
    }
  }
  return count;
}

int Plan::scopeTargets() const {
  return static_cast<int>(targets.size()) - processTargets();
}

Guards defaultGuards() {
  Guards guards;
  guards.protectedBinaries = defaultProtectedBinaries();
  return guards;
}

Plan plan(const GameIdentity& game, const QVector<ProcessSnapshot>& processes,
          const ProcessProfileSet& profiles, const Guards& guards) {
  Plan result;

  QVector<qint64> protectedPids = guards.protectedPids;
  if (!protectedPids.contains(1)) {
    protectedPids.append(1);
  }
  QStringList protectedBinaries = guards.protectedBinaries;
  for (const QString& name : defaultProtectedBinaries()) {
    if (!protectedBinaries.contains(name, Qt::CaseInsensitive)) {
      protectedBinaries.append(name);
    }
  }
  const auto isProtected = [&protectedPids, &protectedBinaries](const ProcessSnapshot& process) {
    if (protectedPids.contains(process.pid)) {
      return true;
    }
    for (const QString& name : protectedBinaries) {
      if (process.comm.compare(name, Qt::CaseInsensitive) == 0) {
        return true;
      }
    }
    return false;
  };
  const auto skipProcess = [&result](const ProcessSnapshot& process, SkipReason reason,
                                     const QString& detail) {
    Skipped skipped;
    skipped.pid = process.pid;
    skipped.procStart = process.procStart;
    skipped.comm = process.comm;
    skipped.reason = reason;
    skipped.detail = detail;
    result.skipped.append(skipped);
  };

  QSet<QString> claimed;
  const auto addProcessTarget = [&result, &claimed](const ProcessSnapshot& process,
                                                    Confidence confidence, const QString& reason) {
    const QString key = processKey(process.pid, process.procStart);
    if (claimed.contains(key)) {
      return;
    }
    claimed.insert(key);
    Target target;
    target.kind = TargetKind::TerminateProcess;
    target.confidence = confidence;
    target.pid = process.pid;
    target.procStart = process.procStart;
    target.comm = process.comm;
    target.label = processLabel(process);
    target.reason = reason;
    result.targets.append(target);
  };

  const auto processByPid = [&processes](qint64 pid) -> const ProcessSnapshot* {
    for (const ProcessSnapshot& process : processes) {
      if (process.pid == pid) {
        return &process;
      }
    }
    return nullptr;
  };

  // 1. What Omakade started itself. The start time is the whole safety story: a
  //    pid alone can have been reused, and then it names somebody else.
  for (const OwnedProcess& owned : game.ownedProcesses) {
    if (owned.pid <= 0) {
      continue;
    }
    const ProcessSnapshot* live = nullptr;
    const ProcessSnapshot* reused = nullptr;
    for (const ProcessSnapshot& process : processes) {
      if (process.pid != owned.pid) {
        continue;
      }
      if (process.procStart == owned.procStart) {
        live = &process;
        break;
      }
      reused = &process;
    }
    if (live == nullptr) {
      if (reused != nullptr) {
        skipProcess(*reused, SkipReason::RecycledPid,
                    QStringLiteral("pid %1 is running as %2 with start time %3, not the process "
                                   "Omakade started at %4.")
                        .arg(owned.pid)
                        .arg(reused->comm)
                        .arg(reused->procStart)
                        .arg(owned.procStart));
      }
      continue;
    }
    if (isProtected(*live)) {
      skipProcess(*live, SkipReason::Protected,
                  QStringLiteral("on the never-signal list, so a tracked pid pointing at it is "
                                 "refused"));
      continue;
    }
    addProcessTarget(*live, Confidence::Exact,
                     QStringLiteral("Omakade started this process and it is still running."));
  }

  // 2. Wine prefixes. The prefix is identity we already hold, so it needs no
  //    matching, and it takes the whole process tree the reported complaint is
  //    about. A prefix with nothing running is a no-op when it is applied.
  QStringList prefixes;
  for (const QString& prefix : game.winePrefixes) {
    const QString cleaned = QDir::cleanPath(prefix);
    if (cleaned.isEmpty() || cleaned == QStringLiteral(".") || prefixes.contains(cleaned)) {
      continue;
    }
    if (!cleaned.startsWith(QLatin1Char('/'))) {
      // A prefix reaches wineserver as an environment value, and a relative one
      // would be resolved against whatever directory Omakade happens to be in.
      // A literal "~" is a relative path too, since nothing here expands it.
      result.notes.append(QStringLiteral("%1 is not an absolute path, so it is not used as a Wine "
                                         "prefix.").arg(prefix));
      continue;
    }
    if (rootIsTooBroad(cleaned)) {
      result.notes.append(QStringLiteral("%1 is not a Wine prefix one game owns, so it is left "
                                         "alone.").arg(cleaned));
      continue;
    }
    prefixes.append(cleaned);
  }
  for (const QString& prefix : prefixes) {
    Target target;
    target.kind = TargetKind::WinePrefix;
    target.confidence = Confidence::Exact;
    target.prefix = prefix;
    target.label = QStringLiteral("Wine prefix %1").arg(prefix);
    target.reason = QStringLiteral("Every process in this Wine prefix closes, including any "
                                   "launcher that shares it.");
    result.targets.append(target);
  }

  // 3. Flatpak. The app id is the scope, and flatpak kill scopes itself.
  if (game.flatpak && !game.flatpakAppId.isEmpty()) {
    Target target;
    target.kind = TargetKind::FlatpakApp;
    target.confidence = Confidence::Exact;
    target.appId = game.flatpakAppId;
    target.label = QStringLiteral("flatpak app %1").arg(game.flatpakAppId);
    target.reason = QStringLiteral("Every process in the app's sandbox closes, including anything "
                                   "else running inside it.");
    result.targets.append(target);
  }

  // 4. Emulator sessions: the profile data that already drives playtime
  //    tracking names the process, and the content path names the game.
  if (!game.emulator.isEmpty() || !game.gamePaths.isEmpty()) {
    if (game.gamePaths.isEmpty()) {
      result.notes.append(QStringLiteral("This game has no recorded content path, so an emulator "
                                         "process cannot be claimed for it."));
    } else {
      for (const SessionMatch& match : ProcessMatcher::match(processes, profiles)) {
        if (!pathMatchesAny(match.gamePath, game.gamePaths)) {
          continue;
        }
        const ProcessSnapshot* process = processByPid(match.pid);
        if (process == nullptr || process->procStart != match.procStart) {
          continue;
        }
        if (isProtected(*process)) {
          skipProcess(*process, SkipReason::Protected,
                      QStringLiteral("matched this game's content but is on the never-signal list"));
          continue;
        }
        addProcessTarget(*process, Confidence::Exact,
                         QStringLiteral("Running this game's content (%1).").arg(match.gamePath));
      }
    }
  }

  // 5. Anything running from the game's install folder. This is the rung that
  //    spans a launcher handoff, where Omakade owns no pid of its own.
  if (!game.installPath.isEmpty() && rootIsTooBroad(game.installPath)) {
    result.notes.append(QStringLiteral("This game's install folder is %1, a shared system folder, "
                                       "so no process is attributed to it.").arg(game.installPath));
  } else if (!game.installPath.isEmpty() && rootNamesAProgram(game.installPath)) {
    result.notes.append(QStringLiteral("This game's install path is %1, the program itself rather "
                                       "than a folder, so no process is attributed to it.")
                            .arg(game.installPath));
  } else if (!game.installPath.isEmpty()) {
    for (const ProcessSnapshot& process : processes) {
      if (!processInside(process, game.installPath)) {
        continue;
      }
      if (isProtected(process)) {
        skipProcess(process, SkipReason::Protected,
                    QStringLiteral("runs from the game's install folder but is on the "
                                   "never-signal list"));
        continue;
      }
      bool covered = false;
      for (const QString& prefix : prefixes) {
        if (processInside(process, prefix)) {
          covered = true;
          break;
        }
      }
      if (covered) {
        skipProcess(process, SkipReason::CoveredByPrefix,
                    QStringLiteral("already closed by the Wine prefix target in this plan"));
        continue;
      }
      addProcessTarget(process, Confidence::Good,
                       QStringLiteral("Running from the game's install folder (%1).").arg(game.installPath));
    }
  }

  if (result.targets.isEmpty() && result.skipped.isEmpty()) {
    result.notes.append(QStringLiteral("Nothing attributable to this game is running."));
  }
  if (!result.targets.isEmpty()) {
    result.notes.append(QStringLiteral("Processes started outside this game's prefix and install "
                                       "folder, anti-cheat services in particular, cannot be "
                                       "attributed and are left alone."));
  }
  return result;
}

QString describe(const Target& target) {
  switch (target.kind) {
  case TargetKind::WinePrefix:
    return QStringLiteral("Close every process in the Wine prefix %1").arg(target.prefix);
  case TargetKind::FlatpakApp:
    return QStringLiteral("Close the flatpak app %1").arg(target.appId);
  case TargetKind::TerminateProcess:
    break;
  }
  return QStringLiteral("Close %1").arg(target.label);
}

QStringList describe(const Plan& plan) {
  QStringList lines;
  lines.reserve(plan.targets.size());
  for (const Target& target : plan.targets) {
    lines.append(describe(target));
  }
  return lines;
}

QString skipReasonText(SkipReason reason) {
  switch (reason) {
  case SkipReason::Protected:
    return QStringLiteral("never signalled");
  case SkipReason::RecycledPid:
    return QStringLiteral("pid was reused by another process");
  case SkipReason::CoveredByPrefix:
    return QStringLiteral("already closed by the prefix");
  case SkipReason::MissingScope:
    break;
  }
  return QStringLiteral("outside this game's attributed scope");
}

} // namespace GameStop
