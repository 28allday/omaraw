pragma ComponentBehavior: Bound
// Creating a catalog: a name and the folder to keep it in. A catalog owns
// its folder — the Develop databases, previews and backups live beside it —
// so this shows the folder that will be created before anything is written.
//
// Used both by the main window and by the startup chooser, which has no
// main window to parent a dialog to; it is a Window rather than a Popup so
// it can serve both.
import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

C.ApplicationWindow {
    id: root
    objectName: "newCatalogDialog"
    title: qsTr("New Catalog")
    width: 520
    height: contents.implicitHeight + Theme.s5 * 2
    minimumWidth: 380
    minimumHeight: 260
    flags: Qt.Dialog
    modality: Qt.ApplicationModal
    color: Theme.windowBg

    // The catalog that was created, once the user has confirmed.
    signal created(string path)

    property string parentFolder: ""

    function open() {
        root.parentFolder = backend.newCatalogParent()
        nameField.text = qsTr("Photos")
        error.text = ""
        root.show()
        root.requestActivate()
        nameField.focusInput()
    }

    readonly property string proposed: backend.proposedCatalogPath(root.parentFolder, nameField.text)

    function create() {
        error.text = ""
        const path = backend.createCatalog(root.parentFolder, nameField.text)
        if (path === "") { error.text = backend.statusMessage; return }
        root.close()
        root.created(path)
    }

    Column {
        id: contents
        anchors.fill: parent
        anchors.margins: Theme.s5
        spacing: Theme.s3

        Text {
            width: parent.width
            text: qsTr("New catalog")
            color: Theme.textPrimary
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsTitle; font.bold: true
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap
            text: qsTr("The catalog records where your photos are stored, along with edits, ratings, keywords and albums.")
            color: Theme.textMuted
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase
        }

        Text {
            text: qsTr("Catalog name"); color: Theme.textMuted
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        }
        SearchField {
            id: nameField
            objectName: "newCatalogName"
            width: parent.width
            placeholder: qsTr("Catalog name")
            tip: qsTr("Name the catalog and the new folder that will hold its edits, keywords and albums.")
            glyph: "database"
            // Naming is not a search: only Return or Create may submit.
            live: false
            onAccepted: root.create()
        }

        Text {
            text: qsTr("Catalog location"); color: Theme.textMuted
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        }
        Row {
            width: parent.width
            spacing: Theme.s2
            Text {
                width: parent.width - browse.width - Theme.s2
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideMiddle; textFormat: Text.PlainText
                text: root.parentFolder
                color: Theme.textPrimary
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase
            }
            ToolButton {
                id: browse
                text: qsTr("Choose…"); showLabel: true
                tip: qsTr("Choose the parent folder for the new catalog. OmaRAW creates its own folder inside it.")
                onClicked: folderDialog.open()
            }
        }

        Text {
            width: parent.width; wrapMode: Text.WrapAnywhere
            textFormat: Text.PlainText
            visible: root.proposed !== ""
            text: qsTr("Creates %1").arg(root.proposed)
            color: Theme.textMuted
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        }
        Text {
            text: qsTr("Photo storage"); color: Theme.textMuted
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap
            text: qsTr("Choose photo storage when you import. Add keeps photos in their current folders; Copy or Move uses a destination you choose. Photo files are stored separately from the catalog.")
            color: Theme.textMuted
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase
        }
        Text {
            id: error
            objectName: "newCatalogError"
            width: parent.width; wrapMode: Text.Wrap
            textFormat: Text.PlainText
            visible: text !== ""
            color: Theme.danger
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        }

        Row {
            anchors.right: parent.right
            spacing: Theme.s2
            ToolButton { objectName: "newCatalogCancel"; text: qsTr("Cancel"); showLabel: true; onClicked: root.close() }
            ToolButton {
                objectName: "newCatalogCreate"
                text: qsTr("Create & Open"); showLabel: true
                tip: qsTr("Create the catalog at the path shown and open it.")
                enabled: root.proposed !== ""
                onClicked: root.create()
            }
        }
    }

    FolderPicker {
        id: folderDialog
        objectName: "folderDialog"
        title: qsTr("Choose catalog location")
        currentFolder: backend.fileUrl(root.parentFolder)
        onAccepted: root.parentFolder = backend.localFile(selectedFolder.toString())
    }
}
