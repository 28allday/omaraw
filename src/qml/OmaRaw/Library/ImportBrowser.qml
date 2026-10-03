import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

FocusScope {
    id: root
    objectName: "importBrowser"
    property var files: []
    property bool skipDuplicates: true
    property bool loading: false
    property string error: ""
    property var excluded: ({})
    property int currentIndex: -1
    property int anchorIndex: -1
    property bool previewing: false
    readonly property int columns: Math.max(1, Math.floor(width / 170))
    readonly property var current: currentIndex >= 0 && currentIndex < files.length ? files[currentIndex] : null
    readonly property int selectedCount: {
        let count = 0
        for (let i = 0; i < files.length; ++i) if (isPicked(i)) ++count
        return count
    }
    function eligible(index) { return index >= 0 && index < files.length && !files[index].unavailableReason && !(skipDuplicates && files[index].alreadyImported) }
    function isPicked(index) { return eligible(index) && excluded[index] !== true }
    function selectedPaths() {
        const paths = []
        for (let i = 0; i < files.length; ++i) if (isPicked(i)) paths.push(files[i].path)
        return paths
    }
    function pickAll(pick) {
        const values = {}
        if (!pick) for (let i = 0; i < files.length; ++i) values[i] = true
        excluded = values
    }
    function toggle(index, range) {
        if (!eligible(index)) return
        const pick = !isPicked(index), values = Object.assign({}, excluded)
        const first = range && anchorIndex >= 0 ? Math.min(anchorIndex, index) : index
        const last = range && anchorIndex >= 0 ? Math.max(anchorIndex, index) : index
        for (let i = first; i <= last; ++i) if (eligible(i)) values[i] = !pick
        excluded = values; anchorIndex = index; currentIndex = index
    }
    function navigate(delta) { currentIndex = Math.max(0, Math.min(files.length - 1, currentIndex + delta)) }
    onFilesChanged: { excluded = ({}); currentIndex = files.length ? 0 : -1; anchorIndex = -1; previewing = false }
    onCurrentIndexChanged: if (currentIndex >= 0) grid.positionViewAtIndex(currentIndex, GridView.Contain)
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Left) navigate(-1)
        else if (event.key === Qt.Key_Right) navigate(1)
        else if (event.key === Qt.Key_Up) navigate(previewing ? -1 : -columns)
        else if (event.key === Qt.Key_Down) navigate(previewing ? 1 : columns)
        else if (event.key === Qt.Key_Space) toggle(currentIndex, event.modifiers & Qt.ShiftModifier)
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) previewing = !previewing
        else if (event.key === Qt.Key_A && event.modifiers & Qt.ControlModifier) pickAll(true)
        else { event.accepted = false; return }
        event.accepted = true
    }
    Item {
        id: toolbar
        width: parent.width; height: Theme.hControl
        Row {
            spacing: Theme.s2
            ToolButton { objectName: "importSelectAll"; text: qsTr("All"); showLabel: true; enabled: !root.loading; onClicked: root.pickAll(true) }
            ToolButton { objectName: "importSelectNone"; text: qsTr("None"); showLabel: true; enabled: !root.loading; onClicked: root.pickAll(false) }
        }
        ToolButton { objectName: "importPreviewToggle"; anchors.right: parent.right; text: root.previewing ? qsTr("Grid") : qsTr("Preview"); showLabel: true; enabled: root.current !== null; checked: root.previewing; onClicked: { root.previewing = !root.previewing; root.forceActiveFocus() } }
    }
    Rectangle {
        anchors.top: toolbar.bottom; anchors.topMargin: Theme.s2
        anchors.bottom: summary.top; anchors.bottomMargin: Theme.s2
        width: parent.width; color: Theme.pasteboard; radius: Theme.rControl
        GridView {
            id: grid
            objectName: "importPhotoGrid"
            anchors.fill: parent; anchors.margins: Theme.s1
            clip: true; visible: !root.previewing
            keyNavigationEnabled: false
            model: root.files
            cellWidth: width / root.columns
            cellHeight: Math.min(112 + Theme.s2 * 3 + Theme.s1 * 3 + Theme.fsLabel * 2.8, Math.max(140, height))
            currentIndex: root.currentIndex
            cacheBuffer: cellHeight
            C.ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                required property int index
                required property var modelData
                objectName: "importPhoto_" + index
                width: grid.cellWidth; height: grid.cellHeight
                Rectangle {
                    anchors.fill: parent; anchors.margins: Theme.s1; radius: Theme.rControl
                    color: root.currentIndex === index ? Theme.selectionFill : Theme.panelBg
                    border.color: root.currentIndex === index ? Theme.accent : Theme.border
                    Image {
                        id: thumbnail
                        objectName: "importThumbnail_" + index
                        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: Theme.s2
                        anchors.bottom: labels.top; anchors.bottomMargin: Theme.s1
                        source: modelData.thumb + "&display=" + backend.displayColour.revision
                        asynchronous: true; fillMode: Image.PreserveAspectFit; smooth: true; mipmap: true
                        opacity: root.isPicked(index) ? 1 : .5
                    }
                    Text {
                        anchors.centerIn: thumbnail
                        visible: thumbnail.status !== Image.Ready
                        text: thumbnail.status === Image.Error ? qsTr("No preview") : qsTr("Loading…")
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                    }
                    Column {
                        id: labels
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: Theme.s2
                        spacing: Theme.s1 / 2
                        Text {
                            objectName: "importPhotoName_" + index
                            width: parent.width; text: modelData.name; textFormat: Text.PlainText; elide: Text.ElideMiddle
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textPrimary
                        }
                        Text {
                            objectName: "importPhotoInfo_" + index
                            width: parent.width; elide: Text.ElideRight; textFormat: Text.PlainText
                            text: modelData.unavailableReason || (modelData.alreadyImported ? qsTr("In library") : modelData.type + " · " + modelData.size)
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                        }
                    }
                    MouseArea {
                        id: photoHover
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: mouse => {
                            root.currentIndex = index; root.forceActiveFocus()
                            if (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) root.toggle(index, mouse.modifiers & Qt.ShiftModifier)
                        }
                        onDoubleClicked: { root.currentIndex = index; root.previewing = true; root.forceActiveFocus() }
                    }
                    Tooltip { text: modelData.name; description: modelData.unavailableReason || ""; visible: photoHover.containsMouse && !!modelData.unavailableReason }
                    C.CheckBox {
                        objectName: "importPick_" + index
                        anchors.left: parent.left; anchors.top: parent.top
                        width: 34; height: 34; padding: 7
                        enabled: root.eligible(index); checked: root.isPicked(index)
                        focusPolicy: Qt.NoFocus
                        Accessible.name: qsTr("Import %1").arg(modelData.name)
                        Accessible.description: modelData.unavailableReason || ""
                        nextCheckState: function() { return checkState }
                        onClicked: { root.toggle(index, false); root.forceActiveFocus() }
                        indicator: Rectangle {
                            x: 7; y: 7; width: 20; height: 20; radius: 3
                            color: parent.checked ? Theme.accent : Theme.controlBg; border.color: Theme.borderStrong
                            Text { anchors.centerIn: parent; text: "✓"; visible: parent.parent.checked; color: Theme.accentText; font.pixelSize: 15 }
                        }
                        contentItem: Item {}
                    }
                }
            }
        }
        Item {
            anchors.fill: parent; visible: root.previewing && root.current !== null
            Image {
                id: large
                objectName: "importLargePreview"
                anchors.fill: parent; anchors.margins: Theme.s3; anchors.bottomMargin: review.height + Theme.s3 * 2
                source: parent.visible && root.current ? root.current.preview + "&display=" + backend.displayColour.revision : ""
                asynchronous: true; fillMode: Image.PreserveAspectFit; smooth: true; mipmap: true
            }
            Text {
                anchors.centerIn: large; visible: large.status !== Image.Ready
                text: large.status === Image.Error ? (root.eligible(root.currentIndex) ? qsTr("Preview unavailable. You can still select this photo.") : qsTr("Preview unavailable.")) : qsTr("Loading preview…")
                width: parent.width - Theme.s5 * 2; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
            Row {
                id: review
                anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: Theme.s2
                spacing: Theme.s2
                IconButton { objectName: "importPrevious"; iconName: "chevron-left"; text: qsTr("Previous photo"); enabled: root.currentIndex > 0; onClicked: root.navigate(-1) }
                ToolButton { objectName: "importPickCurrent"; text: root.isPicked(root.currentIndex) ? qsTr("Selected for import") : qsTr("Select for import"); showLabel: true; checked: root.isPicked(root.currentIndex); enabled: root.eligible(root.currentIndex); onClicked: root.toggle(root.currentIndex, false) }
                IconButton { objectName: "importNext"; iconName: "chevron-right"; text: qsTr("Next photo"); enabled: root.currentIndex + 1 < root.files.length; onClicked: root.navigate(1) }
            }
        }
        Text {
            anchors.centerIn: parent; width: parent.width - Theme.s5 * 2
            visible: root.loading || root.error !== "" || root.files.length === 0
            text: root.loading ? qsTr("Finding photos…") : root.error !== "" ? root.error : qsTr("No supported photos in this folder.")
            textFormat: Text.PlainText; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl; color: Theme.textSecondary
        }
    }
    Column {
        id: summary
        anchors.bottom: parent.bottom; width: parent.width; spacing: Theme.s1
        Text {
            width: parent.width; text: root.files.length === 1 ? qsTr("%1 of 1 photo selected").arg(root.selectedCount) : qsTr("%1 of %2 photos selected").arg(root.selectedCount).arg(root.files.length)
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl; color: Theme.textPrimary
        }
        Text {
            width: parent.width; elide: Text.ElideMiddle; textFormat: Text.PlainText
            text: root.previewing && root.current ? root.current.relative : qsTr("Tick photos to import · Double-click to preview · Space to tick")
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
        }
        Text {
            objectName: "importUnavailableReason"
            width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText
            text: root.current ? root.current.unavailableReason || "" : ""
            visible: text !== ""
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
        }
    }
}
