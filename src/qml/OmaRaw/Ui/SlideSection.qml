import QtQuick

// Clip a stable set of controls while the space beneath its heading opens.
// Collapsing never destroys editors or changes their values.
Item {
    id: root
    property bool expanded: false
    property alias spacing: content.spacing
    property alias bottomPadding: content.bottomPadding
    default property alias sectionContent: content.data
    readonly property bool moving: slide.running
    property real openness: expanded ? 1 : 0
    implicitHeight: content.implicitHeight * openness
    height: implicitHeight
    visible: expanded || height > 0
    enabled: expanded
    clip: true
    Behavior on openness {
        NumberAnimation { id: slide; duration: Theme.dSlow; easing.type: Theme.easing }
    }
    data: [Column { id: content; width: root.width; spacing: Theme.s1 }]
}
