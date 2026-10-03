import QtQuick
import QtQuick.Controls.Basic as C

// Shared by Library browsing and all import/export destination pickers.
ModalPanel {
    id: root
    property string parentFolder: ""
    property string location: ""
    signal created(url folder)
    width: parent ? Math.min(460, parent.width - Theme.s5 * 2) : 460
    function openFor(folder) { parentFolder = folder.toString(); open() }
    function create() {
        const info = backend.createPickerFolder(location, name.text.trim())
        if (info.error) { error.text = info.error; return }
        close(); created(info.url)
    }
    onAboutToShow: { location = backend.localFile(parentFolder); name.text = ""; error.text = "" }
    onOpened: name.forceActiveFocus()
    contentItem: Column {
        spacing: Theme.s3
        Text { text: qsTr("New folder"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading }
        Text {
            objectName: "newFolderLocation"
            width: parent.width; text: qsTr("Create in: %1").arg(root.location); textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        }
        C.TextField {
            id: name; objectName: "pickerNewFolderName"
            width: parent.width; height: Theme.hControl
            color: Theme.textPrimary; placeholderTextColor: Theme.textMuted
            selectionColor: Theme.accent; selectedTextColor: Theme.accentText
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
            Accessible.name: qsTr("Folder name"); placeholderText: qsTr("Folder name"); selectByMouse: true
            leftPadding: Theme.s2; rightPadding: Theme.s2
            background: Rectangle { color: Theme.inputBg; radius: Theme.rControl; border.color: name.activeFocus ? Theme.accent : Theme.border }
            onTextEdited: error.text = ""
            onAccepted: if (text.trim()) root.create()
        }
        Text { id: error; objectName: "newFolderError"; width: parent.width; visible: text !== ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Theme.danger; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase }
        Row {
            anchors.right: parent.right; spacing: Theme.s2
            ToolButton { objectName: "newFolderCancel"; text: qsTr("Cancel"); onClicked: root.close() }
            ToolButton { objectName: "pickerNewFolderCreate"; text: qsTr("Create folder"); checked: true; enabled: name.text.trim() !== ""; onClicked: root.create() }
        }
    }
}
