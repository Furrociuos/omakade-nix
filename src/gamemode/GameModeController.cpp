#include "gamemode/GameModeController.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QThread>

namespace {
constexpr int kPollStepMs = 250;
// A cold start may ask for Game Mode before the compositor has mapped the window.
constexpr int kWindowWaitMs = 3000;
// Televisions take several seconds to accept a mode after the output is enabled.
constexpr int kOutputWaitMs = 10000;
// An HDMI sink only appears once its display is live.
constexpr int kSinkWaitMs = 8000;

bool defaultOwnerAlive(qint64 pid) {
  if (pid <= 0) {
    return false;
  }
  QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
  if (!comm.open(QIODevice::ReadOnly)) {
    return false;
  }
  return comm.readAll().trimmed() == "omakade";
}

QString outputLabel(const GameModeOutput& output) {
  return output.description.isEmpty() ? output.name : output.description;
}
} // namespace

QJsonObject GameModeState::toJson() const {
  return {{"version", 1},
          {"owner_pid", ownerPid},
          {"output", output},
          {"enabled_output", enabledOutput},
          {"output_workspace", outputWorkspace},
          {"focused_output", focusedOutput},
          {"window_workspace", windowWorkspace},
          {"window_placed", windowPlaced},
          {"previous_sink", previousSink},
          {"session_sink", sessionSink},
          {"silenced_notifications", silencedNotifications}};
}

bool GameModeState::fromJson(const QJsonObject& object, GameModeState* state) {
  if (state == nullptr || object.value("version").toInt() != 1) {
    return false;
  }
  state->ownerPid = object.value("owner_pid").toVariant().toLongLong();
  state->output = object.value("output").toString();
  state->enabledOutput = object.value("enabled_output").toBool();
  state->outputWorkspace = object.value("output_workspace").toString();
  state->focusedOutput = object.value("focused_output").toString();
  state->windowWorkspace = object.value("window_workspace").toString();
  state->windowPlaced = object.value("window_placed").toBool();
  state->previousSink = object.value("previous_sink").toString();
  state->sessionSink = object.value("session_sink").toString();
  state->silencedNotifications = object.value("silenced_notifications").toBool();
  return true;
}

QString GameModeController::workspace() { return QStringLiteral("name:omakade"); }

GameModeController::GameModeController(GameModeCompositor* compositor, GameModeAudio* audio,
                                       GameModeNotifications* notifications,
                                       const QString& statePath, Sleep sleep, OwnerAlive ownerAlive)
    : m_compositor(compositor), m_audio(audio), m_notifications(notifications),
      m_statePath(statePath), m_sleep(std::move(sleep)),
      m_ownerAlive(ownerAlive ? std::move(ownerAlive) : OwnerAlive(defaultOwnerAlive)) {}

bool GameModeController::managed() const {
  return m_compositor != nullptr && m_compositor->available();
}

int GameModeController::findOutput(const QVector<GameModeOutput>& outputs, const QString& name,
                                   const QString& description) {
  int byDescription = -1;
  int descriptionMatches = 0;
  int byName = -1;
  for (int index = 0; index < outputs.size(); ++index) {
    const GameModeOutput& output = outputs.at(index);
    if (!description.isEmpty() && output.description == description) {
      ++descriptionMatches;
      // Two identical displays share a description; the connector breaks the tie.
      if (byDescription < 0 || output.name == name) {
        byDescription = index;
      }
    }
    if (!name.isEmpty() && output.name == name && byName < 0) {
      byName = index;
    }
  }
  if (descriptionMatches > 0) {
    return byDescription;
  }
  // A connector that now carries a different display is not the chosen display.
  if (byName >= 0 && !description.isEmpty() && !outputs.at(byName).description.isEmpty()) {
    return -1;
  }
  return byName;
}

bool GameModeController::save(const GameModeState& state) const {
  if (m_statePath.isEmpty() || !QDir().mkpath(QFileInfo(m_statePath).absolutePath())) {
    return false;
  }
  QSaveFile file(m_statePath);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  file.write(QJsonDocument(state.toJson()).toJson(QJsonDocument::Indented));
  return file.commit();
}

bool GameModeController::load(GameModeState* state) const {
  QFile file(m_statePath);
  if (m_statePath.isEmpty() || !file.open(QIODevice::ReadOnly)) {
    return false;
  }
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
  file.close();
  if (!document.isObject() || !GameModeState::fromJson(document.object(), state)) {
    // An unreadable record cannot be acted on and must not block the next session.
    forget();
    return false;
  }
  return true;
}

void GameModeController::forget() const {
  if (!m_statePath.isEmpty()) {
    QFile::remove(m_statePath);
  }
}

bool GameModeController::waitFor(const std::function<bool()>& ready, int timeoutMs) const {
  for (int waited = 0;; waited += kPollStepMs) {
    if (ready()) {
      return true;
    }
    if (waited >= timeoutMs) {
      return false;
    }
    if (m_sleep) {
      m_sleep(kPollStepMs);
    } else {
      QThread::msleep(kPollStepMs);
    }
  }
}

GameModeController::Result GameModeController::enter(const GameModeSettings& settings,
                                                     qint64 windowPid) {
  Result result;
  if (m_active) {
    result.error = QStringLiteral("Game Mode is already on.");
    return result;
  }
  // What a crashed session left changed would otherwise be recorded as the state to
  // return to.
  const Result recovered = recover();
  if (!recovered.ok && recovered.notes.isEmpty()) {
    // Another running Omakade owns the session.
    result.error = recovered.error;
    return result;
  }
  // Recovery that cannot finish is reported once and then dropped, so a tool that has
  // since been removed cannot block Game Mode for good.
  result.notes = recovered.notes;
  forget();

  GameModeState state;
  state.ownerPid = QCoreApplication::applicationPid();
  const bool displayChosen =
      !settings.outputName.isEmpty() || !settings.outputDescription.isEmpty();
  const bool compositor = managed();
  if (displayChosen && !compositor) {
    result.error = QStringLiteral(
        "Choosing a Game Mode display needs Hyprland with a Lua configuration. Choose "
        "Current display instead.");
    return result;
  }
  const auto fail = [&](const QString& message) {
    restore(state, windowPid, false, &result.notes);
    result.error = message;
    return result;
  };

  GameModeWindow window;
  if (compositor) {
    QString error;
    const QVector<GameModeOutput> outputs = m_compositor->outputs(&error);
    if (outputs.isEmpty()) {
      result.error = QStringLiteral("Hyprland did not report any displays.");
      return result;
    }
    (void)waitFor(
        [&] {
          window = m_compositor->windowForPid(windowPid);
          return window.valid();
        },
        kWindowWaitMs);
    int index = -1;
    if (displayChosen) {
      index = findOutput(outputs, settings.outputName, settings.outputDescription);
      if (index < 0) {
        result.error = QStringLiteral("The Game Mode display is not connected: %1")
                           .arg(settings.outputDescription.isEmpty() ? settings.outputName
                                                                     : settings.outputDescription);
        return result;
      }
    } else {
      int focused = -1;
      for (int candidate = 0; candidate < outputs.size(); ++candidate) {
        if (window.valid() && outputs.at(candidate).name == window.output) {
          index = candidate;
        }
        if (outputs.at(candidate).focused) {
          focused = candidate;
        }
      }
      if (index < 0) {
        index = focused;
      }
      if (index < 0) {
        result.error = QStringLiteral("Could not tell which display Omakade is on.");
        return result;
      }
    }
    const GameModeOutput target = outputs.at(index);
    state.output = target.name;
    state.windowWorkspace = window.workspace;
    for (const GameModeOutput& output : outputs) {
      if (output.focused) {
        state.focusedOutput = output.name;
      }
    }
    if (target.enabled) {
      state.outputWorkspace = target.workspace;
    } else {
      state.enabledOutput = true;
      if (!save(state)) {
        result.error =
            QStringLiteral("Could not record Game Mode's state, so nothing was changed.");
        return result;
      }
      if (!m_compositor->setOutputEnabled(target.name, true, &error)) {
        return fail(QStringLiteral("Could not turn on %1.").arg(outputLabel(target)));
      }
      const bool live = waitFor(
          [&] {
            for (const GameModeOutput& output : m_compositor->outputs()) {
              if (output.name == target.name) {
                return output.enabled && !output.workspace.isEmpty();
              }
            }
            return false;
          },
          kOutputWaitMs);
      if (!live) {
        return fail(QStringLiteral("%1 did not turn on. Check that it is powered and set to "
                                   "this computer's input.")
                        .arg(outputLabel(target)));
      }
    }
  }

  if (!settings.sinkName.isEmpty()) {
    if (m_audio == nullptr || !m_audio->available()) {
      return fail(QStringLiteral("The sound output cannot be changed because pactl is not "
                                 "available."));
    }
    const bool present = waitFor(
        [&] {
          for (const GameModeSink& sink : m_audio->sinks()) {
            if (sink.name == settings.sinkName) {
              return true;
            }
          }
          return false;
        },
        state.enabledOutput ? kSinkWaitMs : 0);
    if (!present) {
      return fail(QStringLiteral("The Game Mode sound output is not available."));
    }
    const QString previous = m_audio->defaultSink();
    if (previous != settings.sinkName) {
      state.previousSink = previous;
      state.sessionSink = settings.sinkName;
      if (!save(state)) {
        state.previousSink.clear();
        state.sessionSink.clear();
        return fail(QStringLiteral("Could not record Game Mode's state."));
      }
      if (!m_audio->setDefaultSink(settings.sinkName)) {
        return fail(QStringLiteral("Could not switch to the Game Mode sound output."));
      }
    }
  }

  if (compositor && window.valid() && window.workspace != workspace()) {
    QString error;
    state.windowPlaced = true;
    if (!m_compositor->placeWindow(window.address, workspace(), state.output, &error)) {
      return fail(QStringLiteral("Could not move Omakade to the Game Mode display."));
    }
  }

  // Notifications never decide whether Game Mode starts.
  if (settings.silenceNotifications && m_notifications != nullptr && m_notifications->available()) {
    bool silenced = false;
    if (m_notifications->silenced(&silenced) && !silenced && m_notifications->setSilenced(true)) {
      state.silencedNotifications = true;
    }
  }

  if (!save(state)) {
    result.notes.append(QStringLiteral(
        "Game Mode's state could not be saved. Leave it from Omakade so everything is put back."));
  }
  m_state = state;
  m_active = true;
  result.ok = true;
  result.output = state.output;
  return result;
}

GameModeController::Result GameModeController::exit(qint64 windowPid) {
  if (!m_active) {
    return recover();
  }
  Result result;
  result.output = m_state.output;
  result.ok = restore(m_state, windowPid, false, &result.notes);
  if (!result.ok) {
    result.error = QStringLiteral("Game Mode ended, but not everything could be put back.");
  }
  m_state = {};
  m_active = false;
  return result;
}

GameModeController::Result GameModeController::recover() {
  Result result;
  result.ok = true;
  GameModeState state;
  if (!load(&state)) {
    return result;
  }
  const qint64 self = QCoreApplication::applicationPid();
  if (state.ownerPid == self && m_active) {
    return result;
  }
  if (state.ownerPid != self && m_ownerAlive(state.ownerPid)) {
    result.ok = false;
    result.error = QStringLiteral("Game Mode belongs to another running Omakade.");
    return result;
  }
  result.output = state.output;
  result.ok = restore(state, 0, true, &result.notes);
  if (!result.ok) {
    result.error = QStringLiteral("An interrupted Game Mode session could not be fully undone.");
  }
  return result;
}

bool GameModeController::restore(const GameModeState& state, qint64 windowPid, bool ownerGone,
                                 QStringList* notes) const {
  bool complete = true;
  const auto note = [notes](const QString& text) {
    if (notes != nullptr) {
      notes->append(text);
    }
  };

  if (state.silencedNotifications) {
    if (m_notifications == nullptr || !m_notifications->available() ||
        !m_notifications->setSilenced(false)) {
      complete = false;
      note(QStringLiteral("Notifications are still silenced."));
    }
  }

  const bool compositor = managed();
  if (compositor && !ownerGone && state.windowPlaced) {
    if (windowPid > 0 && !state.windowWorkspace.isEmpty()) {
      const GameModeWindow window = m_compositor->windowForPid(windowPid);
      if (window.valid() && window.workspace == workspace() &&
          !m_compositor->returnWindow(window.address, state.windowWorkspace)) {
        note(QStringLiteral("Omakade's window could not be moved back."));
      }
    }
    // A display Game Mode turned on is about to go away; one that was already on gets
    // back the workspace it was showing.
    if (!state.enabledOutput && !state.output.isEmpty() && !state.outputWorkspace.isEmpty() &&
        state.outputWorkspace != workspace()) {
      m_compositor->focusOutput(state.output);
      m_compositor->focusWorkspace(state.outputWorkspace);
    }
    if (!state.focusedOutput.isEmpty() && state.focusedOutput != state.output) {
      m_compositor->focusOutput(state.focusedOutput);
    }
  }

  if (!state.sessionSink.isEmpty()) {
    if (m_audio == nullptr || !m_audio->available()) {
      complete = false;
      note(QStringLiteral("The sound output could not be put back."));
    } else {
      const QVector<GameModeSink> sinks = m_audio->sinks();
      const auto exists = [&sinks](const QString& name) {
        for (const GameModeSink& sink : sinks) {
          if (sink.name == name) {
            return true;
          }
        }
        return false;
      };
      const QString current = m_audio->defaultSink();
      // A different default means the user chose it during the session; keep it.
      if (current == state.sessionSink || !exists(state.sessionSink)) {
        if (state.previousSink.isEmpty() || !exists(state.previousSink)) {
          note(QStringLiteral("The previous sound output is gone, so the current one was kept."));
        } else if (current != state.previousSink && !m_audio->setDefaultSink(state.previousSink)) {
          complete = false;
          note(QStringLiteral("The sound output could not be put back."));
        }
      }
    }
  }

  if (state.enabledOutput) {
    if (!compositor || !m_compositor->setOutputEnabled(state.output, false)) {
      complete = false;
      note(QStringLiteral("%1 could not be turned off.").arg(state.output));
    }
  }

  if (complete) {
    forget();
  }
  return complete;
}
