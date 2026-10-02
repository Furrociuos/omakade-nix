import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ActionMenu {
    id: panel
    objectName: "gameModeControls"
    title: "GAME MODE"
    fixedHeader: true
    showCloseButton: false
    showHeaderCloseButton: false
    preferredWidth: 520
    headerTextScale: textScale
    initialFocus: backButton
    readonly property var liveGames: typeof GameStop !== "undefined" && GameStop ? GameStop.runningGames : []
    readonly property bool scanning: typeof GameStop !== "undefined" && GameStop && GameStop.scanning
    readonly property bool untrackedGame: Launcher.gameRunning && liveGames.length === 0
    readonly property real textScale: host.couchMode ? Math.max(1, Math.min(2, host.height / 900)) : 1

    function refreshGames() {
        if (typeof GameStop !== "undefined" && GameStop) GameStop.refreshLiveGames()
    }
    function openControls() {
        open()
        refreshGames()
        GameMode.refresh()
    }

    GameStopPanel {
        id: stopAndLeave
        namePrefix: "gameMode"
        host: panel.host
        anchorItem: panel.anchorItem
        leaveGameModeAfterStop: true
    }

    Timer {
        interval: 2000
        running: panel.opened
        repeat: true
        onTriggered: panel.refreshGames()
    }
    Connections {
        target: panel
        function onClosed() { Qt.callLater(panel.host.focusCurrentSurface) }
    }

    Text {
        Layout.fillWidth: true
        text: GameMode.sessionDisplayLabel
        wrapMode: Text.Wrap
        font.family: Theme.fontFamily
        font.pixelSize: 16 * panel.textScale
        font.weight: Font.DemiBold
        color: Theme.foreground
    }
    Text {
        Layout.fillWidth: true
        text: "Sound: " + GameMode.sessionSoundLabel
        wrapMode: Text.Wrap
        font.family: Theme.fontFamily
        font.pixelSize: 12 * panel.textScale
        color: Theme.mutedText
    }
    Text {
        Layout.fillWidth: true
        text: GameMode.statusText || (panel.scanning ? "Checking running games…" : panel.liveGames.length > 0
              ? panel.liveGames.map(game => game.title || game.appId).join(", ")
                + (panel.liveGames.length === 1 ? " is running." : " are running.")
              : panel.untrackedGame
                ? "Game activity is detected, but no safe stop target is available."
                : "Your display, sound and notification changes will be restored when you leave.")
        wrapMode: Text.Wrap
        font.family: Theme.fontFamily
        font.pixelSize: 12 * panel.textScale
        color: Theme.foreground
    }
    MenuAction {
        id: backButton
        objectName: "gameModeBackButton"
        text: "BACK TO LIBRARY"
        enabled: !GameMode.busy
        onClicked: panel.close()
    }
    MenuAction {
        objectName: "gameModeStopAndLeaveButton"
        text: "STOP GAMES AND LEAVE…"
        visible: panel.liveGames.length > 0
        enabled: !GameMode.busy && !panel.scanning
        onClicked: panel.invoke(stopAndLeave.beginAll)
    }
    MenuAction {
        id: leaveButton
        objectName: "gameModeLeaveButton"
        text: panel.liveGames.length > 0 || panel.untrackedGame
              ? "LEAVE WITH GAMES RUNNING" : "LEAVE GAME MODE"
        enabled: !GameMode.busy
        onClicked: {
            panel.close()
            GameMode.exit()
        }
    }
    Text {
        Layout.fillWidth: true
        visible: panel.liveGames.length > 0 || panel.untrackedGame
        text: "Leaving without stopping keeps games running on your desktop."
        wrapMode: Text.Wrap
        font.family: Theme.fontFamily
        font.pixelSize: 11 * panel.textScale
        color: Theme.mutedText
    }
}
