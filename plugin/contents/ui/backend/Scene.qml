import QtQuick 2.5
import com.github.catsout.wallpaperEngineKde 1.2
import ".."

Item{
    id: sceneItem
    anchors.fill: parent
    property alias source: player.source
    property string assets: "assets"
    property int displayMode: background.displayMode
    property string userPropsJson: background.userPropsJson
    property var volumeFade: Common.createVolumeFade(
        sceneItem, 
        Qt.binding(function() { return background.mute ? 0 : background.volume; }),
        (volume) => { player.volume = volume / 100.0; }
    )

    onDisplayModeChanged: {
        if(displayMode == Common.DisplayMode.Scale)
            player.fillMode = SceneViewer.STRETCH;
        else if(displayMode == Common.DisplayMode.Aspect)
            player.fillMode = SceneViewer.ASPECTFIT;
        else if(displayMode == Common.DisplayMode.Crop)
            player.fillMode = SceneViewer.ASPECTCROP;
    }

    // Force fillMode update on background.displayMode change
    Timer {
        id: displayModeFixTimer
        interval: 50
        repeat: false
        onTriggered: sceneItem.displayModeChanged()
    }
    Connections {
        target: background
        function onDisplayModeChanged() {
            displayModeFixTimer.restart();
        }
    }

    readonly property bool stats: background.sceneStats

    SceneViewer {
        id: player
        anchors.fill: parent
        fps: background.fps
        muted: background.mute
        speed: background.speed
        renderScale: background.sceneRenderScale
        assets: sceneItem.assets
        userProperties: sceneItem.userPropsJson
        Component.onCompleted: {
            player.setAcceptMouse(true);
            player.setAcceptHover(true);
        }

        Connections {
            target: player
            function onFirstFrame() {
                background.sig_backendFirstFrame('scene');
            }
        }
    }

    // Debug HUD — shown when sceneStats is enabled
    Rectangle {
        id: debugHud
        visible: sceneItem.stats
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 8
        width: debugText.implicitWidth + 16
        height: debugText.implicitHeight + 12
        color: "#CC000000"
        radius: 4

        property int tick: 0

        function formatMetrics() {
            var m = player.debugMetrics();
            return "scene"
                + "\ntarget fps: " + m.targetFps
                + "\nframes: " + m.frameCount
                + "\nframe time: " + m.frameTimeMs.toFixed(2) + " ms"
                + "\nrunning: " + m.running;
        }

        Text {
            id: debugText
            anchors.centerIn: parent
            color: "#00FF00"
            font.family: "monospace"
            font.pixelSize: 12
            text: debugHud.tick >= 0 ? debugHud.formatMetrics() : ""
        }

        Timer {
            running: debugHud.visible
            interval: 500
            repeat: true
            onTriggered: debugHud.tick++
        }
    }

    Component.onCompleted: {
        background.nowBackend = 'scene';
        sceneItem.displayModeChanged();
    }
    function play() {
        volumeFade.start();
        player.play();
    }
    function pause() {
        volumeFade.stop();
        player.pause();
    }
    
    function getMouseTarget() {
        return Qt.binding(function() { return player; })
    }
}
