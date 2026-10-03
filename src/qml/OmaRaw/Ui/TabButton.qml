import QtQuick
import QtQuick.Controls as C

// Panel tab: uppercase label, cyan text plus a 2 px underline when active.
// Used for both the panel tab strips (PROJECT / EFFECTS / TRANSITIONS) and
// the workspace bar, which differ only in type size.
C.AbstractButton {
    id: root

    property bool uppercase: true
    property int fontSize: Theme.fsControl
    // Optional trailing count, as on the bin rows and smart collections.
    property string badge: ""
    // Horizontal padding; a crowded strip (DockHeader) narrows it to fit.
    property int hPad: Theme.s3
    // One sentence on what the tab holds.
    property string tip: ""

    Tooltip {
        text: root.text
        description: root.tip
        visible: (root.enabled && root.hovered) && !root.down && root.tip !== ""
    }
    readonly property real labelWidth: label.implicitWidth

    implicitWidth: label.implicitWidth + hPad * 2
                   + (badge ? count.implicitWidth + Theme.s2 : 0)
    implicitHeight: Theme.hTab
    leftPadding: hPad
    rightPadding: hPad
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: true
    // Tab reaches it; a click never takes focus — the keyboard belongs to the
    // editing shortcuts (Space, JKL) and must survive a toolbar click.
    focusPolicy: Qt.TabFocus
    checkable: true
    opacity: enabled ? 1.0 : Theme.disabledOpacity

    Accessible.role: Accessible.PageTab
    Accessible.name: text

    background: Rectangle {
        color: root.down ? Theme.pressedOn(Theme.panelBg)
             : (root.enabled && root.hovered) ? Theme.hovered(Theme.panelBg) : "transparent"

        // The active marker. Sits flush to the bottom edge of the strip so a
        // row of tabs reads as one ruled line with one lit segment.
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.activeUnderline
            color: Theme.accent
            visible: root.checked
        }

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.width: root.visualFocus ? Theme.focusRing : 0
            border.color: Theme.accent
        }
    }

    contentItem: Item {
        id: content
        Row {
            spacing: Theme.s2
            anchors.centerIn: parent

            Text {
                id: label
                text: root.uppercase ? root.text.toUpperCase() : root.text
                textFormat: Text.PlainText
                width: Math.min(implicitWidth, Math.max(0, content.width - (root.badge ? count.width + Theme.s2 : 0)))
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: root.fontSize
                font.weight: root.checked ? Theme.wHeading : Theme.wNormal
                font.letterSpacing: root.uppercase ? 0.4 : 0
                color: root.checked ? Theme.accent
                     : (root.enabled && root.hovered) ? Theme.textPrimary : Theme.textSecondary
                anchors.verticalCenter: parent.verticalCenter

                Behavior on color {
                    ColorAnimation { duration: Theme.dFast; easing.type: Theme.easing }
                }
            }

            Text {
                id: count
                text: root.badge
                textFormat: Text.PlainText
                width: Math.min(implicitWidth, Math.max(0, content.width / 3))
                elide: Text.ElideRight
                visible: root.badge !== ""
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsLabel
                color: Theme.textMuted
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
