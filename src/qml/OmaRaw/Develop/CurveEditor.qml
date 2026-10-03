pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// The curve itself: a unit square, the engine's sampled curve, and its
// nodes. Drag a node to move it, click the curve to add one, right-click a
// node (or drag it well outside the square) to remove it. Two nodes always
// remain. While a drag is in flight the curve is previewed with a monotone
// spline in JS; the engine's own samples replace it when the render lands.
Item {
    id: root
    // {xs, ys, samples, type} of the channel being edited
    property var channel: ({})
    property color lineColor: Theme.accent
    property bool dimmed: false
    // The tone curves sit over the picture's histogram; a colour curve does not.
    property bool showHistogram: true
    // For a colour axis: what 0 and 1 stand for, drawn as a strip along the
    // bottom (input) and the left (output). Both empty for a tone curve.
    property color axisFrom: "transparent"
    property color axisTo: "transparent"
    // (xs, ys) after an edit; `final` is false while dragging.
    signal edited(var xs, var ys, bool final)

    property int selected: -1
    property int dragIndex: -1
    property var dragXs: []
    property var dragYs: []
    readonly property var xs: dragIndex >= 0 ? dragXs : (channel.xs || [0, 1])
    readonly property var ys: dragIndex >= 0 ? dragYs : (channel.ys || [0, 1])
    readonly property var samples: channel.samples || []
    readonly property int pad: 7
    readonly property int maxNodes: 20
    // Hover position in curve units, for the readout; -1 when outside.
    property real hoverX: -1
    property real hoverY: -1
    // The curve's input histogram, drawn faintly behind it: the picture as
    // this module receives it, asked for after every render while the
    // editor is on screen. The plain render's histogram stands in until the
    // first one arrives.
    readonly property var histogram: (engine.curveHistogram && engine.curveHistogram.length === 3) ? engine.curveHistogram : (engine.histogram || [])
    onHistogramChanged: root.repaint()
    function askHistogram() { if (visible && showHistogram) engine.loadCurveHistogram() }
    // A hidden editor (another section is open) paints nothing: it notes
    // that it is out of date and paints once when it is shown again.
    property bool stale: false
    function repaint() { if (visible) canvas.requestPaint(); else stale = true }
    function finishDrag() {
        if (dragIndex < 0) return
        root.edited(dragXs, dragYs, true)
        dragIndex = -1
    }
    onVisibleChanged: {
        if (!visible) finishDrag()
        if (visible && stale) { stale = false; canvas.requestPaint() }
        askHistogram()
    }
    Component.onCompleted: askHistogram()
    Connections {
        target: engine
        function onRenderedChanged() { root.askHistogram() }
        function onImageChanged() { root.dragIndex = -1 }
        function onEditStateReplaced() { root.dragIndex = -1 }
        function onHistoryJumped() { root.dragIndex = -1 }
    }
    // Canvas pixels retain their last paint. A fixed RGB curve does not
    // change lineColor when the interface palette changes, so repaint its
    // surrounding grid and background explicitly as those colours change.
    Connections {
        target: Theme
        function onControlBgChanged() { root.repaint() }
        function onPanelBgChanged() { root.repaint() }
        function onBorderChanged() { root.repaint() }
        function onTextMutedChanged() { root.repaint() }
    }

    implicitWidth: 240
    implicitHeight: width
    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Tone curve")
    Accessible.description: qsTr("%1 points. Click the curve to add a point and drag it; right-click a point to remove it.").arg(xs ? xs.length : 0)

    onXsChanged: root.repaint()
    onCurveTypeChanged: root.repaint()
    onYsChanged: root.repaint()
    onSamplesChanged: root.repaint()
    onSelectedChanged: root.repaint()
    onLineColorChanged: root.repaint()
    onDimmedChanged: root.repaint()
    onAxisFromChanged: root.repaint()
    onAxisToChanged: root.repaint()
    onWidthChanged: root.repaint()
    onHeightChanged: root.repaint()

    function clamp01(v) { return Math.min(1, Math.max(0, v)) }
    function toPx(x) { return pad + x * (width - 2 * pad) }
    function toPy(y) { return height - pad - y * (height - 2 * pad) }
    function fromPx(px) { return clamp01((px - pad) / (width - 2 * pad)) }
    function fromPy(py) { return clamp01((height - pad - py) / (height - 2 * pad)) }
    function nearest(px, py) {
        let best = -1, bestD = 10 * 10
        for (let i = 0; i < xs.length; ++i) {
            const dx = toPx(xs[i]) - px, dy = toPy(ys[i]) - py, d = dx * dx + dy * dy
            if (d < bestD) { bestD = d; best = i }
        }
        return best
    }
    // y on the drawn curve at x: the engine's samples, else the preview spline.
    function curveAt(x) {
        if (dragIndex < 0 && samples.length > 1) {
            const t = clamp01(x) * (samples.length - 1), k = Math.min(samples.length - 2, Math.floor(t)), f = t - k
            return samples[k] * (1 - f) + samples[k + 1] * f
        }
        return clamp01(spline(xs, ys, x))
    }
    // The engine's interpolation, so a drag previews the line the render
    // will draw: 2 monotone (Fritsch–Carlson), 1 Catmull-Rom, 0 natural cubic.
    readonly property int curveType: channel.type !== undefined ? channel.type : 2
    function spline(px, py, x) {
        const n = px.length
        if (n < 2) return x
        if (x <= px[0]) return py[0]
        if (x >= px[n - 1]) return py[n - 1]
        if (curveType === 0 && n > 2) return cubic(px, py, x)
        const d = [], m = []
        for (let i = 0; i < n - 1; ++i) d.push((py[i + 1] - py[i]) / Math.max(1e-6, px[i + 1] - px[i]))
        if (curveType === 1) {
            m.push(d[0])
            for (let i = 1; i < n - 1; ++i) m.push((py[i + 1] - py[i - 1]) / Math.max(1e-6, px[i + 1] - px[i - 1]))
            m.push(d[n - 2])
        } else {
            m.push(d[0])
            for (let i = 1; i < n - 1; ++i) m.push(d[i - 1] * d[i] <= 0 ? 0 : (d[i - 1] + d[i]) / 2)
            m.push(d[n - 2])
            for (let i = 0; i < n - 1; ++i) {
                if (d[i] === 0) { m[i] = 0; m[i + 1] = 0; continue }
                const a = m[i] / d[i], b = m[i + 1] / d[i], s = a * a + b * b
                if (s > 9) { const t = 3 / Math.sqrt(s); m[i] = t * a * d[i]; m[i + 1] = t * b * d[i] }
            }
        }
        let i = 0
        while (i < n - 2 && x > px[i + 1]) ++i
        const h = Math.max(1e-6, px[i + 1] - px[i]), t = (x - px[i]) / h, t2 = t * t, t3 = t2 * t
        return (2 * t3 - 3 * t2 + 1) * py[i] + (t3 - 2 * t2 + t) * h * m[i] + (-2 * t3 + 3 * t2) * py[i + 1] + (t3 - t2) * h * m[i + 1]
    }
    // Natural cubic spline (second derivative zero at both ends).
    function cubic(px, py, x) {
        const n = px.length, h = [], a = [], b = [], c = [], r = []
        for (let i = 0; i < n - 1; ++i) h.push(Math.max(1e-6, px[i + 1] - px[i]))
        // Tridiagonal system for the second derivatives of the inner points.
        for (let i = 1; i < n - 1; ++i) {
            a.push(h[i - 1] / 6); b.push((h[i - 1] + h[i]) / 3); c.push(h[i] / 6)
            r.push((py[i + 1] - py[i]) / h[i] - (py[i] - py[i - 1]) / h[i - 1])
        }
        const k = r.length
        for (let i = 1; i < k; ++i) { const w = a[i] / b[i - 1]; b[i] -= w * c[i - 1]; r[i] -= w * r[i - 1] }
        const pp = new Array(n).fill(0)
        for (let i = k - 1; i >= 0; --i) pp[i + 1] = (r[i] - (i + 1 < k ? c[i] * pp[i + 2] : 0)) / b[i]
        let i = 0
        while (i < n - 2 && x > px[i + 1]) ++i
        const t = x - px[i], hi = h[i]
        return py[i] + t * ((py[i + 1] - py[i]) / hi - (pp[i + 1] / 6 + pp[i] / 3) * hi)
             + t * t * (pp[i] / 2) + t * t * t * (pp[i + 1] - pp[i]) / (6 * hi)
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            const w = root.width, h = root.height
            ctx.reset()
            ctx.fillStyle = Theme.controlBg
            ctx.fillRect(0, 0, w, h)
            ctx.strokeStyle = Theme.border
            ctx.lineWidth = 1
            for (let q = 1; q < 4; ++q) {
                const gx = Math.round(root.toPx(q / 4)) + 0.5, gy = Math.round(root.toPy(q / 4)) + 0.5
                ctx.beginPath(); ctx.moveTo(gx, root.pad); ctx.lineTo(gx, h - root.pad); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(root.pad, gy); ctx.lineTo(w - root.pad, gy); ctx.stroke()
            }
            ctx.strokeRect(0.5, 0.5, w - 1, h - 1)
            // the histogram, as a faint mountain
            if (root.showHistogram && root.histogram.length === 3 && root.histogram[0].length > 1) {
                const n = root.histogram[0].length
                let peak = 1
                for (let i = 0; i < n; ++i) peak = Math.max(peak, (root.histogram[0][i] + root.histogram[1][i] + root.histogram[2][i]) / 3)
                ctx.fillStyle = Theme.textMuted
                ctx.globalAlpha = 0.16
                ctx.beginPath()
                ctx.moveTo(root.toPx(0), root.toPy(0))
                for (let i = 0; i < n; ++i) {
                    const v = (root.histogram[0][i] + root.histogram[1][i] + root.histogram[2][i]) / 3 / peak
                    ctx.lineTo(root.toPx(i / (n - 1)), root.toPy(Math.min(1, v * 0.9)))
                }
                ctx.lineTo(root.toPx(1), root.toPy(0))
                ctx.closePath()
                ctx.fill()
                ctx.globalAlpha = 1
            }
            if (root.axisFrom.a > 0 && root.axisTo.a > 0) {
                const across = ctx.createLinearGradient(root.toPx(0), 0, root.toPx(1), 0)
                across.addColorStop(0, root.axisFrom); across.addColorStop(0.5, Theme.textMuted); across.addColorStop(1, root.axisTo)
                ctx.fillStyle = across; ctx.fillRect(root.toPx(0), h - 4, root.toPx(1) - root.toPx(0), 3)
                const up = ctx.createLinearGradient(0, root.toPy(0), 0, root.toPy(1))
                up.addColorStop(0, root.axisFrom); up.addColorStop(0.5, Theme.textMuted); up.addColorStop(1, root.axisTo)
                ctx.fillStyle = up; ctx.fillRect(1, root.toPy(1), 3, root.toPy(0) - root.toPy(1))
            }
            // the identity, for reference
            ctx.strokeStyle = Theme.textMuted
            ctx.globalAlpha = 0.45
            ctx.setLineDash([3, 4])
            ctx.beginPath(); ctx.moveTo(root.toPx(0), root.toPy(0)); ctx.lineTo(root.toPx(1), root.toPy(1)); ctx.stroke()
            ctx.setLineDash([])
            ctx.globalAlpha = root.dimmed ? 0.5 : 1
            // the curve
            ctx.strokeStyle = root.lineColor
            ctx.lineWidth = 2
            ctx.lineJoin = "round"
            ctx.beginPath()
            const steps = 96
            for (let k = 0; k <= steps; ++k) {
                const x = k / steps, y = root.curveAt(x)
                if (k === 0) ctx.moveTo(root.toPx(x), root.toPy(y)); else ctx.lineTo(root.toPx(x), root.toPy(y))
            }
            ctx.stroke()
            // the nodes
            for (let i = 0; i < root.xs.length; ++i) {
                ctx.beginPath()
                ctx.arc(root.toPx(root.xs[i]), root.toPy(root.ys[i]), i === root.selected ? 5 : 4, 0, Math.PI * 2)
                ctx.fillStyle = i === root.selected ? root.lineColor : Theme.panelBg
                ctx.fill()
                ctx.lineWidth = 1.5
                ctx.stroke()
            }
            ctx.globalAlpha = 1
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        preventStealing: true
        cursorShape: root.dragIndex >= 0 ? Qt.ClosedHandCursor : Qt.CrossCursor
        onPressed: mouse => {
            const i = root.nearest(mouse.x, mouse.y)
            let xs = root.xs.slice(), ys = root.ys.slice()
            if (mouse.button === Qt.RightButton) {
                if (i >= 0 && xs.length > 2) { xs.splice(i, 1); ys.splice(i, 1); root.selected = -1; root.edited(xs, ys, true) }
                return
            }
            if (i >= 0) {
                root.dragIndex = i
            } else if (xs.length < root.maxNodes) {
                const x = root.fromPx(mouse.x), y = root.curveAt(x)
                let k = 0
                while (k < xs.length && xs[k] < x) ++k
                xs.splice(k, 0, x); ys.splice(k, 0, y)
                root.dragIndex = k
            } else return
            root.dragXs = xs; root.dragYs = ys
            root.selected = root.dragIndex
        }
        onPositionChanged: mouse => {
            root.hoverX = root.fromPx(mouse.x); root.hoverY = root.fromPy(mouse.y)
            if (root.dragIndex < 0) return
            let xs = root.dragXs.slice(), ys = root.dragYs.slice()
            const i = root.dragIndex
            const lo = i > 0 ? xs[i - 1] + 0.005 : 0, hi = i < xs.length - 1 ? xs[i + 1] - 0.005 : 1
            xs[i] = Math.min(hi, Math.max(lo, root.fromPx(mouse.x)))
            ys[i] = root.fromPy(mouse.y)
            root.dragXs = xs; root.dragYs = ys
            root.edited(xs, ys, false)
        }
        onReleased: mouse => {
            if (root.dragIndex < 0) return
            const i = root.dragIndex
            let xs = root.dragXs.slice(), ys = root.dragYs.slice()
            const out = mouse.y > root.height + 40 || mouse.y < -40 || mouse.x < -40 || mouse.x > root.width + 40
            if (out && xs.length > 2) { xs.splice(i, 1); ys.splice(i, 1); root.selected = -1 }
            // The engine's optimistic update lands synchronously inside
            // edited(), so the nodes never flash back before the drag ends.
            root.edited(xs, ys, true)
            root.dragIndex = -1
        }
        onCanceled: root.finishDrag()
        onExited: { root.hoverX = -1; root.hoverY = -1 }
    }
}
