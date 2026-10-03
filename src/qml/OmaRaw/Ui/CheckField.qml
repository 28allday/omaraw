import QtQuick
import QtQuick.Controls as C

// A yes/no option as a tick box with its caption beside it; the whole line
// is the target. For choices inside a panel, where a switch reads as a
// machine control and a tick reads as an answer.
C.AbstractButton {
    id: root

    // One sentence on what ticking it changes.
    property string tip: ""

    checkable: true
    hoverEnabled: true
    implicitHeight: Theme.hControl
    implicitWidth: box.width + Theme.s2 + caption.implicitWidth
    opacity: enabled ? 1.0 : Theme.disabledOpacity

    Accessible.role: Accessible.CheckBox
    Accessible.name: text
    Accessible.checked: checked

    background: null

    Tooltip {
        text: root.text
        description: root.tip
        visible: (root.enabled && root.hovered) && !root.down && root.tip !== ""
    }

    contentItem: Item {
        Rectangle {
            id: box
            objectName: "checkIndicator"
            width: 14; height: 14
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            radius: Theme.rControl - 1
            readonly property color base: root.checked ? Theme.accent : Theme.controlBg
            color: root.down ? Theme.pressedOn(base) : (root.enabled && root.hovered) ? Theme.hovered(base) : base
            border.width: root.visualFocus ? Theme.focusRing : Theme.hairline
            border.color: root.visualFocus || root.checked ? Theme.accent : (root.enabled && root.hovered) ? Theme.borderStrong : Theme.border
            Icon { anchors.centerIn: parent; name: "check"; size: 11; color: Theme.accentText; visible: root.checked }
        }
        Text {
            id: caption
            anchors.left: box.right; anchors.leftMargin: Theme.s2
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            elide: Text.ElideRight
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
            color: root.checked || (root.enabled && root.hovered) ? Theme.textPrimary : Theme.textSecondary
        }
    }
}
