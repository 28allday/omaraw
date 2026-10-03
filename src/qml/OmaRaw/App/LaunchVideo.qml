import QtQuick
import QtMultimedia
import OmaRaw.Launch

LaunchVideoSurface {
    id: root
    objectName: "launchVideoOutput"
    property url source: "qrc:/launch/Spectrum-Loop.mp4"
    // Loading the media must not start playback before the window is ready.
    property bool playing: false
    readonly property bool failed: player.error !== MediaPlayer.NoError
    readonly property alias player: player
    MediaPlayer {
        id: player
        objectName: "launchVideoPlayer"
        source: root.source
        videoOutput: root
        loops: MediaPlayer.Infinite
        // No AudioOutput: the welcome screen is silent.
        onMediaStatusChanged: if (root.playing && mediaStatus === MediaPlayer.LoadedMedia) play()
    }
    onPlayingChanged: { if (playing) player.play(); else player.pause() }
    Component.onCompleted: if (playing) player.play()
    Component.onDestruction: player.stop()
}
