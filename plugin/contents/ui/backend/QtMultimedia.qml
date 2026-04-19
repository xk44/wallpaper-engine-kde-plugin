import QtQuick 2.5
import QtMultimedia
import ".."

Item{
    id: videoItem
    anchors.fill: parent
    property alias source: player.source
    property int displayMode: background.displayMode
    readonly property bool stats: background.mpvStats
    readonly property real renderScale: background.mpvRenderScale
    property var volumeFade: Common.createVolumeFade(
        videoItem,
        Qt.binding(function() { return background.mute ? 0 : background.volume; }),
        (volume) => { audioOut.volume = volume / 100.0; }
    )

    onDisplayModeChanged: {
        if(displayMode == Common.DisplayMode.Scale)
            videoView.fillMode = VideoOutput.Stretch;
        else if(displayMode == Common.DisplayMode.Aspect)
            videoView.fillMode = VideoOutput.PreserveAspectFit;
        else if(displayMode == Common.DisplayMode.Crop)
            videoView.fillMode = VideoOutput.PreserveAspectCrop;
    }

    // Force fillMode update on background.displayMode change
    Timer {
        id: displayModeFixTimer
        interval: 50
        repeat: false
        onTriggered: videoItem.displayModeChanged()
    }
    Connections {
        target: background
        function onDisplayModeChanged() {
            displayModeFixTimer.restart();
        }
    }

    VideoOutput {
        id: videoView
        anchors.fill: parent
        // Render scale: render at reduced resolution, upscale with bilinear
        layer.enabled: videoItem.renderScale < 1.0
        layer.textureSize: videoItem.renderScale < 1.0
            ? Qt.size(Math.max(1, videoView.width * videoItem.renderScale),
                      Math.max(1, videoView.height * videoItem.renderScale))
            : Qt.size(0, 0)
        layer.smooth: true
    }
    AudioOutput {
        id: audioOut
        volume: 0.0
        muted: background.mute
    }
    MediaPlayer {
        id: player
        loops: MediaPlayer.Infinite
        playbackRate: background.speed
        videoOutput: videoView
        audioOutput: audioOut
    }

    // Debug HUD — shown when stats is enabled
    Rectangle {
        id: debugHud
        visible: videoItem.stats
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 8
        width: debugText.implicitWidth + 16
        height: debugText.implicitHeight + 12
        color: "#CC000000"
        radius: 4

        property int tick: 0

        function formatInfo() {
            var status = "stopped";
            if (player.playbackState === MediaPlayer.PlayingState) status = "playing";
            else if (player.playbackState === MediaPlayer.PausedState) status = "paused";

            var pos = (player.position / 1000).toFixed(1);
            var dur = (player.duration / 1000).toFixed(1);
            var meta = player.metaData;
            var res = meta.value(MediaMetaData.Resolution);
            var resStr = res ? (res.width + "x" + res.height) : "?";
            var vcodec = meta.value(MediaMetaData.VideoCodec) || "?";

            var lines = "backend: QtMultimedia"
                + "\nstatus: " + status
                + "\nposition: " + pos + " / " + dur + " s"
                + "\nresolution: " + resStr
                + "\ncodec: " + vcodec
                + "\nscale: " + videoItem.renderScale.toFixed(2)
                + "\nspeed: " + player.playbackRate.toFixed(1) + "x";
            return lines;
        }

        Text {
            id: debugText
            anchors.centerIn: parent
            color: "#00FF00"
            font.family: "monospace"
            font.pixelSize: 12
            text: debugHud.tick >= 0 ? debugHud.formatInfo() : ""
        }

        Timer {
            running: debugHud.visible
            interval: 500
            repeat: true
            onTriggered: debugHud.tick++
        }
    }

    Component.onCompleted:{
        background.nowBackend = "QtMultimedia";
        videoItem.displayModeChanged();
    }

    function play(){
        pauseTimer.stop();
        player.play();
        volumeFade.start();
    }
    function pause(){
        volumeFade.stop();
        pauseTimer.start();
    }
    Timer{
        id: pauseTimer
        running: false
        repeat: false
        interval: 300
        onTriggered: {
            player.pause();
        }
    }
    function getMouseTarget() {
    }
}
