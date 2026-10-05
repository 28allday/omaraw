import QtQuick
import OmaRaw.Ui

// Key/value row: muted key at a fixed width, value that elides in the middle.
// A truncated value's full text is the tooltip.
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
    Text {
        id: valueText
        anchors.left: key.right
        anchors.right: parent.right
        anchors.rightMargin: Theme.s3
        anchors.verticalCenter: parent.verticalCenter
        text: root.value
        elide: Text.ElideMiddle
        font.family: root.mono ? Theme.monoFamily : Theme.fontFamily
        font.pixelSize: Theme.fsLabel
        color: Theme.textPrimary
    }
    HoverHandler { id: valueHover }
    Tooltip {
        text: root.label
        description: root.value
        visible: valueHover.hovered && valueText.truncated
    }
}
