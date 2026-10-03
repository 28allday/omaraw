import QtQuick

// The grab strip between two SplitView sections. Reads as a seam at rest and
// lights cyan while being dragged, so the boundary being moved is never
// ambiguous in a layout with five of them.
Rectangle {
    id: root

    // SplitView sets these on the handle it instantiates.
    required property bool pressed
    required property bool hovered
    property bool vertical: false

    implicitWidth: vertical ? 0 : Theme.wSplitter
    implicitHeight: vertical ? Theme.wSplitter : 0
    color: pressed ? Theme.accent
         : hovered ? Theme.borderStrong : Theme.windowBg

    Behavior on color {
        ColorAnimation { duration: Theme.dFast; easing.type: Theme.easing }
    }

    // A hairline inside the strip keeps panels visually separated even when
    // the handle itself is the same value as the window ground.
    Rectangle {
        anchors.centerIn: parent
        width: root.vertical ? parent.width : Theme.hairline
        height: root.vertical ? Theme.hairline : parent.height
        color: Theme.border
        visible: !root.pressed
    }
}
