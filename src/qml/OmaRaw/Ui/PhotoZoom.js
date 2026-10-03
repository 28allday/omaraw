.pragma library

function steps(event) {
    // Trackpads can send pixels without any wheel angle. Ignore horizontal
    // scrolling and the zero-delta events which begin/end a touch gesture.
    return event.pixelDelta.y !== 0 ? event.pixelDelta.y / 40 : event.angleDelta.y / 120
}

function nextScale(current, steps, minimum, maximum, fit) {
    const next = Math.max(minimum, Math.min(maximum, current * Math.pow(1.2, Math.max(-8, Math.min(8, steps)))))
    // Stop at Fit when crossing it, so the whole picture is easy to recover.
    return (current < fit && next > fit) || (current > fit && next < fit) ? fit : next
}

function anchor(view, picture, point) {
    const local = view.mapToItem(picture, point.x, point.y)
    return Qt.point(local.x / picture.width, local.y / picture.height)
}

function restore(view, picture, point, anchor) {
    view.cancelFlick()
    view.contentX = Math.max(0, Math.min(view.contentWidth - view.width,
        picture.x + anchor.x * picture.width * picture.scale - point.x))
    view.contentY = Math.max(0, Math.min(view.contentHeight - view.height,
        picture.y + anchor.y * picture.height * picture.scale - point.y))
}
