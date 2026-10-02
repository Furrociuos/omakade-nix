#pragma once

#include <QByteArray>
#include <QFutureWatcher>
#include <QObject>
#include <QString>

// The Game Mode key on Omarchy: Super + Ctrl + G, which Omarchy leaves unbound. Omakade
// never claims a key on its own. The user adds the binding from Settings, and it is one
// line in ~/.config/hypr/bindings.lua that they can read, change or delete.
class GameModeShortcut final : public QObject {
  Q_OBJECT
  // False outside Omarchy, where there is no bindings file in this format to add to.
  Q_PROPERTY(bool available READ available NOTIFY changed)
  // The binding is in the bindings file.
  Q_PROPERTY(bool bound READ bound NOTIFY changed)
  // What the key is already used for when it is not free. Empty when it is.
  Q_PROPERTY(QString takenBy READ takenBy NOTIFY changed)
  // The key of the binding in the file, or the one that would be added.
  Q_PROPERTY(QString keyLabel READ keyLabel NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(QString statusText READ statusText NOTIFY changed)

public:
  // `omarchy` is false when the desktop is not Omarchy. A null `bindingsPath` disables
  // the object, which is what isolated test runs use.
  explicit GameModeShortcut(const QString& bindingsPath, bool omarchy, QObject* parent = nullptr);
  ~GameModeShortcut() override;

  [[nodiscard]] bool available() const { return m_available; }
  [[nodiscard]] bool bound() const { return !m_boundKey.isEmpty(); }
  [[nodiscard]] QString takenBy() const { return m_takenBy; }
  [[nodiscard]] QString keyLabel() const;
  [[nodiscard]] bool busy() const { return m_watcher.isRunning(); }
  [[nodiscard]] QString statusText() const { return m_statusText; }

  Q_INVOKABLE void refresh();
  Q_INVOKABLE void add();
  Q_INVOKABLE void remove();

  [[nodiscard]] static QString defaultKey();
  [[nodiscard]] static QString command();
  // The `o.bind` line Omakade adds.
  [[nodiscard]] static QString bindingLine();
  // What the running Hyprland is given to bind the key now. It leaves exactly one binding.
  [[nodiscard]] static QString liveBindingScript();
  // The key of the first live binding that runs Game Mode, such as "SUPER + CTRL + G".
  // Empty when there is none. Commented lines do not count.
  [[nodiscard]] static QString boundKey(const QString& contents);
  [[nodiscard]] static QString withBinding(const QString& contents);
  // Removes every live Game Mode binding, and the comment Omakade wrote above its own.
  [[nodiscard]] static QString withoutBinding(const QString& contents);
  // Reads `hyprctl -j binds`. The description of whatever else holds the default key, or
  // empty when the key is free or already runs Game Mode.
  [[nodiscard]] static QString takenBy(const QByteArray& bindsJson);
  // "SUPER + CTRL + G" as it is written on a keyboard: "Super + Ctrl + G".
  [[nodiscard]] static QString displayKey(const QString& key);

signals:
  void changed();

private:
  struct Outcome {
    bool available = false;
    QString boundKey;
    QString takenBy;
    QString statusText;
  };
  enum class Action { Refresh, Add, Remove };
  void start(Action action);
  [[nodiscard]] static Outcome run(const QString& path, bool omarchy, Action action);

  QString m_path;
  bool m_omarchy = false;
  bool m_available = false;
  QString m_boundKey;
  QString m_takenBy;
  QString m_statusText;
  QFutureWatcher<Outcome> m_watcher;
};
