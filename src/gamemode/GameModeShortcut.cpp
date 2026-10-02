#include "gamemode/GameModeShortcut.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtConcurrentRun>

namespace {
constexpr int kCommandTimeoutMs = 2500;
// Hyprland's modifier bits for Super and Ctrl.
constexpr int kSuperCtrlMask = 64 | 4;
const QString kComment =
    QStringLiteral("-- Omakade Game Mode. Press it again to leave. Added by Omakade.");

bool hyprctl(const QStringList& arguments, QByteArray* output = nullptr) {
  const QString executable = QStandardPaths::findExecutable(QStringLiteral("hyprctl"));
  if (executable.isEmpty() || qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE").isEmpty()) {
    return false;
  }
  QProcess process;
  process.start(executable, arguments);
  if (!process.waitForStarted(kCommandTimeoutMs)) {
    return false;
  }
  if (!process.waitForFinished(kCommandTimeoutMs)) {
    process.kill();
    process.waitForFinished(kCommandTimeoutMs);
    return false;
  }
  if (output != nullptr) {
    *output = process.readAllStandardOutput();
  }
  return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

const QRegularExpression& bindingPattern() {
  // One uncommented o.bind or o.rebind line whose command runs Game Mode.
  static const QRegularExpression pattern(
      QStringLiteral(R"lua(^[ \t]*o\.(?:re)?bind\(\s*"([^"\n]+)"[^\n]*omakade --game-mode(?:-toggle)?"[^\n]*$)lua"),
      QRegularExpression::MultilineOption);
  return pattern;
}

bool readFile(const QString& path, QString* contents) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  *contents = QString::fromUtf8(file.readAll());
  return true;
}

bool writeFile(const QString& path, const QString& contents) {
  // A bindings file kept in a dotfiles repository is a symlink; write through it.
  const QString target = QFileInfo(path).canonicalFilePath();
  if (target.isEmpty()) {
    return false;
  }
  const QFile::Permissions permissions = QFile::permissions(target);
  QSaveFile file(target);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  file.write(contents.toUtf8());
  if (!file.commit()) {
    return false;
  }
  QFile::setPermissions(target, permissions);
  return true;
}
} // namespace

QString GameModeShortcut::defaultKey() { return QStringLiteral("SUPER + CTRL + G"); }

QString GameModeShortcut::command() { return QStringLiteral("omakade --game-mode-toggle"); }

QString GameModeShortcut::bindingLine() {
  return QStringLiteral("o.bind(\"%1\", \"Game Mode\", \"%2\")").arg(defaultKey(), command());
}

QString GameModeShortcut::boundKey(const QString& contents) {
  const QRegularExpressionMatch match = bindingPattern().match(contents);
  return match.hasMatch() ? match.captured(1).simplified() : QString{};
}

QString GameModeShortcut::withBinding(const QString& contents) {
  if (!boundKey(contents).isEmpty()) {
    return contents;
  }
  QString result = contents;
  if (!result.isEmpty() && !result.endsWith(QLatin1Char('\n'))) {
    result += QLatin1Char('\n');
  }
  if (!result.isEmpty()) {
    result += QLatin1Char('\n');
  }
  return result + kComment + QLatin1Char('\n') + bindingLine() + QLatin1Char('\n');
}

QString GameModeShortcut::withoutBinding(const QString& contents) {
  QStringList kept;
  const QStringList lines = contents.split(QLatin1Char('\n'));
  for (const QString& line : lines) {
    if (!bindingPattern().match(line).hasMatch()) {
      kept.append(line);
      continue;
    }
    if (!kept.isEmpty() && kept.last().trimmed() == kComment) {
      kept.removeLast();
      // The blank line Omakade put above its comment goes too, so adding and removing
      // leaves the file as it was.
      if (kept.size() > 1 && kept.last().trimmed().isEmpty()) {
        kept.removeLast();
      }
    }
  }
  return kept.join(QLatin1Char('\n'));
}

QString GameModeShortcut::takenBy(const QByteArray& bindsJson) {
  const QJsonDocument document = QJsonDocument::fromJson(bindsJson);
  for (const QJsonValue& value : document.array()) {
    const QJsonObject bind = value.toObject();
    if (bind.value(QLatin1String("modmask")).toInt() != kSuperCtrlMask ||
        bind.value(QLatin1String("key")).toString().compare(QLatin1String("G"),
                                                           Qt::CaseInsensitive) != 0) {
      continue;
    }
    const QString description = bind.value(QLatin1String("description")).toString().trimmed();
    if (description == QLatin1String("Game Mode")) {
      continue;
    }
    return description.isEmpty() ? QStringLiteral("another shortcut") : description;
  }
  return {};
}

QString GameModeShortcut::displayKey(const QString& key) {
  QStringList parts;
  for (const QString& part : key.split(QLatin1Char('+'), Qt::SkipEmptyParts)) {
    const QString word = part.trimmed();
    parts.append(word.size() > 1 ? word.at(0).toUpper() + word.mid(1).toLower() : word.toUpper());
  }
  return parts.join(QStringLiteral(" + "));
}

GameModeShortcut::GameModeShortcut(const QString& bindingsPath, bool omarchy, QObject* parent)
    : QObject(parent), m_path(bindingsPath), m_omarchy(omarchy) {
  connect(&m_watcher, &QFutureWatcher<Outcome>::finished, this, [this] {
    const Outcome outcome = m_watcher.result();
    m_available = outcome.available;
    m_boundKey = outcome.boundKey;
    m_takenBy = outcome.takenBy;
    m_statusText = outcome.statusText;
    emit changed();
  });
}

GameModeShortcut::~GameModeShortcut() { m_watcher.waitForFinished(); }

QString GameModeShortcut::keyLabel() const {
  return displayKey(m_boundKey.isEmpty() ? defaultKey() : m_boundKey);
}

void GameModeShortcut::refresh() { start(Action::Refresh); }

void GameModeShortcut::add() { start(Action::Add); }

void GameModeShortcut::remove() { start(Action::Remove); }

void GameModeShortcut::start(Action action) {
  if (m_path.isEmpty() || m_watcher.isRunning()) {
    return;
  }
  m_watcher.setFuture(QtConcurrent::run(
      [path = m_path, omarchy = m_omarchy, action] { return run(path, omarchy, action); }));
  emit changed();
}

GameModeShortcut::Outcome GameModeShortcut::run(const QString& path, bool omarchy,
                                                Action action) {
  Outcome outcome;
  QString contents;
  QByteArray binds;
  if (!omarchy || !readFile(path, &contents) ||
      !hyprctl({QStringLiteral("-j"), QStringLiteral("binds")}, &binds)) {
    return outcome;
  }
  outcome.available = true;
  outcome.boundKey = boundKey(contents);
  outcome.takenBy = outcome.boundKey.isEmpty() ? takenBy(binds) : QString{};

  if (action == Action::Add && outcome.boundKey.isEmpty() && outcome.takenBy.isEmpty()) {
    if (!writeFile(path, withBinding(contents))) {
      outcome.statusText = QStringLiteral("Could not write %1.").arg(path);
      return outcome;
    }
    outcome.boundKey = defaultKey();
    // The file covers every later session. This makes the key work now, whether or not
    // the running Hyprland rereads its configuration on its own.
    hyprctl({QStringLiteral("eval"), bindingLine()});
  } else if (action == Action::Remove && !outcome.boundKey.isEmpty()) {
    if (!writeFile(path, withoutBinding(contents))) {
      outcome.statusText = QStringLiteral("Could not write %1.").arg(path);
      return outcome;
    }
    hyprctl({QStringLiteral("eval"),
             QStringLiteral("hl.unbind(\"%1\")").arg(outcome.boundKey)});
    outcome.boundKey.clear();
    if (hyprctl({QStringLiteral("-j"), QStringLiteral("binds")}, &binds)) {
      outcome.takenBy = takenBy(binds);
    }
  }
  return outcome;
}
