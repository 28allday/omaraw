import QtQuick
import QtQuick.Controls as C

// Thin overlay scrollbar. Stays out of the way in a dense layout: a hairline
// at rest, thickening on hover so it can actually be grabbed.
C.ScrollBar {
    id: root

    padding: 2
    minimumSize: 0.06

    contentItem: Rectangle {
        implicitWidth: root.hovered || root.pressed ? 8 : 4
        implicitHeight: root.hovered || root.pressed ? 8 : 4
        radius: width / 2
        color: root.pressed ? Theme.textSecondary
             : root.hovered ? Theme.textMuted : Theme.borderStrong
        opacity: root.policy === C.ScrollBar.AlwaysOn || root.active ? 1.0 : 0.0

        Behavior on implicitWidth {
            NumberAnimation { duration: Theme.dFast; easing.type: Theme.easing }
        }
        Behavior on implicitHeight {
            NumberAnimation { duration: Theme.dFast; easing.type: Theme.easing }
        }
        Behavior on opacity {
            NumberAnimation { duration: Theme.dNormal; easing.type: Theme.easing }
        }
        Behavior on color {
            ColorAnimation { duration: Theme.dFast; easing.type: Theme.easing }
        }
    }

    background: Rectangle {
        color: Theme.windowBg
        opacity: root.hovered || root.pressed ? 0.6 : 0.0

        Behavior on opacity {
            NumberAnimation { duration: Theme.dFast; easing.type: Theme.easing }
        }
    }
}
