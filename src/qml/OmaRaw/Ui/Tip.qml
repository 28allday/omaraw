import QtQuick
import QtQuick.Controls.Basic

// Tooltip in the chrome palette, after Theme.tooltipDelay. `shortcut`
// renders after the text in muted tabular type.
ToolTip {
    id: t
    property string shortcut: ""
    delay: Theme.tooltipDelay
    padding: Theme.s2

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
    contentItem: Row {
        spacing: Theme.s2
        Text {
            text: t.text
            textFormat: Text.PlainText
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsLabel
        }
        Text {
            visible: t.shortcut !== ""
            text: t.shortcut
            color: Theme.textMuted
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fsLabel
        }
    }
    background: Rectangle {
        color: Theme.panelRaised
        border.width: Theme.hairline
        border.color: Theme.borderStrong
        radius: Theme.rMenu
    }
}
