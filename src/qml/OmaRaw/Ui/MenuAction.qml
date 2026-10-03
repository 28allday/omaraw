import QtQuick
import QtQuick.Controls as C

// One menu row: optional leading check or icon, label, right-aligned shortcut.
// The shortcut column is what makes a menu teach its own keyboard equivalents,
// so it is never omitted where one exists.
C.MenuItem {
    id: root

    property string iconName: ""
    property string shortcut: ""

    implicitHeight: Theme.hRow + Theme.s1 * 2
    // Menu's ListView still lays out invisible items unless their height is zero.
    height: visible ? implicitHeight : 0
    implicitWidth: Theme.s3 * 2 + Theme.szIcon + Theme.s2
                   + label.implicitWidth + Theme.s5 + hint.implicitWidth
    opacity: enabled ? 1.0 : Theme.disabledOpacity
    hoverEnabled: true

    Accessible.name: text

    indicator: Item {
        width: Theme.szIcon
        height: parent.height
        x: Theme.s2

        Icon {
            anchors.centerIn: parent
            name: root.checkable ? (root.checked ? "check" : "") : root.iconName
            size: Theme.szIcon
            color: root.checked ? Theme.accent : Theme.textSecondary
        }
    }

    contentItem: Item {
        anchors.fill: parent

        Text {
            id: label
            text: root.text
            textFormat: Text.PlainText
            anchors.left: parent.left
            anchors.leftMargin: Theme.s2 + Theme.szIcon + Theme.s2
            anchors.right: hint.left
            anchors.rightMargin: hint.visible ? Theme.s3 : 0
            anchors.verticalCenter: parent.verticalCenter
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsControl
            color: root.highlighted ? Theme.textPrimary : Theme.textSecondary
            elide: Text.ElideRight
        }

        Text {
            id: hint
            text: { backend.shortcutOverrides; return backend.shortcutHint(root.shortcut) }
            visible: root.shortcut !== ""
            width: visible ? implicitWidth : 0
            anchors.right: parent.right
            anchors.rightMargin: Theme.s3
            anchors.verticalCenter: parent.verticalCenter
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fsLabel
            color: Theme.textMuted
        }
    }

    background: Rectangle {
        radius: Theme.rControl
        color: root.down ? Theme.pressedOn(Theme.controlBg)
             : root.highlighted || (root.enabled && root.hovered) ? Theme.hoverBg : "transparent"
    }
}
