pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

// Virtualised grid of PhotoCards over the assets model. Cell size follows
// the shell's cardSize; columns fill the width.
Item {
    id: root
    property var shell: null
    property int cardSize: 240
    readonly property int columns: Math.max(1, Math.floor((width - Theme.s2) / (cardSize + Theme.s2)))
    readonly property real cellW: Math.max(1, Math.floor((width - Theme.s2) / columns))
    readonly property real cellH: Math.round(cellW * 0.86)
    // Drag to reorder, only inside an album sorted by its own order.
    readonly property bool reorderable: backend.customOrderAvailable && backend.sortKey === "custom"
    property int dragFrom: -1
    property int dropAt: -1

    function scrollToCurrent() {
        const row = assets.rowOf(backend.currentId)
        if (row >= 0) grid.positionViewAtIndex(row, GridView.Contain)
    }
    Connections {
        target: backend
        function onSelectionChanged() { root.scrollToCurrent() }
    }

    GridView {
        id: grid
        anchors.fill: parent
        anchors.leftMargin: Theme.s2
        anchors.topMargin: Theme.s2
        clip: true
        model: assets
        cellWidth: root.cellW
        cellHeight: root.cellH
        cacheBuffer: Math.max(0, root.cellH * 2)
        boundsBehavior: Flickable.StopAtBounds
        C.ScrollBar.vertical: ScrollBar {}
        focus: true

        delegate: Item {
            id: cell
            required property int index
            required property int assetId
            required property string filename
            required property string format
            required property bool isRaw
            required property string thumb
            required property int rating
            required property int flag
            required property string label
            required property string capturedTime
            required property bool selected
            required property bool current
            required property bool offline
            required property bool edited
            required property int variant
            required property int editFlags
            required property bool hasGps
            required property bool sidecarStale
            required property string variantName
            required property int stackId
            required property int stackPos
            required property int stackCount
            width: grid.cellWidth; height: grid.cellHeight
            z: dragHandle.active ? 2 : 0
            // The card follows the pointer while dragged and snaps back on release; the
            // cell it hovers over shows a drop line on its leading edge.
            DragHandler {
                id: dragHandle
                enabled: root.reorderable
                target: card
                dragThreshold: 8
                onActiveChanged: {
                    if (active) { root.dragFrom = cell.index; root.dropAt = cell.index; return }
                    const to = root.dropAt
                    root.dragFrom = -1; root.dropAt = -1
                    card.x = 0; card.y = 0
                    if (to >= 0 && to !== cell.index) backend.moveInAlbumTo(cell.assetId, to)
                }
                onCentroidChanged: {
                    if (!active) return
                    const p = cell.mapToItem(grid.contentItem, centroid.position.x, centroid.position.y)
                    const i = grid.indexAt(p.x, p.y)
                    if (i >= 0) root.dropAt = i
                }
            }
            Rectangle {
                visible: root.dropAt === cell.index && root.dragFrom >= 0 && root.dragFrom !== cell.index
                width: 3; height: parent.height - Theme.s2
                x: root.dropAt > root.dragFrom ? parent.width - Theme.s2 - 1 : -1
                color: Theme.accent
                z: 3
            }
            PhotoCard {
                id: card
                anchors.rightMargin: Theme.s2
                anchors.bottomMargin: Theme.s2
                width: parent.width - Theme.s2; height: parent.height - Theme.s2
                opacity: dragHandle.active ? 0.85 : 1
                assetId: cell.assetId
                filename: cell.filename; format: cell.format; isRaw: cell.isRaw
                thumb: cell.thumb; rating: cell.rating; flag: cell.flag; label: cell.label
                capturedTime: cell.capturedTime; selected: cell.selected; current: cell.current
                offline: cell.offline; edited: cell.edited; editFlags: cell.editFlags; hasGps: cell.hasGps; sidecarStale: cell.sidecarStale
                variant: cell.variant
                variantLabel: cell.variant ? (cell.variantName !== "" ? cell.variantName : qsTr("Copy %1").arg(cell.variant)) : ""
                stackCount: cell.stackCount; stackPos: cell.stackPos
                stackExpanded: cell.stackId ? backend.isStackExpanded(cell.stackId) : false
                onClicked: mods => {
                    if (mods & 0x80000000) return // right-click on an already selected card keeps the set
                    const mode = (mods & Qt.ShiftModifier) ? 2 : (mods & Qt.ControlModifier) ? 1 : 0
                    backend.select(cell.assetId, mode)
                    grid.forceActiveFocus()
                }
                onDoubleClicked: { backend.select(cell.assetId, 0); root.shell.browserMode = "loupe" }
                onRated: r => backend.setRating(r, cell.assetId)
                onContextRequested: (x, y) => { if (backend.isSelected(cell.assetId)) backend.setCurrent(cell.assetId); else backend.select(cell.assetId, 0); root.shell.cardMenu.popup() }
            }
        }

        Keys.onPressed: event => {
            // Shift extends the selection from the anchor instead of replacing it.
            const move = (event.modifiers & Qt.ShiftModifier) ? d => backend.extendSelection(d) : d => backend.step(d)
            if (event.key === Qt.Key_Right) { move(1); event.accepted = true }
            else if (event.key === Qt.Key_Left) { move(-1); event.accepted = true }
            else if (event.key === Qt.Key_Down) { move(root.columns); event.accepted = true }
            else if (event.key === Qt.Key_Up) { move(-root.columns); event.accepted = true }
            else if (event.key === Qt.Key_Home) { backend.selectRow(0); event.accepted = true }
            else if (event.key === Qt.Key_End) { backend.selectRow(assets.count - 1); event.accepted = true }
        }
    }

    EmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.s5 * 2, 420)
        visible: assets.count === 0 && !backend.importing
        iconName: backend.totalCount === 0 ? "images" : "filter"
        title: backend.totalCount === 0 ? qsTr("Your catalog is empty") : qsTr("Nothing matches")
        description: backend.totalCount === 0
            ? qsTr("Browse your folders, preview the photos and choose which ones to import. Add links to existing photos, or copy them to a folder you choose.")
            : qsTr("No photos in %1 match the current filters.").arg(backend.sourceTitle)
        actionText: backend.totalCount === 0 ? qsTr("Browse Photos") : qsTr("Clear Filters")
        onActionTriggered: {
            if (backend.totalCount === 0) root.shell.browsePhotos("")
            else { backend.filterText = ""; backend.filterRating = 0; backend.filterFlag = ""; backend.filterLabel = "" }
        }
    }
}
