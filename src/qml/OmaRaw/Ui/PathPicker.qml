import QtCore
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts

// Shared open, save and folder browser. Listings are asynchronous; paths are
// revalidated before acceptance, with explicit confirmation for replacement.
ModalPanel {
    id: root
    enum Mode { OpenFile, OpenFiles, SaveFile, ChooseFolder }
    property int fileMode: PathPicker.OpenFile
    readonly property bool folderMode: fileMode === PathPicker.ChooseFolder
    readonly property bool saveMode: fileMode === PathPicker.SaveFile
    readonly property bool multiple: fileMode === PathPicker.OpenFiles
    property string title: qsTr("Choose a folder")
    property string description: ""
    property string acceptLabel: folderMode ? qsTr("Choose folder") : saveMode ? qsTr("Save") : qsTr("Open")
    property url currentFolder: ""
    property url selectedFolder: ""
    property url selectedFile: ""
    property var selectedFiles: []
    property var nameFilters: []
    property int filterIndex: 0
    property string defaultSuffix: ""
    property bool imagePreview: false
    readonly property bool showImagePreview: imagePreview && fileMode === PathPicker.OpenFile
    property string pendingSelection: ""
    readonly property var previewEntry: showImagePreview && opened && !loading && !pathPending
                                        && marked.indexOf(folders.currentIndex) >= 0
                                        ? entries[folders.currentIndex] || ({}) : ({})
    property alias fileName: nameField.text
    property var marked: []
    property int selectionAnchor: -1
    property var pendingSave: ({})
    readonly property var activeFilters: {
        if (!nameFilters.length) return []
        const match = nameFilters[Math.min(filterIndex, nameFilters.length - 1)].match(/\(([^()]*)\)\s*$/)
        return match ? match[1].trim().split(/\s+/) : []
    }
    property var folderInfo: ({})
    property var places: []
    property string error: ""
    property var entries: []
    property bool loading: false
    property int listingRequest: 0
    readonly property bool pathPending: pathField.text !== (folderInfo.path || "")
    readonly property url candidate: folders.currentIndex >= 0 && folders.currentIndex < entries.length
                                    ? entries[folders.currentIndex].url : currentFolder
    readonly property bool ready: !pathPending && !loading && error === "" && currentFolder.toString() !== ""
                                  && (folderMode || (saveMode ? nameField.text.trim() !== "" : marked.length > 0))
    signal accepted()

    function navigate(path) {
        pendingSelection = ""
        const info = backend.browseFolder(path.toString())
        if (info.error) { error = info.error; return false }
        folderInfo = info
        currentFolder = info.url
        folders.currentIndex = -1
        pathField.text = info.path
        error = ""
        loadFolders()
        folders.forceActiveFocus()
        return true
    }
    function openFor(path) {
        if (path) currentFolder = backend.fileUrl(path.toString())
        open()
    }
    function accept() {
        if (!ready) return
        if (folderMode) {
            const info = backend.browseFolder(candidate.toString())
            if (info.error) { error = info.error; return }
            selectedFolder = info.url
            currentFolder = info.url
        } else if (saveMode) {
            const name = nameField.text.trim()
            if (name === "." || name === ".." || /[\/\\]/.test(name)) { error = qsTr("Enter a file name without path separators."); return }
            const info = backend.pickerFile(folderInfo.path + "/" + name, true, defaultSuffix)
            if (info.error) { error = info.error; return }
            if (info.exists) { pendingSave = info; replaceDialog.open(); return }
            selectedFile = info.url; selectedFiles = [info.url]
        } else {
            const files = []
            for (const index of marked) {
                const info = backend.pickerFile(entries[index].url, false, "")
                if (info.error) { error = info.error; return }
                files.push(info.url)
            }
            selectedFiles = files; selectedFile = files[0]
        }
        close()
        accepted()
    }
    function replaceFile() {
        const info = backend.pickerFile(pendingSave.url, true, "")
        if (info.error) { error = info.error; replaceDialog.close(); return }
        selectedFile = info.url; selectedFiles = [info.url]
        replaceDialog.close(); close(); accepted()
    }
    function selectEntry(index, modifiers) {
        if (index < 0 || index >= entries.length) return
        folders.currentIndex = index
        folders.positionViewAtIndex(index, ListView.Contain)
        if (!folderMode) {
            if (entries[index].isDir) marked = []
            else if (multiple && (modifiers & Qt.ShiftModifier) && selectionAnchor >= 0) {
                const range = []
                for (let i = Math.min(index, selectionAnchor); i <= Math.max(index, selectionAnchor); ++i)
                    if (!entries[i].isDir) range.push(i)
                marked = range
            } else if (multiple && (modifiers & Qt.ControlModifier)) {
                marked = marked.indexOf(index) >= 0 ? marked.filter(i => i !== index) : marked.concat([index])
            } else marked = [index]
            if (saveMode && !entries[index].isDir) nameField.text = entries[index].name
        }
        if (!(modifiers & Qt.ShiftModifier)) selectionAnchor = index
        folders.forceActiveFocus()
    }
    function activateEntry(index) {
        if (index < 0 || index >= entries.length) return
        if (entries[index].isDir) navigate(entries[index].url)
        else { if (!multiple || marked.indexOf(index) < 0) selectEntry(index, Qt.NoModifier); accept() }
    }
    function openPath() {
        if (folderMode || !backend.browseFolder(pathField.text).error) { navigate(pathField.text); return }
        const info = backend.pickerFile(pathField.text, saveMode, defaultSuffix)
        if (info.error) { error = info.error; return }
        if (saveMode) { navigate(info.parent); nameField.text = info.name; return }
        if (showImagePreview) {
            if (navigate(info.parent)) pendingSelection = info.url.toString()
            return
        }
        selectedFile = info.url; selectedFiles = [info.url]; currentFolder = info.parent
        close(); accepted()
    }
    function up() { if (folderInfo.parent) navigate(folderInfo.parent) }
    function loadFolders() {
        if (!currentFolder.toString()) return
        entries = []
        marked = []; selectionAnchor = -1
        folders.currentIndex = -1
        loading = true
        listingRequest = backend.requestPathListing(currentFolder, hidden.checked, folderMode, activeFilters)
    }
    onActiveFiltersChanged: if (opened) loadFolders()
    function refreshPlaces() {
        const items = [
            {name: qsTr("Pictures"), path: StandardPaths.writableLocation(StandardPaths.PicturesLocation), icon: "image"},
            {name: qsTr("Home"), path: StandardPaths.writableLocation(StandardPaths.HomeLocation), icon: "folder"},
            {name: qsTr("Downloads"), path: StandardPaths.writableLocation(StandardPaths.DownloadLocation), icon: "download"},
            {name: qsTr("Computer"), path: "/", icon: "hard-drive"}
        ]
        for (const drive of backend.devices())
            items.push({name: drive.name, path: drive.path, icon: "hard-drive"})
        places = items
    }
    onAboutToShow: {
        selectedFolder = ""; pendingSave = ({})
        refreshPlaces()
        if (!folderMode && !currentFolder.toString() && selectedFile.toString()) {
            const initial = backend.pickerFile(selectedFile, saveMode, defaultSuffix)
            if (!initial.error) { currentFolder = initial.parent; if (saveMode) nameField.text = initial.name }
        }
        const start = currentFolder.toString() || StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        if (!navigate(start)) navigate(StandardPaths.writableLocation(StandardPaths.HomeLocation))
    }

    width: parent ? Math.min(showImagePreview ? 1040 : 800, parent.width - Theme.s5 * 2) : showImagePreview ? 1040 : 800
    height: parent ? Math.min(580, parent.height - Theme.s5 * 2) : 580

    Shortcut { sequence: "Ctrl+L"; enabled: root.opened && !replaceDialog.visible && !newFolderDialog.visible; onActivated: { pathField.forceActiveFocus(); pathField.selectAll() } }
    Shortcut { sequence: "Alt+Up"; enabled: root.opened && !replaceDialog.visible && !newFolderDialog.visible; onActivated: root.up() }
    Timer { interval: 4000; running: root.opened; repeat: true; onTriggered: root.refreshPlaces() }
    Connections {
        target: backend
        function onPathListingReady(request, entries, error) {
            if (request !== root.listingRequest) return
            root.entries = entries
            root.loading = false
            root.error = error
            folders.currentIndex = -1
            if (root.pendingSelection !== "") {
                const index = entries.findIndex(entry => entry.url.toString() === root.pendingSelection)
                root.pendingSelection = ""
                if (index >= 0) root.selectEntry(index, Qt.NoModifier)
                else if (!error) root.error = qsTr("This file is not shown by the selected file type.")
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.s3
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: root.title; color: Theme.textPrimary; elide: Text.ElideRight
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading
            }
            IconButton { iconName: "x"; text: qsTr("Cancel"); onClicked: root.close() }
        }
        Text {
            Layout.fillWidth: true
            visible: root.description !== ""
            text: root.description; wrapMode: Text.Wrap
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s2
            IconButton {
                objectName: "folderPickerUp"
                iconName: "arrow-up"; text: qsTr("Parent folder"); shortcut: "Alt+Up"
                enabled: !!root.folderInfo.parent && root.folderInfo.parent.toString() !== root.currentFolder.toString()
                onClicked: root.up()
            }
            C.TextField {
                id: pathField
                objectName: "folderPickerPath"
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.hControl
                color: Theme.textPrimary; selectionColor: Theme.accent; selectedTextColor: Theme.accentText
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
                leftPadding: Theme.s2; rightPadding: Theme.s2
                selectByMouse: true
                Accessible.name: qsTr("Folder path")
                background: Rectangle { color: Theme.inputBg; radius: Theme.rControl; border.color: pathField.activeFocus ? Theme.accent : Theme.border }
                onTextEdited: root.error = ""
                onAccepted: root.openPath()
            }
            ToolButton { text: qsTr("Go"); showLabel: true; onClicked: root.openPath() }
            ToolButton {
                objectName: "pickerNewFolder"
                iconName: "folder-plus"; text: qsTr("New folder"); showLabel: true
                enabled: !root.pathPending && !root.loading && root.error === "" && root.currentFolder.toString() !== ""
                tip: qsTr("Create a folder inside the folder shown here.")
                onClicked: newFolderDialog.open()
            }
        }
        Text {
            objectName: "folderPickerError"
            Layout.fillWidth: true
            visible: root.error !== ""
            text: root.error; wrapMode: Text.Wrap
            color: Theme.danger; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.s3
            C.ScrollView {
                id: placesScroll
                visible: !root.showImagePreview || root.width >= 900
                Layout.preferredWidth: Math.min(170, root.availableWidth * 0.27)
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                C.ScrollBar.vertical: ScrollBar {}
                Column {
                    width: placesScroll.availableWidth
                    spacing: Theme.s1
                    Text {
                        width: parent.width; height: Theme.hRow
                        text: qsTr("Places"); color: Theme.textMuted
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
                    }
                    Repeater {
                        model: root.places
                        C.ItemDelegate {
                            id: place
                            required property var modelData
                            width: parent.width; height: Theme.hTab
                            padding: Theme.s2
                            Accessible.name: modelData.name
                            background: Rectangle { radius: Theme.rControl; color: place.hovered || place.visualFocus ? Theme.hoverBg : "transparent" }
                            contentItem: RowLayout {
                                spacing: Theme.s2
                                Icon { name: place.modelData.icon; color: Theme.textMuted }
                                Text {
                                    Layout.fillWidth: true
                                    text: place.modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight
                                    color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
                                }
                            }
                            onClicked: root.navigate(modelData.path)
                        }
                    }
                }
            }
            Rectangle { visible: placesScroll.visible; Layout.fillHeight: true; width: Theme.hairline; color: Theme.border }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.windowBg; radius: Theme.rControl
                ListView {
                    id: folders
                    objectName: "folderPickerList"
                    anchors.fill: parent; anchors.margins: Theme.s1
                    clip: true; focus: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: root.entries
                    currentIndex: -1
                    keyNavigationEnabled: true
                    enabled: !root.loading && root.error === ""
                    C.ScrollBar.vertical: ScrollBar {}
                    Keys.onPressed: event => {
                        let index = currentIndex
                        if (event.key === Qt.Key_Up) index = Math.max(0, index - 1)
                        else if (event.key === Qt.Key_Down) index = Math.min(root.entries.length - 1, index + 1)
                        else if (event.key === Qt.Key_Home) index = 0
                        else if (event.key === Qt.Key_End) index = root.entries.length - 1
                        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { root.activateEntry(index); event.accepted = true; return }
                        else if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier) && root.multiple) {
                            root.marked = root.entries.map((entry, i) => entry.isDir ? -1 : i).filter(i => i >= 0)
                            event.accepted = true; return
                        } else return
                        root.selectEntry(index, event.modifiers); event.accepted = true
                    }
                    delegate: C.ItemDelegate {
                        id: folderRow
                        required property int index
                        required property var modelData
                        readonly property string fileName: modelData.name
                        readonly property url fileUrl: modelData.url
                        readonly property bool selected: root.folderMode ? ListView.isCurrentItem : root.marked.indexOf(index) >= 0
                        objectName: "folderPickerRow_" + index
                        width: ListView.view.width; height: Theme.hTab + Theme.s1
                        padding: Theme.s2
                        Accessible.name: fileName
                        Accessible.role: Accessible.ListItem
                        Accessible.selected: selected
                        background: Rectangle {
                            radius: Theme.rControl
                            color: folderRow.selected ? Theme.selectionFill : pointer.containsMouse ? Theme.hoverBg : "transparent"
                            border.width: folderRow.selected || folderRow.ListView.isCurrentItem ? Theme.hairline : 0
                            border.color: folderRow.selected ? Theme.accent : Theme.borderStrong
                        }
                        contentItem: RowLayout {
                            spacing: Theme.s2
                            Icon { name: folderRow.modelData.isDir ? "folder" : "file"; color: Theme.textSecondary }
                            Text {
                                Layout.fillWidth: true
                                text: folderRow.fileName; textFormat: Text.PlainText; elide: Text.ElideMiddle
                                color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
                            }
                            Icon { name: "chevron-right"; color: Theme.textMuted; visible: folderRow.modelData.isDir }
                        }
                        onClicked: root.selectEntry(index, Qt.NoModifier)
                        MouseArea {
                            id: pointer
                            anchors.fill: parent; hoverEnabled: true
                            onClicked: mouse => root.selectEntry(folderRow.index, mouse.modifiers)
                            onDoubleClicked: root.activateEntry(folderRow.index)
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent; width: parent.width - Theme.s5 * 2
                    visible: root.loading || root.entries.length === 0
                    text: root.loading ? qsTr("Loading…") : root.error !== "" ? qsTr("Folder unavailable")
                          : root.folderMode ? qsTr("No subfolders\nYou can still choose this folder.") : qsTr("No matching files in this folder")
                    horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                    color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase
                }
            }
            Rectangle {
                objectName: "pickerPreviewPane"
                visible: root.showImagePreview
                Layout.preferredWidth: Math.min(360, root.availableWidth * 0.45)
                Layout.fillHeight: true
                color: Theme.pasteboard; radius: Theme.rControl
                border.color: Theme.border
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: Theme.s3
                    spacing: Theme.s2
                    Text {
                        text: qsTr("Preview"); color: Theme.textSecondary
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
                    }
                    Item {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        Image {
                            id: photograph; objectName: "pickerPreviewImage"
                            anchors.fill: parent
                            source: root.previewEntry.preview || ""
                            asynchronous: true; fillMode: Image.PreserveAspectFit
                            visible: status === Image.Ready
                            Accessible.name: qsTr("Preview of %1").arg(root.previewEntry.name || "")
                        }
                        Text {
                            objectName: "pickerPreviewStatus"
                            anchors.centerIn: parent; width: parent.width
                            visible: photograph.status !== Image.Ready
                            text: photograph.status === Image.Loading ? qsTr("Loading preview…")
                                  : photograph.status === Image.Error ? qsTr("Preview unavailable")
                                  : qsTr("Select a photograph to preview it")
                            wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
                            color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase
                        }
                    }
                    Text {
                        objectName: "pickerPreviewName"
                        Layout.fillWidth: true
                        text: root.previewEntry.name || ""; textFormat: Text.PlainText; elide: Text.ElideMiddle
                        color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.previewEntry.detail || ""; elide: Text.ElideRight
                        color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            CheckField { id: hidden; objectName: "folderPickerHidden"; text: qsTr("Show hidden folders"); onToggled: root.loadFolders() }
            Item { Layout.fillWidth: true }
            Text { text: root.multiple ? qsTr("Ctrl or Shift to select several files") : root.showImagePreview ? qsTr("Select to preview · Open to choose") : qsTr("Double-click to open a folder"); color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: !root.folderMode && root.nameFilters.length > 0
            Text { Layout.preferredWidth: Theme.s5 * 3; text: qsTr("File type"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase }
            ComboField {
                objectName: "pickerFilter"
                Layout.fillWidth: true
                model: root.nameFilters; currentIndex: root.filterIndex
                onActivated: i => root.filterIndex = i
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.saveMode
            Text { Layout.preferredWidth: Theme.s5 * 3; text: qsTr("File name"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase }
            C.TextField {
                id: nameField; objectName: "pickerFileName"
                Layout.fillWidth: true; Layout.preferredHeight: Theme.hControl
                color: Theme.textPrimary; placeholderTextColor: Theme.textMuted
                selectionColor: Theme.accent; selectedTextColor: Theme.accentText
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
                leftPadding: Theme.s2; rightPadding: Theme.s2; selectByMouse: true
                Accessible.name: qsTr("File name"); placeholderText: qsTr("Enter a file name")
                background: Rectangle { color: Theme.inputBg; radius: Theme.rControl; border.color: nameField.activeFocus ? Theme.accent : Theme.border }
                onTextEdited: root.error = ""
                onAccepted: root.accept()
            }
        }
        Rectangle { Layout.fillWidth: true; height: Theme.hairline; color: Theme.border }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s3
            Text {
                objectName: "folderPickerSelection"
                Layout.fillWidth: true
                text: root.pathPending ? qsTr("Press Enter to open this path")
                    : !root.folderMode && root.marked.length > 1 ? qsTr("%1 files selected").arg(root.marked.length)
                    : backend.localFile(root.candidate)
                textFormat: Text.PlainText; elide: Text.ElideMiddle
                color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase
            }
            ToolButton { objectName: "folderPickerCancel"; text: qsTr("Cancel"); showLabel: true; onClicked: root.close() }
            ToolButton {
                objectName: "folderPickerAccept"
                text: root.acceptLabel; showLabel: true; checked: true
                enabled: root.ready
                onClicked: root.accept()
            }
        }
    }
    ModalPanel {
        id: replaceDialog; objectName: "pickerReplaceDialog"
        width: parent ? Math.min(460, parent.width - Theme.s5 * 2) : 460
        contentItem: Column {
            spacing: Theme.s3
            Text { width: parent.width; text: qsTr("Replace this file?"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading }
            Text { width: parent.width; text: qsTr("%1 already exists. Saving will replace its contents.").arg(root.pendingSave.name || ""); textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase }
            Row {
                anchors.right: parent.right; spacing: Theme.s2
                ToolButton { objectName: "pickerReplaceCancel"; text: qsTr("Cancel"); onClicked: replaceDialog.close() }
                ToolButton { objectName: "pickerReplaceConfirm"; text: qsTr("Replace"); checked: true; onClicked: root.replaceFile() }
            }
        }
    }
    NewFolderDialog {
        id: newFolderDialog; objectName: "pickerNewFolderDialog"
        parentFolder: root.currentFolder.toString()
        onCreated: folder => root.navigate(folder)
    }
}
