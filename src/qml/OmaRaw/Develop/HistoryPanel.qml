pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as C
import OmaRaw.Ui

// The engine's history stack, oldest first. Click an entry to go back to
// it; the next change truncates what follows, as in every RAW developer.
Column {
    id: root
    width: parent ? parent.width : 260
    spacing: 0
    property var dockPanel: null
    readonly property real listHeight: steps.height
    property real maximumListHeight: Theme.hRow * 10
    readonly property bool aiVersion: backend.aiVersions.some(v => v.current && v.operation !== "original")
    DockHeader {
        dockPanel: root.dockPanel
        width: parent.width
        tabs: [qsTr("History")]
        helpSection: "develop"
        trailing: [
            IconButton { iconName: "rotate-ccw"; text: root.aiVersion ? qsTr("Reset this version") : qsTr("Back to original"); tip: root.aiVersion ? qsTr("Resets edits and clears development history on this AI version. This reset cannot be undone. Use Photo versions to open the source photo; both versions are kept.") : qsTr("Restores the camera original and clears the development history, including redo steps. This reset cannot be undone."); enabled: engine.history.length > 0; onClicked: engine.resetToOriginal() }
        ]
    }
    Rectangle {
        width: parent.width; height: Theme.hRow
        color: engine.historyEnd === 0 ? Theme.controlBg : "transparent"
        Text {
            anchors.left: parent.left; anchors.leftMargin: Theme.s3
            anchors.verticalCenter: parent.verticalCenter
            text: root.aiVersion ? qsTr("Version starting point") : qsTr("Original")
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
            color: engine.historyEnd === 0 ? Theme.textPrimary : Theme.textSecondary
        }
        TapHandler { onTapped: engine.jumpHistory(0) }
    }
    // Ten steps by default; the dock divider can change the height. Keep the
    // current step visible when the size or history changes.
    ListView {
        id: steps
        objectName: "historySteps"
        // Shows there is more above or below once the list is capped.
        C.ScrollBar.vertical: ScrollBar { id: listBar; policy: steps.contentHeight > steps.height ? C.ScrollBar.AlwaysOn : C.ScrollBar.AlwaysOff }
        width: parent.width
        height: Math.min(count * Theme.hRow, root.maximumListHeight)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: engine.history
        currentIndex: engine.historyEnd - 1
        onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
        onHeightChanged: if (currentIndex >= 0) Qt.callLater(() => positionViewAtIndex(currentIndex, ListView.Contain))
        onCountChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
        delegate: Rectangle {
            id: row
            required property int index
            required property var modelData
            width: ListView.view.width - (listBar.visible ? Theme.s2 : 0); height: Theme.hRow
            readonly property bool active: index + 1 === engine.historyEnd
            Accessible.role: Accessible.ListItem
            Accessible.name: Names.history(modelData.op, modelData.label) + (active ? qsTr(", current step") : "")
            Accessible.onPressAction: engine.jumpHistory(index + 1)
            readonly property bool undone: index + 1 > engine.historyEnd
            color: active ? Theme.controlBg : "transparent"
            opacity: undone ? 0.5 : 1
            Text {
                anchors.left: parent.left; anchors.leftMargin: Theme.s3
                anchors.right: num.left
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: Names.history(row.modelData.op, row.modelData.label) + (row.modelData.enabled ? "" : qsTr(" (off)"))
                elide: Text.ElideRight
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
                color: row.active ? Theme.textPrimary : Theme.textSecondary
            }
            Text {
                id: num
                anchors.right: parent.right; anchors.rightMargin: Theme.s3
                anchors.verticalCenter: parent.verticalCenter
                text: row.index + 1
                font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
            TapHandler { onTapped: engine.jumpHistory(row.index + 1) }
        }
    }
    Text {
        visible: engine.history.length === 0
        x: Theme.s3; topPadding: Theme.s2
        text: engine.imageId >= 0 ? qsTr("No edits yet.") : qsTr("No image in the engine.")
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
    }
}
