pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as C

Column {
    id: root
    property string caption: qsTr("Saved settings")
    property var entries: []
    signal chosen(var values)
    signal saveRequested(string name)
    signal removeRequested(string name)
    spacing: Theme.s2
    Text {
        width: parent.width
        text: root.caption
        color: Theme.textSecondary
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        wrapMode: Text.Wrap
    }
    ComboField {
        width: parent.width
        model: [qsTr("Choose saved settings…")].concat(root.entries.map(p => p.name))
        tipTitle: root.caption; tip: qsTr("Fills every field on this page from a saved set.")
        onActivated: i => {
            if (i <= 0) return
            const preset = root.entries[i - 1]
            nameField.text = preset.name
            root.chosen(preset.values)
        }
    }
    C.TextField {
        id: nameField
        width: parent.width; height: Theme.hControl
        placeholderText: qsTr("Name to save or update")
        color: Theme.textPrimary; placeholderTextColor: Theme.textMuted
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
        maximumLength: 80
        Accessible.name: root.caption + qsTr(" name")
        background: Rectangle { color: Theme.inputBg; radius: Theme.rControl; border.color: nameField.activeFocus ? Theme.accent : Theme.border }
        onAccepted: if (text.trim() !== "") root.saveRequested(text.trim())
    }
    Row {
        spacing: Theme.s2
        ToolButton { text: qsTr("Save"); showLabel: true; tip: qsTr("Keeps the fields as they are now under this name; an existing name is updated."); enabled: nameField.text.trim() !== ""; onClicked: root.saveRequested(nameField.text.trim()) }
        ToolButton {
            text: qsTr("Delete"); showLabel: true
            tip: qsTr("Removes the saved set with this name.")
            enabled: root.entries.some(p => p.name === nameField.text.trim())
            onClicked: { root.removeRequested(nameField.text.trim()); nameField.text = "" }
        }
    }
}
