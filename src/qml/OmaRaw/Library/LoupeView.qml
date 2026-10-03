import QtQuick
import OmaRaw.Ui
import "../Ui/PhotoZoom.js" as PhotoZoom

// One photo large, from the latest edited preview (or the embedded preview
// before editing). Wheel zooms, click toggles 1:1, and dragging pans.
Rectangle {
    id: root
    property int assetId: backend.currentId
    property int previewRevision: 0
    property string source: (previewRevision, assetId ? backend.thumbSource(assetId, 2048) : "")
    Connections {
        target: backend
        function onPreviewChanged(id) { if (id === root.assetId) ++root.previewRevision }
        // Selection and display-profile changes can keep the same asset ID.
        function onSelectionChanged() { ++root.previewRevision }
    }
    property real zoom: 0 // zero follows Fit; positive values scale the preview
    property bool toggleOnTap: true
    property bool showZoomBadge: true
    property string previewObjectName: "libraryPreviewImage"
    function resetZoom() {
        zoom = 0
        flick.contentX = 0
        flick.contentY = 0
    }
    color: Theme.pasteboard

    Flickable {
        id: flick
        objectName: "libraryPhotoViewport"
        anchors.fill: parent
        contentWidth: Math.max(width, img.width * img.scale)
        contentHeight: Math.max(height, img.height * img.scale)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Image {
            id: img
            objectName: root.previewObjectName
            source: root.source
            asynchronous: true
            smooth: true; mipmap: true
            fillMode: Image.Pad
            readonly property real fit: implicitWidth > 0
                ? Math.max(0.001, Math.min((flick.width - Theme.s4) / implicitWidth, (flick.height - Theme.s4) / implicitHeight)) : 1
            scale: root.zoom > 0 ? root.zoom : fit
            transformOrigin: Item.TopLeft
            x: Math.max(0, (flick.width - width * scale) / 2)
            y: Math.max(0, (flick.height - height * scale) / 2)
        }
        TapHandler {
            enabled: root.toggleOnTap
            onTapped: {
                root.zoom = root.zoom > 0 ? 0 : 1
                flick.contentX = Math.max(0, (flick.contentWidth - flick.width) / 2)
                flick.contentY = Math.max(0, (flick.contentHeight - flick.height) / 2)
            }
            onDoubleTapped: root.resetZoom()
        }
    }
    WheelHandler {
        parent: flick
        target: null
        enabled: img.status === Image.Ready
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        acceptedModifiers: Qt.KeyboardModifierMask
        onWheel: event => {
            const steps = PhotoZoom.steps(event)
            if (!steps) { event.accepted = false; return }
            const point = flick.mapFromItem(flick.contentItem, event.x, event.y)
            const anchor = PhotoZoom.anchor(flick, img, point)
            const scale = PhotoZoom.nextScale(img.scale, steps, Math.min(img.fit, 0.1), Math.max(img.fit, 8), img.fit)
            root.zoom = Math.abs(scale - img.fit) < 0.000001 ? 0 : scale
            PhotoZoom.restore(flick, img, point, anchor)
        }
    }
    Icon {
        anchors.centerIn: parent
        visible: img.status === Image.Loading
        name: "image"; size: 28; color: Theme.textMuted
    }
    EmptyState {
        anchors.centerIn: parent
        visible: root.assetId !== 0 && img.status === Image.Error
        iconName: "image"
        title: qsTr("Preview unavailable")
        description: qsTr("A preview could not be read or generated for this photo.")
    }
    EmptyState {
        anchors.centerIn: parent
        visible: root.assetId === 0
        iconName: "image"
        title: qsTr("No photo selected")
        description: qsTr("Pick a photo in the grid or filmstrip.")
    }
    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: Theme.s3
        width: zoomText.implicitWidth + Theme.s3; height: Theme.hControl
        radius: Theme.rControl
        color: Theme.scrim
        visible: root.showZoomBadge && img.status === Image.Ready
        Text {
            id: zoomText
            anchors.centerIn: parent
            text: root.zoom > 0 ? qsTr("%1% · preview").arg(Math.round(root.zoom * 100)) : qsTr("fit · preview")
            font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
        }
    }
}
