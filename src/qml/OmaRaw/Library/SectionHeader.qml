import QtQuick
import OmaRaw.Ui

// Sidebar section title: uppercase muted label, optional trailing actions.
Item {
    id: root
    property string title: ""
    property string helpSection: ""
    signal helpRequested()
    onHelpRequested: { const w = root.Window.window; if (w && w.help) w.help(root.helpSection) }
    property alias trailing: slot.data
    width: parent ? parent.width : 200
    height: Theme.hRow + Theme.s2
    Text {
        anchors.left: parent.left
        anchors.leftMargin: Theme.s3
        anchors.right: helpButton.left
        anchors.rightMargin: Theme.s1
        anchors.verticalCenter: parent.verticalCenter
        text: root.title.toUpperCase()
        elide: Text.ElideRight
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fsLabel
        font.weight: Theme.wHeading
        font.letterSpacing: 0.6
        color: Theme.textMuted
    }
    IconButton {
        id: helpButton
        objectName: "dockHelp"
        anchors.right: slot.left
        anchors.verticalCenter: parent.verticalCenter
        width: visible ? Theme.szIconHit : 0
        visible: root.helpSection !== ""
        iconName: "circle-help"; text: qsTr("Help for %1").arg(root.title)
        tip: qsTr("Opens the guide for this section.")
        onClicked: root.helpRequested()
    }
    Row {
        id: slot
        anchors.right: parent.right
        anchors.rightMargin: Theme.s2
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0
    }
}
