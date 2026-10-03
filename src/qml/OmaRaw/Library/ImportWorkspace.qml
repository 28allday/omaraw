import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

// How a folder comes in: added in place, or copied / moved / copied and
// verified to a destination, into date subfolders, under a rename pattern,
// skipping photos the catalog already holds. The last choices persist.
Item {
    id: root
    objectName: "importWorkspace"
    property string folder: ""
    property var preview: ({})
    readonly property var modes: ["copy", "move", "add", "verify"]
    readonly property var subfolders: ["none", "year", "year-date", "date"]
    readonly property var renames: ["keep", "date-name", "prefix-seq"]
    property string mode: "add"
    property string destination: ""
    property string subfolder: "none"
    property string rename: "keep"
    property string prefix: ""
    property bool skipDuplicates: true
    property bool hashDuplicates: false
    property bool smartPreviews: false
    AutoTagSettings { id: tagSettings }
    property string backup: ""
    property bool eject: false
    property var metadata: ({})
    readonly property bool removable: folder !== "" && backend.isRemovableSource(folder)
    readonly property bool inPlace: mode === "add"
    readonly property string photoStorage: inPlace ? (preview.folder || backend.localFile(folder)) : destination
    readonly property string storageDescription: inPlace
        ? qsTr("Keep photos in their current folders and add links to this catalog.")
        : mode === "move"
        ? qsTr("Move photos to the chosen folder and link to them. Files leave their source folder; sidecars move with them.")
        : mode === "verify"
        ? qsTr("Copy photos to the chosen folder, verify each copy, then link to it. Source files stay where they are.")
        : qsTr("Copy photos to the chosen folder and link to the copies. Source files stay where they are.")
    property bool loading: false
    property bool recursive: true
    property int previewRequest: -1
    property string previewError: ""
    property string importError: ""
    property int page: 0
    property int settingsWidth: 340
    property bool initialized: false
    property bool ownImport: false
    property bool needsScan: false
    property alias photoBrowser: browser
    signal completed()
    readonly property bool wide: width >= 820
    readonly property int selectedCount: browser.selectedCount
    readonly property bool ready: folder !== "" && !backend.importing && !loading && previewError === "" && selectedCount > 0 && (inPlace || destination !== "")
    function scan() {
        preview = ({}); previewError = ""; importError = ""; loading = true
        previewRequest = backend.requestImportPreview(folder, recursive)
    }
    function showFolderError(message) {
        backend.cancelImportPreview(); previewRequest = -1
        preview = ({}); previewError = message; loading = false
    }
    Connections {
        target: backend
        function onImportPreviewReady(request, result, error) {
            if (request !== root.previewRequest) return
            root.preview = result; root.previewError = error; root.loading = false
            const focus = root.Window.window ? root.Window.window.activeFocusItem : null
            if (root.visible && !(focus && focus.hasOwnProperty("cursorPosition"))) browser.forceActiveFocus()
        }
        function onImportFinished(imported, skipped) {
            if (!root.ownImport) return
            root.ownImport = false
            const result = backend.lastImportResult()
            if (result.error) root.importError = result.error
            else if (result.cancelled) root.importError = backend.statusMessage
            else {
                browser.pickAll(false)
                root.needsScan = true
                if (imported > 0) root.completed()
                else root.importError = backend.statusMessage
            }
        }
    }
    function openFor(folderUrl) {
        folder = backend.localFile(folderUrl)
        if (!initialized) {
        const o = backend.importOptions()
        mode = modes.indexOf(o.mode) >= 0 ? o.mode : "add"
        destination = o.destination || ""
        subfolder = subfolders.indexOf(o.subfolder) >= 0 ? o.subfolder : "none"
        rename = renames.indexOf(o.rename) >= 0 ? o.rename : "keep"
        prefix = o.prefix || ""
        skipDuplicates = o.skipDuplicates !== false
        hashDuplicates = o.hashDuplicates === true
        smartPreviews = o.smartPreviews === true
        backup = o.backup || ""
        eject = o.eject === true
        metadata = o.metadata || ({})
        recursive = o.recursive !== false
        prefixField.text = prefix
        initialized = true
        }
        page = 0; scan(); needsScan = false
    }
    function run() {
        if (!ready) return
        importError = ""
        const started = backend.importFolderWith(folder, { selectedFiles: browser.selectedPaths(), recursive: recursive, mode: mode, destination: destination, subfolder: subfolder, rename: rename, prefix: prefix, skipDuplicates: skipDuplicates, hashDuplicates: hashDuplicates, backup: backup, eject: eject && removable, metadata: metadata, smartPreviews: smartPreviews, autoTag: backend.autoTagEnabled })
        if (started) ownImport = true
        else importError = backend.statusMessage
    }

    FolderPicker {
        id: destDialog
        objectName: "importStorageDialog"
        title: qsTr("Choose photo storage")
        description: qsTr("Choose where your imported photos will be stored.")
        onAccepted: root.destination = backend.localFile(selectedFolder)
    }
    FolderPicker {
        id: backupDialog
        objectName: "importBackupDialog"
        title: qsTr("Choose where the second copies go")
        description: qsTr("Choose a separate location for an additional copy of your photos.")
        onAccepted: root.backup = backend.localFile(selectedFolder)
    }

    component FieldRow: Item {
        property string label: ""
        default property alias content: slot.data
        width: parent.width; height: Theme.fsLabel + Theme.s1 + Theme.hControl + Theme.s2
        Text {
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            text: parent.label
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
        }
        Item { id: slot; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: Theme.hControl }
    }

    onVisibleChanged: {
        if (!visible && loading) { backend.cancelImportPreview(); previewRequest = -1; loading = false; needsScan = true }
        else if (visible && needsScan && folder !== "") { needsScan = false; scan() }
    }
    Component.onDestruction: backend.cancelImportPreview()

    Rectangle { anchors.fill: parent; color: Theme.panelBg }
    Column {
        id: layout
        anchors.fill: parent; anchors.margins: Theme.s3; spacing: Theme.s3
        Item {
            id: heading
            width: parent.width; height: Theme.hControl
            Text {
                anchors.left: parent.left; anchors.right: refresh.left; anchors.rightMargin: Theme.s2; anchors.verticalCenter: parent.verticalCenter
                text: root.folder !== "" ? root.folder : qsTr("Choose a folder on the left to preview its photos")
                textFormat: Text.PlainText; elide: Text.ElideMiddle
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl; color: Theme.textPrimary
            }
            IconButton { id: refresh; objectName: "importRefresh"; anchors.right: parent.right; iconName: "refresh-cw"; text: qsTr("Refresh photos"); enabled: root.folder !== "" && !backend.importing; onClicked: root.scan() }
        }
        Row {
            id: sourceRow
            width: parent.width; height: Theme.hControl; spacing: Theme.s3
            Toggle { objectName: "importRecursive"; label: qsTr("Include subfolders"); enabled: !backend.importing; checkable: false; checked: root.recursive; onClicked: { root.recursive = !root.recursive; root.scan() } }
            SegmentedControl {
                id: pageTabs; objectName: "importPageTabs"
                visible: !root.wide
                labels: [qsTr("Photos"), qsTr("Import settings")]; currentIndex: root.page; onActivated: i => root.page = i
            }
        }
        Item {
            id: body
            width: parent.width
            height: layout.height - heading.height - sourceRow.height - actions.height - Theme.hairline - Theme.s3 * 4
            ImportBrowser {
                id: browser
                visible: root.wide || root.page === 0
                enabled: !backend.importing
                anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: root.wide ? parent.width - scroll.width - Theme.s4 : parent.width
                files: root.preview.files || []
                skipDuplicates: root.skipDuplicates
                loading: root.loading; error: root.previewError
            }
            C.ScrollView {
                id: scroll; objectName: "importOptionsScroll"
                visible: root.wide || root.page === 1
                enabled: !backend.importing
                anchors.right: parent.right; height: parent.height
                width: root.wide ? root.settingsWidth : parent.width
                contentWidth: availableWidth; clip: true
                C.ScrollBar.vertical: ScrollBar {}
        Column {
          id: contents
          width: scroll.availableWidth
          spacing: Theme.s2
          Text {
              width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText
              text: qsTr("Into catalog: %1").arg(backend.catalogName)
              font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
          }
          Text {
              objectName: "importCatalogLocation"
              width: parent.width; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText
              text: qsTr("Catalog location: %1").arg(backend.catalogPath)
              font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
          }
          Text {
              width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText
              text: (root.preview.count || 0) === 1 ? qsTr("Source: 1 photo · %1").arg(root.preview.size || "")
                  : qsTr("Source: %1 photos · %2").arg(root.preview.count || 0).arg(root.preview.size || "")
              font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
          }
          Item { width: 1; height: Theme.s1 }
          FieldRow {
              label: qsTr("Method")
              ComboField {
                  objectName: "importMethod"
                  anchors.verticalCenter: parent.verticalCenter; width: parent.width
                  model: [qsTr("Copy"), qsTr("Move"), qsTr("Add"), qsTr("Verified copy")]
                  currentIndex: Math.max(0, root.modes.indexOf(root.mode))
                  onActivated: i => root.mode = root.modes[i]
              }
          }
          Text {
              objectName: "importStorageDescription"
              width: parent.width; wrapMode: Text.Wrap
              text: root.storageDescription
              font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
          }
          FieldRow {
              label: qsTr("Photo storage")
              Text {
                  objectName: "importPhotoStorage"
                  anchors.left: parent.left; anchors.right: chooseBtn.visible ? chooseBtn.left : parent.right; anchors.rightMargin: Theme.s2
                  anchors.verticalCenter: parent.verticalCenter
                  text: root.photoStorage !== "" ? root.photoStorage : qsTr("Choose a folder…")
                  elide: Text.ElideMiddle; textFormat: Text.PlainText
                  font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
                  color: root.photoStorage !== "" ? Theme.textSecondary : Theme.warning
              }
              ToolButton { id: chooseBtn; visible: !root.inPlace; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Choose…"); showLabel: true; onClicked: destDialog.openFor(root.destination) }
          }
          FieldRow {
              label: qsTr("Subfolders")
              visible: !root.inPlace
              ComboField {
                  anchors.verticalCenter: parent.verticalCenter; width: parent.width
                  model: [qsTr("None"), qsTr("Year"), qsTr("Year / Date"), qsTr("Date")]
                  tipTitle: qsTr("Subfolders"); tip: qsTr("How the destination is divided up by capture date.")
                  currentIndex: Math.max(0, root.subfolders.indexOf(root.subfolder))
                  onActivated: i => root.subfolder = root.subfolders[i]
              }
          }
          FieldRow {
              label: qsTr("File names")
              visible: !root.inPlace
              Row {
                  anchors.fill: parent
                  spacing: Theme.s2
                  ComboField {
                      anchors.verticalCenter: parent.verticalCenter; width: root.rename === "prefix-seq" ? parent.width * .58 : parent.width
                      model: [qsTr("Keep"), qsTr("Date + name"), qsTr("Prefix + number")]
                      tipTitle: qsTr("Rename"); tip: qsTr("What the copied files are called; the originals' names are kept in the catalog.")
                      currentIndex: Math.max(0, root.renames.indexOf(root.rename))
                      onActivated: i => root.rename = root.renames[i]
                  }
                  SearchField {
                      id: prefixField
                      visible: root.rename === "prefix-seq"
                      anchors.verticalCenter: parent.verticalCenter
                      width: parent.width * .42 - Theme.s2
                      placeholder: qsTr("Prefix")
                      tip: qsTr("The text every renamed file starts with, before its number.")
                      live: true
                      onTextChanged: root.prefix = text
                  }
              }
          }
          Item { width: parent.width; height: Theme.hControl + Theme.s1
              Toggle { width: parent.width; anchors.verticalCenter: parent.verticalCenter; label: qsTr("Skip photos already in the catalog"); tip: qsTr("A file with the same name, size and capture time as one in the catalog is left out."); checkable: false; checked: root.skipDuplicates; onClicked: root.skipDuplicates = !root.skipDuplicates }
          }
          Item {
              width: parent.width; height: Theme.hControl + Theme.s1
              visible: root.skipDuplicates
              Toggle { width: parent.width; anchors.verticalCenter: parent.verticalCenter; label: qsTr("Check file contents too (slower)"); tip: qsTr("Reads every file to catch a duplicate that was renamed."); checkable: false; checked: root.hashDuplicates; onClicked: root.hashDuplicates = !root.hashDuplicates }
          }
          Item { width: parent.width; height: Theme.hControl + Theme.s1
              Toggle { objectName: "importSmartPreviews"; width: parent.width; anchors.verticalCenter: parent.verticalCenter; label: qsTr("Build Smart Previews after import"); tip: qsTr("Save smaller editable previews beside the catalog for offline editing. Reconnect originals for full detail and export."); checkable: false; checked: root.smartPreviews; onClicked: root.smartPreviews = !root.smartPreviews }
          }
          Item { width: parent.width; height: Theme.hControl + Theme.s1
              Toggle { objectName: "importAutoTag"; width: parent.width; anchors.verticalCenter: parent.verticalCenter; label: qsTr("Automatically tag imported photos"); tip: qsTr("Tag new imports locally on the CPU. Off stops scanning and keeps existing tags. Choose collections in Tagging settings."); checkable: false; checked: backend.autoTagEnabled; onClicked: backend.autoTagEnabled = !backend.autoTagEnabled }
          }
          ToolButton { objectName: "importAutoTagSettings"; text: qsTr("Tagging settings…"); showLabel: true; onClicked: tagSettings.open() }
          FieldRow {
              label: qsTr("Second copy")
              Text {
                  anchors.left: parent.left; anchors.right: backupButtons.left; anchors.rightMargin: Theme.s2
                  anchors.verticalCenter: parent.verticalCenter
                  text: root.backup !== "" ? root.backup : qsTr("None — one copy only")
                  elide: Text.ElideMiddle; textFormat: Text.PlainText
                  font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
                  color: root.backup !== "" ? Theme.textSecondary : Theme.textMuted
              }
              Row {
                  id: backupButtons
                  anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                  spacing: Theme.s1
                  ToolButton { text: qsTr("Choose…"); showLabel: true; tip: qsTr("A second folder, such as another drive, that gets its own copy of every imported file."); onClicked: backupDialog.openFor(root.backup) }
                  IconButton { visible: root.backup !== ""; iconName: "x"; text: qsTr("No second copy"); tip: qsTr("Imports to the destination only."); onClicked: root.backup = "" }
              }
          }
          FieldRow {
              label: qsTr("Card")
              visible: root.removable
              Toggle { width: parent.width; anchors.verticalCenter: parent.verticalCenter; label: qsTr("Eject after a clean verified import"); checkable: false; checked: root.eject; enabled: root.mode === "verify"; onClicked: root.eject = !root.eject }
          }
          SavedSettings {
              width: parent.width
              caption: qsTr("Metadata preset")
              entries: backend.metadataImportPresets
              onChosen: values => root.metadata = values
              onSaveRequested: name => backend.saveWorkflowPreset("metadata", name, root.metadata)
              onRemoveRequested: name => backend.deleteWorkflowPreset("metadata", name)
          }
          Repeater {
              model: [{ key: "creator", label: qsTr("Creator") }, { key: "copyright", label: qsTr("Copyright") },
                      { key: "title", label: qsTr("Title") }, { key: "caption", label: qsTr("Caption") },
                      { key: "keywords", label: qsTr("Keywords, separated by commas") }]
              C.TextField {
                  id: metadataField
                  required property var modelData
                  width: contents.width; height: Theme.hControl
                  text: root.metadata[modelData.key] || ""
                  placeholderText: modelData.label
                  Accessible.name: modelData.label
                  color: Theme.textPrimary; placeholderTextColor: Theme.textMuted
                  font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
                  background: Rectangle { color: Theme.inputBg; radius: Theme.rControl; border.color: metadataField.activeFocus ? Theme.accent : Theme.border }
                  onTextEdited: { const values = Object.assign({}, root.metadata); values[modelData.key] = text; root.metadata = values }
              }
          }
          Text {
              width: parent.width; wrapMode: Text.Wrap
              text: qsTr("Metadata applies to new catalog photos. Empty fields keep existing values; keywords are added. Reimporting keeps your catalog edits.")
              font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
          }
        }
            }
        }
        Rectangle { width: parent.width; height: Theme.hairline; color: Theme.border }
        Item {
            id: actions
            width: parent.width; height: Math.max(Theme.hControl, importMessage.implicitHeight)
            Text {
                id: importMessage
                anchors.left: parent.left; anchors.right: actionButtons.left; anchors.rightMargin: Theme.s2
                text: backend.importing ? backend.importStatus : root.importError
                textFormat: Text.PlainText; wrapMode: Text.Wrap
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: root.importError !== "" ? Theme.warning : Theme.textSecondary
            }
            Row {
                id: actionButtons; anchors.right: parent.right; spacing: Theme.s2
                ToolButton { objectName: "importStop"; visible: root.ownImport && backend.importing; text: qsTr("Stop import"); onClicked: backend.cancelImport() }
                ToolButton { objectName: "importConfirm"; text: root.selectedCount === 1 ? qsTr("Import 1 photo") : qsTr("Import %1 photos").arg(root.selectedCount); showLabel: true; checked: true; enabled: root.ready; onClicked: root.run() }
            }
        }
    }
}
