#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

// The Settings switch for omakade-guide-button, the user service that toggles Game Mode with
// a controller's Guide or Home button. It is off until the user turns it on, and turning it
// off stops the service and removes it from the session's startup, so nothing is left behind.
// systemd is the only record of the choice; there is no separate setting to drift from it.
class GameModeGuideButton final : public QObject {
  Q_OBJECT
  // False when the service is not installed or there is no systemd user session.
  Q_PROPERTY(bool available READ available NOTIFY changed)
  Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
  Q_PROPERTY(bool running READ running NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(QString statusText READ statusText NOTIFY changed)

public:
  static constexpr auto kUnit = "omakade-guide-button.service";

  struct Result {
    bool ok = false;
    QString output;
  };
  // Runs `systemctl --user` with the arguments given.
  using Systemctl = std::function<Result(const QStringList& arguments)>;

  struct Outcome {
    bool available = false;
    bool enabled = false;
    bool running = false;
    QString statusText;
  };
  enum class Action { Refresh, Enable, Disable };

  // `managed` is false in isolated test runs, which never touch the user's services.
  explicit GameModeGuideButton(bool managed, QObject* parent = nullptr);
  ~GameModeGuideButton() override;

  [[nodiscard]] bool available() const { return m_outcome.available; }
  [[nodiscard]] bool enabled() const { return m_outcome.enabled; }
  [[nodiscard]] bool running() const { return m_outcome.running; }
  [[nodiscard]] bool busy() const { return m_watcher.isRunning(); }
  [[nodiscard]] QString statusText() const { return m_outcome.statusText; }

  Q_INVOKABLE void refresh();
  Q_INVOKABLE void enable();
  Q_INVOKABLE void disable();

  [[nodiscard]] static Outcome run(const Systemctl& systemctl, Action action);

signals:
  void changed();

private:
  void start(Action action);

  bool m_managed = false;
  Outcome m_outcome;
  QFutureWatcher<Outcome> m_watcher;
};
