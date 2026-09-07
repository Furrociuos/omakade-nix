pragma Singleton

import QtQuick

// One answer to "does typing here need help from the app". Every field used to decide this for
// itself, and each one wrote the same couch mode test by hand. When the rule changed to follow
// the controller instead of the mode, the two fields that went through a shared function picked
// it up and the seven that tested couch mode inline did not, so a keyboard appeared on some
// fields and not others. There is one place to change it now.
QtObject {
    id: root

    // Set once from the window; every field reads the answer rather than the inputs.
    property bool couchMode: false

    // Couch mode always needs it. On a desktop it depends on whether the controller is the thing
    // being used, so a pad plugged in for gaming does not make a keyboard appear on a mouse
    // click, while a pad used to reach a field does get one.
    readonly property bool keyboardNeeded:
        root.couchMode || (typeof Controller !== "undefined" && Controller !== null
                           && Controller.driving)
}
