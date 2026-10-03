import QtQuick

// Paint only the visible window onto the image. At native zoom the image
// can be 100 MP; a guide must not allocate or upload a bitmap that large.
Canvas {
    id: root
    property var points: []
    property real imageWidth: parent ? parent.width : 0
    property real imageHeight: parent ? parent.height : 0
    property rect viewport: Qt.rect(0, 0, imageWidth, imageHeight)
    property real shiftX: 0
    property real shiftY: 0
    property color penColor: "white"
    property real penWidth: 2
    property real penOpacity: 0.45
    property real centreWidth: 0
    property bool closed: false
    x: Math.floor(Math.max(0, viewport.x))
    y: Math.floor(Math.max(0, viewport.y))
    width: Math.max(0, Math.ceil(Math.min(imageWidth, viewport.x + viewport.width)) - x)
    height: Math.max(0, Math.ceil(Math.min(imageHeight, viewport.y + viewport.height)) - y)
    visible: points.length > 0 && width > 0 && height > 0
    onPointsChanged: requestPaint()
    onImageWidthChanged: requestPaint()
    onImageHeightChanged: requestPaint()
    onXChanged: requestPaint()
    onYChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onShiftXChanged: requestPaint()
    onShiftYChanged: requestPaint()
    onPenColorChanged: requestPaint()
    onPenWidthChanged: requestPaint()
    onPenOpacityChanged: requestPaint()
    onCentreWidthChanged: requestPaint()
    onClosedChanged: requestPaint()
    onVisibleChanged: requestPaint()
    onPaint: {
        const c = getContext("2d")
        c.reset()
        if (!points.length) return
        c.translate(shiftX-x, shiftY-y)
        c.strokeStyle = penColor; c.fillStyle = penColor
        c.lineWidth = penWidth; c.globalAlpha = penOpacity
        c.lineCap = "round"; c.lineJoin = "round"
        c.beginPath()
        if (points.length === 1) {
            // A click is a valid brush dab, and needs visible feedback too.
            c.arc(points[0].x*imageWidth, points[0].y*imageHeight, penWidth/2, 0, Math.PI*2)
            c.fill()
            return
        }
        c.moveTo(points[0].x*imageWidth, points[0].y*imageHeight)
        for (let i = 1; i < points.length; ++i) c.lineTo(points[i].x*imageWidth, points[i].y*imageHeight)
        if (closed) c.closePath()
        c.stroke()
        if (centreWidth > 0) { c.globalAlpha = 1; c.lineWidth = centreWidth; c.stroke() }
    }
}
