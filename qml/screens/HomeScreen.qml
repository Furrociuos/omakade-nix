import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

FocusScope {
    id: root
    property bool couchMode: false
    readonly property real scaleFactor: couchMode ? 1.25 : 1
    property string focusedIdentity: ""
    property string notice: ""
    signal libraryRequested()
    signal gameRequested(var game)
    function focusHome() { libraryButton.forceActiveFocus() }
    function focusKey(game) { return game.queueKey ? "queue:" + game.queueKey : game.identity || "" }
    function focusIdentity(identity) {
        for (let i = 0; i < list.count; ++i) {
            const game = list.model[i].game
            if (game && focusKey(game) === identity) {
                list.currentIndex = i
                list.positionViewAtIndex(i, ListView.Contain)
                list.forceLayout()
                const row = list.itemAtIndex(i)
                if (row) row.focusRow()
                return
            }
        }
        focusHome()
    }
    function queueAction(game, operation) {
        let identity = focusKey(game)
        if (operation === "remove") {
            const queued = Home.queue
            const index = queued.findIndex(item => item.queueKey === game.queueKey)
            const neighbor = queued[index + 1] || queued[index - 1]
            identity = neighbor ? focusKey(neighbor) : ""
        }
        if (operation === "add") Home.enqueue(game.source, game.runner || "", game.appId)
        else if (operation === "remove") Home.remove(game.queueKey)
        else Home.move(game.queueKey, operation === "up" ? -1 : 1)
        Qt.callLater(function() { root.focusIdentity(identity) })
    }
    Connections {
        target: Home
        function onChanged() {
            if (root.visible && root.activeFocus && root.focusedIdentity !== "") {
                const identity = root.focusedIdentity
                Qt.callLater(function() { if (root.visible && root.activeFocus) root.focusIdentity(identity) })
            }
        }
    }
    function reveal(item) {
        if (item && item.homeRow !== undefined) {
            list.currentIndex = item.homeRow
            list.positionViewAtIndex(list.currentIndex, ListView.Contain)
        }
    }
    function navigate(current, key) {
        if (current === libraryButton && key === Qt.Key_Down) {
            for (let i = 0; i < list.count; ++i) {
                if (list.model[i].game) { focusIdentity(focusKey(list.model[i].game)); return true }
            }
            return true
        }
        if (!current || current.homeRow === undefined || (key !== Qt.Key_Up && key !== Qt.Key_Down)) return false
        let next = current.homeRow + (key === Qt.Key_Down ? 1 : -1)
        while (next >= 0 && next < list.count && list.model[next].heading) next += key === Qt.Key_Down ? 1 : -1
        if (next < 0) { focusHome(); return true }
        if (next >= list.count) return true
        list.currentIndex = next
        list.positionViewAtIndex(next, ListView.Contain)
        list.forceLayout()
        const row = list.itemAtIndex(next)
        if (row) row.focusRow()
        return true
    }
    Rectangle { anchors.fill: parent; color: Theme.darkerBackground }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.couchMode ? 40 : 24
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            Text { text: "HOME"; color: Theme.brightForeground; font.family: Theme.fontFamily; font.pixelSize: 30 * root.scaleFactor }
            Item { Layout.fillWidth: true }
            GlassButton { id: libraryButton; objectName: "homeLibraryButton"; text: "LIBRARY"; onActiveFocusChanged: if (activeFocus) root.focusedIdentity = ""; onClicked: root.libraryRequested() }
            GlassButton { text: "SEARCH"; compact: true; onClicked: root.Window.window.openLibrarySearch() }
            GlassButton { text: "SETTINGS"; compact: true; onClicked: root.Window.window.diagnosticsOpen = true }
            GlassButton { text: root.couchMode ? "DESKTOP" : "COUCH"; compact: true; onClicked: root.Window.window.setCouchMode(!root.couchMode) }

        }
        Text {
            Layout.fillWidth: true
            text: "Pick up where you left off, or choose what to play next."
            color: Theme.mutedText; font.family: Theme.fontFamily; wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true; visible: Home.error !== "" || root.notice !== ""; text: Home.error || root.notice
            color: Theme.brightForeground; font.family: Theme.fontFamily; wrapMode: Text.Wrap
        }
        ListView {
            id: list
            objectName: "homeList"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; spacing: 8
            ScrollBar.vertical: ScrollBar { }
            model: {
                const rows = [{heading: "CONTINUE PLAYING"}]
                for (const game of Home.recent) rows.push({game: game, queued: false})
                if (!Home.recent.length) rows.push({heading: "Your recently played games will appear here."})
                rows.push({heading: "UP NEXT"})
                for (const game of Home.queue) rows.push({game: game, queued: true})
                if (!Home.queue.length) rows.push({heading: "Open a game's details and choose ADD TO UP NEXT."})
                return rows
            }
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                width: list.width
                height: modelData.heading ? 40 : 82 * root.scaleFactor
                readonly property var game: modelData.game || ({})
                function focusRow() { openButton.forceActiveFocus() }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    visible: !!row.modelData.heading; text: row.modelData.heading || ""
                    color: Theme.mutedText; font.family: Theme.fontFamily; wrapMode: Text.Wrap
                }
                RowLayout {
                    anchors.fill: parent
                    visible: !row.modelData.heading
                    spacing: 8
                    Rectangle {
                        Layout.preferredWidth: 48 * root.scaleFactor
                        Layout.preferredHeight: 72 * root.scaleFactor
                        radius: 4
                        color: row.game.accentStart || Theme.background
                        Text {
                            anchors.centerIn: parent
                            text: (row.game.title || "?").substring(0, 1)
                            color: Theme.brightForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: 26 * root.scaleFactor
                            visible: !cover.ready
                        }
                        CoverArtwork {
                            id: cover
                            anchors.fill: parent
                            source: row.game.coverPath || ""
                        }
                    }
                    GlassButton {
                        id: openButton
                        objectName: "homeOpen-" + row.index
                        property int homeRow: row.index
                        property Item controllerRightTarget: row.modelData.queued ? upButton : addButton
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        clip: true
                        text: (row.game.title || "Unavailable game") + (row.game.available ? "" : " · UNAVAILABLE")
                        Accessible.name: text
                        onActiveFocusChanged: if (activeFocus) root.focusedIdentity = root.focusKey(row.game)
                        onClicked: {
                            if (row.game.available) root.gameRequested(row.game)
                            else root.notice = "Reconnect the drive or enable this game's source in Settings."
                        }
                    }
                    GlassButton {
                        id: addButton
                        property Item controllerLeftTarget: openButton
                        property int homeRow: row.index
                        visible: !row.modelData.queued
                        compact: true
                        text: "+ NEXT"
                        onClicked: { root.queueAction(row.game, "add") }
                    }
                    GlassButton {
                        id: upButton
                        property Item controllerLeftTarget: openButton
                        property Item controllerRightTarget: downButton
                        property int homeRow: row.index
                        visible: !!row.modelData.queued; compact: true; text: "↑"; Accessible.name: "Move up"
                        onClicked: { root.queueAction(row.game, "up") }
                    }
                    GlassButton {
                        id: downButton
                        property Item controllerLeftTarget: upButton
                        property Item controllerRightTarget: removeButton
                        property int homeRow: row.index
                        visible: !!row.modelData.queued; compact: true; text: "↓"; Accessible.name: "Move down"
                        onClicked: { root.queueAction(row.game, "down") }
                    }
                    GlassButton {
                        id: removeButton
                        property Item controllerLeftTarget: downButton
                        property int homeRow: row.index
                        visible: !!row.modelData.queued; compact: true; text: "REMOVE"
                        onClicked: { root.queueAction(row.game, "remove") }
                    }
                }
            }
        }
    }
}
