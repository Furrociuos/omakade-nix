pragma Singleton
import QtQuick

QtObject {
    // Shared type and spacing roles. Screens multiply these by their couch scale.
    readonly property int body: 14
    readonly property int supporting: 12
    readonly property int label: 12
    readonly property int section: 18
    readonly property int heading: 24
    readonly property int controlHeight: 42
    readonly property int gap: 12
    readonly property int sectionGap: 22
}
