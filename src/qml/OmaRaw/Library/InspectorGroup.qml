import QtQuick
import OmaRaw.Ui

// Collapsible inspector section: chevron + title, then the content column.
Column {
    id: root
    property string title: ""
    property string helpSection: ""
    property bool expanded: true
    default property alias content: body.data
    width: parent ? parent.width : 300
    spacing: 0
    Rectangle {
        width: parent.width; height: Theme.hDockHeader
        color: Theme.panelBg
        Item {
            anchors.left: parent.left
            anchors.leftMargin: Theme.s2
            anchors.right: helpButton.left
            anchors.rightMargin: Theme.s1
            height: parent.height
            Icon { id: chevron; anchors.verticalCenter: parent.verticalCenter; name: root.expanded ? "chevron-down" : "chevron-right"; size: 14; color: Theme.textMuted }
            Text {
                anchors.left: chevron.right; anchors.leftMargin: Theme.s1
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: root.title.toUpperCase()
                elide: Text.ElideRight
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; font.weight: Theme.wHeading
                font.letterSpacing: 0.6
                color: Theme.textSecondary
            }
            TapHandler { onTapped: root.expanded = !root.expanded }
            HoverHandler { id: headerHover }
            Tooltip {
                text: root.title
                description: root.expanded ? qsTr("Click to fold this section.") : qsTr("Click to unfold this section.")
                visible: headerHover.hovered
            }
        }
        IconButton {
            id: helpButton
            objectName: "dockHelp"
            anchors.right: parent.right; anchors.rightMargin: Theme.s1
            anchors.verticalCenter: parent.verticalCenter
            width: visible ? Theme.szIconHit : 0
            visible: root.helpSection !== ""
            iconName: "circle-help"; text: qsTr("Help for %1").arg(root.title)
            tip: qsTr("Opens the guide for this section.")
            onClicked: { const w = root.Window.window; if (w && w.help) w.help(root.helpSection) }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.hairline; color: Theme.border }
    }
    Column {
        id: body
        width: parent.width
        visible: root.expanded
        spacing: Theme.s1
        topPadding: Theme.s2
        bottomPadding: Theme.s2
    }
}
