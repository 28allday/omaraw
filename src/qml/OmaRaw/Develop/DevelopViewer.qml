import QtQuick
import QtQuick.Window
import OmaRaw.Ui
import "../Ui/PhotoZoom.js" as PhotoZoom
import OmaRaw.Library

// Centre of Develop: the engine render when there is one, the embedded
// preview until it arrives, and a status pill saying which is showing.
Rectangle {
    id: root
    // The canvas behind the picture: the shell's viewer background choice.
    property color canvas: Theme.pasteboard
    color: canvas
    readonly property bool showingRender: view.hasImage
    // Saved settings and original comparison share a bounded native-detail
    // layer. The stored snapshot overview remains available while loading.
    property string compareSource: ""
    property int compareSnapshotId: 0
    property string compareLabel: ""
    property string compareMode: "split"
    property real splitAt: 0.5
    readonly property bool comparing: compareSource !== "" && view.hasImage
    readonly property bool originalComparison: comparing && compareSource.startsWith("image://engine-original/")
    readonly property bool snapshotComparison: comparing && compareSnapshotId > 0 && engine.snapshotId === compareSnapshotId
    readonly property bool nativeComparison: originalComparison || snapshotComparison
    readonly property int comparisonWidth: snapshotComparison ? engine.snapshotFullWidth : originalComparison ? engine.originalFullWidth : 0
    readonly property int comparisonHeight: snapshotComparison ? engine.snapshotFullHeight : originalComparison ? engine.originalFullHeight : 0
    readonly property string comparisonSource: snapshotComparison && engine.snapshotSource !== "" ? engine.snapshotSource : compareSource
    onComparisonWidthChanged: detailTimer.restart()
    onNativeComparisonChanged: detailTimer.restart()
    onOriginalComparisonChanged: detailTimer.restart()
    onCompareModeChanged: detailTimer.restart()
    onSplitAtChanged: detailTimer.restart()
    signal compareClosed()
    // Where the render is drawn, in this item's coordinates (for overlays).
    // Paint mode for brush masks, driven from the Local panel.
    property bool localToolsActive: false
    // Display only: hiding a mask never bypasses its adjustment or edits it.
    property bool maskVisualsShown: true
    property bool maskPeekWasShown: true
    property bool maskPeekHadCoverage: false
    function toggleMaskCoverage() {
        const show = !(maskVisualsShown && engine.maskShown)
        if (show) maskVisualsShown = true
        engine.maskShown = show
    }
    function peekMaskCoverage(on) {
        if (on) {
            maskPeekWasShown = maskVisualsShown; maskPeekHadCoverage = engine.maskShown
            maskVisualsShown = true; engine.maskShown = true
        } else {
            engine.maskShown = maskPeekHadCoverage; maskVisualsShown = maskPeekWasShown
        }
    }
    property bool brushMode: false
    property bool penMode: false
    property bool penNewLocal: true
    property real penFeather: 0.02
    property alias penEditor: penOverlay
    signal penFinished()
    property real brushSize: 0.05
    property real brushHardness: 0.6
    property real brushFlow: 1.0
    // Range picking, driven from the Local panel: a click seeds the band.
    property int pickChannel: -1
    property bool colourRangeNew: false
    function focusRangePicker() { rangePicker.forceActiveFocus() }
    signal picked()
    // White balance picking: a click names a neutral.
    property bool wbPickMode: false
    signal wbPicked()
    // Retouch placing, driven from the Retouch panel; the overlay shows
    // while that tab is open.
    property bool spotMode: false
    property int spotAlgorithm: 2
    property int spotShape: 1
    property real spotSize: 0.03
    property real spotFeather: 0.01
    property bool retouchShown: false
    // Crop & straighten: the engine renders the whole frame, the overlay
    // holds the rectangle, the strip below has ratios and the angle.
    readonly property bool cropMode: engine.cropMode
    readonly property real displayScale: backend.displayColour.windowScale
    property real requestedPercent: 0
    function zoomToPreset(value, percent) {
        requestedPercent = percent
        engine.zoom = percent > 0 ? view.zoomForPercent(percent) : value
    }
    function zoomToPercent(percent) { zoomToPreset(0, percent) }
    function updatePercentZoom() {
        if (requestedPercent > 0 && engine.fullWidth > 0 && view.fitW > 0)
            engine.zoom = view.zoomForPercent(requestedPercent)
    }
    onDisplayScaleChanged: { Qt.callLater(updatePercentZoom); detailTimer.restart() }
    function activateViewer() {
        engine.setViewerActive(root, root.visible)
        if (root.visible) engine.setViewSize(view.width, view.height)
        detailTimer.restart()
    }
    onVisibleChanged: activateViewer()
    Component.onCompleted: activateViewer()
    Component.onDestruction: engine.setViewerActive(root, false)
    Timer {
        id: detailTimer
        interval: 80
        onTriggered: {
            if (!root.visible || engine.zoom <= 0 || img.width <= 0 || img.height <= 0 || engine.fullWidth <= 0) {
                if (!root.visible) return
                engine.setDetailView(0, 0, 0, 0, 0)
                engine.setOriginalDetailView(0, 0, 0, 0, 0)
                return
            }
            engine.setDetailView((view.contentX - img.x) / img.width, (view.contentY - img.y) / img.height,
                view.width / img.width, view.height / img.height, img.width * root.displayScale / engine.fullWidth)
            if (!root.nativeComparison || engine.cropMode || root.comparisonWidth <= 0) {
                engine.setOriginalDetailView(0, 0, 0, 0, 0)
            } else if (root.compareMode === "side") {
                engine.setOriginalDetailView(-sideImage.x / sideImage.width, -sideImage.y / sideImage.height,
                    sidePane.width / sideImage.width, sidePane.height / sideImage.height,
                    sideImage.width * root.displayScale / root.comparisonWidth)
            } else {
                const x = Math.max(0, view.contentX - img.x - splitImage.x), y = Math.max(0, view.contentY - img.y - splitImage.y)
                const right = Math.min(img.width * root.splitAt, view.contentX - img.x + view.width) - splitImage.x
                engine.setOriginalDetailView(x / splitImage.width, y / splitImage.height, Math.max(0, right - x) / splitImage.width,
                    view.height / splitImage.height, splitImage.width * root.displayScale / root.comparisonWidth)
            }
        }
    }
    Connections {
        target: engine
        function onRenderedChanged() { detailTimer.restart() }
        function onFullSizeChanged() { Qt.callLater(root.updatePercentZoom); detailTimer.restart() }
        function onZoomChanged() {
            if (root.requestedPercent > 0 && Math.abs(engine.zoom - view.zoomForPercent(root.requestedPercent)) > 0.000001)
                root.requestedPercent = 0
            detailTimer.restart()
        }
        function onCropModeChanged() { detailTimer.restart() }
        function onImageChanged() { painter.clear(); retouchPaint.clear() }
        function onActiveLocalChanged() { painter.clear() }
        function onOriginalChanged() { detailTimer.restart() }
        function onSnapshotChanged() { detailTimer.restart() }
    }
    Text {
        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: Theme.s3
        z: 20
        visible: (root.snapshotComparison && engine.snapshotStatus !== "") || ((engine.detailBusy || engine.detailLimited) && engine.zoom > 0)
        width: Math.min(implicitWidth, parent.width - Theme.s3 * 2)
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
        text: root.snapshotComparison && engine.snapshotStatus !== "" ? engine.snapshotStatus
            : engine.detailBusy ? qsTr("Loading detail…") : qsTr("Showing central detail. Pan or zoom in to inspect.")
        color: Theme.textPrimary; style: Text.Outline; styleColor: Theme.windowBg
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
    }

    readonly property rect imageRect: Qt.rect(view.x + img.x - view.contentX, view.y + img.y - view.contentY, img.width, img.height)
    // Side by side: the snapshot takes the left half.
    Item {
        id: sidePane
        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
        anchors.margins: Theme.s3
        width: root.comparing && root.compareMode === "side" ? (parent.width - Theme.s3 * 3) / 2 : 0
        visible: width > 0
        clip: true
        WheelHandler {
            target: null
            enabled: view.hasImage
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedModifiers: Qt.KeyboardModifierMask
            onWheel: event => view.wheelZoom(event,
                Qt.point(event.x + (view.width - sidePane.width) / 2, event.y))
        }
        Image {
            id: sideImage
            objectName: "comparisonSideImage"
            readonly property bool nativeDetail: root.nativeComparison && engine.zoom > 0 && root.comparisonWidth > 0 && engine.fullWidth > 0 && !engine.cropMode
            width: nativeDetail ? root.comparisonWidth * img.width / engine.fullWidth : sidePane.width
            height: nativeDetail ? root.comparisonHeight * img.height / engine.fullHeight : sidePane.height
            x: nativeDetail ? sidePane.width / 2 - (view.contentX + view.width / 2 - img.x) / img.width * width : 0
            y: nativeDetail ? sidePane.height / 2 - (view.contentY + view.height / 2 - img.y) / img.height * height : 0
            source: root.comparisonSource ? root.comparisonSource + (root.comparisonSource.indexOf("?") >= 0 ? "&" : "?") + "display=" + backend.displayColour.revision : ""
            fillMode: Image.PreserveAspectFit
            cache: false; asynchronous: true; smooth: true; mipmap: true
            Repeater {
                model: root.nativeComparison ? engine.originalDetailTiles : []
                Image {
                    required property var modelData
                    x: modelData.x * sideImage.width; y: modelData.y * sideImage.height
                    width: modelData.width * sideImage.width; height: modelData.height * sideImage.height
                    source: modelData.source; cache: false; smooth: view.percent < 100
                    visible: sideImage.nativeDetail
                }
            }
        }
    }
    // Fit, or view × zoom with drag-to-pan. A fitted overview supplies the
    // base image; visible native tiles supply the detail at higher zoom.
    Flickable {
        id: view
        objectName: "photoViewport"
        anchors.left: sidePane.visible ? sidePane.right : parent.left
        anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
        anchors.margins: Theme.s3
        anchors.bottomMargin: root.cropMode ? cropToolbar.height + Theme.s3 * 2 : Theme.s3
        readonly property bool hasImage: engine.renderSource !== "" && img.status === Image.Ready
        readonly property real zoom: engine.zoom > 0 ? engine.zoom : 1
        readonly property real imageAspect: engine.fullHeight > 0 ? engine.fullWidth / engine.fullHeight : img.implicitHeight > 0 ? img.implicitWidth / img.implicitHeight : 1
        readonly property real fitW: Math.min(width, height * imageAspect)
        readonly property real fitH: fitW / imageAspect
        onFitWChanged: Qt.callLater(root.updatePercentZoom)
        contentWidth: Math.max(width, fitW * zoom)
        contentHeight: Math.max(height, fitH * zoom)
        clip: true
        interactive: engine.zoom > 0
        boundsBehavior: Flickable.StopAtBounds
        onWidthChanged: if (root.visible) engine.setViewSize(width, height)
        onHeightChanged: if (root.visible) engine.setViewSize(width, height)
        property bool wheelZooming: false
        property bool recenterAfterZoom: false
        function recenter() {
            if (!recenterAfterZoom) return
            recenterAfterZoom = false
            contentX = Math.max(0, (contentWidth - width) / 2)
            contentY = Math.max(0, (contentHeight - height) / 2)
        }
        onZoomChanged: if (!wheelZooming) {
            recenterAfterZoom = true
            Qt.callLater(recenter)
        }
        onContentXChanged: detailTimer.restart()
        onContentYChanged: detailTimer.restart()
        // Zoom presets. `engine.zoom` is a multiple of the fit size (0 = fit);
        // a percentage is image pixels per screen pixel, so it needs the
        // developed size. Fill covers the view on the short side.
        readonly property real fillZoom: fitW > 0 && fitH > 0 ? Math.max(width / fitW, height / fitH) : 0
        function zoomForPercent(pct) { return engine.fullWidth > 0 && fitW > 0 ? (pct / 100) * engine.fullWidth / (fitW * root.displayScale) : 0 }
        readonly property real percent: engine.fullWidth > 0 && fitW > 0 ? zoom * fitW * root.displayScale / engine.fullWidth * 100 : 0
        readonly property var presets: [[0, qsTr("Fit"), 0], [fillZoom, qsTr("Fill"), 0], [zoomForPercent(50), "50%", 50], [zoomForPercent(100), "100%", 100], [zoomForPercent(200), "200%", 200]]
        function isAt(z) { return Math.abs(engine.zoom - z) < 0.000001 }
        function stepPreset(dir) {
            // Fit has effective scale 1. Order wheel steps by image size
            // so smaller percentage presets remain reachable from Fit.
            const zs = presets.map(p => ({ value: p[0], scale: p[0] > 0 ? p[0] : 1, percent: p[2] }))
                .sort((a, b) => a.scale - b.scale)
            const current = engine.zoom > 0 ? engine.zoom : 1
            const next = dir > 0 ? zs.find(p => p.scale > current + 0.000001)
                                 : zs.reverse().find(p => p.scale < current - 0.000001)
            if (next) root.zoomToPreset(next.value, next.percent)
        }
        function wheelZoom(event, point) {
            const steps = PhotoZoom.steps(event)
            if (!steps || !hasImage) { event.accepted = false; return }
            const anchor = PhotoZoom.anchor(view, img, point)
            recenterAfterZoom = false
            wheelZooming = true
            if (event.modifiers & Qt.ControlModifier) {
                stepPreset(steps > 0 ? 1 : -1)
            } else {
                const scale = PhotoZoom.nextScale(zoom, steps, Math.min(1, zoomForPercent(10)),
                    Math.min(256, Math.max(1, zoomForPercent(800))), 1)
                if (Math.abs(scale - 1) < 0.000001) root.zoomToPreset(0, 0)
                else root.zoomToPercent(scale * fitW * root.displayScale / engine.fullWidth * 100)
            }
            PhotoZoom.restore(view, img, point, anchor)
            wheelZooming = false
        }
        Image {
            id: img
            objectName: "engineImage"
            width: view.fitW * view.zoom
            height: view.fitH * view.zoom
            x: Math.max(0, (view.contentWidth - width) / 2)
            y: Math.max(0, (view.contentHeight - height) / 2)
            source: engine.renderSource
            cache: false
            asynchronous: false
            fillMode: Image.Stretch
            smooth: true
            mipmap: true
            onWidthChanged: detailTimer.restart()
            onHeightChanged: detailTimer.restart()
        }
        Repeater {
            model: engine.detailTiles
            Image {
                required property var modelData
                x: img.x + modelData.x * img.width
                y: img.y + modelData.y * img.height
                width: modelData.width * img.width
                height: modelData.height * img.height
                source: modelData.source
                cache: false; asynchronous: false
                smooth: view.percent < 100
                visible: root.visible && engine.zoom > 0
            }
        }
        // Split compare: the snapshot covers the left of the divider, the
        // live render shows to its right. The handle drags the divider.
        Item {
            id: splitPane
            objectName: "splitPane"
            visible: root.comparing && root.compareMode === "split"
            x: img.x; y: img.y; width: img.width * root.splitAt; height: img.height
            clip: true
            Rectangle { anchors.fill: parent; color: root.canvas }
            Image {
                id: splitImage
                objectName: "comparisonSplitImage"
                readonly property bool nativeDetail: root.nativeComparison && engine.zoom > 0 && root.comparisonWidth > 0 && engine.fullWidth > 0 && !engine.cropMode
                width: nativeDetail ? root.comparisonWidth * img.width / engine.fullWidth : img.width
                height: nativeDetail ? root.comparisonHeight * img.height / engine.fullHeight : img.height
                x: (img.width - width) / 2; y: (img.height - height) / 2
                source: root.comparisonSource ? root.comparisonSource + (root.comparisonSource.indexOf("?") >= 0 ? "&" : "?") + "display=" + backend.displayColour.revision : ""
                fillMode: Image.PreserveAspectFit
                cache: false; asynchronous: true; smooth: true; mipmap: true
            }
            Repeater {
                model: root.nativeComparison ? engine.originalDetailTiles : []
                Image {
                    required property var modelData
                    x: splitImage.x + modelData.x * splitImage.width; y: splitImage.y + modelData.y * splitImage.height
                    width: modelData.width * splitImage.width; height: modelData.height * splitImage.height
                    source: modelData.source; cache: false; smooth: view.percent < 100
                    visible: splitImage.nativeDetail
                }
            }
        }
        Rectangle {
            visible: splitPane.visible
            x: img.x + img.width * root.splitAt - 1; y: img.y
            width: 2; height: img.height
            color: Theme.accent
        }
        Item {
            id: splitHandle
            visible: splitPane.visible
            x: img.x + img.width * root.splitAt - 10; y: img.y
            width: 20; height: img.height
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SplitHCursor
                preventStealing: true
                onPositionChanged: mouse => {
                    const p = mapToItem(img, mouse.x, mouse.y)
                    root.splitAt = Math.max(0.02, Math.min(0.98, p.x / img.width))
                }
            }
        }
        // What the active local's mask actually covers, painted over the
        // render. Outlines alone say nothing once a range is narrowing it.
        Image {
            objectName: "localMaskCoverage"
            x: img.x; y: img.y; width: img.width; height: img.height
            source: engine.maskSource
            visible: root.maskVisualsShown && engine.maskShown && source !== "" && !root.cropMode
            fillMode: Image.Stretch
            smooth: true
            cache: false
        }
        // Clipping indicators: red where highlights clip, blue where shadows do.
        Image {
            x: img.x; y: img.y; width: img.width; height: img.height
            source: engine.clippingSource
            visible: engine.clippingShown && source !== "" && !root.cropMode
            fillMode: Image.Stretch
            smooth: false
            cache: false
        }
        Image {
            x: img.x; y: img.y; width: img.width; height: img.height
            source: engine.rawClippingSource
            visible: engine.rawClippingShown && source !== ""
            fillMode: Image.Stretch; smooth: false; cache: false
        }
        Image {
            x: img.x; y: img.y; width: img.width; height: img.height
            source: engine.retouchPreviewSource
            visible: root.retouchShown && engine.retouchPreviewScale >= 0 && source !== "" && !root.cropMode
            fillMode: Image.Stretch; cache: false
        }
        // Local adjustment shapes, drawn over the render; the active one drags.
        LocalOverlay {
            id: localOverlay
            x: img.x; y: img.y; width: img.width; height: img.height
            objectName: "localShapeOverlay"
            visible: root.maskVisualsShown && root.localToolsActive && view.hasImage && engine.locals.length > 0 && !root.cropMode
            paintSize: root.brushSize
            viewport: Qt.rect(view.contentX-img.x, view.contentY-img.y, view.width, view.height)
        }
        PenMaskOverlay {
            id: penOverlay
            x: img.x; y: img.y; width: img.width; height: img.height
            visible: root.maskVisualsShown && root.localToolsActive && view.hasImage && !root.cropMode
            enabled: visible && !root.brushMode && root.pickChannel < 0
            drawing: root.penMode
            newLocal: root.penNewLocal; feather: root.penFeather
            viewport: Qt.rect(view.contentX-img.x, view.contentY-img.y, view.width, view.height)
            onFinished: root.penFinished()
        }
        // Paint mode: drag anywhere on the render to lay a stroke into the
        // active local. Nodes are thinned so a slow drag is not a thousand
        // of them; the engine smooths what it gets into a spline.
        MouseArea {
            id: painter
            objectName: "localBrushPaint"
            x: img.x; y: img.y; width: img.width; height: img.height
            enabled: root.maskVisualsShown && root.localToolsActive && root.brushMode && view.hasImage && engine.activeLocal >= 0 && !root.cropMode
            visible: enabled
            cursorShape: Qt.CrossCursor
            preventStealing: true
            property var nodes: []
            property int imageId: -1
            property int localId: -1
            function clear() { nodes = []; localOverlay.painting = []; imageId = -1; localId = -1 }
            onEnabledChanged: if (!enabled) clear()
            onVisibleChanged: if (!visible) clear()
            onCanceled: clear()
            function addNode(px, py) {
                if (nodes.length >= 2048) return
                const nx = Math.max(0, Math.min(1, px / width)), ny = Math.max(0, Math.min(1, py / height))
                const last = nodes.length > 0 ? nodes[nodes.length - 1] : null
                if (last && Math.hypot((nx - last.x)*width, (ny - last.y)*height) < 3) return
                nodes = nodes.concat([{ x: nx, y: ny }])
                localOverlay.painting = nodes
            }
            onPressed: mouse => { clear(); imageId = engine.imageId; localId = engine.activeLocal; addNode(mouse.x, mouse.y) }
            onPositionChanged: mouse => { if (pressed && imageId === engine.imageId && localId === engine.activeLocal) addNode(mouse.x, mouse.y) }
            onReleased: {
                if (nodes.length > 0 && imageId === engine.imageId && localId === engine.activeLocal)
                    engine.addBrushStroke(nodes, root.brushSize, root.brushHardness, root.brushFlow)
                clear()
            }
        }
        // Spot placing: a click puts a spot under the cursor; heal and clone
        // get a source the same size a little to the side, kept in frame.
        MouseArea {
            id: retouchPaint
            objectName: "retouchPaint"
            x: img.x; y: img.y; width: img.width; height: img.height
            enabled: root.spotMode && view.hasImage && root.retouchShown && !root.cropMode
            visible: enabled
            cursorShape: Qt.CrossCursor
            preventStealing: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            property var points: []
            property bool finishing: false
            property int pressedImage: -1
            function clear() { points = []; finishing = false; pressedImage = -1 }
            onEnabledChanged: if(!enabled) clear()
            onVisibleChanged: if (!visible) clear()
            Connections { target: root; function onSpotShapeChanged() { retouchPaint.points = [] } }
            onPressed: mouse => { pressedImage = engine.imageId; if(mouse.button === Qt.LeftButton && root.spotShape === 3) points = [{x: mouse.x/width, y: mouse.y/height}] }
            onPositionChanged: mouse => {
                if(pressedImage !== engine.imageId || !(pressedButtons & Qt.LeftButton) || root.spotShape !== 3 || points.length >= 2048) return
                const p={x: Math.max(0,Math.min(1,mouse.x/width)),y: Math.max(0,Math.min(1,mouse.y/height))}
                const last=points[points.length-1]
                if(!last || Math.hypot((p.x-last.x)*width,(p.y-last.y)*height)>3) points=[...points,p]
            }
            function finish() {
                if(points.length < (root.spotShape === 4 ? 3 : 1)) {points=[];return}
                const p=points[0], dx=root.spotSize*2.6*Math.min(width,height)/width
                engine.toolAction("retouch","draw",{shape:root.spotShape,algorithm:root.spotAlgorithm,points:points,
                    radius:root.spotSize,border:root.spotFeather,sx:p.x+dx<.98?p.x+dx:p.x-dx,sy:p.y})
                points=[]
            }
            onReleased: mouse => {
                if(pressedImage === engine.imageId && mouse.button === Qt.LeftButton && root.spotShape === 3) finish()
                Qt.callLater(() => finishing = false)
            }
            onCanceled: clear()
            onDoubleClicked: mouse => { if(mouse.button === Qt.LeftButton && root.spotShape === 4) { finishing = true; finish() } }
            onClicked: mouse => {
                if (pressedImage !== engine.imageId) return
                if(finishing) {finishing = false;return}
                if(mouse.button === Qt.RightButton) {points=[];return}
                if(root.spotShape === 3) return
                if(root.spotShape === 4) { if(points.length < 2048) points=[...points,{x:mouse.x/width,y:mouse.y/height}];return }
                const nx = Math.max(0, Math.min(1, mouse.x / width)), ny = Math.max(0, Math.min(1, mouse.y / height))
                const dx = root.spotSize * 2.6 * Math.min(width, height) / width
                const sx = nx + dx <= 0.98 ? nx + dx : nx - dx
                engine.addSpot(root.spotAlgorithm, nx, ny, root.spotSize, root.spotFeather, sx, ny)
            }
            StrokeGuide {
                objectName: "retouchWetGuide"
                viewport: Qt.rect(view.contentX-img.x, view.contentY-img.y, view.width, view.height)
                points: retouchPaint.points
                penColor: Theme.accent
                penWidth: root.spotShape === 3 ? Math.max(2, root.spotSize*Math.min(parent.width,parent.height)*2) : 2
                penOpacity: 0.55
                closed: root.spotShape === 4 && points.length > 2
            }
        }
        // Existing target/source handles take the press before the placing
        // area, so leaving Place enabled does not create accidental spots.
        RetouchOverlay {
            x: img.x; y: img.y; width: img.width; height: img.height
            visible: view.hasImage && root.retouchShown && engine.spots.length > 0 && !root.cropMode
            enabled: retouchPaint.points.length === 0
            viewport: Qt.rect(view.contentX-img.x, view.contentY-img.y, view.width, view.height)
        }
        // Pick mode: one click reads the pixel under the cursor into the
        // active local's range, then the mode drops.
        MouseArea {
            id: rangePicker
            objectName: "localRangePicker"
            x: img.x; y: img.y; width: img.width; height: img.height
            enabled: root.maskVisualsShown && root.localToolsActive && root.pickChannel >= 0 && view.hasImage
                     && (engine.activeLocal >= 0 || (root.pickChannel === 3 && root.colourRangeNew)) && !root.cropMode && !engine.busy && engine.ai.mode === ""
            visible: enabled
            cursorShape: Qt.CrossCursor
            preventStealing: true
            property point start: Qt.point(0, 0)
            Keys.onShortcutOverride: event => { if (event.key === Qt.Key_Escape) event.accepted = true }
            Keys.onEscapePressed: { root.picked(); root.forceActiveFocus() }
            onPressed: mouse => start = Qt.point(mouse.x, mouse.y)
            onReleased: mouse => {
                if (root.pickChannel === 3) {
                    engine.pickColourRange(start.x / width, start.y / height, mouse.x / width, mouse.y / height, root.colourRangeNew)
                    engine.maskShown = true
                } else engine.pickRange(root.pickChannel, Math.max(0, Math.min(1, mouse.x / width)), Math.max(0, Math.min(1, mouse.y / height)))
                root.picked()
            }
            Rectangle {
                visible: rangePicker.pressed && root.pickChannel === 3
                x: Math.min(rangePicker.start.x, rangePicker.mouseX); y: Math.min(rangePicker.start.y, rangePicker.mouseY)
                width: Math.abs(rangePicker.mouseX-rangePicker.start.x); height: Math.abs(rangePicker.mouseY-rangePicker.start.y)
                color: "#224080ff"; border.color: "white"; border.width: 1
            }
        }
        // White balance: one click on something neutral sets the illuminant.
        MouseArea {
            x: img.x; y: img.y; width: img.width; height: img.height
            enabled: root.wbPickMode && view.hasImage && !root.cropMode
            visible: enabled
            cursorShape: Qt.CrossCursor
            preventStealing: true
            onClicked: mouse => {
                engine.pickWhiteBalance(Math.max(0, Math.min(1, mouse.x / width)), Math.max(0, Math.min(1, mouse.y / height)))
                root.wbPicked()
            }
        }
        // The crop rectangle over the whole frame, while the tool is open.
        CropOverlay {
            id: cropOverlay
            objectName: "cropOverlay"
            x: img.x; y: img.y; width: img.width; height: img.height
            visible: root.cropMode && view.hasImage
        }
        // Double-click toggles fit / 1:1 when no on-picture tool is active.
        AiSelectionOverlay {
            x: img.x; y: img.y; width: img.width; height: img.height
            visible: engine.ai.mode !== "" && view.hasImage && !root.cropMode
            z: 15
        }
        TapHandler {
            objectName: "viewerZoomGesture"
            enabled: engine.ai.mode === "" && !root.cropMode && (!root.localToolsActive || !root.maskVisualsShown) && !root.retouchShown && !root.wbPickMode
            onDoubleTapped: engine.zoom > 0 ? root.zoomToPreset(0, 0) : root.zoomToPercent(100)
        }
    }
    WheelHandler {
        parent: view
        target: null
        enabled: view.hasImage
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        acceptedModifiers: Qt.KeyboardModifierMask
        onWheel: event => view.wheelZoom(event, view.mapFromItem(view.contentItem, event.x, event.y))
    }
    LoupeView {
        anchors.fill: parent
        visible: !view.hasImage && backend.currentId > 0
    }
    EmptyState {
        anchors.centerIn: parent
        visible: backend.currentId === 0
        iconName: "image"
        title: qsTr("No photo selected")
        description: qsTr("Pick one in the filmstrip.")
    }
    Rectangle {
        anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: Theme.s3
        width: note.implicitWidth + Theme.s4; height: Theme.hControl + Theme.s1
        radius: Theme.rControl; color: Theme.scrim
        // A finished render needs no label; the pill speaks for waiting,
        // proofing and the embedded preview.
        visible: backend.currentId > 0 && note.text !== ""
        Text {
            id: note
            anchors.centerIn: parent
            text: engine.busy && !view.hasImage ? qsTr("Rendering with the engine…")
                : view.hasImage && engine.proofing ? qsTr("Soft proof · %1%2").arg(engine.proofProfileName).arg(engine.proofGamutWarning ? qsTr(" · gamut warning") : "")
                : view.hasImage ? ""
                : engine.ready ? qsTr("Embedded preview") : engine.status
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
            color: view.hasImage && engine.proofing ? Theme.accent : view.hasImage ? Theme.textSecondary : Theme.warning
        }
    }
    // Compare strip: what is on each side, the layout toggle, and a way out.
    Rectangle {
        id: compareStrip
        objectName: "compareStrip"
        anchors.bottom: zoomControls.top; anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: Theme.s2
        width: Math.min(compareIcon.width + comparisonLabel.implicitWidth + compareModes.implicitWidth
                        + compareClose.width + Theme.s2 * 3 + Theme.s3 * 2, parent.width - Theme.s3 * 2)
        height: Theme.hControl + Theme.s2
        radius: Theme.rControl; color: Theme.scrim
        visible: root.comparing && !root.cropMode
        Row {
            id: compareRow
            anchors.centerIn: parent
            width: parent.width - Theme.s3 * 2
            spacing: Theme.s2
            Icon { id: compareIcon; anchors.verticalCenter: parent.verticalCenter; name: "columns-2"; size: 13; color: Theme.accent }
            Text {
                id: comparisonLabel
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, parent.width - compareIcon.width - compareModes.width - compareClose.width - Theme.s2 * 3)
                textFormat: Text.PlainText
                text: qsTr("%1  ·  Current").arg(root.compareLabel)
                elide: Text.ElideRight
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
            }
            SegmentedControl {
                id: compareModes
                anchors.verticalCenter: parent.verticalCenter
                labels: [qsTr("Split"), qsTr("Side by side")]
                tips: [qsTr("One picture with a divider you can drag across it."), qsTr("Both pictures whole, next to each other.")]
                currentIndex: root.compareMode === "side" ? 1 : 0
                onActivated: i => root.compareMode = i === 1 ? "side" : "split"
            }
            IconButton { id: compareClose; anchors.verticalCenter: parent.verticalCenter; iconName: "x"; text: qsTr("Stop comparing"); tip: qsTr("Back to the current render alone."); onClicked: root.compareClosed() }
        }
    }
    CropToolbar {
        id: cropToolbar
        anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: Theme.s3
        width: Math.min(800, parent.width - Theme.s3 * 2)
        height: implicitHeight
        visible: root.cropMode
        overlay: cropOverlay
    }
    // Tool pills: crop & straighten, clipping indicators, false colour.
    Row {
        anchors.left: parent.left; anchors.top: parent.top; anchors.margins: Theme.s3
        spacing: Theme.s1
        visible: view.hasImage || root.cropMode
        Rectangle {
            width: Theme.hControl + Theme.s1; height: Theme.hControl + Theme.s1
            radius: Theme.rControl; color: root.cropMode ? Theme.accent : Theme.scrim
            IconButton {
                anchors.centerIn: parent
                iconName: "crop"; text: qsTr("Crop & straighten"); shortcut: "R"
                tip: qsTr("Drag the frame's edges and corners, straighten, pick a ratio.")
                iconColor: root.cropMode ? Theme.accentText : Theme.textSecondary
                onClicked: engine.cropMode = !engine.cropMode
            }
        }
        Rectangle {
            width: Theme.hControl + Theme.s1; height: Theme.hControl + Theme.s1
            radius: Theme.rControl; color: engine.clippingShown ? Theme.accent : Theme.scrim
            IconButton {
                anchors.centerIn: parent
                iconName: "triangle-alert"; text: qsTr("Clipping indicators"); shortcut: "J"
                tip: qsTr("Paints red where highlights are lost and blue where shadows are.")
                iconColor: engine.clippingShown ? Theme.accentText : Theme.textSecondary
                onClicked: engine.clippingShown = !engine.clippingShown
            }
        }
        Rectangle {
            id: falseColourPill
            width: Theme.hControl + Theme.s1; height: Theme.hControl + Theme.s1
            radius: Theme.rControl; color: engine.falseColour ? Theme.accent : Theme.scrim
            IconButton {
                objectName: "falseColourButton"
                anchors.centerIn: parent
                iconName: "palette"; text: qsTr("False colour"); shortcut: "F"
                tip: qsTr("Repaints the picture by brightness and shows an on-screen colour guide. The graph shows how much of the picture is in each zone.")
                iconColor: engine.falseColour ? Theme.accentText : Theme.textSecondary
                onClicked: engine.toggleFalseColour()
            }
        }
    }
    Row {
        id: zoomControls
        objectName: "zoomControls"
        anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: Theme.s3
        spacing: Theme.s1
        visible: view.hasImage && !root.cropMode
        Repeater {
            model: view.presets
            Rectangle {
                id: zoomPill
                required property var modelData
                readonly property bool at: view.isAt(modelData[0])
                width: zl.implicitWidth + Theme.s3; height: Theme.hControl
                radius: Theme.rControl
                color: at ? Theme.accent : Theme.scrim
                Text {
                    id: zl
                    anchors.centerIn: parent
                    text: modelData[1]
                    font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel
                    color: parent.at ? Theme.accentText : Theme.textSecondary
                }
                TapHandler { onTapped: root.zoomToPreset(parent.modelData[0], parent.modelData[2]) }
                HoverHandler { id: zoomHover }
                Tooltip {
                    text: zoomPill.modelData[1]
                    description: zoomPill.modelData[2] === 0
                                 ? (zoomPill.modelData[1] === qsTr("Fit") ? qsTr("The whole picture in the view; double-click the picture to toggle with 100%.") : qsTr("Fills the view on the short side."))
                                 : qsTr("%1 picture pixels per screen pixel; Ctrl+wheel steps through these.").arg(zoomPill.modelData[2] / 100)
                    visible: zoomHover.hovered
                }
            }
        }
        // What the screen shows: image pixels per screen pixel.
        Rectangle {
            visible: view.percent > 0
            width: pct.implicitWidth + Theme.s3; height: Theme.hControl
            radius: Theme.rControl; color: Theme.scrim
            Text {
                id: pct
                anchors.centerIn: parent
                text: Math.round(view.percent) + "%"
                font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
        }
    }
    FalseColourGuide {
        anchors.right: parent.right
        anchors.bottom: root.cropMode ? cropToolbar.top : compareStrip.visible ? compareStrip.top : zoomControls.top
        anchors.rightMargin: Theme.s3; anchors.bottomMargin: Theme.s2
        width: Math.min(implicitWidth, parent.width - Theme.s3 * 2)
        height: implicitHeight
        active: engine.falseColour
        visible: active && view.hasImage
        bands: engine.falseColourBands
    }
    Rectangle {
        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: Theme.s3
        width: 8; height: 8; radius: 4
        color: Theme.accent
        visible: engine.busy && view.hasImage
        SequentialAnimation on opacity {
            running: engine.busy; loops: Animation.Infinite
            NumberAnimation { to: 0.2; duration: 400 } NumberAnimation { to: 1; duration: 400 }
        }
    }
}
