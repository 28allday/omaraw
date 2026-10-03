pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

ModalPanel {
    id: root
    objectName: "imageMatchComparison"
    required property var match
    property bool showAfter: true
    property real centreX: .5
    property real centreY: .5
    readonly property real displayScale: backend.displayColour.windowScale || 1
    width: Math.min(1160, parent ? parent.width - 32 : 1160)
    height: Math.min(820, parent ? parent.height - 32 : 820)
    contentItem: ColumnLayout {
        spacing: Theme.s2
        Text { text: qsTr("Image Match"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; color: Theme.textPrimary }
        Flow {
            Layout.fillWidth: true; spacing: Theme.s2
            SegmentedControl {
                objectName: "imageMatchBeforeAfter"; labels: [qsTr("Before"), qsTr("After")]; currentIndex: root.showAfter ? 1 : 0
                onActivated: i => root.showAfter = i === 1
            }
            SegmentedControl {
                objectName: "imageMatchZoom"; labels: [qsTr("Fit"), qsTr("100%")]; currentIndex: root.match.detail ? 1 : 0
                enabled: root.match.hasPreview && !root.match.busy
                onActivated: i => { if (i === 0) root.match.showFit(); else root.match.detailAt(root.centreX, root.centreY) }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: Theme.s2
            Repeater {
                model: [true, false]
                ColumnLayout {
                    id: column
                    required property bool modelData
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 1; spacing: Theme.s1
                    Text {
                        text: column.modelData ? qsTr("Reference") : root.showAfter ? qsTr("Matched target") : qsTr("Target before match")
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
                    }
                    Rectangle {
                        id: viewport
                        Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 100; color: "#181818"; clip: true
                        Image {
                            id: picture
                            objectName: column.modelData ? "imageMatchReferenceComparison" : "imageMatchTargetComparison"
                            anchors.centerIn: parent
                            width: root.match.detail ? sourceSize.width / root.displayScale : parent.width
                            height: root.match.detail ? sourceSize.height / root.displayScale : parent.height
                            fillMode: Image.PreserveAspectFit; cache: false; asynchronous: true
                            smooth: !root.match.detail
                            source: column.modelData ? root.match.referenceSource : root.showAfter ? root.match.after : root.match.before
                        }
                        MouseArea {
                            anchors.fill: parent; enabled: !column.modelData && root.match.hasPreview && !root.match.busy
                            cursorShape: root.match.detail ? Qt.OpenHandCursor : Qt.CrossCursor
                            property real pressX: 0; property real pressY: 0
                            onPressed: mouse => { pressX = mouse.x; pressY = mouse.y }
                            onReleased: mouse => {
                                if (root.match.detail) {
                                    root.centreX = Math.max(0, Math.min(1, root.centreX - (mouse.x - pressX) * root.displayScale / root.match.previewArea.frameWidth))
                                    root.centreY = Math.max(0, Math.min(1, root.centreY - (mouse.y - pressY) * root.displayScale / root.match.previewArea.frameHeight))
                                } else {
                                    root.centreX = Math.max(0, Math.min(1, (mouse.x - (width - picture.paintedWidth)/2) / picture.paintedWidth))
                                    root.centreY = Math.max(0, Math.min(1, (mouse.y - (height - picture.paintedHeight)/2) / picture.paintedHeight))
                                }
                                root.match.detailAt(root.centreX, root.centreY)
                            }
                        }
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            text: root.match.detail ? qsTr("100%: one photo pixel per display pixel. Drag the target to inspect another area.") : qsTr("Click the target to inspect that area at 100%. Both previews use the same viewing and monitor transforms.")
            color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; maximumLineCount: 3; elide: Text.ElideRight
            text: root.match.busy ? root.match.status : (root.match.report.warnings || []).join(" ") || root.match.status
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        }
        Flow {
            Layout.fillWidth: true; spacing: Theme.s2
            ToolButton { objectName: "applyImageMatchComparison"; text: qsTr("Apply to photo"); showLabel: true; enabled: root.match.hasPreview && !root.match.busy && !engine.busy; onClicked: root.match.apply() }
            ToolButton { text: qsTr("Update preview"); showLabel: true; enabled: root.match.hasReference && !root.match.busy && !engine.busy; onClicked: root.match.preview() }
            ToolButton { text: qsTr("Reset match"); iconName: "rotate-ccw"; showLabel: true; enabled: !root.match.busy; onClicked: { root.match.reset(); root.close() } }
            ToolButton { text: qsTr("Close"); showLabel: true; onClicked: root.close() }
            ToolButton { text: qsTr("Cancel"); showLabel: true; visible: root.match.busy; onClicked: root.match.cancel() }
        }
    }
}
