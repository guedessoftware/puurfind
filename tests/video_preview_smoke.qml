import QtQuick
import QtQuick.Window
import QtMultimedia

Window {
    visible: true
    width: 640
    height: 360

    property string videoPath: Qt.application.arguments.length > 1
                               ? Qt.application.arguments[Qt.application.arguments.length - 1]
                               : ""

    AudioOutput {
        id: audioOutput
        muted: true
    }

    MediaPlayer {
        id: player
        source: videoPath
        audioOutput: audioOutput
        videoOutput: videoOutput

        onPositionChanged: {
            if (position >= 200) {
                console.log("PURRFIND_VIDEO_OK", videoPath, duration, videoOutput.sourceRect)
                Qt.quit()
            }
        }
        onErrorOccurred: function(error, errorString) {
            console.error("PURRFIND_VIDEO_ERROR", error, errorString)
            Qt.exit(2)
        }
    }

    VideoOutput {
        id: videoOutput
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectFit
    }

    Timer {
        interval: 12000
        running: true
        onTriggered: {
            console.error("PURRFIND_VIDEO_TIMEOUT", player.mediaStatus, player.errorString)
            Qt.exit(3)
        }
    }

    Component.onCompleted: {
        if (!videoPath.length) {
            console.error("Usage: qml video_preview_smoke.qml -- /path/to/video")
            Qt.exit(1)
            return
        }
        player.play()
    }
}
