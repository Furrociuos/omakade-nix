import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var entry: Metadata ? Metadata.current : ({})
    required property var game
    property bool couchMode: false
    property real uiScale: 1
    property bool editing: false
    property bool panelMode: false
    signal textEntryRequested(var target, string title, bool password, string placeholder)
    // The details page navigates by an explicit controller chain, and a section left out of it
    // is unreachable however plainly it is on screen: arrow keys follow the chain in preference
    // to the geometry. This section was missing from it entirely, so pressing down off the
    // collections row jumped straight past it to the achievements. These name the way in and the
    // way out; the page wires them to whatever sits either side.
    property Item previousSection: null
    property Item nextSection: null
    readonly property Item firstControl: artworkButton
    readonly property Item lastControl: !root.editing ? artworkButton
                                      : coverSearchButton.visible && coverSearchButton.enabled
                                        ? coverSearchButton
                                        : artworkButton
    Layout.fillWidth: true
    spacing: 10
    visible: Metadata !== null && !game.isPortal
    readonly property string gameKey: game.metadataKey || ""
    onGameKeyChanged: { editing = false; if (Metadata) Metadata.inspect(game) }
    Component.onCompleted: if (Metadata) Metadata.inspect(game)
    // Identifying a game by hand takes precedence over the background pass, which would
    // otherwise hold the service busy and leave every control here disabled.
    onEditingChanged: if (Metadata) Metadata.setEditing(editing)
    Component.onDestruction: if (Metadata) Metadata.setEditing(false)
    Connections {
        target: Metadata
        function onPortraitSelected(key) {
            if (key !== root.gameKey) return
            root.editing = false
            if (!root.panelMode) artworkButton.forceActiveFocus()
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Text { Layout.fillWidth: true; text: root.panelMode ? "MATCH & PORTRAIT" : "RATING & COVER ART"; color: Theme.brightForeground; font.family: Theme.fontFamily; font.pixelSize: 12 * root.uiScale }
        GlassButton {
            id: artworkButton
            objectName: "metadataArtworkButton"
            compact: true
            text: root.editing ? "DONE" : "IDENTIFY / ARTWORK"
            property Item controllerUpTarget: root.previousSection
            // Expanded, down goes into the section rather than past it. Collapsed, there is
            // nothing inside to reach, so it goes on to whatever follows.
            property Item controllerDownTarget: root.editing ? identifyButton : root.nextSection
            onClicked: root.editing = !root.editing
        }
    }
    Text {
        Layout.fillWidth: true
        text: !Metadata ? "" : root.entry.rating >= 0
              ? "IGDB  " + root.entry.rating + " / 100 · " + root.entry.ratingCount + " ratings"
              : "No rating available"
        color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: 12 * root.uiScale
    }
    ColumnLayout {
        Layout.fillWidth: true; spacing: 10; visible: root.editing
        Text {
            Layout.fillWidth: true; wrapMode: Text.Wrap
            text: Metadata ? (root.entry.title || root.game.title) + (root.entry.year ? " (" + root.entry.year + ")" : "") + " · " + (root.entry.matchStatus || "Not identified") : ""
            color: Theme.mutedText; font.family: Theme.fontFamily; font.pixelSize: 11 * root.uiScale
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: titleSearch; objectName: "metadataTitleField"; Layout.fillWidth: true; text: root.game.title || ""
                placeholderTextColor: Theme.mutedText
                    background: Rectangle {
                        radius: Math.max(5, Theme.cornerRadius)
                        color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.045)
                        border.width: titleSearch.activeFocus ? 2 : 1
                        border.color: titleSearch.activeFocus ? Theme.accent : Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.12)
                    }
                    property bool controllerNavigation: root.couchMode || (Controller !== null && Controller.driving)
                Accessible.name: "Game title for identification"
                color: Theme.foreground; font.family: Theme.fontFamily
                Keys.onReturnPressed: event => { if (TextEntry.keyboardNeeded) { root.textEntryRequested(titleSearch, "GAME TITLE", false, "Search title"); event.accepted = true } else Metadata.search(text) }
                Keys.onEnterPressed: event => { if (TextEntry.keyboardNeeded) { root.textEntryRequested(titleSearch, "GAME TITLE", false, "Search title"); event.accepted = true } else Metadata.search(text) }
            
                rightPadding: titleSearchClear.visible ? titleSearchClear.reservedWidth : 12
                property Item controllerRightTarget: titleSearchClear.visible ? titleSearchClear : null
                FieldClearButton { id: titleSearchClear; field: titleSearch }
            }
            GlassButton {
                id: identifyButton
                objectName: "metadataIdentifyButton"
                compact: true
                text: "SEARCH IGDB"
                property Item controllerUpTarget: artworkButton
                property Item controllerDownTarget: rejectButton
                enabled: Metadata && !Metadata.busy && Insights && Insights.configured
                onClicked: Metadata.search(titleSearch.text)
            }
        }
        Flow {
            Layout.fillWidth: true; spacing: 8
            GlassButton {
                id: rejectButton
                objectName: "metadataRejectButton"
                compact: true
                text: "NOT THIS GAME"
                property Item controllerUpTarget: identifyButton
                property Item controllerDownTarget: coverSearchButton
                property Item controllerRightTarget: choosePortraitButton
                enabled: Metadata && !Metadata.busy
                onClicked: Metadata.rejectMatch()
            }
            GlassButton {
                id: choosePortraitButton
                objectName: "metadataChoosePortraitButton"
                compact: true
                text: "CHOOSE PORTRAIT"
                property Item controllerUpTarget: identifyButton
                property Item controllerDownTarget: coverSearchButton
                property Item controllerLeftTarget: rejectButton
                property Item controllerRightTarget: clearCoverButton
                enabled: Metadata && Metadata.hasGridKey && !Metadata.busy
                onClicked: Metadata.findCovers()
            }
            GlassButton {
                id: clearCoverButton
                objectName: "metadataClearCoverButton"
                compact: true
                text: "CLEAR COVER"
                property Item controllerUpTarget: identifyButton
                property Item controllerDownTarget: coverSearchButton
                property Item controllerLeftTarget: choosePortraitButton
                enabled: Metadata && Metadata.hasGridKey && !Metadata.busy
                onClicked: Metadata.clearGridSelection()
            }
        }
        // The two catalogues do not always agree on a name: SteamGridDB files Dragon Quest V
        // under Hand of the Heavenly Bride while IGDB gives its Japanese title, and nothing
        // automatic bridges that. The name to search for can be typed here instead.
        RowLayout {
            Layout.fillWidth: true
            visible: Metadata && Metadata.hasGridKey
            TextField {
                id: coverSearch; objectName: "metadataCoverField"; Layout.fillWidth: true; text: root.game.title || ""
                placeholderText: "Search SteamGridDB by name"
                placeholderTextColor: Theme.mutedText
                background: Rectangle {
                    radius: Math.max(5, Theme.cornerRadius)
                    color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.045)
                    border.width: coverSearch.activeFocus ? 2 : 1
                    border.color: coverSearch.activeFocus ? Theme.accent : Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.12)
                }
                property bool controllerNavigation: root.couchMode || (Controller !== null && Controller.driving)
                Accessible.name: "Cover art search"
                color: Theme.foreground; font.family: Theme.fontFamily
                Keys.onReturnPressed: event => { if (TextEntry.keyboardNeeded) { root.textEntryRequested(coverSearch, "COVER SEARCH", false, "Search cover art"); event.accepted = true } else Metadata.searchCovers(text) }
                Keys.onEnterPressed: event => { if (TextEntry.keyboardNeeded) { root.textEntryRequested(coverSearch, "COVER SEARCH", false, "Search cover art"); event.accepted = true } else Metadata.searchCovers(text) }
            
                rightPadding: coverSearchClear.visible ? coverSearchClear.reservedWidth : 12
                property Item controllerRightTarget: coverSearchClear.visible ? coverSearchClear : null
                FieldClearButton { id: coverSearchClear; field: coverSearch }
            }
            GlassButton {
                id: coverSearchButton
                objectName: "metadataCoverSearchButton"
                compact: true
                text: "SEARCH COVERS"
                // The last control in the section, so this is where the controller leaves it.
                property Item controllerUpTarget: rejectButton
                property Item controllerDownTarget: root.nextSection
                enabled: Metadata && Metadata.hasGridKey && !Metadata.busy
                onClicked: Metadata.searchCovers(coverSearch.text)
            }
        }
        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: Metadata ? Metadata.status : ""; color: Theme.mutedText; font.family: Theme.fontFamily; font.pixelSize: 10 * root.uiScale }
        Repeater {
            model: Metadata ? Metadata.candidates : []
            GlassButton {
                required property var modelData
                required property int index
                Layout.fillWidth: true; compact: true
                text: modelData.title + (modelData.year ? " · " + modelData.year : "")
                      + (modelData.edition ? " · " + modelData.edition : "")
                      + (modelData.releaseRegions && modelData.releaseRegions.length
                         ? " · " + modelData.releaseRegions.join(", ") : "")
                      + (modelData.id ? " · ID " + modelData.id : "")
                enabled: Metadata && !Metadata.busy
                onClicked: { Metadata.chooseMatch(index); Metadata.chooseGridGame(index) }
            }
        }
        Flow {
            Layout.fillWidth: true; spacing: 12
            Repeater {
                model: Metadata ? Metadata.covers : []
                Column {
                    required property var modelData
                    required property int index
                    spacing: 6; width: 120 * root.uiScale
                    Image { width: parent.width; height: width * 1.5; source: modelData.url; asynchronous: true; fillMode: Image.PreserveAspectFit; sourceSize.width: 180 }
                    GlassButton { width: parent.width; compact: true; text: "USE COVER"; enabled: Metadata && !Metadata.busy; onClicked: Metadata.chooseCover(index) }
                }
            }
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.Wrap
            text: "Ratings from IGDB. Portraits from SteamGridDB. Your custom cover always takes priority. Connections are managed in Settings."
            color: Theme.mutedText; font.family: Theme.fontFamily; font.pixelSize: 10 * root.uiScale
        }
    }
}
