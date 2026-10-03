pragma ComponentBehavior: Bound
import QtQuick

// Exclusive choice shown as one welded strip — the view-mode and split-screen
// switches in the concepts. Segments may be icons or short labels; mixing the
// two inside one control is deliberately not supported, because the two align
// on different optical centres.
Rectangle {
    id: root

    // Either a list of icon names or a list of labels.
    property var icons: []
    property var labels: []
    property int currentIndex: 0
    // Per-segment accessible names; required when using `icons`.
    property var names: []
    // Per-segment descriptions for the tooltip, or one for the whole strip.
    property var tips: []
    property string tip: ""

    signal activated(int index)

    // The caller owns currentIndex (it binds it to its own state and updates
    // that state in onActivated), so the control never assigns it: that would
    // cut the caller's binding. A click lights its segment at once and hands
    // back to currentIndex when the caller's state catches up, or after a
    // moment if the caller refused the change.
    property int asked: -1
    readonly property int shownIndex: asked >= 0 ? asked : currentIndex
    onCurrentIndexChanged: asked = -1
    Timer { id: askedTimer; interval: 1500; onTriggered: root.asked = -1 }
    function choose(i) {
        if (i < 0 || i >= root.model.length) return
        root.asked = i
        askedTimer.restart()
        root.activated(i)
    }

    activeFocusOnTab: true
    // Left and Right are also the photo-stepping shortcuts; while this has
    // the keyboard they are its own. At an end they do nothing: a second
    // activated() on a toggling strip (the flag filter) would clear it.
    Keys.onShortcutOverride: event => { if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) event.accepted = true }
    Keys.onLeftPressed: if (shownIndex > 0) choose(shownIndex - 1)
    Keys.onRightPressed: if (shownIndex < model.length - 1) choose(shownIndex + 1)
    Accessible.role: Accessible.Grouping

    readonly property var model: icons.length ? icons : labels
    readonly property bool iconMode: icons.length > 0

    implicitWidth: row.implicitWidth + Theme.hairline * 2
    implicitHeight: Theme.hControl
    color: Theme.controlBg
    radius: Theme.rControl
    border.width: activeFocus ? Theme.focusRing : Theme.hairline
    border.color: activeFocus ? Theme.accent : Theme.border

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 0

        Repeater {
            model: root.model

            delegate: Item {
                id: seg
                required property int index
                required property var modelData

                width: root.iconMode
                       ? Theme.szIconHit
                       : segLabel.implicitWidth + Theme.s3 * 2
                height: root.height - Theme.hairline * 2

                readonly property bool current: index === root.shownIndex

                Rectangle {
                    anchors.fill: parent
                    // Only the ends get the control's radius, so the strip
                    // reads as a single object rather than adjacent pills.
                    topLeftRadius: seg.index === 0 ? Theme.rControl : 0
                    bottomLeftRadius: seg.index === 0 ? Theme.rControl : 0
                    topRightRadius: seg.index === root.model.length - 1 ? Theme.rControl : 0
                    bottomRightRadius: seg.index === root.model.length - 1 ? Theme.rControl : 0
                    readonly property color base: seg.current ? Theme.accent : Theme.controlBg
                    color: tap.pressed ? Theme.pressedOn(base)
                         : hover.hovered ? Theme.hovered(base) : base

                    Behavior on color {
                        ColorAnimation { duration: Theme.dFast; easing.type: Theme.easing }
                    }
                }

                // Separator between unlit segments only — a divider beside the
                // lit segment would cut into its fill.
                Rectangle {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.hairline
                    height: parent.height - Theme.s1 * 2
                    color: Theme.border
                    visible: seg.index < root.model.length - 1
                             && !seg.current
                             && seg.index + 1 !== root.shownIndex
                }

                Icon {
                    anchors.centerIn: parent
                    visible: root.iconMode
                    name: root.iconMode ? seg.modelData : ""
                    size: Theme.szIcon
                    color: seg.current ? Theme.accentText : Theme.textSecondary
                }

                Text {
                    id: segLabel
                    anchors.centerIn: parent
                    visible: !root.iconMode
                    text: root.iconMode ? "" : seg.modelData
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fsControl
                    color: seg.current ? Theme.accentText : Theme.textSecondary
                }

                HoverHandler { id: hover }

                Tooltip {
                    readonly property string name: seg.index < root.names.length ? root.names[seg.index]
                                                 : root.iconMode ? "" : String(seg.modelData)
                    text: name
                    description: seg.index < root.tips.length ? root.tips[seg.index] : root.tip
                    visible: hover.hovered && text !== "" && (description !== "" || root.iconMode)
                }

                TapHandler {
                    id: tap
                    onTapped: {
                        root.choose(seg.index)
                    }
                }

                Accessible.role: Accessible.RadioButton
                Accessible.name: seg.index < root.names.length ? root.names[seg.index]
                               : root.iconMode ? seg.modelData : String(seg.modelData)
                Accessible.checked: seg.current
            }
        }
    }
}
