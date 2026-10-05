import QtQuick
import QtQuick.Controls as C

// Icon-only button. Because it carries no visible label it MUST be given a
// `text` — that is what the tooltip shows and what a screen reader announces.
C.AbstractButton {
    id: root

    // `text` is the accessible name and tooltip title; `shortcut` is appended
    // to the tooltip as the brief requires ("Blade Tool  B").
    property string iconName: ""
    property string shortcut: ""
    // One sentence under the tooltip's title: what pressing it does.
    property string tip: ""
    // No filled well. Callers that already draw a plate (the Develop tool
    // chips) set this so a light-mode hover fill cannot cover the glyph.
    property bool bare: false
    // Drawn on Theme.scrim. The glyph stays light and the hover plate stays
    // dark, in Light and in Dark.
    property bool onScrim: false
    property color iconColor: onScrim ? Theme.scrimText : (checked ? Theme.accent : Theme.textSecondary)
    // Flat buttons carry no chrome until hovered — used in dense toolbars
    // where a grid of filled boxes would read as noise.
    property bool flat: true

    implicitWidth: Theme.szIconHit
    implicitHeight: Theme.szIconHit
    hoverEnabled: true
    // Tab reaches it; a click never takes focus — the keyboard belongs to the
    // editing shortcuts (Space, JKL) and must survive a toolbar click.
    focusPolicy: Qt.TabFocus
    opacity: enabled ? 1.0 : Theme.disabledOpacity

    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.description: shortcut ? qsTr("Shortcut: %1").arg(shortcut) : ""

    background: Rectangle {
        radius: Theme.rControl
        color: {
            if (root.onScrim) {
                if (root.down)
                    return Theme.scrimPressed
                if (root.enabled && root.hovered)
                    return Theme.scrimHover
                return "transparent"
            }
            if (root.bare)
                return "transparent"
            const base = root.checked || !root.flat ? Theme.controlBg : Theme.panelRaised
            if (root.down)
                return Theme.pressedOn(base)
            if ((root.enabled && root.hovered))
                return Theme.hovered(base)
            return root.checked || !root.flat ? Theme.controlBg : "transparent"
        }
        border.width: root.visualFocus ? Theme.focusRing : 0
        border.color: Theme.accent

        Behavior on color {
            ColorAnimation { duration: Theme.dFast; easing.type: Theme.easing }
        }
    }

    // AbstractButton stretches contentItem to fill, so the glyph is centred
    // inside a filling wrapper rather than being the contentItem itself.
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: root.iconName
            size: Theme.szIcon
            color: root.iconColor
        }
    }

    Tooltip {
        text: root.text
        shortcut: root.shortcut
        description: root.tip
        visible: (root.enabled && root.hovered) && !root.down && root.text !== ""
    }
}
