import QtQuick
import OmaRaw.Ui

// Two photos side by side: the current one and its partner — the other
// selected photo, or the next one in the browser. Swap with the arrow.
Rectangle {
    id: root
    color: Theme.pasteboard
    property int leftId: backend.currentId
    property int rightId: backend.comparePartner()
    Connections { target: backend; function onSelectionChanged() { root.leftId = backend.currentId; root.rightId = backend.comparePartner() } }
    Row {
        anchors.fill: parent
        spacing: Theme.s2
        Repeater {
            model: [root.leftId, root.rightId]
            Rectangle {
                required property int index
                required property int modelData
                width: (root.width - Theme.s2) / 2; height: root.height
                color: Theme.pasteboard
                border.width: index === 0 ? Theme.selectionRing : Theme.hairline
                border.color: index === 0 ? Theme.accent : Theme.border
                LoupeView { anchors.fill: parent; anchors.margins: 2; assetId: parent.modelData }
                Rectangle {
                    anchors.top: parent.top; anchors.left: parent.left; anchors.margins: Theme.s3
                    width: cap.implicitWidth + Theme.s3; height: Theme.hControl
                    radius: Theme.rControl; color: Theme.scrim
                    Text {
                        id: cap
                        anchors.centerIn: parent
                        text: parent.parent.modelData ? backend.info(parent.parent.modelData).filename : qsTr("Select a second photo")
                        textFormat: Text.PlainText   // names and filenames are not markup
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
                    }
                }
                TapHandler { onTapped: if (parent.modelData) backend.select(parent.modelData, 0) }
            }
        }
    }
    EmptyState {
        anchors.centerIn: parent
        visible: root.leftId === 0
        iconName: "columns-2"
        title: qsTr("Nothing to compare")
        description: qsTr("Select two photos, or one and the next is used.")
    }
}
