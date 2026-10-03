pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// A readable key on the viewer, independent of the inspector and its graph.
// Swatches use the mapper's own band colours, including in Colour Critical.
Rectangle {
    id: root
    objectName: "falseColourGuide"
    property var bands: []
    property bool active: false
    property bool expanded: true
    onActiveChanged: if (active) expanded = true
    implicitWidth: Theme.s5 * 12
    implicitHeight: content.implicitHeight + Theme.s2 * 2
    color: Theme.panelBg
    border.color: Theme.borderStrong
    border.width: Theme.hairline
    radius: Theme.rControl

    // Reading or folding the key must not paint a mask or pan the photo.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }

    Column {
        id: content
        x: Theme.s3; y: Theme.s2
        width: parent.width - Theme.s3 * 2
        spacing: Theme.s1
        Row {
            width: parent.width
            Text {
                width: parent.width - fold.width
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("False colour guide")
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading
                color: Theme.textPrimary
            }
            IconButton {
                id: fold
                objectName: "falseColourGuideToggle"
                iconName: root.expanded ? "chevron-down" : "chevron-up"
                text: root.expanded ? qsTr("Collapse colour guide") : qsTr("Expand colour guide")
                onClicked: root.expanded = !root.expanded
            }
        }
        Text {
            width: parent.width
            visible: root.expanded
            text: qsTr("Colours show how bright each area is.")
            wrapMode: Text.WordWrap
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
        }
        Column {
            width: parent.width
            visible: root.expanded
            spacing: Theme.s1
            Repeater {
                model: [
                    { band: 0, label: qsTr("Purple · Clipped shadows") },
                    { band: 1, label: qsTr("Blue · Deep shadows") },
                    { band: 2, label: qsTr("Teal · Shadows") },
                    { band: 4, label: qsTr("Green · Middle grey") },
                    { band: 6, label: qsTr("Pink · Light skin reference") },
                    { band: 8, label: qsTr("Yellow · Highlights") },
                    { band: 9, label: qsTr("Orange · Near white") },
                    { band: 10, label: qsTr("Red · Clipped highlights") },
                    { band: -1, label: qsTr("Grey · Other tones") }
                ]
                Row {
                    id: zone
                    required property var modelData
                    width: parent.width
                    spacing: Theme.s2
                    Rectangle {
                        width: Theme.s3; height: Theme.s3
                        anchors.verticalCenter: parent.verticalCenter
                        radius: Theme.rControl
                        color: zone.modelData.band >= 0 && root.bands.length > zone.modelData.band
                               ? root.bands[zone.modelData.band].colour : Qt.rgba(0.5, 0.5, 0.5, 1)
                        border.color: Theme.borderStrong; border.width: Theme.hairline
                    }
                    Text {
                        width: parent.width - Theme.s3 - Theme.s2
                        text: zone.modelData.label
                        wrapMode: Text.WordWrap
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textPrimary
                    }
                }
            }
        }
        Text {
            width: parent.width
            visible: root.expanded
            text: qsTr("Skin tones vary. Clipping describes this preview; RAW detail may still be recoverable.")
            wrapMode: Text.WordWrap
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
        }
    }
}
