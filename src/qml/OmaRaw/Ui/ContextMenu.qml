import QtQuick
import QtQuick.Controls as C

// Styled menu, used both for right-click context menus and the title bar's
// application menus. Items come from MenuAction, which lays the shortcut out
// in a right-aligned column so a column of hints stays scannable.
C.Menu {
    id: root
    focus: true

    implicitWidth: 220
    property real slideOffset: 0
    contentItem.transform: Translate { y: root.slideOffset }
    padding: Theme.s1
    // Menus are one of the few places the brief allows motion, so a menu that
    // opens instantly reads as a glitch rather than as speed.
    topPadding: Theme.s1
    bottomPadding: Theme.s1

    delegate: MenuAction {}

    background: Rectangle {
        transform: Translate { y: root.slideOffset }
        implicitWidth: root.implicitWidth
        color: Theme.panelRaised
        radius: Theme.rMenu
        border.width: Theme.hairline
        border.color: Theme.border
    }

    enter: Transition {
        NumberAnimation { target: root; property: "slideOffset"; from: -Theme.s2; to: 0; duration: Theme.dSlow; easing.type: Theme.easing }
        NumberAnimation {
            property: "opacity"; from: 0; to: 1
            duration: Theme.dFast; easing.type: Theme.easing
        }
    }
    exit: Transition {
        NumberAnimation { target: root; property: "slideOffset"; from: 0; to: -Theme.s2; duration: Theme.dFast; easing.type: Theme.easing }
        NumberAnimation {
            property: "opacity"; from: 1; to: 0
            duration: Theme.dFast; easing.type: Theme.easing
        }
    }
}
