import QtQuick
import QtQuick.Controls as C

// One line of a panel's list of sections: a chevron and a name. The open
// section's controls follow it; the others wait underneath as single lines,
// so the whole of a panel's contents is on view without a row of tabs.
C.AbstractButton {
    id: root

    property bool open: false
    // One sentence on what the section holds.
    property string tip: ""
    // Alt-click, for a caller that can put the whole section back.
    property bool altResets: false
    signal altClicked()
    // Buttons at the right of the line, for the open section.
    property alias trailing: trailingSlot.data

    implicitHeight: Math.round(Theme.hControl * 1.5)
    hoverEnabled: true
    focusPolicy: Qt.TabFocus

    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.description: open ? qsTr("Open") : qsTr("Closed")

    Tooltip { text: root.text; description: root.altResets && root.tip !== "" ? root.tip + " " + qsTr("Alt-click puts the whole section back.") : root.tip; visible: (root.enabled && root.hovered) && !root.down && root.tip !== "" && !root.open }
    // Takes the press only with Alt held; otherwise it falls through to the button.
    MouseArea {
        anchors.fill: parent
        enabled: root.altResets
        acceptedButtons: Qt.LeftButton
        onPressed: mouse => { if (mouse.modifiers & Qt.AltModifier) root.altClicked(); else mouse.accepted = false }
    }

    background: Rectangle {
        color: root.down ? Theme.pressedOn(Theme.panelBg) : (root.enabled && root.hovered) ? Theme.hovered(Theme.panelBg) : Theme.panelBg
        Rectangle { width: parent.width; height: Theme.hairline; color: Theme.border }
        Rectangle { visible: root.visualFocus; anchors.fill: parent; color: "transparent"; border.width: Theme.focusRing; border.color: Theme.accent }
    }
    contentItem: Item {
        Icon {
            id: chevron
            anchors.left: parent.left; anchors.leftMargin: Theme.s3 - 2
            anchors.verticalCenter: parent.verticalCenter
            name: root.open ? "chevron-down" : "chevron-right"
            size: 14
            color: root.open || (root.enabled && root.hovered) ? Theme.textPrimary : Theme.textSecondary
        }
        Text {
            anchors.left: chevron.right; anchors.leftMargin: Theme.s2
            anchors.right: trailingSlot.left; anchors.rightMargin: Theme.s2
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            elide: Text.ElideRight
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl + 1; font.weight: Theme.wHeading
            color: root.open || (root.enabled && root.hovered) ? Theme.textPrimary : Theme.textSecondary
        }
        Row {
            id: trailingSlot
            anchors.right: parent.right; anchors.rightMargin: Theme.s2
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.s1
        }
    }
}
