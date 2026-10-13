import QtQuick
import QtQuick.Controls as C

// Search input with a leading glyph and a clear affordance that only appears
// once there is something to clear.
Rectangle {
    id: root

    property alias text: input.text
    property string placeholder: qsTr("Search…")
    // The leading glyph. Fields that take a name rather than a query say so.
    property string glyph: "search"
    // Search-as-you-type is the default; set false where a query is expensive
    // and should wait for Return.
    property bool live: true
    property bool clearOnEscape: true
    signal escapePressed()
    // One sentence on what to type here.
    property string tip: ""

    // True while the field owns the keyboard — owners use it to stand the
    // editing shortcuts down so typing a name never blades a clip.
    readonly property bool active: input.activeFocus

    signal accepted(string text)
    // Put the caret in the field (the Item itself never takes focus).
    function focusInput() { input.forceActiveFocus(); input.selectAll() }
    signal cleared()

    implicitWidth: 200
    implicitHeight: Theme.hField
    color: Theme.inputBg
    radius: Theme.rControl
    border.width: Theme.hairline
    border.color: input.activeFocus ? Theme.accent : hover.hovered ? Theme.borderStrong : Theme.border

    Behavior on border.color {
        ColorAnimation { duration: Theme.dFast; easing.type: Theme.easing }
    }

    HoverHandler { id: hover }
    Tooltip {
        text: root.placeholder
        description: root.tip
        visible: hover.hovered && !input.activeFocus && root.tip !== ""
    }

    Icon {
        id: glyph
        name: root.glyph
        size: Theme.szIcon
        color: Theme.textMuted
        anchors.left: parent.left
        anchors.leftMargin: Theme.s3
        anchors.verticalCenter: parent.verticalCenter
    }

    C.TextField {
        id: input
        anchors.left: glyph.right
        anchors.leftMargin: Theme.s2
        anchors.right: clear.visible ? clear.left : parent.right
        anchors.rightMargin: Theme.s2
        anchors.verticalCenter: parent.verticalCenter
        height: parent.height

        placeholderText: root.placeholder
        placeholderTextColor: Theme.textMuted
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fsControl
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentText
        verticalAlignment: Text.AlignVCenter
        padding: 0
        background: null

        Accessible.role: Accessible.EditableText
        Accessible.name: root.placeholder

        onTextChanged: if (root.live) root.accepted(text)
        onAccepted: root.accepted(text)
        Keys.onEscapePressed: {
            if (!root.clearOnEscape) {
                root.escapePressed()
            } else if (text !== "") {
                text = ""
                root.cleared()
            } else {
                focus = false
            }
        }
    }

    IconButton {
        id: clear
        iconName: "x"
        text: qsTr("Clear search")
        visible: input.text !== ""
        implicitWidth: Theme.szIcon + Theme.s1 * 2
        implicitHeight: Theme.szIcon + Theme.s1 * 2
        anchors.right: parent.right
        anchors.rightMargin: Theme.s1
        anchors.verticalCenter: parent.verticalCenter
        onClicked: {
            input.text = ""
            input.forceActiveFocus()
            root.cleared()
        }
    }
}
