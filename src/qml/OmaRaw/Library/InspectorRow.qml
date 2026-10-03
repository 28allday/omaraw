import QtQuick
import OmaRaw.Ui

// Key/value row: muted key at a fixed width, selectable value that elides.
Item {
    id: root
    property string label: ""
    property string value: ""
    property bool mono: false
    property bool hideEmpty: true
    width: parent ? parent.width : 300
    height: visible ? Theme.hRow : 0
    visible: !hideEmpty || value !== ""
    Text {
        id: key
        anchors.left: parent.left
        anchors.leftMargin: Theme.s3
        anchors.verticalCenter: parent.verticalCenter
        width: 104
        text: root.label
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        color: Theme.textMuted
        elide: Text.ElideRight
    }
    TextInput {
        anchors.left: key.right
        anchors.right: parent.right
        anchors.rightMargin: Theme.s3
        anchors.verticalCenter: parent.verticalCenter
        text: root.value
        // Show the start of a long value, not its tail.
        onTextChanged: cursorPosition = 0
        readOnly: true
        selectByMouse: true
        clip: true
        font.family: root.mono ? Theme.monoFamily : Theme.fontFamily
        font.pixelSize: Theme.fsLabel
        color: Theme.textPrimary
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentText
    }
}
