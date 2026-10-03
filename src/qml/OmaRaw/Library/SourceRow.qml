import QtQuick
import OmaRaw.Ui

// One row of the source sidebar: icon, name, count; cyan when current.
Rectangle {
    id: root
    property string iconName: "folder"
    property string name: ""
    property int count: -1
    property int depth: 0
    property bool current: false
    property bool online: true
    property bool expandable: false
    property bool expanded: true
    // A tiny picture in place of the icon (an album's cover).
    property string thumb: ""
    signal clicked()
    signal toggled()
    signal contextRequested()

    width: parent ? parent.width : 200
    height: Theme.hRow
    color: hover.hovered ? Theme.hovered(current ? Theme.controlBg : Theme.panelBg)
                        : current ? Theme.controlBg : "transparent"
    opacity: online ? 1 : 0.55

    HoverHandler { id: hover }
    TapHandler { onTapped: root.clicked() }
    TapHandler { acceptedButtons: Qt.RightButton; onTapped: root.contextRequested() }

    Rectangle {
        anchors.left: parent.left
        width: 2; height: parent.height
        color: Theme.accent
        visible: root.current
    }
    Row {
        anchors.left: parent.left
        anchors.leftMargin: Theme.s3 + root.depth * Theme.s4
        anchors.right: countText.left
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.s2
        Item {
            width: Theme.szIcon; height: Theme.szIcon
            anchors.verticalCenter: parent.verticalCenter
            visible: root.expandable
            Icon {
                anchors.centerIn: parent
                name: root.expanded ? "chevron-down" : "chevron-right"
                size: 14; color: Theme.textMuted
            }
            TapHandler { onTapped: root.toggled() }
        }
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.thumb === "" || cover.status !== Image.Ready
            name: root.iconName
            color: root.current ? Theme.accent : Theme.textSecondary
        }
        Image {
            id: cover
            anchors.verticalCenter: parent.verticalCenter
            visible: root.thumb !== "" && status === Image.Ready
            width: visible ? Theme.szIcon : 0; height: Theme.szIcon
            source: root.thumb
            sourceSize.width: 64; sourceSize.height: 64
            fillMode: Image.PreserveAspectCrop
            asynchronous: true; smooth: true
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.name
            textFormat: Text.PlainText
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsControl
            color: root.current ? Theme.textPrimary : Theme.textSecondary
            elide: Text.ElideMiddle
            width: Math.min(implicitWidth, root.width - Theme.s3 - root.depth * Theme.s4 - Theme.szIcon * 2 - Theme.s2 * 3 - countText.width - Theme.s3)
        }
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: !root.online
            name: "unplug"; size: 12; color: Theme.warning
        }
    }
    Text {
        id: countText
        anchors.right: parent.right
        anchors.rightMargin: Theme.s3
        anchors.verticalCenter: parent.verticalCenter
        text: root.count >= 0 ? Number(root.count).toLocaleString(Qt.locale(), "f", 0) : ""
        font.family: Theme.monoFamily
        font.pixelSize: Theme.fsLabel
        color: Theme.textMuted
    }
}
