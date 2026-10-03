import QtQuick
import QtQuick.Controls as C

// Small switch for the Inspector's per-module enables (De-Esser, Noise
// Reduction). Deliberately restrained: a short track and a square-ish knob,
// never a large rounded consumer switch.
C.AbstractButton {
    id: root

    property string label: ""
    // One sentence on what switching it changes.
    property string tip: ""
    readonly property int trackWidth: 26
    readonly property int trackHeight: 14

    checkable: true
    hoverEnabled: true
    implicitHeight: Math.max(trackHeight, Theme.hRow, caption.implicitHeight)
    implicitWidth: trackWidth + (label ? caption.implicitWidth + Theme.s2 : 0)
    opacity: enabled ? 1.0 : Theme.disabledOpacity

    Accessible.role: Accessible.CheckBox
    Accessible.name: label !== "" ? label : text
    Accessible.checked: checked

    background: null

    Tooltip {
        text: root.label !== "" ? root.label : root.text
        description: root.tip
        visible: (root.enabled && root.hovered) && !root.down && (root.tip !== "" || (root.label === "" && root.text !== ""))
    }

    contentItem: Item {
        anchors.fill: parent

        Text {
            id: caption
            objectName: "toggleCaption"
            text: root.label
            visible: root.label !== ""
            anchors.left: parent.left
            anchors.right: track.left; anchors.rightMargin: Theme.s2
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText; wrapMode: Text.WordWrap
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsLabel
            color: (root.enabled && root.hovered) ? Theme.textPrimary : Theme.textSecondary
        }

        Rectangle {
            id: track
            objectName: "toggleTrack"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: root.trackWidth
            height: root.trackHeight
            radius: Theme.rControl
            readonly property color base: root.checked ? Theme.accent : Theme.controlBg
            color: root.down ? Theme.pressedOn(base) : (root.enabled && root.hovered) ? Theme.hovered(base) : base
            border.width: root.visualFocus ? Theme.focusRing : Theme.hairline
            border.color: root.visualFocus ? Theme.accent
                        : root.checked ? Theme.accent : (root.enabled && root.hovered) ? Theme.borderStrong : Theme.border

            Behavior on color {
                ColorAnimation { duration: Theme.dFast; easing.type: Theme.easing }
            }

            Rectangle {
                id: knob
                width: root.trackHeight - 4
                height: width
                radius: Theme.rControl - 1
                y: 2
                x: root.checked ? track.width - width - 2 : 2
                color: root.checked ? Theme.accentText
                     : (root.enabled && root.hovered) ? Theme.textPrimary : Theme.textSecondary

                Behavior on x {
                    NumberAnimation { duration: Theme.dFast; easing.type: Theme.easing }
                }
                Behavior on color {
                    ColorAnimation { duration: Theme.dFast; easing.type: Theme.easing }
                }
            }
        }
    }
}
