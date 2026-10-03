import QtCore
import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

// The Library's disk browser. Folder clicks review files without cataloging them.
Rectangle {
    id: root
    objectName: "libraryFolderBrowser"
    color: Theme.panelBg
    property string currentFolder: ""
    property var favourites: []
    property var folderInfo: ({})
    property var entries: []
    property var places: []
    property string error: ""
    property bool loading: false
    property bool initialized: false
    property int listingRequest: -1
    signal folderSelected(string path)
    signal navigationFailed(string message)
    readonly property bool favourite: favourites.indexOf(currentFolder) >= 0
    Settings {
        category: "importBrowser"
        property alias lastFolder: root.currentFolder
        property alias favouriteFolders: root.favourites
    }
    function refreshPlaces() {
        const items = [
            {name: qsTr("Pictures"), path: StandardPaths.writableLocation(StandardPaths.PicturesLocation), icon: "image"},
            {name: qsTr("Home"), path: StandardPaths.writableLocation(StandardPaths.HomeLocation), icon: "folder"},
            {name: qsTr("Downloads"), path: StandardPaths.writableLocation(StandardPaths.DownloadLocation), icon: "download"},
            {name: qsTr("Computer"), path: "/", icon: "hard-drive"}
        ]
        for (const drive of backend.devices()) items.push({name: drive.name, path: drive.dcim ? drive.path + "/DCIM" : drive.path, icon: drive.dcim ? "camera" : "hard-drive"})
        places = items
    }
    function navigate(path) {
        const info = backend.browseFolder(path.toString())
        if (info.error) { error = info.error; navigationFailed(error); return false }
        currentFolder = info.path; folderInfo = info; pathField.text = info.path
        error = ""; entries = []; loading = true; initialized = true
        listingRequest = backend.requestPathListing(info.path, false, true, [])
        folders.currentIndex = -1
        folderSelected(info.path)
        return true
    }
    function activate(path) {
        refreshPlaces()
        if (path) return navigate(path)
        if (initialized) return true
        if (!navigate(currentFolder || StandardPaths.writableLocation(StandardPaths.PicturesLocation)))
            return navigate(StandardPaths.writableLocation(StandardPaths.HomeLocation))
        return true
    }
    function up() { if (folderInfo.parent) navigate(folderInfo.parent) }
    function toggleFavourite() {
        if (!currentFolder) return
        favourites = favourite ? favourites.filter(path => path !== currentFolder) : favourites.concat([currentFolder])
    }
    NewFolderDialog {
        id: newFolderDialog; objectName: "browseNewFolderDialog"
        onCreated: folder => root.navigate(folder)
    }
    Timer { interval: 4000; running: root.visible; repeat: true; onTriggered: root.refreshPlaces() }
    Connections {
        target: backend
        function onPathListingReady(request, entries, error) {
            if (request !== root.listingRequest) return
            root.entries = entries; root.error = error; root.loading = false
        }
    }
    Shortcut { sequence: "Ctrl+L"; enabled: root.visible; onActivated: pathField.focusInput() }
    Shortcut { sequence: "Alt+Up"; enabled: root.visible; onActivated: root.up() }
    Column {
        id: navigation
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: Theme.s3; spacing: Theme.s2
        Row {
            spacing: Theme.s1
            IconButton { objectName: "browseUp"; iconName: "arrow-up"; text: qsTr("Parent folder"); shortcut: "Alt+Up"; enabled: root.currentFolder !== "" && root.currentFolder !== "/"; onClicked: root.up() }
            IconButton { objectName: "browseRefresh"; iconName: "refresh-cw"; text: qsTr("Refresh folder"); enabled: root.currentFolder !== ""; onClicked: root.navigate(root.currentFolder) }
            IconButton { objectName: "browseFavourite"; iconName: "star"; text: root.favourite ? qsTr("Remove favourite folder") : qsTr("Favourite this folder"); checked: root.favourite; enabled: root.currentFolder !== ""; onClicked: root.toggleFavourite() }
        }
        SearchField { id: pathField; objectName: "browsePath"; width: parent.width; glyph: "folder"; placeholder: qsTr("Folder path…"); live: false; tip: qsTr("Enter a folder path. Ctrl+L focuses this field."); onAccepted: path => root.navigate(path) }
        ToolButton {
            objectName: "browseNewFolder"
            width: parent.width; iconName: "folder-plus"; text: qsTr("New folder"); showLabel: true
            enabled: root.currentFolder !== "" && !root.loading && root.error === "" && pathField.text === root.currentFolder
            tip: qsTr("Create a folder inside the folder you are browsing.")
            onClicked: newFolderDialog.openFor(root.currentFolder)
        }
    }
    C.ScrollView {
        id: shortcuts
        anchors.top: navigation.bottom; anchors.topMargin: Theme.s2
        width: parent.width; height: Math.min(placesColumn.implicitHeight, root.height * .4)
        contentWidth: availableWidth; clip: true
        C.ScrollBar.vertical: ScrollBar {}
        Column {
            id: placesColumn
            width: shortcuts.availableWidth
            SectionHeader { title: qsTr("Places") }
            Repeater {
                model: root.places
                SourceRow { required property var modelData; objectName: "browsePlace_" + modelData.path; name: modelData.name; iconName: modelData.icon; current: root.currentFolder === modelData.path; onClicked: root.navigate(modelData.path) }
            }
            SectionHeader { title: qsTr("Favourites"); visible: root.favourites.length > 0 }
            Repeater {
                model: root.favourites
                SourceRow { required property string modelData; name: modelData.split("/").filter(part => part !== "").pop() || "/"; iconName: "star"; current: root.currentFolder === modelData; onClicked: root.navigate(modelData) }
            }
        }
    }
    SectionHeader { id: folderTitle; anchors.top: shortcuts.bottom; width: parent.width; title: qsTr("Folders") }
    Text {
        id: current
        anchors.top: folderTitle.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: Theme.s3
        height: Theme.hRow
        text: root.currentFolder.split("/").filter(part => part !== "").pop() || "/"; textFormat: Text.PlainText; elide: Text.ElideMiddle
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl; color: Theme.textPrimary
    }
    ListView {
        id: folders; objectName: "browseFolders"
        anchors.top: current.bottom; anchors.bottom: message.top; width: parent.width; clip: true
        model: root.entries
        C.ScrollBar.vertical: ScrollBar {}
        delegate: SourceRow {
            required property var modelData
            objectName: "browseFolder_" + modelData.name
            name: modelData.name; depth: 1
            onClicked: root.navigate(modelData.url)
        }
    }
    Text {
        id: message
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: Theme.s3
        text: root.error || (root.loading ? qsTr("Finding folders…") : root.entries.length ? "" : qsTr("No subfolders"))
        textFormat: Text.PlainText; wrapMode: Text.Wrap
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: root.error ? Theme.warning : Theme.textMuted
    }
}
