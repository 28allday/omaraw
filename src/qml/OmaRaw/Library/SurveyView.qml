pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// Every selected photo at once, as large as they fit. Click one to make
// it current; culling keys still act on the whole selection.
Rectangle {
    id: root
    color: Theme.pasteboard
    property var ids: backend.selectedIds()
    Connections { target: backend; function onSelectionChanged() { root.ids = backend.selectedIds() } }
    readonly property int n: ids.length
    readonly property int cols: n <= 1 ? 1 : n <= 2 ? 2 : n <= 6 ? 3 : n <= 12 ? 4 : 5
    readonly property int rows: Math.max(1, Math.ceil(n / cols))
    Grid {
        anchors.fill: parent
        anchors.margins: Theme.s2
        columns: root.cols
        spacing: Theme.s2
        Repeater {
            model: root.ids
            Rectangle {
                id: tile
                required property int modelData
                width: (root.width - Theme.s2 * (root.cols + 1)) / root.cols
                height: (root.height - Theme.s2 * (root.rows + 1)) / root.rows
                color: Theme.colourCritical ? Theme.pasteboard : Theme.panelBg
                border.width: backend.currentId === modelData ? Theme.selectionRing : Theme.hairline
                border.color: backend.currentId === modelData ? Theme.accent : Theme.border
                LoupeView {
                    previewObjectName: "surveyPreview_" + tile.modelData
                    anchors.fill: parent; anchors.margins: Theme.s1
                    assetId: tile.modelData
                    toggleOnTap: false
                    showZoomBadge: false
                }
                Row {
                    anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: Theme.s2
                    spacing: Theme.s2
                    RatingStars { rating: backend.info(parent.parent.modelData).rating || 0; size: 10 }
                    LabelDots { label: backend.info(parent.parent.modelData).label || ""; size: 8 }
                }
                TapHandler { onTapped: backend.setCurrent(parent.modelData) }
                TapHandler { acceptedButtons: Qt.RightButton; onTapped: backend.select(parent.modelData, 1) }
            }
        }
    }
    EmptyState {
        anchors.centerIn: parent
        visible: root.n === 0
        iconName: "layout-dashboard"
        title: qsTr("Nothing selected")
        description: qsTr("Shift-click or Ctrl-click photos in the grid, then Survey shows them together.")
    }
}
