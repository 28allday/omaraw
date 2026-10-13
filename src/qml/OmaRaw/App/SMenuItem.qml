import QtQuick
import QtQuick.Controls.Basic
import OmaRaw.Ui

// Menu entry: label left, shortcut right in muted tabular type, a check
// glyph for checkable entries, a chevron for submenu titles.
// Every row reserves the check column, so a checkable command such as
// Crop & Straighten lines up with the plain commands beside it.
MenuItem {
    id: mi
    property string shortcut: ""
    property string tip: subMenu && subMenu.tip !== undefined ? subMenu.tip : ""
    hoverEnabled: true
    Accessible.description: tip
    implicitHeight: Theme.hRow + Theme.s1
    // Fit command names and shortcut hints; very long recent-catalog paths
    // still elide so they cannot stretch a menu across the entire window.
    implicitWidth: Math.min(480, Math.max(236,
        Math.ceil(label.implicitWidth + hint.implicitWidth) + Theme.s3 * 2 + Theme.s2
        + Theme.szIcon + (mi.subMenu ? Theme.szIcon : 0)))
    padding: 0
    contentItem: Item {
        Text {
            id: label
            anchors.left: parent.left
            anchors.leftMargin: Theme.s3 + Theme.szIcon
            anchors.right: hint.left
            anchors.rightMargin: Theme.s2
            anchors.verticalCenter: parent.verticalCenter
            text: mi.text
            textFormat: Text.PlainText
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsControl
            color: !mi.enabled ? Theme.textMuted
                 : mi.highlighted ? Theme.textPrimary : Theme.textSecondary
            elide: Text.ElideRight
        }
        Text {
            id: hint
            anchors.right: parent.right
            anchors.rightMargin: mi.subMenu ? Theme.s2 + Theme.szIcon : Theme.s3
            anchors.verticalCenter: parent.verticalCenter
            // The keys in force: the shortcut editor may have moved them.
            text: { backend.shortcutOverrides; return backend.shortcutHint(mi.shortcut) }
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fsLabel
            color: Theme.textMuted
        }
        Icon {
            visible: mi.checkable && mi.checked
            anchors.left: parent.left
            anchors.leftMargin: Theme.s2
            anchors.verticalCenter: parent.verticalCenter
            name: "check"; color: Theme.accent
        }
        Icon {
            visible: mi.subMenu !== null
            anchors.right: parent.right
            anchors.rightMargin: Theme.s1
            anchors.verticalCenter: parent.verticalCenter
            name: "chevron-right"; color: Theme.textMuted
        }
    }
    indicator: null
    arrow: null
    background: Rectangle {
        anchors.fill: parent
        anchors.leftMargin: Theme.s1; anchors.rightMargin: Theme.s1
        radius: Theme.rControl
        color: mi.down ? Theme.pressedOn(Theme.controlBg)
             : mi.highlighted || mi.hovered ? Theme.hoverBg : "transparent"
    }
    Tooltip {
        text: mi.text
        shortcut: { backend.shortcutOverrides; return backend.shortcutHint(mi.shortcut) }
        description: mi.tip
        visible: mi.hovered && !mi.down && mi.tip !== "" && !(mi.subMenu && mi.subMenu.visible)
    }
}
