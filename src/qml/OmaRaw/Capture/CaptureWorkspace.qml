pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui
import OmaRaw.Library

// Capture: tethered shooting over libgphoto2. Left: session and cameras.
// Centre: live view or the last capture, the shutter. Right: the controls
// the camera actually exposes. Captures go into the session folder and
// straight into the catalog.
Item {
    id: root
    property var shell: null
    objectName: "captureWorkspace"

    FolderPicker {
        id: sessionDialog
        objectName: "sessionDialog"
        title: qsTr("Session folder — captures land here")
        onAccepted: capture.sessionFolder = selectedFolder.toString()
    }

    Column {
        anchors.fill: parent
        spacing: 0
        Item {
            width: parent.width
            height: parent.height - (root.shell.filmstripVisible ? strip.height : 0)

            // ── left: session + cameras ────────────────────────────────
            Rectangle {
                id: left
                anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: root.shell.sourceDockWidth
                color: Theme.panelBg
                Rectangle { anchors.right: parent.right; width: Theme.hairline; height: parent.height; color: Theme.border }
                C.ScrollView {
                    anchors.fill: parent; anchors.rightMargin: Theme.hairline
                    clip: true
                    C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
                    contentWidth: availableWidth
                    Column {
                        width: parent.width
                        spacing: 0
                        SectionHeader { title: qsTr("Session"); helpSection: "capture" }
                        InspectorRow { label: qsTr("Folder"); value: capture.sessionFolder; hideEmpty: false }
                        Row {
                            x: Theme.s3; spacing: Theme.s2
                            ToolButton { iconName: "folder-open"; text: qsTr("Choose…"); showLabel: true; tip: qsTr("The folder every captured frame is written into and catalogued from."); onClicked: sessionDialog.open() }
                        }
                        Item {
                            width: parent.width; height: Theme.hRow + Theme.s1
                            Text {
                                anchors.left: parent.left; anchors.leftMargin: Theme.s3; width: 72
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Name")
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                            }
                            SearchField {
                                anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 72
                                anchors.right: parent.right; anchors.rightMargin: Theme.s3
                                anchors.verticalCenter: parent.verticalCenter
                                placeholder: qsTr("Session name")
                                tip: qsTr("A name for this shoot; it fills {session} in the file name template.")
                                text: capture.sessionName
                                live: false
                                onAccepted: t => capture.sessionName = t.trim()
                            }
                        }
                        Item {
                            width: parent.width; height: Theme.hRow + Theme.s1
                            Text {
                                anchors.left: parent.left; anchors.leftMargin: Theme.s3; width: 72
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Template")
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                            }
                            SearchField {
                                anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 72
                                anchors.right: parent.right; anchors.rightMargin: Theme.s3
                                anchors.verticalCenter: parent.verticalCenter
                                placeholder: "{session}_{seq:4}"
                                tip: qsTr("How captured files are named: {session} is the session name, {seq:4} a four-digit counter, {date} and {time} the moment of capture.")
                                text: capture.nameTemplate
                                live: false
                                onAccepted: t => capture.nameTemplate = t.trim()
                            }
                        }
                        InspectorRow { label: qsTr("Next file"); value: capture.nextName; mono: true }
                        InspectorRow { label: qsTr("Shots"); value: String(capture.shotCount); mono: true }
                        Text {
                            x: Theme.s3; width: parent.width - Theme.s3 * 2; wrapMode: Text.WordWrap
                            text: qsTr("Tokens: {session} {seq} {seq:N} {date} {time} {model}. The camera's extension is kept; existing names get a counter.")
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                        }
                        Column {
                            x: Theme.s3; width: parent.width - Theme.s3 * 2
                            spacing: Theme.s1
                            Toggle { label: qsTr("Delete from card after verified download"); tip: qsTr("Frees the card once the frame has been read back and checked on disk."); checked: capture.deleteFromCamera; onClicked: capture.deleteFromCamera = !capture.deleteFromCamera }
                            Toggle { label: qsTr("Accept shots taken on the body"); tip: qsTr("Frames taken with the camera's own shutter are downloaded and catalogued too."); checked: capture.listenToBody; onClicked: capture.listenToBody = !capture.listenToBody }
                        }
                        Item { width: 1; height: Theme.s3 }
                        SectionHeader { title: qsTr("Cameras"); helpSection: "capture" }
                        Row {
                            x: Theme.s3; spacing: Theme.s2
                            ToolButton { iconName: "refresh-cw"; text: qsTr("Look again"); showLabel: true; tip: qsTr("Scans the USB ports for cameras."); enabled: capture.available && !capture.busy; onClicked: capture.refresh() }
                            ToolButton { visible: capture.connected; iconName: "unplug"; text: qsTr("Disconnect"); showLabel: true; tip: qsTr("Releases the camera so it can be unplugged or used by another program."); onClicked: capture.disconnectCamera() }
                        }
                        Repeater {
                            model: capture.cameras
                            SourceRow {
                                required property int index
                                required property var modelData
                                iconName: "camera"
                                name: modelData.model
                                count: -1
                                current: capture.connected && capture.cameraPort === modelData.port
                                onClicked: if (!capture.connected || capture.cameraPort !== modelData.port) capture.connectCamera(index)
                            }
                        }
                        Text {
                            visible: capture.cameras.length === 0
                            x: Theme.s3; width: parent.width - Theme.s3 * 2; wrapMode: Text.WordWrap
                            text: capture.available ? qsTr("No camera found. Connect one by USB, switch it on, set it to PC or PTP mode, and make sure no file manager has mounted it.")
                                                    : qsTr("Camera control is unavailable: %1").arg(capture.status)
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                        }
                        Item { width: 1; height: Theme.s3 }
                        SectionHeader { title: qsTr("Camera"); helpSection: "capture" ; visible: capture.connected }
                        InspectorRow { visible: capture.connected; label: qsTr("Model"); value: capture.cameraModel }
                        InspectorRow { visible: capture.connected; label: qsTr("Port"); value: capture.cameraPort; mono: true }
                        InspectorRow { visible: capture.connected; label: qsTr("Remote shutter"); value: capture.canCapture ? qsTr("yes") : qsTr("not offered") }
                        InspectorRow { visible: capture.connected; label: qsTr("Live view"); value: capture.canPreview ? qsTr("yes") : qsTr("not offered") }
                        Text {
                            visible: capture.connected && capture.summary !== ""
                            x: Theme.s3; width: parent.width - Theme.s3 * 2; wrapMode: Text.WordWrap
                            text: capture.summary
                            font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                        }
                    }
                }
            }

            // ── centre: live view / last capture + shutter ──────────────
            Rectangle {
                id: centre
                anchors.left: left.right; anchors.right: right.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                color: Theme.pasteboard
                Image {
                    id: live
                    anchors.fill: parent; anchors.margins: Theme.s3
                    anchors.bottomMargin: bar.height + Theme.s3 * 2
                    visible: capture.liveView && capture.liveSource !== ""
                    source: capture.liveSource
                    cache: false; asynchronous: false
                    fillMode: Image.PreserveAspectFit; smooth: true
                }
                LoupeView {
                    anchors.fill: parent
                    anchors.bottomMargin: bar.height + Theme.s3
                    visible: !live.visible && backend.currentId > 0
                }
                EmptyState {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - Theme.s5 * 2, 480)
                    visible: !live.visible && backend.currentId === 0
                    iconName: "camera"
                    title: capture.connected ? qsTr("Ready to shoot") : qsTr("No camera connected")
                    description: capture.connected ? qsTr("Press the shutter here or on the body; each frame lands in the session folder and the catalog.")
                                                   : qsTr("Pick a camera on the left. Captures show here as they arrive.")
                }
                Rectangle {
                    anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter
                    anchors.topMargin: Theme.s3
                    width: note.implicitWidth + Theme.s4; height: Theme.hControl + Theme.s1
                    radius: Theme.rControl; color: Theme.scrim
                    Text {
                        id: note
                        anchors.centerIn: parent
                        text: live.visible ? qsTr("Live view · %1 × %2 · %3 fps").arg(capture.liveWidth).arg(capture.liveHeight).arg(capture.liveFps.toFixed(1)) : capture.status
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
                        color: capture.connected ? Theme.textSecondary : Theme.warning
                    }
                }
                // shutter bar
                Rectangle {
                    id: bar
                    anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottomMargin: Theme.s3
                    width: barRow.implicitWidth + Theme.s4 * 2; height: 56
                    radius: Theme.rControl; color: Theme.scrim
                    Row {
                        id: barRow
                        anchors.centerIn: parent
                        spacing: Theme.s3
                        ToolButton {
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: capture.liveView ? "eye-off" : "eye"
                            text: capture.liveView ? qsTr("Stop live view") : qsTr("Live view")
                            showLabel: true
                            tip: qsTr("Streams the camera's viewfinder here; it drains the battery faster.")
                            enabled: capture.connected && capture.canPreview
                            onClicked: capture.liveView = !capture.liveView
                        }
                        Rectangle {
                            id: shutter
                            objectName: "shutterButton"
                            anchors.verticalCenter: parent.verticalCenter
                            width: 44; height: 44; radius: 22
                            readonly property bool can: capture.connected && capture.canCapture && !capture.busy
                            color: can ? (shutterTap.pressed ? Theme.pressedOn(Theme.accent) : sh.hovered ? Theme.hovered(Theme.accent) : Theme.accent) : Theme.controlBg
                            border.width: 3; border.color: can ? Theme.accentText : Theme.borderStrong
                            HoverHandler { id: sh }
                            TapHandler { id: shutterTap; enabled: shutter.can; onTapped: capture.capture() }
                            Tip { visible: sh.hovered; text: shutter.can ? qsTr("Capture (Space)") : capture.connected ? qsTr("This camera does not offer a remote shutter") : qsTr("Connect a camera first") }
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 1
                            Text { text: capture.connected ? capture.cameraModel : qsTr("No camera"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl; color: Theme.textPrimary }
                            Text { text: qsTr("%1 shot%2 · next %3").arg(capture.shotCount).arg(capture.shotCount === 1 ? "" : "s").arg(capture.nextName); font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted }
                        }
                    }
                }
            }

            // ── right: camera controls ─────────────────────────────────
            Rectangle {
                id: right
                anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: root.shell.inspectorWidth
                color: Theme.panelBg
                Rectangle { anchors.left: parent.left; width: Theme.hairline; height: parent.height; color: Theme.border }
                C.ScrollView {
                    anchors.fill: parent; anchors.leftMargin: Theme.hairline
                    clip: true
                    C.ScrollBar.vertical: ScrollBar {}
                    C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
                    contentWidth: availableWidth
                    Column {
                        width: parent.width
                        spacing: 0
                        InspectorGroup {
                            title: qsTr("Camera controls"); helpSection: "capture"
                            Text {
                                visible: !capture.connected || capture.controls.length === 0
                                x: Theme.s3; width: parent.width - Theme.s3 * 2; wrapMode: Text.WordWrap
                                text: capture.connected ? qsTr("This camera reports no settings.") : qsTr("Connect a camera to see the settings it exposes. Only what the camera offers is shown; read-only values are listed as is.")
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                            }
                            Row {
                                visible: capture.connected
                                x: Theme.s3; spacing: Theme.s2
                                ToolButton { iconName: "refresh-cw"; text: qsTr("Re-read from camera"); showLabel: true; tip: qsTr("Fetches the settings again after changing them on the body."); onClicked: capture.reloadControls() }
                            }
                            // The well-known exposure settings first, then the rest by section.
                            Repeater {
                                model: root.orderedControls
                                Item {
                                    id: ctl
                                    required property var modelData
                                    width: parent.width; height: Theme.hRow + Theme.s1
                                    Text {
                                        anchors.left: parent.left; anchors.leftMargin: Theme.s3; width: 104
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: ctl.modelData.label
                                        elide: Text.ElideRight
                                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                                    }
                                    ComboField {
                                        visible: ctl.modelData.type === 0 && !ctl.modelData.readOnly
                                        anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 104
                                        anchors.right: parent.right; anchors.rightMargin: Theme.s3
                                        anchors.verticalCenter: parent.verticalCenter
                                        model: ctl.modelData.choices || []
                                        tipTitle: ctl.modelData.label; tip: qsTr("Sets this on the camera straight away.")
                                        currentIndex: Math.max(0, (ctl.modelData.choices || []).indexOf(ctl.modelData.value))
                                        onActivated: i => capture.setControl(ctl.modelData.name, ctl.modelData.choices[i])
                                    }
                                    Toggle {
                                        visible: ctl.modelData.type === 1 && !ctl.modelData.readOnly
                                        anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 104
                                        anchors.verticalCenter: parent.verticalCenter
                                        label: ""
                                        text: ctl.modelData.label
                                        tip: qsTr("Switches this on the camera straight away.")
                                        checked: ctl.modelData.value === "1"
                                        onClicked: capture.setControl(ctl.modelData.name, ctl.modelData.value === "1" ? "0" : "1")
                                    }
                                    SearchField {
                                        visible: ctl.modelData.type === 2 && !ctl.modelData.readOnly
                                        anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 104
                                        anchors.right: parent.right; anchors.rightMargin: Theme.s3
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: ctl.modelData.value
                                        placeholder: ctl.modelData.label
                                        tip: qsTr("Type a value and press Return to set it on the camera.")
                                        live: false
                                        onAccepted: t => capture.setControl(ctl.modelData.name, t)
                                    }
                                    SliderField {
                                        visible: ctl.modelData.type === 3 && !ctl.modelData.readOnly
                                        anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 104
                                        anchors.right: parent.right; anchors.rightMargin: Theme.s3
                                        anchors.verticalCenter: parent.verticalCenter
                                        label: ""; labelWidth: 0
                                        tipTitle: ctl.modelData.label; tip: qsTr("Sets this on the camera as you drag.")
                                        from: ctl.modelData.lo || 0; to: ctl.modelData.hi || 1; stepSize: ctl.modelData.step || 0
                                        value: Number(ctl.modelData.value)
                                        onEditingFinished: v => capture.setControl(ctl.modelData.name, String(v))
                                    }
                                    Text {
                                        visible: ctl.modelData.readOnly || ctl.modelData.type === 4
                                        anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 104
                                        anchors.right: parent.right; anchors.rightMargin: Theme.s3
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: ctl.modelData.value
                                        elide: Text.ElideRight
                                        font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        Filmstrip {
            id: strip
            shell: root.shell
            visible: root.shell.filmstripVisible
            width: parent.width
            height: root.shell.filmstripHeight
        }
    }

    // Exposure first, then everything else the camera lists; status-only
    // sections (dates, serials) go to the end.
    readonly property var favourites: ["shutterspeed", "aperture", "f-number", "iso", "whitebalance", "exposurecompensation", "imageformat", "imagequality",
                                       "drivemode", "focusmode", "meteringmode", "capturetarget", "colorspace", "picturestyle", "aspectratio"]
    readonly property var orderedControls: {
        const all = capture.controls.slice()
        const rank = c => { const i = root.favourites.indexOf(c.name); if (i >= 0) return i; return 100 + (c.readOnly ? 1000 : 0) + (c.section === "Camera Status Information" || c.section === "Other PTP Device Properties" ? 500 : 0) }
        all.sort((a, b) => rank(a) - rank(b))
        return all
    }
    Shortcut { sequence: "Space"; enabled: root.visible && !root.shell.typing; onActivated: capture.capture() }
}
