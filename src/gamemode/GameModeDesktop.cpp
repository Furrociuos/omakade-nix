#include "gamemode/GameModeDesktop.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {
constexpr int kCommandTimeoutMs = 2500;

// Runs one command to completion. False when it is missing, stalls, or exits non-zero.
bool run(const QString& program, const QStringList& arguments, QByteArray* output = nullptr) {
  const QString executable = QStandardPaths::findExecutable(program);
  if (executable.isEmpty()) {
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

void setError(QString* error, const QString& text) {
  if (error != nullptr) {
    *error = text;
  }
}
} // namespace

bool HyprlandGameModeCompositor::available() {
  int known = m_available.load();
  if (known < 0) {
    QByteArray output;
    const bool usable = !qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE").isEmpty() &&
                        run(QStringLiteral("hyprctl"),
                            {QStringLiteral("eval"), QStringLiteral("return 1")}, &output) &&
                        output.trimmed() == "ok";
    known = usable ? 1 : 0;
    m_available.store(known);
  }
  return known == 1;
}

QString HyprlandGameModeCompositor::workspaceSelector(const QJsonObject& workspace) {
  const QString name = workspace.value(QLatin1String("name")).toString();
  const int id = workspace.value(QLatin1String("id")).toInt();
  if (name.isEmpty()) {
    return {};
  }
  if (id > 0) {
    return QString::number(id);
  }
  if (name.startsWith(QLatin1String("special"))) {
    return name;
  }
  return QStringLiteral("name:") + name;
}

QVector<GameModeOutput> HyprlandGameModeCompositor::parseOutputs(const QByteArray& json,
                                                                 QString* error) {
  QVector<GameModeOutput> outputs;
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
    setError(error, parseError.errorString());
    return outputs;
  }
  for (const QJsonValue& value : document.array()) {
    const QJsonObject monitor = value.toObject();
    GameModeOutput output;
    output.name = monitor.value(QLatin1String("name")).toString();
    if (output.name.isEmpty()) {
      continue;
    }
    output.id = monitor.value(QLatin1String("id")).toInt(-1);
    output.description = monitor.value(QLatin1String("description")).toString();
    output.enabled = !monitor.value(QLatin1String("disabled")).toBool();
    output.focused = monitor.value(QLatin1String("focused")).toBool();
    output.width = monitor.value(QLatin1String("width")).toInt();
    output.height = monitor.value(QLatin1String("height")).toInt();
    if (output.enabled) {
      output.workspace =
          workspaceSelector(monitor.value(QLatin1String("activeWorkspace")).toObject());
    }
    outputs.append(output);
  }
  return outputs;
}

GameModeWindow HyprlandGameModeCompositor::parseWindow(const QByteArray& clientsJson,
                                                       const QVector<GameModeOutput>& outputs,
                                                       qint64 pid) {
  GameModeWindow fallback;
  const QJsonDocument document = QJsonDocument::fromJson(clientsJson);
  if (pid <= 0 || !document.isArray()) {
    return fallback;
  }
  for (const QJsonValue& value : document.array()) {
    const QJsonObject client = value.toObject();
    if (client.value(QLatin1String("pid")).toVariant().toLongLong() != pid ||
        !client.value(QLatin1String("mapped")).toBool()) {
      continue;
    }
    GameModeWindow window;
    window.address = client.value(QLatin1String("address")).toString();
    if (!validAddress(window.address)) {
      continue;
    }
    window.workspace = workspaceSelector(client.value(QLatin1String("workspace")).toObject());
    const int monitor = client.value(QLatin1String("monitor")).toInt(-1);
    for (const GameModeOutput& output : outputs) {
      if (output.id == monitor) {
        window.output = output.name;
      }
    }
    if (client.value(QLatin1String("class")).toString().endsWith(QLatin1String("Omakade"))) {
      return window;
    }
    if (!fallback.valid()) {
      fallback = window;
    }
  }
  return fallback;
}

QString HyprlandGameModeCompositor::luaString(const QString& value) {
  QString quoted = QStringLiteral("\"");
  for (const QChar character : value) {
    const char16_t code = character.unicode();
    if (code == u'\\' || code == u'"') {
      quoted += QLatin1Char('\\');
      quoted += character;
    } else if (code < 0x20 || code == 0x7f) {
      quoted += QStringLiteral("\\%1").arg(static_cast<int>(code), 3, 10, QLatin1Char('0'));
    } else {
      quoted += character;
    }
  }
  return quoted + QLatin1Char('"');
}

bool HyprlandGameModeCompositor::validAddress(const QString& address) {
  static const QRegularExpression pattern(QStringLiteral("^0x[0-9a-fA-F]{1,16}$"));
  return pattern.match(address).hasMatch();
}

QString HyprlandGameModeCompositor::outputScript(const QString& name, bool enabled) {
  // Only the disabled flag is set, so a mode, position and scale the user configured for
  // this output stay as they are.
  return QStringLiteral("hl.monitor({ output = %1, disabled = %2 })")
      .arg(luaString(name), enabled ? QStringLiteral("false") : QStringLiteral("true"));
}

QString HyprlandGameModeCompositor::placeScript(const QString& address, const QString& workspace,
                                                const QString& output) {
  const QString window = luaString(QStringLiteral("address:") + address);
  // Focusing the output first makes a new workspace open there, not wherever focus was.
  return QStringLiteral("hl.dispatch(hl.dsp.focus({ monitor = %1 }))\n"
                        "hl.dispatch(hl.dsp.window.move({ window = %2, workspace = %3 }))\n"
                        "hl.dispatch(hl.dsp.focus({ window = %2 }))")
      .arg(luaString(output), window, luaString(workspace));
}

QString HyprlandGameModeCompositor::returnScript(const QString& address, const QString& workspace) {
  return QStringLiteral(
             "hl.dispatch(hl.dsp.window.move({ window = %1, workspace = %2, follow = false }))")
      .arg(luaString(QStringLiteral("address:") + address), luaString(workspace));
}

bool HyprlandGameModeCompositor::eval(const QString& script, QString* error) {
  QByteArray output;
  const bool ran = run(QStringLiteral("hyprctl"), {QStringLiteral("eval"), script}, &output);
  if (ran && output.trimmed() == "ok") {
    return true;
  }
  setError(error, QString::fromUtf8(output).trimmed());
  return false;
}

QVector<GameModeOutput> HyprlandGameModeCompositor::outputs(QString* error) {
  QByteArray json;
  if (!run(QStringLiteral("hyprctl"),
           {QStringLiteral("-j"), QStringLiteral("monitors"), QStringLiteral("all")}, &json)) {
    setError(error, QStringLiteral("hyprctl did not answer"));
    return {};
  }
  return parseOutputs(json, error);
}

bool HyprlandGameModeCompositor::setOutputEnabled(const QString& name, bool enabled,
                                                  QString* error) {
  return !name.isEmpty() && eval(outputScript(name, enabled), error);
}

GameModeWindow HyprlandGameModeCompositor::windowForPid(qint64 pid) {
  QByteArray clients;
  if (!run(QStringLiteral("hyprctl"), {QStringLiteral("-j"), QStringLiteral("clients")},
           &clients)) {
    return {};
  }
  return parseWindow(clients, outputs(), pid);
}

bool HyprlandGameModeCompositor::placeWindow(const QString& address, const QString& workspace,
                                             const QString& output, QString* error) {
  return validAddress(address) && !workspace.isEmpty() && !output.isEmpty() &&
         eval(placeScript(address, workspace, output), error);
}

bool HyprlandGameModeCompositor::returnWindow(const QString& address, const QString& workspace,
                                              QString* error) {
  return validAddress(address) && !workspace.isEmpty() &&
         eval(returnScript(address, workspace), error);
}

bool HyprlandGameModeCompositor::focusWorkspace(const QString& workspace, QString* error) {
  return !workspace.isEmpty() &&
         eval(QStringLiteral("hl.dispatch(hl.dsp.focus({ workspace = %1 }))")
                  .arg(luaString(workspace)),
              error);
}

bool HyprlandGameModeCompositor::focusOutput(const QString& name, QString* error) {
  return !name.isEmpty() &&
         eval(QStringLiteral("hl.dispatch(hl.dsp.focus({ monitor = %1 }))").arg(luaString(name)),
              error);
}

bool PactlGameModeAudio::available() {
  return run(QStringLiteral("pactl"), {QStringLiteral("get-default-sink")});
}

QVector<GameModeSink> PactlGameModeAudio::parseSinks(const QByteArray& json, QString* error) {
  QVector<GameModeSink> sinks;
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
    setError(error, parseError.errorString());
    return sinks;
  }
  for (const QJsonValue& value : document.array()) {
    const QJsonObject object = value.toObject();
    GameModeSink sink;
    sink.name = object.value(QLatin1String("name")).toString();
    if (sink.name.isEmpty()) {
      continue;
    }
    sink.description = object.value(QLatin1String("description")).toString();
    sinks.append(sink);
  }
  return sinks;
}

QVector<GameModeSink> PactlGameModeAudio::sinks(QString* error) {
  QByteArray json;
  if (!run(QStringLiteral("pactl"),
           {QStringLiteral("-f"), QStringLiteral("json"), QStringLiteral("list"),
            QStringLiteral("sinks")},
           &json)) {
    setError(error, QStringLiteral("pactl did not answer"));
    return {};
  }
  return parseSinks(json, error);
}

QString PactlGameModeAudio::defaultSink() {
  QByteArray output;
  if (!run(QStringLiteral("pactl"), {QStringLiteral("get-default-sink")}, &output)) {
    return {};
  }
  return QString::fromUtf8(output).trimmed();
}

bool PactlGameModeAudio::setDefaultSink(const QString& name, QString* error) {
  if (name.isEmpty() || name.startsWith(QLatin1Char('-'))) {
    return false;
  }
  if (run(QStringLiteral("pactl"), {QStringLiteral("set-default-sink"), name})) {
    return true;
  }
  setError(error, QStringLiteral("pactl could not select %1").arg(name));
  return false;
}

bool OmarchyGameModeNotifications::available() {
  return !QStandardPaths::findExecutable(QStringLiteral("omarchy-shell")).isEmpty();
}

bool OmarchyGameModeNotifications::silenced(bool* silenced) {
  QByteArray output;
  if (!run(QStringLiteral("omarchy-shell"),
           {QStringLiteral("notifications"), QStringLiteral("dndState")}, &output)) {
    return false;
  }
  const QByteArray state = output.trimmed();
  if (state != "on" && state != "off") {
    return false;
  }
  if (silenced != nullptr) {
    *silenced = state == "on";
  }
  return true;
}

bool OmarchyGameModeNotifications::setSilenced(bool silenced) {
  if (!run(QStringLiteral("omarchy-shell"),
           {QStringLiteral("notifications"), QStringLiteral("setDnd"),
            silenced ? QStringLiteral("true") : QStringLiteral("false")})) {
    return false;
  }
  // The bar's indicator reads the state on request, the same way Omarchy's own toggle
  // refreshes it.
  run(QStringLiteral("omarchy-shell"),
      {QStringLiteral("-q"), QStringLiteral("omarchy.indicators"), QStringLiteral("refresh")});
  return true;
}
