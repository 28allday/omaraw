import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

// Library: sources | browser (toolbar / grid or loupe / cull bar) | inspector,
// filmstrip along the bottom. Dock widths persist through the shell.
Item {
    id: root
    property var shell: null
    property bool browsing: false
    readonly property bool reviewing: browsing && visible
    property alias importWorkspace: importWorkspace
    function browse(path) {
        browsing = true
        folderBrowser.activate(path || "")
    }
    function selectAllPhotos(pick) { if (reviewing) importWorkspace.photoBrowser.pickAll(pick); else if (pick) backend.selectAll(); else backend.clearSelection() }
    function stepPhoto(delta) { if (reviewing) importWorkspace.photoBrowser.navigate(delta); else backend.step(delta) }
    function showPhotos(mode) { if (reviewing) importWorkspace.photoBrowser.previewing = mode === "loupe"; else shell.browserMode = mode }
    property var currentInfo: ({})
    function reloadInfo() { currentInfo = backend.info(backend.currentId) }
    Component.onCompleted: reloadInfo()
    Connections {
        target: backend
        function onSelectionChanged() { root.reloadInfo() }
    }

    Column {
        anchors.fill: parent
        spacing: 0
        Item {
            width: parent.width
            height: parent.height - (!root.browsing && root.shell.filmstripVisible ? filmstrip.height : 0)

            Rectangle {
                id: sources
                color: Theme.panelBg
                visible: root.shell.sourceDockVisible
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: visible ? root.shell.sourceDockWidth : 0
                SegmentedControl {
                    id: sourceTabs; objectName: "librarySourceTabs"
                    anchors.top: parent.top; anchors.left: parent.left; anchors.margins: Theme.s3
                    labels: [qsTr("Library"), qsTr("Browse")]; currentIndex: root.browsing ? 1 : 0
                    onActivated: i => { if (i === 1) root.browse(""); else root.browsing = false }
                }
                SourceSidebar {
                    shell: root.shell; visible: !root.browsing
                    anchors.top: sourceTabs.bottom; anchors.topMargin: Theme.s3
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                }
                FolderBrowser {
                    id: folderBrowser; visible: root.browsing; enabled: !importWorkspace.ownImport
                    anchors.top: sourceTabs.bottom; anchors.topMargin: Theme.s3
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    onFolderSelected: path => importWorkspace.openFor(path)
                    onNavigationFailed: message => importWorkspace.showFolderError(message)
                }
            }
            SplitterHandle {
                id: leftSplit
                visible: sources.visible
                anchors.left: sources.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: Theme.wSplitter
                pressed: leftDrag.active; hovered: leftHover.hovered
                HoverHandler { id: leftHover; cursorShape: Qt.SplitHCursor }
                DragHandler {
                    id: leftDrag
                    target: null
                    xAxis.enabled: true; yAxis.enabled: false
                    property real startW: 0
                    onActiveChanged: if (active) startW = root.shell.sourceDockWidth
                    onTranslationChanged: root.shell.sourceDockWidth = Math.max(Theme.wSourceDockMin, Math.min(Theme.wSourceDockMax, startW + translation.x))
                }
            }

            Column {
                id: browser
                visible: !root.browsing
                anchors.left: leftSplit.visible ? leftSplit.right : parent.left
                anchors.right: rightSplit.visible ? rightSplit.left : parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                spacing: 0
                BrowserToolbar { id: toolbar; shell: root.shell; width: parent.width }
                Item {
                    width: parent.width
                    height: parent.height - toolbar.height - cull.height
                    PhotoGrid {
                        anchors.fill: parent
                        visible: root.shell.browserMode === "grid"
                        shell: root.shell
                        cardSize: root.shell.cardSize
                    }
                    DetailList {
                        anchors.fill: parent
                        visible: root.shell.browserMode === "list"
                        shell: root.shell
                    }
                    LoupeView {
                        anchors.fill: parent
                        visible: root.shell.browserMode === "loupe"
                    }
                    CompareView { anchors.fill: parent; visible: root.shell.browserMode === "compare" }
                    SurveyView { anchors.fill: parent; visible: root.shell.browserMode === "survey" }
                }
                CullBar { id: cull; shell: root.shell; width: parent.width; currentInfo: root.currentInfo }
            }

            SplitterHandle {
                id: rightSplit
                visible: inspector.visible
                anchors.right: inspector.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: Theme.wSplitter
                pressed: rightDrag.active; hovered: rightHover.hovered
                HoverHandler { id: rightHover; cursorShape: Qt.SplitHCursor }
                DragHandler {
                    id: rightDrag
                    target: null
                    xAxis.enabled: true; yAxis.enabled: false
                    property real startW: 0
                    onActiveChanged: if (active) startW = root.shell.inspectorWidth
                    onTranslationChanged: root.shell.inspectorWidth = Math.max(Theme.wInspectorMin, Math.min(Theme.wInspectorMax, startW - translation.x))
                }
            }
            LibraryInspector {
                id: inspector
                shell: root.shell
                visible: !root.browsing && root.shell.inspectorVisible
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: visible ? root.shell.inspectorWidth : 0
            }
            ImportWorkspace {
                id: importWorkspace
                visible: root.browsing
                anchors.left: leftSplit.visible ? leftSplit.right : parent.left
                anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                settingsWidth: Math.max(300, Math.min(360, root.shell.inspectorWidth))
                onCompleted: {
                    if (root.browsing) {
                        backend.filterText = ""; backend.filterRating = 0; backend.filterFlag = ""; backend.filterLabel = ""; backend.filterExtra = ({})
                        backend.setSource("previousImport")
                        root.browsing = false
                    }
                }
            }
        }
        Filmstrip {
            id: filmstrip
            shell: root.shell
            visible: !root.browsing && root.shell.filmstripVisible
            width: parent.width
            height: root.shell.filmstripHeight
        }
    }
}
