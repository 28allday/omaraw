import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui
import OmaRaw.Launch

C.ApplicationWindow {
    id: root
    objectName: "launchWindow"
    property string version: ""
    property var quote: ({})
    property bool reduceMotion: false
    property bool confirmed: false
    property bool firstFramePresented: false
    property bool playbackReady: false
    property url videoComponent: "LaunchVideo.qml"
    readonly property color surface: "#10171c"
    readonly property color ink: "#e2e8eb"
    readonly property color muted: "#929fa8"
    readonly property color accent: "#87c8df"
    readonly property string launchFamily: launchFont.name
    readonly property real layoutScale: Math.max(.25, Math.min(1,
        (Screen.desktopAvailableWidth - 32) / 560, (Screen.desktopAvailableHeight - 32) / 380))
    readonly property int compactWidth: Math.floor(560 * layoutScale)
    readonly property int compactHeight: Math.floor(380 * layoutScale)
    readonly property string displayVersion: version.indexOf("-dev-") >= 0
        ? version.substring(0, version.indexOf("-dev-") + 4) : version
    visible: true
    title: qsTr("Welcome to OmaRAW")
    flags: Qt.Dialog | Qt.FramelessWindowHint
    width: compactWidth; height: compactHeight
    minimumWidth: compactWidth; maximumWidth: compactWidth
    minimumHeight: compactHeight; maximumHeight: compactHeight
    color: surface
    font.family: launchFamily
    FontLoader { id: launchFont; source: "qrc:/launch/fonts/JetBrainsMono-Regular.ttf" }

    // Keep the opening card still until the compositor presents its first frame.
    onFrameSwapped: {
        if (visible && !firstFramePresented) {
            firstFramePresented = true
            if (!reduceMotion) playbackDelay.start()
        }
    }
    onVisibleChanged: {
        if (!visible) {
            playbackDelay.stop()
            playbackReady = false
            firstFramePresented = false
        }
    }
    Timer {
        id: playbackDelay
        interval: 750
        onTriggered: root.playbackReady = root.visible && !root.reduceMotion
    }
    function enter() { confirmed = true; close() }
    Shortcut { sequence: "Return"; enabled: !buildDetails.opened; onActivated: root.enter() }
    Shortcut { sequence: "Enter"; enabled: !buildDetails.opened; onActivated: root.enter() }
    Shortcut { sequence: "Escape"; enabled: !buildDetails.opened; onActivated: root.close() }

    // Keep the compact layout together on small screens. An opaque window
    // avoids transparent corner cutouts inside the compositor's square frame.
    Item {
        id: card
        width: 560; height: 380
        scale: root.layoutScale; transformOrigin: Item.TopLeft
        MouseArea {
            width: parent.width; height: 130
            onPressed: root.startSystemMove()
        }
        Rectangle {
            x: 32; y: 38; width: 44; height: 44; color: "#172630"
            Icon { anchors.centerIn: parent; name: "aperture"; size: 42; color: root.accent }
        }
        Text {
            objectName: "launchTitle"
            x: 96; y: 34; width: 220; height: 52
            text: "OmaRAW"; color: root.ink
            font.family: root.launchFamily; font.pixelSize: 34; font.weight: Font.Normal
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            objectName: "launchVersion"
            x: 96; y: 98; width: 220
            text: root.displayVersion; color: root.muted
            font.family: root.launchFamily; font.pixelSize: 12
            elide: Text.ElideMiddle
        }
        Item {
            id: film
            objectName: "launchSpectrum"
            x: 334; y: 34
            width: 194; height: 90
            clip: true
            readonly property rect artworkBounds: Qt.rect(96, 64, 1728, 752)
            readonly property real artworkScale: Math.min(width / artworkBounds.width, height / artworkBounds.height)
            Item {
                x: (film.width - film.artworkBounds.width * film.artworkScale) / 2 - film.artworkBounds.x * film.artworkScale
                y: (film.height - film.artworkBounds.height * film.artworkScale) / 2 - film.artworkBounds.y * film.artworkScale
                width: 1920 * film.artworkScale; height: 1080 * film.artworkScale
                LaunchVideoSurface {
                    objectName: "launchPoster"
                    anchors.fill: parent
                    posterSource: "qrc:/launch/Spectrum-Poster.jpg"
                    backgroundColor: root.surface
                }
                Loader {
                    id: video
                    objectName: "launchVideoLoader"
                    anchors.fill: parent
                    active: root.visible && !root.reduceMotion
                    source: root.videoComponent
                    visible: item && !item.failed
                    onLoaded: {
                        item.backgroundColor = Qt.binding(() => root.surface)
                        item.posterSource = "qrc:/launch/Spectrum-Poster.jpg"
                        item.playing = Qt.binding(() => root.playbackReady && root.visible)
                    }
                }
            }
        }
        Column {
            id: quotation
            x: 32; y: 154; width: 496; spacing: 10
            Text {
                objectName: "launchQuote"
                width: parent.width; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                readonly property string wording: root.quote.text || ""
                text: "“" + (wording.length < 65 ? wording.replace(", ", ",\n") : wording) + "”"
                font.family: root.launchFamily; font.pixelSize: 18
                lineHeight: 1.25; color: root.ink
            }
            C.AbstractButton {
                id: author
                objectName: "launchAttribution"
                width: attribution.implicitWidth; height: 22
                text: root.quote.author || ""
                hoverEnabled: true; focusPolicy: Qt.TabFocus
                Accessible.name: qsTr("Quote by %1. Open source.").arg(text)
                contentItem: Text {
                    id: attribution
                    text: "— " + (root.quote.author || "")
                    font.family: root.launchFamily; font.pixelSize: 12
                    color: author.hovered || author.visualFocus ? root.accent : root.muted
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: Qt.openUrlExternally(root.quote.source)
                C.ToolTip { text: qsTr("Quote source"); visible: author.hovered; delay: 700; font.family: root.launchFamily }
            }
        }
        Item {
            id: footer
            objectName: "launchFooter"
            x: 32; y: 316; width: 496; height: 44
            Text {
                objectName: "launchCopyright"
                x: 0; y: 0
                text: qsTr("© 2026 Gavin Nugent")
                color: root.muted; font.family: root.launchFamily; font.pixelSize: 12
            }
            C.AbstractButton {
                id: detailsButton
                objectName: "launchBuildDetails"
                x: 0; y: 24; width: 156; height: 24
                hoverEnabled: true; focusPolicy: Qt.TabFocus
                Accessible.name: qsTr("Build and licence details")
                onClicked: buildDetails.open()
                contentItem: Row {
                    spacing: 8
                    Text {
                        text: qsTr("Build & licence")
                        color: detailsButton.hovered || detailsButton.visualFocus ? root.ink : root.muted
                        font.family: root.launchFamily; font.pixelSize: 12
                    }
                    Text { text: "+"; color: root.accent; font.family: root.launchFamily; font.pixelSize: 12 }
                }
            }
            C.Button {
                id: okay
                objectName: "launchOkay"
                x: 352; y: 0; width: 144; height: 44
                text: qsTr("Continue"); focus: true
                onClicked: root.enter()
                contentItem: Item {
                  Row {
                    anchors.centerIn: parent
                    spacing: 12
                    Text {
                        text: okay.text
                        anchors.verticalCenter: parent.verticalCenter
                        color: "#10232d"; font.family: root.launchFamily; font.pixelSize: 15
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "→"; font.family: root.launchFamily; font.pixelSize: 16; color: "#10232d"
                    }
                  }
                }
                background: Rectangle {
                    color: okay.down ? "#6fb0c7" : okay.hovered ? "#9dd8eb" : root.accent
                    border.width: okay.visualFocus ? 2 : 0; border.color: root.ink
                }
            }
        }
    }
    C.Popup {
        id: buildDetails
        objectName: "launchBuildPopup"
        parent: C.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(root.width - 32 * root.layoutScale, 480 * root.layoutScale)
        padding: 20 * root.layoutScale
        modal: true; focus: true
        closePolicy: C.Popup.CloseOnEscape | C.Popup.CloseOnPressOutside
        background: Rectangle { color: root.surface; border.width: 1; border.color: "#475967" }
        contentItem: Column {
            spacing: 14 * root.layoutScale
            Text {
                objectName: "launchFullVersion"
                width: parent.width; wrapMode: Text.WrapAnywhere
                text: "OmaRAW " + root.version; color: root.ink
                font.family: root.launchFamily; font.pixelSize: 14 * root.layoutScale
            }
            Text {
                objectName: "launchLicence"
                width: parent.width; wrapMode: Text.WordWrap
                text: qsTr("Copyright © 2026 Gavin Nugent.\n\nGNU GPL v3 or later · Free software\nProvided without warranty.\n\nFull licence and component notices: Help → About OmaRAW.\nJetBrains Mono: SIL Open Font License 1.1.")
                color: root.muted; font.family: root.launchFamily; font.pixelSize: 12 * root.layoutScale
                lineHeight: 1.25
            }
            C.Button {
                id: closeDetails
                objectName: "launchCloseDetails"
                text: qsTr("Close"); width: 110 * root.layoutScale; height: 40 * root.layoutScale
                onClicked: buildDetails.close()
                contentItem: Text {
                    text: closeDetails.text; color: "#10232d"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    font.family: root.launchFamily; font.pixelSize: 15 * root.layoutScale
                }
                background: Rectangle { color: closeDetails.down ? "#6fb0c7" : root.accent }
            }
        }
    }
}
