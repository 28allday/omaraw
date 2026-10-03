import QtQuick
import QtQuick.Controls as C

// Toolbar button that may carry an icon, a label, or both. Checkable when it
// represents a mode (the timeline's select/blade/slip tools) rather than an
// action; a checked tool shows a filled well plus a cyan glyph so the active
// mode is readable at a glance across a 38 px strip.
C.AbstractButton {
    id: root

    property string iconName: ""
    property string shortcut: ""
    property bool showLabel: text !== "" && iconName === ""
    // One sentence under the tooltip's title: what pressing it does. A
    // labelled button shows a tooltip only when it has one.
    property string tip: ""

    implicitWidth: Math.max(Theme.szIconHit,
                            (iconName ? Theme.szIcon : 0) + (showLabel ? caption.implicitWidth : 0)
                            + (iconName && showLabel ? Theme.s1 : 0) + Theme.s2 * 2)
    implicitHeight: Theme.hControl
    leftPadding: Theme.s2
    rightPadding: Theme.s2
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: true
    // Tab reaches it; a click never takes focus — the keyboard belongs to the
    // editing shortcuts (Space, JKL) and must survive a toolbar click.
    focusPolicy: Qt.TabFocus
    opacity: enabled ? 1.0 : Theme.disabledOpacity

    Accessible.role: checkable ? Accessible.CheckBox : Accessible.Button
    Accessible.name: text
    Accessible.description: shortcut ? qsTr("Shortcut: %1").arg(shortcut) : ""

    background: Rectangle {
        radius: Theme.rControl
        color: {
            const base = root.checked ? Theme.controlBg : Theme.panelRaised
            if (root.down)
                return Theme.pressedOn(base)
            if ((root.enabled && root.hovered))
                return Theme.hovered(base)
            return root.checked ? Theme.controlBg : "transparent"
        }
        border.width: root.visualFocus ? Theme.focusRing : 0
        border.color: Theme.accent

        Behavior on color {
            ColorAnimation { duration: Theme.dFast; easing.type: Theme.easing }
        }
    }

    // The control stretches its contentItem. Centre a natural-width group
    // inside that wrapper, reserving the icon before eliding a long caption.
    contentItem: Item {
        id: content
        Row {
            spacing: Theme.s1
            anchors.centerIn: parent

            Icon {
                name: root.iconName
                visible: root.iconName !== ""
                size: Theme.szIcon
                color: root.checked ? Theme.accent : Theme.textSecondary
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                id: caption
                textFormat: Text.PlainText
                text: root.text
                visible: root.showLabel
                width: Math.min(implicitWidth, Math.max(0, content.width - (root.iconName ? Theme.szIcon + Theme.s1 : 0)))
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsControl
                color: root.checked ? Theme.accent : Theme.textSecondary
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    Tooltip {
        text: root.text
        shortcut: root.shortcut
        description: root.tip
        visible: (root.enabled && root.hovered) && !root.down && root.text !== "" && (!root.showLabel || root.tip !== "")
    }
}
