pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// The crop rectangle over the whole frame (sized to the image item it sits
// on). Drag inside to move it, drag an edge or corner to resize, with the
// locked aspect kept from the opposite side. Outside the rectangle the
// picture is dimmed; inside, a rule-of-thirds grid. The engine hears the
// rectangle once, on release: a drag is not a dozen history items.
Item {
    id: root
    objectName: "cropOverlay"
    readonly property var crop: engine.crop || ({})
    // Pending rectangle while dragging, else the engine's.
    property bool dragging: false
    property var pend: ({ l: 0, t: 0, r: 1, b: 1 })
    readonly property real l: dragging ? pend.l : (crop.cx !== undefined ? crop.cx : 0)
    readonly property real t: dragging ? pend.t : (crop.cy !== undefined ? crop.cy : 0)
    readonly property real r: dragging ? pend.r : (crop.cw !== undefined ? crop.cw : 1)
    readonly property real b: dragging ? pend.b : (crop.ch !== undefined ? crop.ch : 1)
    readonly property int ratioN: crop.ratioN || 0
    readonly property int ratioD: crop.ratioD || 0
    // Locked aspect as width/height in FRACTION space (0 = free): the
    // image's own pixel aspect is folded in, so 1:1 is square on screen.
    readonly property real lock: aspectLock(ratioN, ratioD)
    function aspectLock(n, d) {
        if (n <= 0 || width <= 0 || height <= 0) return 0
        let px = 1.0
        if (d === 0) px = (crop.origW > 0 && crop.origH > 0) ? crop.origW / crop.origH : 1.0 * width / height  // original: the image's own, not the straightened frame
        else px = d < 0 ? Math.abs(d) / n : n / Math.abs(d)
        if (d !== 0 && n < Math.abs(d)) px = 1 / px  // n:d written small:large
        return px * height / width
    }
    readonly property real minSize: 0.02
    readonly property int handle: 9
    // Guide drawn inside the rectangle: thirds, golden ratio, diagonals, none.
    property string grid: "thirds"
    // Ruler: drag a line along something that should be level or plumb.
    property bool ruler: false
    property real rulerX0: -1
    property real rulerY0: -1
    property real rulerX1: -1
    property real rulerY1: -1
    property bool guided: false
    property var guides: []
    property var pendingGuide: null
    function cancelGesture() {
        dragging = false
        mouse.grip = -1
        pendingGuide = null
        rulerX0 = -1; rulerY0 = -1; rulerX1 = -1; rulerY1 = -1
    }
    onVisibleChanged: if (!visible) cancelGesture()
    Connections {
        target: engine
        function onRenderedChanged() { root.guides = []; root.pendingGuide = null }
        function onImageChanged() { root.cancelGesture(); root.guided = false; root.ruler = false; root.guides = [] }
        function onCropModeChanged() { if (!engine.cropMode) { root.cancelGesture(); root.guided = false; root.ruler = false; root.guides = [] } }
    }

    function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }
    function commit(rect) {
        engine.setCrop(rect.l, rect.t, rect.r, rect.b, root.ratioN, root.ratioD, true)
    }
    // Refit the current rectangle to a new aspect around its centre, then
    // shrink until it sits inside the frame.
    function fitToLock(rect, lockValue) {
        if (lockValue <= 0) return rect
        const cx = (rect.l + rect.r) / 2, cy = (rect.t + rect.b) / 2
        let w = rect.r - rect.l, h = rect.b - rect.t
        if (w / h > lockValue) w = h * lockValue; else h = w / lockValue
        // keep inside the unit square
        if (w > 1) { w = 1; h = w / lockValue }
        if (h > 1) { h = 1; w = h * lockValue }
        let l2 = cx - w / 2, t2 = cy - h / 2
        l2 = clamp(l2, 0, 1 - w); t2 = clamp(t2, 0, 1 - h)
        return { l: l2, t: t2, r: l2 + w, b: t2 + h }
    }
    function setRatio(n, d) {
        const rect = fitToLock({ l: root.l, t: root.t, r: root.r, b: root.b }, aspectLock(n, d))
        engine.setCrop(rect.l, rect.t, rect.r, rect.b, n, d, true)
    }
    function flipRatio() {
        if (root.ratioN <= 0 || root.ratioD === 0) return
        setRatio(root.ratioN, -root.ratioD)
    }
    function reset() { engine.setCrop(0, 0, 1, 1, 0, 0, true) }

    // dimmed outside
    Rectangle { x: 0; y: 0; width: parent.width; height: root.t * parent.height; color: Theme.scrim; opacity: 0.6 }
    Rectangle { x: 0; y: root.b * parent.height; width: parent.width; height: parent.height - y; color: Theme.scrim; opacity: 0.6 }
    Rectangle { x: 0; y: root.t * parent.height; width: root.l * parent.width; height: (root.b - root.t) * parent.height; color: Theme.scrim; opacity: 0.6 }
    Rectangle { x: root.r * parent.width; y: root.t * parent.height; width: parent.width - x; height: (root.b - root.t) * parent.height; color: Theme.scrim; opacity: 0.6 }

    Item {
        id: frame
        x: root.l * root.width; y: root.t * root.height
        width: (root.r - root.l) * root.width; height: (root.b - root.t) * root.height
        Rectangle { anchors.fill: parent; color: "transparent"; border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.9) }
        // guide lines
        readonly property var splits: root.grid === "thirds" ? [1 / 3, 2 / 3] : root.grid === "golden" ? [0.382, 0.618] : []
        Repeater {
            model: frame.splits
            Rectangle { required property var modelData; x: frame.width * modelData; y: 0; width: 1; height: frame.height; color: Qt.rgba(1, 1, 1, 0.35) }
        }
        Repeater {
            model: frame.splits
            Rectangle { required property var modelData; y: frame.height * modelData; x: 0; height: 1; width: frame.width; color: Qt.rgba(1, 1, 1, 0.35) }
        }
        Repeater {
            model: root.grid === "diagonals" ? [[0,0,45], [1,0,135], [0,1,-45], [1,1,-135]] : []
            Rectangle {
                required property var modelData
                x: modelData[0]*frame.width; y: modelData[1]*frame.height
                width: Math.SQRT2*Math.min(frame.width,frame.height); height: 1
                color: Qt.rgba(1,1,1,.35)
                transformOrigin: Item.TopLeft; rotation: modelData[2]
            }
        }
        // handles: corners and edge midpoints
        Repeater {
            model: [[0, 0], [0.5, 0], [1, 0], [0, 0.5], [1, 0.5], [0, 1], [0.5, 1], [1, 1]]
            Rectangle {
                required property var modelData
                x: modelData[0] * frame.width - root.handle / 2
                y: modelData[1] * frame.height - root.handle / 2
                width: root.handle; height: root.handle; radius: 2
                color: Theme.panelBg; border.width: 1.5; border.color: Theme.accent
            }
        }
    }
    Text {
        x: frame.x + Theme.s1; y: frame.y + frame.height + Theme.s1
        visible: y + height < root.height
        text: qsTr("%1 × %2").arg(Math.round((root.r - root.l) * engine.renderWidth)).arg(Math.round((root.b - root.t) * engine.renderHeight))
              + (root.ratioN > 0 ? "  ·  " + (root.ratioD === 0 ? qsTr("original") : (root.ratioD < 0 ? Math.abs(root.ratioD) + ":" + root.ratioN : root.ratioN + ":" + root.ratioD)) : "")
        font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
    }

    // the ruler line while it is being drawn
    Repeater {
        model: root.guided ? root.guides.concat(root.pendingGuide ? [root.pendingGuide] : []) : []
        Item {
            required property var modelData
            required property int index
            x: modelData.x0 * root.width; y: modelData.y0 * root.height
            Rectangle {
                width: Math.hypot((modelData.x1 - modelData.x0) * root.width, (modelData.y1 - modelData.y0) * root.height)
                height: 2; color: Theme.accent
                transformOrigin: Item.TopLeft
                rotation: Math.atan2((modelData.y1 - modelData.y0) * root.height, (modelData.x1 - modelData.x0) * root.width) * 180 / Math.PI
            }
            Rectangle { x: -4; y: -4; width: 8; height: 8; radius: 4; color: Theme.accent }
            Text {
                x: 7; y: -14; text: index + 1
                color: Theme.textPrimary; style: Text.Outline; styleColor: Theme.windowBg
                font.pixelSize: Theme.fsControl
            }
        }
    }
    MouseArea {
        anchors.fill: parent
        enabled: root.guided && !engine.busy
        visible: root.guided
        cursorShape: Qt.CrossCursor; preventStealing: true
        onPressed: m => {
            if (root.guides.length >= 4) return
            root.pendingGuide = { x0: m.x/root.width, y0: m.y/root.height, x1: m.x/root.width, y1: m.y/root.height }
        }
        onPositionChanged: m => {
            if (!root.pendingGuide) return
            root.pendingGuide = { x0: root.pendingGuide.x0, y0: root.pendingGuide.y0,
                x1: root.clamp(m.x/root.width, 0, 1), y1: root.clamp(m.y/root.height, 0, 1) }
        }
        onReleased: {
            const line = root.pendingGuide
            if (line && Math.hypot((line.x1-line.x0)*root.width, (line.y1-line.y0)*root.height) >= 12)
                root.guides = root.guides.concat([line])
            root.pendingGuide = null
        }
        onCanceled: root.pendingGuide = null
    }
    Rectangle {
        visible: root.ruler && root.rulerX0 >= 0
        x: root.rulerX0 * root.width; y: root.rulerY0 * root.height
        width: Math.hypot((root.rulerX1 - root.rulerX0) * root.width, (root.rulerY1 - root.rulerY0) * root.height)
        height: 2
        transformOrigin: Item.TopLeft
        rotation: Math.atan2((root.rulerY1 - root.rulerY0) * root.height, (root.rulerX1 - root.rulerX0) * root.width) * 180 / Math.PI
        color: Theme.accent
    }
    MouseArea {
        anchors.fill: parent
        enabled: root.ruler && !engine.busy
        visible: enabled
        cursorShape: Qt.CrossCursor
        preventStealing: true
        onPressed: m => { root.rulerX0 = m.x / root.width; root.rulerY0 = m.y / root.height; root.rulerX1 = root.rulerX0; root.rulerY1 = root.rulerY0 }
        onCanceled: root.cancelGesture()
        onPositionChanged: m => { if (root.rulerX0 >= 0) { root.rulerX1 = m.x / root.width; root.rulerY1 = m.y / root.height } }
        onReleased: {
            if (root.rulerX0 >= 0 && Math.hypot((root.rulerX1 - root.rulerX0) * root.width, (root.rulerY1 - root.rulerY0) * root.height) > 8)
                engine.straightenAlong(root.rulerX0, root.rulerY0, root.rulerX1, root.rulerY1)
            root.rulerX0 = -1; root.rulerY0 = -1; root.rulerX1 = -1; root.rulerY1 = -1
            root.ruler = false
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: !root.ruler && !root.guided && !engine.geometryBusy
        hoverEnabled: true
        preventStealing: true
        // -1 none; 0..7 the handle order above; 8 move
        property int grip: -1
        property real startX: 0
        property real startY: 0
        property var start: ({ l: 0, t: 0, r: 1, b: 1 })
        function hit(px, py) {
            const fx = px / root.width, fy = py / root.height
            const hx = (root.handle + 4) / root.width, hy = (root.handle + 4) / root.height
            const near = (a, b2) => Math.abs(a - b2) <= 1.0
            const xs = [root.l, (root.l + root.r) / 2, root.r], ys = [root.t, (root.t + root.b) / 2, root.b]
            const spots = [[0, 0], [1, 0], [2, 0], [0, 1], [2, 1], [0, 2], [1, 2], [2, 2]]
            for (let i = 0; i < spots.length; ++i)
                if (Math.abs(fx - xs[spots[i][0]]) <= hx && Math.abs(fy - ys[spots[i][1]]) <= hy) return i
            if (fx > root.l && fx < root.r && fy > root.t && fy < root.b) return 8
            return -1
        }
        cursorShape: {
            const g = grip >= 0 ? grip : hit(mouseX, mouseY)
            switch (g) {
            case 0: case 7: return Qt.SizeFDiagCursor
            case 2: case 5: return Qt.SizeBDiagCursor
            case 1: case 6: return Qt.SizeVerCursor
            case 3: case 4: return Qt.SizeHorCursor
            case 8: return grip === 8 ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            }
            return Qt.ArrowCursor
        }
        onPressed: m => {
            grip = hit(m.x, m.y)
            if (grip < 0) return
            startX = m.x; startY = m.y
            start = { l: root.l, t: root.t, r: root.r, b: root.b }
            root.pend = start
            root.dragging = true
        }
        onPositionChanged: m => {
            if (grip < 0 || !root.dragging) return
            const dx = (m.x - startX) / root.width, dy = (m.y - startY) / root.height
            let l = start.l, t = start.t, r = start.r, b = start.b
            const w0 = r - l, h0 = b - t
            if (grip === 8) {
                l = root.clamp(start.l + dx, 0, 1 - w0); t = root.clamp(start.t + dy, 0, 1 - h0)
                r = l + w0; b = t + h0
            } else {
                const spots = [[0, 0], [1, 0], [2, 0], [0, 1], [2, 1], [0, 2], [1, 2], [2, 2]]
                const sx = spots[grip][0], sy = spots[grip][1]   // 0 left/top, 1 middle, 2 right/bottom
                if (sx === 0) l = root.clamp(start.l + dx, 0, start.r - root.minSize)
                if (sx === 2) r = root.clamp(start.r + dx, start.l + root.minSize, 1)
                if (sy === 0) t = root.clamp(start.t + dy, 0, start.b - root.minSize)
                if (sy === 2) b = root.clamp(start.b + dy, start.t + root.minSize, 1)
                if (root.lock > 0) {
                    const aspect = root.lock
                    const ax = sx === 0 ? start.r : sx === 2 ? start.l : (start.l+start.r)/2
                    const ay = sy === 0 ? start.b : sy === 2 ? start.t : (start.t+start.b)/2
                    const maxW = sx === 1 ? 2*Math.min(ax,1-ax) : sx === 0 ? ax : 1-ax
                    const maxH = sy === 1 ? 2*Math.min(ay,1-ay) : sy === 0 ? ay : 1-ay
                    const limit = Math.min(maxW,maxH*aspect)
                    const wantW = w0 + (sx === 0 ? -dx : dx)
                    const wantH = h0 + (sy === 0 ? -dy : dy)
                    // Project corner motion onto the locked aspect in screen
                    // space, so vertical as well as horizontal drags respond.
                    const wx = root.width*root.width, wy = root.height*root.height/aspect
                    const wanted = sx === 1 ? wantH*aspect : sy === 1 ? wantW
                                   : (wantW*wx + wantH*wy)/(wx + wy/aspect)
                    const w = root.clamp(wanted,Math.min(limit,Math.max(root.minSize,root.minSize*aspect)),limit)
                    const h = w/aspect
                    l = sx === 0 ? ax-w : sx === 2 ? ax : ax-w/2
                    r = l+w
                    t = sy === 0 ? ay-h : sy === 2 ? ay : ay-h/2
                    b = t+h
                }
            }
            root.pend = { l: l, t: t, r: r, b: b }
        }
        onReleased: {
            if (grip < 0 || !root.dragging) return
            const rect = root.pend
            grip = -1
            if (Math.max(Math.abs(rect.l-start.l)*root.width, Math.abs(rect.r-start.r)*root.width,
                         Math.abs(rect.t-start.t)*root.height, Math.abs(rect.b-start.b)*root.height) > 0.1)
                root.commit(rect)
            root.dragging = false
        }
        onCanceled: root.cancelGesture()
    }
}
