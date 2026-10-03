import QtQuick
import OmaRaw.Ui

// RGB histogram of the current preview: 64 bins per channel, drawn as
// three translucent fills over the panel ground. Bins come from the
// backend (computed from the cached preview, not the RAW).
Rectangle {
    id: root
    property var bins: [] // [r[64], g[64], b[64]]
    // What the bins were measured on, for the label and the screen reader.
    property string source: qsTr("the embedded preview")
    color: Theme.windowBg
    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("RGB histogram")
    Accessible.description: {
        if (!bins || bins.length !== 3 || bins[0].length === 0) return qsTr("No histogram")
        const n = bins[0].length; let sum = 0, weight = 0
        for (let i = 0; i < n; ++i) { const v = (bins[0][i] + bins[1][i] + bins[2][i]) / 3; sum += v; weight += v * (i + 0.5) / n }
        const mean = sum > 0 ? weight / sum : 0
        return qsTr("Measured on %1; tones centre at %2 percent of the range").arg(source).arg(Math.round(mean * 100))
    }
    border.width: Theme.hairline
    border.color: Theme.border
    implicitHeight: 96
    onBinsChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    Canvas {
        id: canvas
        anchors.fill: parent
        anchors.margins: 1
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (!root.bins || root.bins.length < 3) return
            let peak = 1
            for (let c = 0; c < 3; ++c) for (let i = 0; i < root.bins[c].length; ++i) peak = Math.max(peak, root.bins[c][i])
            const colors = [Theme.histRed, Theme.histGreen, Theme.histBlue]
            for (let c = 0; c < 3; ++c) {
                const b = root.bins[c]
                const n = b.length
                ctx.beginPath()
                ctx.moveTo(0, height)
                for (let i = 0; i < n; ++i) {
                    const x = i / (n - 1) * width
                    const y = height - Math.log(1 + b[i]) / Math.log(1 + peak) * height
                    ctx.lineTo(x, y)
                }
                ctx.lineTo(width, height)
                ctx.closePath()
                ctx.fillStyle = colors[c]
                ctx.fill()
            }
        }
    }
    Text {
        anchors.centerIn: parent
        visible: !root.bins || root.bins.length < 3
        text: qsTr("No preview")
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
    }
}
