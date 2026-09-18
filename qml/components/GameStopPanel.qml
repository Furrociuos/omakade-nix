import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Confirms and applies a stop, for one game or for every game that has
// something attributable. Everything it shows comes from GameStop, so the list
// on screen is the list that will be signalled.
ActionMenu {
    id: stopPanel
    property string namePrefix: ""
    property string heading: "STOP GAME"
    // "one" for a single game, "all" for the global action.
    property string mode: "one"
    property var game: ({})
    property var targets: []
    property var notes: []
    property var games: []
    property bool pending: false
    property string resultMessage: ""
    property var resultLines: []

    objectName: namePrefix + "gameStopPanel"
    title: heading
    showCloseButton: false
    fixedHeader: true
    preferredWidth: 460
    // The safe action, not the one that signals: the menu focuses this on open.
    initialFocus: cancelStop

    function refresh() {
        const available = typeof GameStop !== "undefined" && GameStop
        if (mode === "all") {
            targets = []
            notes = []
            games = available ? GameStop.liveGames() : []
            return
        }
        games = []
        targets = available ? GameStop.preview(game) : []
        notes = available ? GameStop.notesFor(game) : []
    }

    function begin(gameRow) {
        mode = "one"
        heading = "STOP GAME"
        game = gameRow || ({})
        resultMessage = ""
        resultLines = []
        pending = false
        refresh()
        open()
        Qt.callLater(cancelStop.forceActiveFocus)
    }

    function beginAll() {
        mode = "all"
        heading = "STOP ALL GAMES"
        game = ({})
        resultMessage = ""
        resultLines = []
        pending = false
        refresh()
        open()
        Qt.callLater(cancelStop.forceActiveFocus)
    }

    function hasSomethingToStop() {
        if (mode === "all")
            return games.length > 0
        return targets.length > 0
    }

    Connections {
        target: typeof GameStop !== "undefined" ? GameStop : null
        function onFinished(okay, message, lines) {
            stopPanel.pending = false
            stopPanel.resultMessage = message
            stopPanel.resultLines = lines
            if (stopPanel.opened)
                Qt.callLater(doneStop.forceActiveFocus)
        }
    }

    Text {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.mutedText
        font.family: Theme.fontFamily
        font.pixelSize: 12
        lineHeight: 1.2
        visible: !stopPanel.pending && stopPanel.resultMessage.length === 0
        text: stopPanel.hasSomethingToStop()
              ? "These processes will be asked to close, and forced after a few seconds if they do not:"
              : (stopPanel.mode === "all"
                 ? "No game on this machine can be attributed to something running."
                 : "Nothing attributable to this game is running.")
    }

    Text {
        Layout.fillWidth: true
        visible: stopPanel.mode === "one" && stopPanel.targets.length > 0
                 && !stopPanel.pending && stopPanel.resultMessage.length === 0
        wrapMode: Text.Wrap
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: 13
        font.weight: Font.DemiBold
        text: stopPanel.game.title || ""
    }

    Repeater {
        model: stopPanel.mode === "all" ? [] : stopPanel.targets
        Text {
            required property var modelData
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: 12
            text: "•  " + modelData
        }
    }

    Repeater {
        model: stopPanel.mode === "all" ? stopPanel.games : []
        ColumnLayout {
            required property var modelData
            Layout.fillWidth: true
            spacing: 2
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: 13
                font.weight: Font.DemiBold
                text: modelData.title || modelData.appId || ""
            }
            Repeater {
                model: modelData.lines || []
                Text {
                    required property var modelData
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    text: "•  " + modelData
                }
            }
        }
    }

    Repeater {
        model: stopPanel.notes
        Text {
            required property var modelData
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.mutedText
            font.family: Theme.fontFamily
            font.pixelSize: 12
            text: modelData
        }
    }

    Text {
        Layout.fillWidth: true
        visible: stopPanel.pending
        wrapMode: Text.Wrap
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: 12
        font.weight: Font.DemiBold
        text: "Stopping…"
    }

    MenuAction {
        id: cancelStop
        objectName: stopPanel.namePrefix + "cancelStop"
        Layout.fillWidth: true
        text: "CANCEL"
        visible: !stopPanel.pending && stopPanel.resultMessage.length === 0
        onClicked: {
            stopPanel.close()
            stopPanel.doneControl.forceActiveFocus()
        }
    }

    MenuAction {
        objectName: stopPanel.namePrefix + "confirmStop"
        Layout.fillWidth: true
        visible: !stopPanel.pending && stopPanel.resultMessage.length === 0
                 && stopPanel.hasSomethingToStop()
        text: stopPanel.mode === "all" ? "STOP ALL GAMES" : "STOP THIS GAME"
        onClicked: {
            stopPanel.pending = true
            if (stopPanel.mode === "all")
                GameStop.stopAll()
            else
                GameStop.stop(stopPanel.game)
        }
    }

    Text {
        Layout.fillWidth: true
        visible: !stopPanel.pending && stopPanel.resultMessage.length > 0
        wrapMode: Text.Wrap
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: 12
        font.weight: Font.DemiBold
        text: stopPanel.resultMessage
    }

    Repeater {
        model: stopPanel.resultMessage.length > 0 ? stopPanel.resultLines : []
        Text {
            required property var modelData
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.mutedText
            font.family: Theme.fontFamily
            font.pixelSize: 12
            text: modelData
        }
    }

    MenuAction {
        id: doneStop
        objectName: stopPanel.namePrefix + "dismissStop"
        Layout.fillWidth: true
        visible: !stopPanel.pending && stopPanel.resultMessage.length > 0
        text: "DONE"
        onClicked: {
            stopPanel.resultMessage = ""
            stopPanel.resultLines = []
            stopPanel.close()
            stopPanel.doneControl.forceActiveFocus()
        }
    }

    onClosed: {
        pending = false
        resultMessage = ""
        resultLines = []
    }
}
