import QtQuick
import QtQuick.Controls as C

// The shared effect switch: green when applied, an unlit outline when off.
// Keep the existing type name and checked/clicked contract for callers.
C.AbstractButton {
    id: root

    // One sentence on what hiding it changes.
    property string tip: ""
    // Quiet until the pointer is near, unless it is reporting "hidden".
    property bool prominent: true

    checkable: true
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    implicitWidth: Theme.szIconHit
    implicitHeight: Theme.szIconHit
    opacity: !enabled ? Theme.disabledOpacity : !root.prominent && !(root.enabled && root.hovered) && !root.visualFocus ? 0.45 : 1.0

    Accessible.role: Accessible.CheckBox
    Accessible.name: text
    Accessible.checked: checked

    Tooltip {
        text: root.checked ? qsTr("Hide %1").arg(root.text) : qsTr("Show %1").arg(root.text)
        description: root.tip !== "" ? root.tip
                   : root.checked ? qsTr("Applied. Click to see the picture without it; its settings are kept.")
                                  : qsTr("Not applied. Click to bring it back, or move one of its controls.")
        visible: (root.enabled && root.hovered) && !root.down && root.text !== ""
    }

    background: Rectangle {
        radius: Theme.rControl
        color: root.down ? Theme.pressedOn(Theme.controlBg) : (root.enabled && root.hovered) ? Theme.hovered(Theme.panelRaised) : "transparent"
        border.width: root.visualFocus ? Theme.focusRing : 0
        border.color: Theme.accent
    }
    contentItem: Item {
        EffectDot {
            anchors.centerIn: parent
            applied: root.checked
        }
    }
}
