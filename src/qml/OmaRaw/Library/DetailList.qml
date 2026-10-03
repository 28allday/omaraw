pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

// The browser as a table: one row per photo with a small thumbnail, the
// file name, capture time, camera, lens, exposure, ISO, stars, label,
// flag and size. Column headers sort; click selects like the grid.
Rectangle {
    id: root
    property var shell: null
    color: Theme.windowBg
    objectName: "detailList"

    readonly property var columns: [
        { key: "", title: "", width: 64 },
        { key: "filename", title: qsTr("File"), width: 220 },
        { key: "captured", title: qsTr("Captured"), width: 130 },
        { key: "", title: qsTr("Camera"), width: 170 },
        { key: "", title: qsTr("Lens"), width: 170 },
        { key: "", title: qsTr("Exposure"), width: 150 },
        { key: "", title: qsTr("ISO"), width: 60 },
        { key: "rating", title: qsTr("Rating"), width: 90 },
        { key: "", title: qsTr("Label"), width: 50 },
        { key: "", title: qsTr("Flag"), width: 40 },
        { key: "", title: qsTr("Size"), width: 80 },
        { key: "edited", title: qsTr("Edited"), width: 60 }
    ]
    readonly property int rowH: 44
    readonly property int totalWidth: columns.reduce((a, c) => a + c.width, 0)

    function scrollToCurrent() {
        const row = assets.rowOf(backend.currentId)
        if (row >= 0) list.positionViewAtIndex(row, ListView.Contain)
    }
    Connections {
        target: backend
        function onSelectionChanged() { root.scrollToCurrent() }
    }

    Flickable {
        id: hscroll
        anchors.fill: parent
        contentWidth: Math.max(width, root.totalWidth)
        contentHeight: height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        C.ScrollBar.horizontal: ScrollBar { }
        Column {
            width: hscroll.contentWidth
            height: hscroll.height
            spacing: 0
            // header
            Rectangle {
                width: parent.width; height: Theme.hRow + Theme.s1
                color: Theme.panelBg
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.hairline; color: Theme.border }
                Row {
                    anchors.fill: parent
                    Repeater {
                        model: root.columns
                        Item {
                            required property var modelData
                            width: modelData.width; height: parent.height
                            readonly property bool sortable: modelData.key !== ""
                            readonly property bool active: sortable && backend.sortKey === modelData.key
                            Text {
                                anchors.left: parent.left; anchors.leftMargin: Theme.s2
                                anchors.verticalCenter: parent.verticalCenter
                                text: parent.modelData.title + (parent.active ? (backend.sortDescending ? " ↓" : " ↑") : "")
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; font.weight: Theme.wHeading
                                color: parent.active ? Theme.accent : Theme.textMuted
                            }
                            HoverHandler { id: hh; enabled: parent.sortable }
                            TapHandler {
                                enabled: parent.sortable
                                onTapped: {
                                    if (backend.sortKey === parent.modelData.key) backend.sortDescending = !backend.sortDescending
                                    else { backend.sortKey = parent.modelData.key; backend.sortDescending = false }
                                }
                            }
                        }
                    }
                }
            }
            ListView {
                id: list
                width: parent.width
                height: parent.height - Theme.hRow - Theme.s1
                model: assets
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                C.ScrollBar.vertical: ScrollBar { }
                focus: true
                delegate: Rectangle {
                    id: row
                    required property int index
                    required property int assetId
                    required property string filename
                    required property string thumb
                    required property string capturedTime
                    required property string make
                    required property string model
                    required property string lens
                    required property string exposure
                    required property int iso
                    required property int rating
                    required property string label
                    required property int flag
                    required property string size
                    required property bool edited
                    required property bool selected
                    required property bool current
                    required property int variant
                    required property string variantName
                    required property int stackCount
                    width: list.width; height: root.rowH
                    objectName: "listRow"
                    color: selected ? Theme.panelRaised : (index % 2 ? Theme.alternateRowBg : Theme.windowBg)
                    opacity: flag < 0 ? 0.5 : 1
                    Rectangle { anchors.left: parent.left; width: 2; height: parent.height; color: Theme.accent; visible: row.current && row.selected }
                    Row {
                        anchors.fill: parent
                        Item {
                            width: root.columns[0].width; height: parent.height
                            Image {
                                anchors.centerIn: parent
                                width: 56; height: 38
                                source: row.thumb
                                asynchronous: true; cache: true
                                fillMode: Image.PreserveAspectFit; smooth: true; mipmap: true
                                sourceSize.width: 0
                            }
                        }
                        Item {
                            width: root.columns[1].width; height: parent.height
                            Row {
                                anchors.left: parent.left; anchors.leftMargin: Theme.s2; anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.s1
                                Text {
                                    text: row.filename + (row.variant ? "  ·  " + (row.variantName !== "" ? row.variantName : qsTr("Copy %1").arg(row.variant)) : "")
                                    textFormat: Text.PlainText; elide: Text.ElideMiddle
                                    width: Math.min(implicitWidth, parent.width - (row.stackCount > 1 ? 34 : 0))
                                    font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
                                    color: row.selected ? Theme.textPrimary : Theme.textSecondary
                                }
                                Rectangle {
                                    visible: row.stackCount > 1
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: st.implicitWidth + Theme.s2; height: 16; radius: Theme.rControl; color: Theme.controlBg
                                    Text { id: st; anchors.centerIn: parent; text: "▣ " + row.stackCount; font.family: Theme.monoFamily; font.pixelSize: 9; color: Theme.accent }
                                }
                            }
                        }
                        Text { width: root.columns[2].width; height: parent.height; leftPadding: Theme.s2; verticalAlignment: Text.AlignVCenter; text: row.capturedTime; elide: Text.ElideRight; font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted }
                        Text { width: root.columns[3].width; height: parent.height; leftPadding: Theme.s2; verticalAlignment: Text.AlignVCenter; text: (row.make + " " + row.model).trim(); textFormat: Text.PlainText; elide: Text.ElideRight; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary }
                        Text { width: root.columns[4].width; height: parent.height; leftPadding: Theme.s2; verticalAlignment: Text.AlignVCenter; text: row.lens; textFormat: Text.PlainText; elide: Text.ElideRight; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary }
                        Text { width: root.columns[5].width; height: parent.height; leftPadding: Theme.s2; verticalAlignment: Text.AlignVCenter; text: row.exposure; elide: Text.ElideRight; font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary }
                        Text { width: root.columns[6].width; height: parent.height; leftPadding: Theme.s2; verticalAlignment: Text.AlignVCenter; text: row.iso ? String(row.iso) : ""; font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary }
                        Item {
                            width: root.columns[7].width; height: parent.height
                            RatingStars { anchors.left: parent.left; anchors.leftMargin: Theme.s2; anchors.verticalCenter: parent.verticalCenter; rating: row.rating; interactive: true; onRated: r => backend.setRating(r, row.assetId) }
                        }
                        Item { width: root.columns[8].width; height: parent.height; LabelDots { anchors.left: parent.left; anchors.leftMargin: Theme.s2; anchors.verticalCenter: parent.verticalCenter; label: row.label } }
                        Item { width: root.columns[9].width; height: parent.height; FlagMark { anchors.left: parent.left; anchors.leftMargin: Theme.s2; anchors.verticalCenter: parent.verticalCenter; flag: row.flag } }
                        Text { width: root.columns[10].width; height: parent.height; leftPadding: Theme.s2; verticalAlignment: Text.AlignVCenter; text: row.size; font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted }
                        Item { width: root.columns[11].width; height: parent.height; Icon { anchors.left: parent.left; anchors.leftMargin: Theme.s2 + 4; anchors.verticalCenter: parent.verticalCenter; visible: row.edited; name: "sliders-horizontal"; size: 12; color: Theme.accent } }
                    }
                    TapHandler {
                        id: selectionTap
                        acceptedButtons: Qt.LeftButton
                        onTapped: { backend.select(row.assetId, (selectionTap.point.modifiers & Qt.ShiftModifier) ? 2 : (selectionTap.point.modifiers & Qt.ControlModifier) ? 1 : 0); list.forceActiveFocus() }
                        onDoubleTapped: if (!(selectionTap.point.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) { backend.select(row.assetId, 0); root.shell.browserMode = "loupe" }
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: { if (backend.isSelected(row.assetId)) backend.setCurrent(row.assetId); else backend.select(row.assetId, 0); root.shell.cardMenu.popup() }
                    }
                }
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Down) { backend.step(1); event.accepted = true }
                    else if (event.key === Qt.Key_Up) { backend.step(-1); event.accepted = true }
                }
            }
        }
    }
    EmptyState {
        anchors.centerIn: parent
        visible: assets.count === 0
        iconName: "list"
        title: qsTr("Nothing to list")
        description: backend.filterActive ? qsTr("The filter hides everything here.") : qsTr("Import a folder to begin.")
    }
}
