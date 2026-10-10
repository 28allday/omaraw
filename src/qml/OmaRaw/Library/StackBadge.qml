import QtQuick
import OmaRaw.Ui

// Stack badge for a grid card or filmstrip cell. A collapsed stack's top says
// how many it holds; an open stack numbers every member, 1/5 to 5/5. The top
// stays filled with the accent so it stands out inside an open stack.
Rectangle {
    id: root
    property int assetId: 0
    property int stackCount: 0
    property int stackPos: 0
    property bool expanded: false
    readonly property bool isTop: stackPos === 0
    visible: stackCount > 1
    width: srow.implicitWidth + Theme.s2; height: 16
    radius: Theme.rControl
    color: isTop ? Theme.accent : Theme.scrim
    Row {
        id: srow
        anchors.centerIn: parent
        spacing: 3
        Icon { anchors.verticalCenter: parent.verticalCenter; name: "layers"; size: 10; color: root.isTop ? Theme.accentText : Theme.accent }
        Text {
            objectName: "stackBadgeText"
            anchors.verticalCenter: parent.verticalCenter
            text: root.isTop && !root.expanded ? String(root.stackCount) : (root.stackPos + 1) + "/" + root.stackCount
            font.family: Theme.monoFamily; font.pixelSize: 9; font.weight: Theme.wHeading
            color: root.isTop ? Theme.accentText : Theme.textPrimary
        }
    }
    TapHandler { onTapped: backend.toggleStack(root.assetId) }
    HoverHandler { id: stackHover }
    Tooltip {
        text: root.expanded ? qsTr("Collapse the stack") : qsTr("Expand the stack")
        description: root.expanded && root.isTop ? qsTr("The top of this stack of %1; it stands for the rest when collapsed.").arg(root.stackCount)
                   : root.expanded ? qsTr("Photo %1 of %2 in this stack.").arg(root.stackPos + 1).arg(root.stackCount)
                   : qsTr("%1 photos are stacked here; the top one stands for the rest.").arg(root.stackCount)
        visible: stackHover.hovered
    }
}
