pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// The six colour labels as dots. In `picker` mode every dot shows and the
// current one is ringed; otherwise only the set label draws.
Row {
    id: root
    property string label: ""
    property bool picker: false
    property int size: 10
    signal picked(string label)
    function labelText(colour) { const names = backend.labelNames; return names[colour] ? names[colour] : backend.labelName(colour) }
    spacing: picker ? 3 : 0
    // Keyboard: Tab reaches the picker, the arrows move between colours and
    // Space or Enter sets (or, on the set one, removes) that label.
    // The ring starts on the photo's own label and follows it when the photo
    // changes; the arrows move it from there without cutting that tie.
    property int keyOffset: 0
    onLabelChanged: keyOffset = 0
    readonly property int keyIndex: Math.max(0, Math.min(Theme.labelNames.length - 1, Math.max(0, Theme.labelNames.indexOf(root.label)) + keyOffset))
    activeFocusOnTab: picker
    Keys.onShortcutOverride: event => { if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) event.accepted = true }
    Keys.onLeftPressed: if (keyIndex > 0) keyOffset -= 1
    Keys.onRightPressed: if (keyIndex < Theme.labelNames.length - 1) keyOffset += 1
    Keys.onSpacePressed: root.picked(Theme.labelNames[keyIndex])
    Keys.onReturnPressed: root.picked(Theme.labelNames[keyIndex])
    Keys.onEnterPressed: root.picked(Theme.labelNames[keyIndex])
    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Colour label")
    Repeater {
        model: root.picker ? Theme.labelNames : (root.label ? [root.label] : [])
        Rectangle {
            id: dot
            required property string modelData
            width: root.size; height: root.size; radius: 2
            color: Theme.labelColor(modelData)
            required property int index
            readonly property bool keyed: root.picker && root.activeFocus && root.keyIndex === index
            border.width: keyed || (root.picker && root.label === modelData) ? 2 : 0
            border.color: keyed ? Theme.accent : Theme.textPrimary
            Accessible.role: Accessible.Button
            Accessible.name: root.labelText(dot.modelData)
            Accessible.checkable: root.picker
            Accessible.checked: root.label === dot.modelData
            TapHandler { enabled: root.picker; onTapped: root.picked(parent.modelData) }
            HoverHandler { id: h; enabled: root.picker }
            Tooltip {
                text: root.labelText(dot.modelData)
                description: root.label === dot.modelData ? qsTr("Click again to remove the label.") : qsTr("Colour-labels the photo, for sorting and filtering.")
                visible: h.hovered
            }
        }
    }
}
