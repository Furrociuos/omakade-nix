import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Rectangle {
    id: root
    objectName: "libraryRepairPanel"
    color: Theme.background
    signal dismissed()
    signal openGame(string kind)
    property var service: typeof LibraryRepair !== "undefined" ? LibraryRepair : null
    readonly property real uiScale: root.Window.window && root.Window.window.couchMode ? 1.35 : 1
    readonly property var entries: root.service ? root.service.entries : []
    property var game: root.service ? root.service.current : ({})
    readonly property int currentPosition: {
        if (!root.game.metadataKey) return 0
        for (let index = 0; index < root.entries.length; ++index)
            if (root.entries[index].metadataKey === root.game.metadataKey) return index + 1
        return 0
    }
    readonly property bool metadataRetryAvailable: {
        for (const entry of root.entries)
            if ((entry.reasons || []).includes("identification") || (entry.reasons || []).includes("artwork"))
                return true
        return false
    }
    function reveal(item) { root.Window.window.revealInScrollView(reviewScroll, item) }
    function focusEditor() { closeButton.forceActiveFocus() }
    component RepairButton: GlassButton {
        id: repairButton
        displayScale: root.uiScale
        onActiveFocusChanged: if (activeFocus) root.reveal(repairButton)
    }
    function actionForReasonRow(start, direction) {
        for (let index = start; index >= 0 && index < reasonItems.count; index += direction) {
            const row = reasonItems.itemAt(index)
            const target = row ? direction < 0 ? row.lastAction : row.firstAction : null
            if (target) return target
        }
        return null
    }
    MouseArea { anchors.fill: parent }
    ScrollView {
        id: reviewScroll
        anchors.centerIn: parent
        width: Math.max(1, Math.min(parent.width - 64, 1100 * root.uiScale))
        height: parent.height - 64
        contentWidth: availableWidth
        ColumnLayout {
            width: reviewScroll.availableWidth
            spacing: 16
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "REPAIR LIBRARY"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 26 * root.uiScale
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
                Text {
                    objectName: "libraryRepairProgress"
                    text: root.currentPosition + " OF " + root.entries.length
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 12 * root.uiScale
                }
                RepairButton {
                    id: closeButton
                    objectName: "libraryRepairCloseButton"
                    text: "CLOSE"
                    displayScale: root.uiScale
                    property Item controllerDownTarget: sourceFilter
                    onClicked: root.dismissed()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                ThemedComboBox {
                    id: sourceFilter
                    objectName: "libraryRepairSourceFilter"
                    uiScale: root.uiScale
                    font.pixelSize: 13 * root.uiScale
                    implicitHeight: 40 * root.uiScale
                    Layout.fillWidth: true
                    model: ["All sources"].concat(root.service ? root.service.sources : [])
                    currentIndex: Math.max(0, model.indexOf(root.service && root.service.source
                                                               ? root.service.source : "All sources"))
                    Accessible.name: "Filter repair games by source"
                    onActiveFocusChanged: if (activeFocus) root.reveal(sourceFilter)
                    property Item controllerUpTarget: closeButton
                    property Item controllerRightTarget: reasonFilter
                    property Item controllerDownTarget: root.actionForReasonRow(0, 1) || previousButton
                    onActivated: root.service.source = currentIndex ? currentText : ""
                }
                ThemedComboBox {
                    id: reasonFilter
                    objectName: "libraryRepairReasonFilter"
                    uiScale: root.uiScale
                    font.pixelSize: 13 * root.uiScale
                    implicitHeight: 40 * root.uiScale
                    Layout.fillWidth: true
                    property var values: ["", "identification", "artwork", "missing-file",
                                          "missing-storage", "runtime", "source-error",
                                          "unavailable", "duplicates"]
                    model: ["All reasons", "Needs identification", "Missing artwork",
                            "Game file moved or missing", "Drive or folder disconnected",
                            "Emulator or core unavailable", "Source scan failed", "Unavailable",
                            "Duplicate suggestions"]
                    currentIndex: Math.max(0, values.indexOf(root.service ? root.service.reason : ""))
                    Accessible.name: "Filter repair games by reason"
                    onActiveFocusChanged: if (activeFocus) root.reveal(reasonFilter)
                    property Item controllerLeftTarget: sourceFilter
                    property Item controllerUpTarget: closeButton
                    property Item controllerDownTarget: root.actionForReasonRow(0, 1) || previousButton
                    onActivated: root.service.reason = values[currentIndex]
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 18 * root.uiScale
                Rectangle {
                    Layout.preferredWidth: 160 * root.uiScale
                    Layout.preferredHeight: 240 * root.uiScale
                    radius: 8 * root.uiScale
                    color: Theme.darkerBackground
                    border.color: Qt.alpha(Theme.foreground, 0.14)
                    CoverArtwork {
                        anchors.fill: parent
                        anchors.margins: 3 * root.uiScale
                        source: root.game.coverPath || ""
                        visible: source.toString().length > 0
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: !root.game.coverPath
                        text: "NO COVER"
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 11 * root.uiScale
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 12 * root.uiScale
                    Text {
                        objectName: "libraryRepairGameTitle"
                        Layout.fillWidth: true
                        text: root.game.title || "Nothing left to review in these filters"
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: 24 * root.uiScale
                        font.weight: Font.DemiBold
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: !!root.game.title
                        text: (root.game.source || "Unknown source") + "  ·  "
                              + (root.game.system || "Unknown platform")
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 12 * root.uiScale
                        elide: Text.ElideRight
                    }
                    ColumnLayout {
                        id: reasonRows
                        objectName: "libraryRepairReasonRows"
                        Layout.fillWidth: true
                        spacing: 10 * root.uiScale
                        Repeater {
                            id: reasonItems
                            model: root.game.reasonDetails || []
                            FocusScope {
                                id: reasonRow
                                required property int index
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: reasonRowLayout.implicitHeight
                                readonly property string reasonKey: modelData.key || ""
                                readonly property Item firstAction:
                                    reasonKey === "identification" ? correctIdentityButton
                                    : reasonKey === "artwork" ? chooseArtworkButton
                                    : reasonKey === "duplicates" ? linkButton
                                    : reasonKey === "missing-storage" ? recheckButton
                                    : reasonKey === "runtime" ? launchSetupButton
                                    : reasonKey === "source-error" ? retrySourceButton : null
                                readonly property Item lastAction:
                                    reasonKey === "identification" && undoIdentityButton.visible ? undoIdentityButton
                                    : reasonKey === "artwork" && undoArtworkButton.visible ? undoArtworkButton
                                    : firstAction
                                RowLayout {
                                    id: reasonRowLayout
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    spacing: 12 * root.uiScale
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 3 * root.uiScale
                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.label || reasonRow.reasonKey
                                            color: Theme.brightForeground
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 12 * root.uiScale
                                            font.weight: Font.DemiBold
                                            wrapMode: Text.Wrap
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.detail || ""
                                            color: Theme.mutedText
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 11 * root.uiScale
                                            elide: Text.ElideMiddle
                                            wrapMode: Text.NoWrap
                                            visible: text.length > 0
                                        }
                                    }
                                    Flow {
                                        Layout.preferredWidth: Math.min(390 * root.uiScale, implicitWidth)
                                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                        spacing: 8 * root.uiScale
                                        RepairButton {
                                            id: correctIdentityButton
                                            objectName: visible ? "libraryRepairCorrectIdentityButton" : ""
                                            visible: reasonRow.reasonKey === "identification"
                                            text: "CORRECT IDENTITY"
                                            displayScale: root.uiScale
                                            enabled: !!root.game.title && !Metadata.busy
                                            property Item controllerRightTarget:
                                                undoIdentityButton.visible ? undoIdentityButton : null
                                            property Item controllerUpTarget:
                                                root.actionForReasonRow(reasonRow.index - 1, -1) || reasonFilter
                                            property Item controllerDownTarget:
                                                root.actionForReasonRow(reasonRow.index + 1, 1) || previousButton
                                            KeyNavigation.right: controllerRightTarget
                                            KeyNavigation.up: controllerUpTarget
                                            KeyNavigation.down: controllerDownTarget
                                            onClicked: root.openGame("identity")
                                        }
                                        RepairButton {
                                            id: undoIdentityButton
                                            objectName: visible ? "libraryRepairUndoIdentityButton" : ""
                                            visible: reasonRow.reasonKey === "identification" && !!root.game.undoIdentity
                                            text: "UNDO IDENTITY"
                                            displayScale: root.uiScale
                                            property Item controllerLeftTarget: correctIdentityButton
                                            property Item controllerUpTarget:
                                                root.actionForReasonRow(reasonRow.index - 1, -1) || reasonFilter
                                            property Item controllerDownTarget:
                                                root.actionForReasonRow(reasonRow.index + 1, 1) || previousButton
                                            KeyNavigation.left: correctIdentityButton
                                            KeyNavigation.up: controllerUpTarget
                                            KeyNavigation.down: controllerDownTarget
                                            onClicked: root.service.undo("identity")
                                        }
                                        RepairButton {
                                            id: chooseArtworkButton
                                            objectName: visible ? "libraryRepairChooseArtworkButton" : ""
                                            visible: reasonRow.reasonKey === "artwork"
                                            text: "CHOOSE ARTWORK"
                                            displayScale: root.uiScale
                                            enabled: !!root.game.title && !Metadata.busy
                                            property Item controllerRightTarget:
                                                undoArtworkButton.visible ? undoArtworkButton : null
                                            property Item controllerUpTarget:
                                                root.actionForReasonRow(reasonRow.index - 1, -1) || reasonFilter
                                            property Item controllerDownTarget:
                                                root.actionForReasonRow(reasonRow.index + 1, 1) || previousButton
                                            KeyNavigation.right: controllerRightTarget
                                            KeyNavigation.up: controllerUpTarget
                                            KeyNavigation.down: controllerDownTarget
                                            onClicked: root.openGame("artwork")
                                        }
                                        RepairButton {
                                            id: undoArtworkButton
                                            objectName: visible ? "libraryRepairUndoArtworkButton" : ""
                                            visible: reasonRow.reasonKey === "artwork" && !!root.game.undoArtwork
                                            text: "UNDO ARTWORK"
                                            displayScale: root.uiScale
                                            property Item controllerLeftTarget: chooseArtworkButton
                                            property Item controllerUpTarget:
                                                root.actionForReasonRow(reasonRow.index - 1, -1) || reasonFilter
                                            property Item controllerDownTarget:
                                                root.actionForReasonRow(reasonRow.index + 1, 1) || previousButton
                                            KeyNavigation.left: chooseArtworkButton
                                            KeyNavigation.up: controllerUpTarget
                                            KeyNavigation.down: controllerDownTarget
                                            onClicked: root.service.undo("artwork")
                                        }
                                        RepairButton {
                                            id: linkButton
                                            objectName: visible ? "libraryRepairLinkButton" : ""
                                            visible: reasonRow.reasonKey === "duplicates"
                                            text: "LAUNCH SETUP / LINK"
                                            displayScale: root.uiScale
                                            enabled: !!root.game.title
                                            property Item controllerUpTarget:
                                                root.actionForReasonRow(reasonRow.index - 1, -1) || reasonFilter
                                            property Item controllerDownTarget:
                                                root.actionForReasonRow(reasonRow.index + 1, 1) || previousButton
                                            KeyNavigation.up: controllerUpTarget
                                            KeyNavigation.down: controllerDownTarget
                                            onClicked: root.openGame("")
                                        }
                                        RepairButton {
                                            id: recheckButton
                                            objectName: visible ? "libraryRepairRecheckButton" : ""
                                            visible: reasonRow.reasonKey === "missing-storage"
                                            text: "RECHECK"
                                            displayScale: root.uiScale
                                            property Item controllerUpTarget:
                                                root.actionForReasonRow(reasonRow.index - 1, -1) || reasonFilter
                                            property Item controllerDownTarget:
                                                root.actionForReasonRow(reasonRow.index + 1, 1) || previousButton
                                            KeyNavigation.up: controllerUpTarget
                                            KeyNavigation.down: controllerDownTarget
                                            onClicked: root.service.recheck()
                                        }
                                        RepairButton {
                                            id: launchSetupButton
                                            objectName: visible ? "libraryRepairLaunchSetupButton" : ""
                                            visible: reasonRow.reasonKey === "runtime"
                                            text: "LAUNCH SETUP"
                                            displayScale: root.uiScale
                                            enabled: !!root.game.title
                                            property Item controllerUpTarget:
                                                root.actionForReasonRow(reasonRow.index - 1, -1) || reasonFilter
                                            property Item controllerDownTarget:
                                                root.actionForReasonRow(reasonRow.index + 1, 1) || previousButton
                                            KeyNavigation.up: controllerUpTarget
                                            KeyNavigation.down: controllerDownTarget
                                            onClicked: root.openGame("")
                                        }
                                        RepairButton {
                                            id: retrySourceButton
                                            objectName: visible ? "libraryRepairRetrySourceButton" : ""
                                            visible: reasonRow.reasonKey === "source-error"
                                            text: "RETRY SOURCE"
                                            displayScale: root.uiScale
                                            property Item controllerUpTarget:
                                                root.actionForReasonRow(reasonRow.index - 1, -1) || reasonFilter
                                            property Item controllerDownTarget:
                                                root.actionForReasonRow(reasonRow.index + 1, 1) || previousButton
                                            KeyNavigation.up: controllerUpTarget
                                            KeyNavigation.down: controllerDownTarget
                                            onClicked: root.service.retrySource()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                RepairButton {
                    id: previousButton
                    objectName: "libraryRepairPreviousButton"
                    text: "PREVIOUS"
                    displayScale: root.uiScale
                    enabled: !!root.game.title
                    property Item controllerRightTarget: nextButton
                    property Item controllerUpTarget:
                        root.actionForReasonRow(reasonItems.count - 1, -1) || reasonFilter
                    property Item controllerDownTarget:
                        root.metadataRetryAvailable ? retryThisGameButton : closeButton
                    KeyNavigation.right: nextButton
                    KeyNavigation.up: controllerUpTarget
                    KeyNavigation.down: controllerDownTarget
                    onClicked: root.service.move(-1)
                }
                RepairButton {
                    id: nextButton
                    objectName: "libraryRepairNextButton"
                    text: "NEXT"
                    displayScale: root.uiScale
                    enabled: !!root.game.title
                    property Item controllerLeftTarget: previousButton
                    property Item controllerRightTarget:
                        root.metadataRetryAvailable ? retryThisGameButton : closeButton
                    property Item controllerUpTarget:
                        root.actionForReasonRow(reasonItems.count - 1, -1) || reasonFilter
                    property Item controllerDownTarget:
                        root.metadataRetryAvailable ? retryThisGameButton : closeButton
                    KeyNavigation.left: previousButton
                    KeyNavigation.up: controllerUpTarget
                    KeyNavigation.down: controllerDownTarget
                    onClicked: root.service.move(1)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: root.metadataRetryAvailable
                RepairButton {
                    id: retryThisGameButton
                    objectName: "libraryRepairRetryThisGameButton"
                    text: "RETRY THIS GAME"
                    displayScale: root.uiScale
                    enabled: !!root.game.title && (root.game.reasons || []).some(reason =>
                        reason === "identification" || reason === "artwork") && !Metadata.busy
                    property Item controllerRightTarget: selectForRetryButton
                    property Item controllerUpTarget: nextButton
                    KeyNavigation.right: selectForRetryButton
                    KeyNavigation.up: nextButton
                    onClicked: root.service.retry(false)
                }
                RepairButton {
                    id: selectForRetryButton
                    objectName: "libraryRepairSelectForRetryButton"
                    text: root.game.selected ? "REMOVE FROM RETRY" : "SELECT FOR RETRY"
                    displayScale: root.uiScale
                    enabled: !!root.game.title && (root.game.reasons || []).some(reason =>
                        reason === "identification" || reason === "artwork")
                    property Item controllerLeftTarget: retryThisGameButton
                    property Item controllerRightTarget: retrySelectedButton
                    property Item controllerUpTarget: nextButton
                    KeyNavigation.left: retryThisGameButton
                    KeyNavigation.right: retrySelectedButton
                    KeyNavigation.up: nextButton
                    onClicked: root.service.toggleSelected()
                }
                RepairButton {
                    id: retrySelectedButton
                    objectName: "libraryRepairRetrySelectedButton"
                    text: "RETRY SELECTED (" + (root.service ? root.service.selectedTitles.length : 0) + ")"
                    displayScale: root.uiScale
                    enabled: root.service && root.service.selectedTitles.length > 0 && !Metadata.busy
                    property Item controllerLeftTarget: selectForRetryButton
                    property Item controllerRightTarget: stopRetryButton.enabled ? stopRetryButton : null
                    property Item controllerUpTarget: nextButton
                    KeyNavigation.left: selectForRetryButton
                    KeyNavigation.right: controllerRightTarget
                    KeyNavigation.up: nextButton
                    onClicked: root.service.retrySelected()
                }
                RepairButton {
                    id: stopRetryButton
                    objectName: "libraryRepairStopRetryButton"
                    text: "STOP RETRY"
                    displayScale: root.uiScale
                    enabled: Metadata.busy
                    property Item controllerLeftTarget: retrySelectedButton
                    property Item controllerUpTarget: nextButton
                    KeyNavigation.left: retrySelectedButton
                    KeyNavigation.up: nextButton
                    onClicked: Metadata.cancel()
                }
            }
            Text {
                objectName: "libraryRepairSelectionStatus"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12 * root.uiScale
                color: Theme.mutedText
                visible: root.service && root.service.selectedTitles.length > 0
                text: visible ? "Selected for retry: " + root.service.selectedTitles.join(", ") : ""
            }
            Text {
                objectName: "libraryRepairMessage"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12 * root.uiScale
                color: Theme.mutedText
                text: root.service ? root.service.message : ""
            }
            Text {
                objectName: "libraryRepairMetadataStatus"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12 * root.uiScale
                color: Theme.mutedText
                text: Metadata.status + (Metadata.busy ? " • " + Metadata.pending + " pending" : "")
            }
        }
    }
}
