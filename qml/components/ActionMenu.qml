import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: menu
    required property var host
    required property Item anchorItem
    property string title: "ACTIONS"
    property Item initialFocus: null
    default property alias actions: actionColumn.data

    parent: Overlay.overlay
    width: Math.min(320 * (host.couchMode ? Math.max(1, Math.min(2.4, host.height / 900)) : 1), host.width - 48)
    height: Math.min(implicitHeight, host.height - 48)
    padding: 16
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // Close and restore the invoker before another editor captures its return focus.
    function invoke(action) {
        close()
        Qt.callLater(action)
    }

    onAboutToShow: {
        if (host.activeActionMenu && host.activeActionMenu !== menu)
            host.activeActionMenu.close()
        host.activeActionMenu = menu
        const position = anchorItem.mapToItem(Overlay.overlay, 0, anchorItem.height)
        x = Math.max(24, Math.min(host.width - width - 24, position.x))
        y = Math.max(24, Math.min(host.height - height - 24, position.y + 8))
    }
    onOpened: {
        // A reopened popup may restore its old child focus before this signal.
        // Start at the first enabled action, not the item after that old child.
        if (initialFocus && initialFocus.visible && initialFocus.enabled) {
            host.focusWithin(contentItem, true, initialFocus)
            return
        }
        for (const action of actionColumn.children) {
            if (action.visible && action.enabled && action.activeFocusOnTab) {
                host.focusWithin(contentItem, true, action)
                return
            }
        }
        host.focusWithin(contentItem, true, closeButton)
    }
    onClosed: {
        if (host.activeActionMenu === menu)
            host.activeActionMenu = null
        if (anchorItem.visible && anchorItem.enabled)
            anchorItem.forceActiveFocus(Qt.TabFocusReason)
    }

    background: Rectangle {
        color: Theme.background
        radius: Math.max(8, Theme.cornerRadius)
        border.color: Theme.mutedText
    }
    contentItem: ScrollView {
        // Modal popups block the window shortcuts. Handle navigation inside
        // the popup so physical keyboard input follows the controller path.
        Keys.priority: Keys.BeforeItem
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                host.focusWithin(menu.contentItem,
                    event.key !== Qt.Key_Backtab && !(event.modifiers & Qt.ShiftModifier))
                event.accepted = true
            } else {
                host.handleArrowKey(menu.contentItem, event)
            }
        }
        implicitHeight: menuColumn.implicitHeight
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            id: menuColumn
            width: parent.width
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: menu.title
                color: Theme.mutedText
                font.family: Theme.fontFamily
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
            ColumnLayout {
                id: actionColumn
                Layout.fillWidth: true
                spacing: 6
            }
            GlassButton {
                id: closeButton
                objectName: "actionMenuCloseButton"
                Layout.fillWidth: true
                text: "CLOSE"
                compact: true
                onClicked: menu.close()
            }
        }
    }
}
