import QtQuick
import QtQuick.Controls as C

// Delayed tooltip carrying an optional shortcut: appears after
// Theme.tooltipDelay and reads "Blade Tool  B". The shortcut sits in a dimmer weight so
// the label still reads first.
C.ToolTip {
    id: root

    property string shortcut: ""
    // An optional second line: one sentence on what the control does. Wraps
    // at a readable width so a tooltip never runs across the window.
    property string description: ""

    delay: Theme.tooltipDelay
    padding: 0

    // Beside the control, on the side towards the middle of the window, never
    // over it: a tip in a side dock opens over the picture instead of on the
    // control, or the one above it, that the pointer is reading. In the middle
    // band (toolbars, filmstrip) it goes below, or above near the bottom edge.
    readonly property real gap: Theme.s3
    function place() {
        const p = parent
        if (!p || !p.Window.window) return
        const w = p.Window.width, h = p.Window.height
        const at = p.mapToItem(null, 0, 0)
        const centre = at.x + p.width / 2
        if (centre > w * 2 / 3) { x = -implicitWidth - gap; y = (p.height - implicitHeight) / 2 }
        else if (centre < w / 3) { x = p.width + gap; y = (p.height - implicitHeight) / 2 }
        else { x = (p.width - implicitWidth) / 2; y = at.y + p.height + gap + implicitHeight > h ? -implicitHeight - gap : p.height + gap }
    }
    onAboutToShow: place()

    contentItem: Column {
        spacing: 2
        leftPadding: Theme.s2
        rightPadding: Theme.s2
        topPadding: Theme.s1
        bottomPadding: Theme.s1

        Row {
            spacing: Theme.s2
            Text {
                text: root.text
                width: Math.min(implicitWidth, 300)
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsLabel
                color: Theme.textPrimary
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: root.shortcut
                visible: root.shortcut !== ""
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fsLabel
                color: Theme.textMuted
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Text {
            text: root.description
            visible: root.description !== ""
            width: Math.min(implicitWidth, 300)
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsLabel
            color: Theme.textSecondary
        }
    }

    background: Rectangle {
        color: Theme.panelRaised
        radius: Theme.rMenu
        border.width: Theme.hairline
        border.color: Theme.border
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"; from: 0; to: 1
            duration: Theme.dFast; easing.type: Theme.easing
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"; from: 1; to: 0
            duration: Theme.dFast; easing.type: Theme.easing
        }
    }
}
