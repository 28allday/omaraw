pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// The four workspaces, centred in the title bar, in the order a job moves
// through them: gather, develop, shoot, deliver.
Row {
    id: root
    readonly property var workspaces: ["Library", "Develop", "Capture", "Output"]
    readonly property var descriptions: [
        qsTr("Import, organise, rate and select photos."),
        qsTr("Adjust the current photo without changing its original file."),
        qsTr("Connect a camera for tethered shooting and live view."),
        qsTr("Prepare exports, contact sheets and prints.")
    ]
    property string current: "Library"
    signal activated(string name)
    spacing: 0
    Repeater {
        model: root.workspaces
        delegate: TabButton {
            required property string modelData
            required property int index
            text: modelData
            tip: root.descriptions[index]
            height: root.height
            checked: root.current === modelData
            fontSize: Theme.fsControl
            onClicked: root.activated(modelData)
        }
    }
}
