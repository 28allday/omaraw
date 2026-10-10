pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

// Left dock of the Library: catalog, folders, albums, smart albums, recent
// imports. Every row is a real source; counts come from the catalog.
Rectangle {
    id: root
    property var shell: null
    color: Theme.panelBg

    // Re-read the lists whenever the catalog changes (import, flag, album).
    property var sections: []
    property var folders: []
    property var albums: []
    property var smart: []
    property var imports: []
    property var keywords: []
    property var devices: []
    property var collapsed: ({})
    property var menuKeyword: ({})
    function reload() {
        sections = backend.catalogSections()
        folders = backend.folderTree()
        albums = backend.albums().filter(a => a.name !== "__quick")
        smart = backend.smartAlbums()
        imports = backend.recentImports()
        keywords = backend.keywordTree()
        devices = backend.devices()
    }
    // Keyword rows collapse like folders; their keys are prefixed so a
    // keyword and a folder of the same name never share a state.
    function keywordHidden(k) {
        for (const p in collapsed)
            if (p.startsWith("kw:") && collapsed[p] && "kw:" + k.path !== p && ("kw:" + k.path).startsWith(p + "/")) return true
        return false
    }
    function keywordHasChildren(k) { return keywords.some(o => o.path !== k.path && o.path.startsWith(k.path + "/")) }
    // An album row hides when any ancestor album is collapsed.
    function albumHidden(a) {
        let p = a.parent
        for (let guard = 0; p && guard < 32; ++guard) {
            if (collapsed["al:" + p]) return true
            const parentRow = albums.find(o => o.id === p)
            p = parentRow ? parentRow.parent : 0
        }
        return false
    }
    Component.onCompleted: reload()
    // Folder actions: relink a moved folder, rescan every folder.
    property var menuFolder: ({})
    FolderPicker {
        id: relinkDialog
        objectName: "relinkDialog"
        title: qsTr("Where is \"%1\" now?").arg(root.menuFolder.name || "")
        onAccepted: {
            const err = backend.relinkFolder(root.menuFolder.id, selectedFolder.toString())
            if (err !== "") backend.setStatus(qsTr("Could not relink: %1").arg(err))
        }
    }
    // Albums: rename, remove the selection, delete; smart albums: edit, delete.
    property var menuAlbum: ({})
    SmartAlbumDialog { id: smartDialog }
    C.Popup {
        id: renameAlbumDialog
        modal: true
        parent: C.Overlay.overlay
        anchors.centerIn: parent
        width: 360; padding: Theme.s4
        background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }
        onOpened: { albumNameField.text = root.menuAlbum.name || ""; albumNameField.forceActiveFocus() }
        Column {
            width: parent.width
            spacing: Theme.s3
            Text { text: qsTr("Rename album"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading; color: Theme.textPrimary }
            SearchField {
                id: albumNameField
                width: parent.width
                placeholder: qsTr("Album name")
                tip: qsTr("The new name for this album.")
                live: false
                onAccepted: t => { if (t.trim() !== "") { backend.renameAlbum(root.menuAlbum.id, t.trim()); renameAlbumDialog.close() } }
            }
            Row {
                anchors.right: parent.right
                spacing: Theme.s2
                ToolButton { text: qsTr("Cancel"); showLabel: true; onClicked: renameAlbumDialog.close() }
                ToolButton { text: qsTr("Rename"); showLabel: true; enabled: albumNameField.text.trim() !== ""; onClicked: { backend.renameAlbum(root.menuAlbum.id, albumNameField.text.trim()); renameAlbumDialog.close() } }
            }
        }
    }
    ContextMenu {
        id: albumMenu
        MenuAction { text: qsTr("Show Photos"); iconName: "images"; onTriggered: backend.setSource("album", root.menuAlbum.id) }
        MenuAction { text: qsTr("Add Selection"); iconName: "plus"; enabled: backend.currentId > 0; onTriggered: backend.addSelectionToAlbum(root.menuAlbum.id) }
        MenuAction { text: qsTr("Remove Selection from Album"); iconName: "minus"; enabled: backend.currentId > 0; onTriggered: backend.removeSelectionFromAlbum(root.menuAlbum.id) }
        MenuAction { text: qsTr("Rename…"); iconName: "pencil"; onTriggered: renameAlbumDialog.open() }
        MenuAction { text: qsTr("New Album Inside…"); iconName: "folder-plus"; onTriggered: root.shell.newAlbumInside(root.menuAlbum.id) }
        MenuAction { text: qsTr("Move to Top Level"); iconName: "arrow-up"; enabled: (root.menuAlbum.parent || 0) > 0; onTriggered: backend.setAlbumParent(root.menuAlbum.id, 0) }
        MenuAction { text: qsTr("Delete Album"); iconName: "trash-2"; onTriggered: backend.deleteAlbum(root.menuAlbum.id) }
    }
    ContextMenu {
        id: keywordMenu
        MenuAction { text: qsTr("Show Photos"); iconName: "images"; onTriggered: backend.setSource("keyword", root.menuKeyword.id) }
        MenuAction { text: qsTr("Add to Selection"); iconName: "plus"; enabled: backend.selectedCount > 0; onTriggered: backend.addKeyword(root.menuKeyword.path) }
        MenuAction { text: qsTr("Remove from Selection"); iconName: "minus"; enabled: backend.selectedCount > 0; onTriggered: backend.removeKeyword(root.menuKeyword.path) }
        MenuAction { text: qsTr("Synonyms…"); iconName: "tag"; onTriggered: synonymsDialog.open() }
        MenuAction { text: qsTr("Delete Keyword and Children"); iconName: "trash-2"; onTriggered: backend.deleteKeyword(root.menuKeyword.id) }
    }
    C.Popup {
        id: synonymsDialog
        modal: true
        parent: C.Overlay.overlay
        anchors.centerIn: parent
        width: 380; padding: Theme.s4
        background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }
        onOpened: { synonymsField.text = backend.keywordSynonyms(root.menuKeyword.id).join(", "); synonymsField.forceActiveFocus() }
        function save() { backend.setKeywordSynonyms(root.menuKeyword.id, synonymsField.text); synonymsDialog.close() }
        Column {
            width: parent.width
            spacing: Theme.s3
            Text { text: qsTr("Synonyms for %1").arg(root.menuKeyword.name || ""); textFormat: Text.PlainText; font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading; color: Theme.textPrimary }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: qsTr("Other words for the same thing, separated by commas. Search finds the photo by any of them, and typing one suggests the keyword.")
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
            SearchField {
                id: synonymsField
                width: parent.width
                placeholder: qsTr("e.g. Venezia, La Serenissima")
                tip: qsTr("Other words this keyword answers to in search, separated by commas.")
                live: false
                onAccepted: t => synonymsDialog.save()
            }
            Row {
                anchors.right: parent.right
                spacing: Theme.s2
                ToolButton { text: qsTr("Cancel"); showLabel: true; onClicked: synonymsDialog.close() }
                ToolButton { text: qsTr("Save"); showLabel: true; onClicked: synonymsDialog.save() }
            }
        }
    }
    ContextMenu {
        id: smartMenu
        MenuAction { text: qsTr("Show Photos"); iconName: "images"; onTriggered: backend.setSource("smart", root.menuAlbum.id) }
        MenuAction { text: qsTr("Edit Rules…"); iconName: "pencil"; onTriggered: smartDialog.openFor(root.menuAlbum.id, root.menuAlbum.name) }
        MenuAction { text: qsTr("Delete Smart Album"); iconName: "trash-2"; onTriggered: backend.deleteAlbum(root.menuAlbum.id) }
    }
    ContextMenu {
        id: folderMenu
        MenuAction { text: qsTr("Relink Folder…"); iconName: "link"; onTriggered: relinkDialog.open() }
        MenuAction { text: qsTr("Rescan Folders"); iconName: "refresh-cw"; onTriggered: backend.rescanFolders() }
        MenuAction { text: qsTr("Show Photos"); iconName: "images"; onTriggered: backend.setSource("folder", root.menuFolder.id) }
        MenuAction { text: qsTr("Watch for New Photos"); checkable: true; checked: root.menuFolder.watched === true; onTriggered: backend.setFolderWatched(root.menuFolder.id, !(root.menuFolder.watched === true)) }
        MenuAction { text: qsTr("Remove from Catalog…"); iconName: "unlink"; onTriggered: removeFolderDialog.open() }
    }
    C.Popup {
        id: removeFolderDialog
        objectName: "removeFolderDialog"
        modal: true
        parent: C.Overlay.overlay
        anchors.centerIn: parent
        width: 400; padding: Theme.s4
        background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }
        Column {
            width: parent.width
            spacing: Theme.s3
            Text {
                width: parent.width
                text: qsTr("Remove \"%1\" from the catalog?").arg(root.menuFolder.name || "")
                textFormat: Text.PlainText; wrapMode: Text.Wrap
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading; color: Theme.textPrimary
            }
            Text {
                width: parent.width
                objectName: "removeFolderText"
                text: (root.menuFolder.count || 0) === 1
                      ? qsTr("1 photo in this folder and the folders inside it will leave the catalog, with its edits, rating, keywords and album places. The file stays on disk; import the folder again to bring it back.")
                      : qsTr("%1 photos in this folder and the folders inside it will leave the catalog, with their edits, ratings, keywords and album places. The files stay on disk; import the folder again to bring them back.").arg(root.menuFolder.count || 0)
                textFormat: Text.PlainText; wrapMode: Text.Wrap
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl; color: Theme.textSecondary
            }
            Row {
                anchors.right: parent.right
                spacing: Theme.s2
                ToolButton { text: qsTr("Cancel"); showLabel: true; onClicked: removeFolderDialog.close() }
                ToolButton { objectName: "removeFolderConfirm"; text: qsTr("Remove"); showLabel: true; onClicked: { backend.removeFolder(root.menuFolder.id); removeFolderDialog.close() } }
            }
        }
    }
    Connections {
        target: backend
        function onFoldersChanged() { root.reload() }
        function onCatalogChanged() { root.reload() }
    }
    function isCurrent(kind, id) { return backend.sourceKind === kind && backend.sourceId === (id || 0) }
    function folderHidden(f) {
        // Hidden when any ancestor in the list is collapsed.
        for (const p in collapsed)
            if (collapsed[p] && f.path !== p && f.path.startsWith(p + "/")) return true
        return false
    }
    function hasChildren(f) { return folders.some(o => o.path !== f.path && o.path.startsWith(f.path + "/")) }

    Rectangle {
        anchors.right: parent.right
        width: Theme.hairline; height: parent.height
        color: Theme.border
    }

    C.ScrollView {
        anchors.fill: parent
        anchors.rightMargin: Theme.hairline
        clip: true
        C.ScrollBar.vertical: ScrollBar {}
        C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
        contentWidth: availableWidth

        Column {
            width: parent.width
            spacing: 0
            Item { width: 1; height: Theme.s2 }

            SectionHeader { title: qsTr("Catalog"); helpSection: "library" }
            Rectangle {
                width: parent.width - Theme.s3 * 2
                anchors.horizontalCenter: parent.horizontalCenter
                height: Theme.hControl + Theme.s1
                radius: Theme.rControl
                color: Theme.controlBg
                border.width: Theme.hairline
                border.color: Theme.border
                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.s2
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.s2
                    Icon { name: "database"; anchors.verticalCenter: parent.verticalCenter; color: Theme.accent }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: backend.catalogName
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
                        color: Theme.textPrimary
                    }
                }
                Tip { visible: hover.hovered; text: backend.catalogPath }
                HoverHandler { id: hover }
            }
            Item { width: 1; height: Theme.s2 }
            Repeater {
                model: root.sections
                SourceRow {
                    required property var modelData
                    iconName: modelData.icon
                    name: modelData.name
                    count: modelData.count
                    current: root.isCurrent(modelData.kind, 0)
                    onClicked: backend.setSource(modelData.kind, 0)
                }
            }

            Item { width: 1; height: Theme.s3 }
            SectionHeader {
                title: qsTr("Folders"); helpSection: "library"
                trailing: [
                    IconButton { iconName: "plus"; text: qsTr("Import a folder"); shortcut: "Ctrl+I"; tip: qsTr("Choose a source folder, then add links to its photos or copy them to a destination."); onClicked: root.shell.browsePhotos("") }
                ]
            }
            Repeater {
                model: root.folders
                SourceRow {
                    required property var modelData
                    visible: !root.folderHidden(modelData)
                    height: visible ? Theme.hRow : 0
                    iconName: modelData.watched ? "eye" : modelData.depth === 0 ? "hard-drive" : "folder"
                    name: modelData.name
                    count: modelData.count
                    depth: modelData.depth
                    online: modelData.online
                    expandable: root.hasChildren(modelData)
                    expanded: !root.collapsed[modelData.path]
                    current: root.isCurrent("folder", modelData.id)
                    onClicked: backend.setSource("folder", modelData.id)
                    onContextRequested: { root.menuFolder = modelData; folderMenu.popup() }
                    onToggled: {
                        const c = Object.assign({}, root.collapsed)
                        c[modelData.path] = !c[modelData.path]
                        root.collapsed = c
                    }
                }
            }
            EmptyState {
                visible: root.folders.length === 0
                width: parent.width; height: 64
                title: qsTr("No folders yet")
                description: qsTr("Import photos to link an existing folder or choose a destination.")
            }

            Item { width: 1; height: Theme.s3 }
            SectionHeader {
                title: qsTr("Albums"); helpSection: "organising"
                trailing: [
                    IconButton { iconName: "plus"; text: qsTr("New album from the selection"); tip: qsTr("A hand-picked set; drag photos onto it later to add more."); onClicked: root.shell.newAlbum() }
                ]
            }
            Repeater {
                model: root.albums
                SourceRow {
                    required property var modelData
                    visible: !root.albumHidden(modelData)
                    height: visible ? Theme.hRow : 0
                    iconName: "book-image"
                    thumb: modelData.thumb || ""
                    name: modelData.name
                    count: modelData.count
                    depth: modelData.depth || 0
                    expandable: root.albums.some(a => a.parent === modelData.id)
                    expanded: !root.collapsed["al:" + modelData.id]
                    current: root.isCurrent("album", modelData.id)
                    onClicked: backend.setSource("album", modelData.id)
                    onContextRequested: { root.menuAlbum = modelData; albumMenu.popup() }
                    onToggled: { const c = Object.assign({}, root.collapsed); c["al:" + modelData.id] = !c["al:" + modelData.id]; root.collapsed = c }
                }
            }
            Text {
                visible: root.albums.length === 0
                x: Theme.s3; width: parent.width - Theme.s3 * 2
                text: qsTr("Select photos, then + to make an album.")
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }

            Item { width: 1; height: Theme.s3 }
            SectionHeader {
                title: qsTr("Smart Albums"); helpSection: "organising"
                trailing: [
                    IconButton { iconName: "plus"; text: qsTr("New smart album from rules"); tip: qsTr("A set that fills itself from rules such as rating, camera or date."); onClicked: smartDialog.openFor(0, "") }
                ]
            }
            Repeater {
                model: root.smart
                SourceRow {
                    required property var modelData
                    iconName: "sparkles"
                    name: modelData.name
                    count: modelData.count
                    current: root.isCurrent("smart", modelData.id)
                    onClicked: backend.setSource("smart", modelData.id)
                    onContextRequested: { root.menuAlbum = modelData; smartMenu.popup() }
                }
            }
            Text {
                visible: root.smart.length === 0
                x: Theme.s3; width: parent.width - Theme.s3 * 2
                text: qsTr("Press + to build one from rules: stars, flags, labels, keywords, camera, dates.")
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }

            Item { width: 1; height: Theme.s3 }
            SectionHeader { title: qsTr("Keywords"); helpSection: "organising" }
            Repeater {
                model: root.keywords
                SourceRow {
                    required property var modelData
                    visible: !root.keywordHidden(modelData)
                    height: visible ? Theme.hRow : 0
                    iconName: "tag"
                    name: modelData.name
                    count: modelData.count
                    depth: modelData.depth
                    expandable: root.keywordHasChildren(modelData)
                    expanded: !root.collapsed["kw:" + modelData.path]
                    current: root.isCurrent("keyword", modelData.id)
                    onClicked: backend.setSource("keyword", modelData.id)
                    onContextRequested: { root.menuKeyword = modelData; keywordMenu.popup() }
                    onToggled: {
                        const c = Object.assign({}, root.collapsed)
                        c["kw:" + modelData.path] = !c["kw:" + modelData.path]
                        root.collapsed = c
                    }
                }
            }
            Text {
                visible: root.keywords.length === 0
                x: Theme.s3; width: parent.width - Theme.s3 * 2
                text: qsTr("Add keywords in the inspector; a slash makes levels: Travel/Italy/Venice.")
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }

            Item { width: 1; height: Theme.s3 }
            SectionHeader { title: qsTr("Recent Imports"); helpSection: "import" }
            Repeater {
                model: root.imports
                SourceRow {
                    required property var modelData
                    iconName: "download"
                    name: modelData.date
                    count: modelData.count
                    current: root.isCurrent("import", modelData.id)
                    onClicked: backend.setSource("import", modelData.id)
                }
            }

            Item { width: 1; height: Theme.s3 }
            SectionHeader { title: qsTr("Devices"); helpSection: "import" }
            // Mounted volumes, polled while the sidebar is up; a card with a DCIM folder imports from there.
            Timer { interval: 4000; running: root.visible; repeat: true; onTriggered: root.devices = backend.devices() }
            Repeater {
                model: root.devices
                Item {
                    required property var modelData
                    width: parent.width; height: Theme.hRow * 2
                    readonly property string capacity: {
                        const gb = b => (b / 1e9).toFixed(b >= 1e10 ? 0 : 1)
                        return qsTr("%1 GB free of %2 GB").arg(gb(modelData.bytesFree)).arg(gb(modelData.bytesTotal))
                    }
                    Icon { id: devIcon; anchors.left: parent.left; anchors.leftMargin: Theme.s3; anchors.verticalCenter: parent.verticalCenter; name: modelData.removable ? (modelData.dcim ? "camera" : "hard-drive") : "hard-drive"; color: Theme.textSecondary }
                    Column {
                        anchors.left: devIcon.right; anchors.leftMargin: Theme.s2; anchors.right: devImport.left; anchors.rightMargin: Theme.s2
                        anchors.verticalCenter: parent.verticalCenter
                        Text { width: parent.width; text: modelData.name; elide: Text.ElideRight; textFormat: Text.PlainText; font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl; color: Theme.textPrimary }
                        Row {
                            spacing: Theme.s1
                            Rectangle { width: 80; height: 4; radius: 2; color: Theme.controlBg; anchors.verticalCenter: parent.verticalCenter
                                Rectangle { width: modelData.bytesTotal > 0 ? parent.width * (1 - modelData.bytesFree / modelData.bytesTotal) : 0; height: parent.height; radius: 2; color: Theme.accent } }
                            Text { text: parent.parent.parent.capacity; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted }
                        }
                    }
                    IconButton { id: devImport; anchors.right: parent.right; anchors.rightMargin: Theme.s2; anchors.verticalCenter: parent.verticalCenter; iconName: "download"; text: qsTr("Import from %1").arg(modelData.name); onClicked: root.shell.browsePhotos(backend.fileUrl(modelData.dcim ? modelData.path + "/DCIM" : modelData.path).toString()) }
                }
            }
            Text {
                visible: root.devices.length === 0
                x: Theme.s3; width: parent.width - Theme.s3 * 2
                text: qsTr("No card or external drive is mounted.")
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
            Item { width: 1; height: Theme.s5 }
        }
    }
}
