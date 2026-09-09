import QtQuick

QtObject {
    id: root
    property var request: ({})
    property bool pending: false
    property bool failed: false
    property string message: ""
    signal dispatchRequested(var request)

    function begin(value) {
        if (pending) return false
        // Selection can change before dispatch. Keep the installation the user chose.
        request = JSON.parse(JSON.stringify(value))
        failed = false
        message = "Opening " + request.title + "..."
        pending = true
        dispatch.start()
        return true
    }

    function finish(success, text) {
        dispatch.stop()
        cooldown.stop()
        pending = success
        failed = !success
        message = text
        if (success) cooldown.start()
        else pending = false
    }

    // Give the pressed button and status text a frame to appear before starting
    // the external launcher. Repeated keyboard/controller presses are ignored.
    property Timer dispatch: Timer {
        interval: 50
        onTriggered: root.dispatchRequested(root.request)
    }
    property Timer cooldown: Timer {
        interval: 2000
        onTriggered: { root.pending = false; root.message = "" }
    }
}
