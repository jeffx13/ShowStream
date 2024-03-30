import QtQuick 2.15
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.3
import "../components"
import MpvPlayer 1.0
Control {
    id: controlBar
    required property MpvObject mpv
    background: Rectangle {
        color: '#d0303030'
    }
    hoverEnabled: true


    signal sidebarButtonClicked()
    signal folderButtonClicked()
    signal seekRequested(int time)
    signal playPauseButtonClicked()
    signal settingsButtonClicked()
    signal volumeButtonClicked()
    signal serversButtonClicked()

    property bool isPlaying: false
    property int time: 0
    onTimeChanged:{
        if (!timeSlider.pressed)
            timeSlider.value = time;
    }
    property int duration: 0
    property alias volumeButton: volumeButton
    property int buttonSize : height * 0.5

    function toHHMMSS(seconds){
        var hours = Math.floor(seconds / 3600);
        seconds -= hours*3600;
        var minutes = Math.floor(seconds / 60);
        seconds -= minutes*60;

        hours = hours < 10 ? "0" + hours : hours;
        minutes = minutes < 10 ? "0" + minutes : minutes;
        seconds = seconds < 10 ? "0" + seconds : seconds;

        hours = hours === "00" ? "" : hours + ':';
        return hours + minutes + ':' + seconds;
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 1
        Slider {
            id: timeSlider
            from: 0
            to: duration
            focusPolicy: Qt.NoFocus
            hoverEnabled: true
            live: true
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 1
            onPressedChanged: {
                if (!pressed)  // released
                    seekRequested(value);
            }
            MouseArea{
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                cursorShape: Qt.PointingHandCursor
                propagateComposedEvents: true
            }

            background: Rectangle {
                id:backgroundRect
                x: timeSlider.leftPadding
                // y: timeSlider.availableHeight / 2 - height
                implicitHeight: timeSlider.hovered || timeSlider.pressed ? controlBar.height * 0.2 : controlBar.height * 0.1
                width: timeSlider.availableWidth
                height: implicitHeight
                color: "#828281" //grey

                Rectangle {
                    width: timeSlider.visualPosition * parent.width
                    height: parent.height
                    color: "#00AEEC" //turqoise
                }
            }

            handle: Rectangle {
                id: handle
                visible: timeSlider.hovered || timeSlider.pressed
                width: controlBar.height * 0.2
                height: controlBar.height * 0.2
                radius: width / 2
                color: timeSlider.pressed ? "#f0f0f0" : "#f6f6f6"
                border.color: "#bdbebf"

                x: timeSlider.leftPadding + timeSlider.visualPosition * (timeSlider.availableWidth - width)
                // y: timeSlider.availableHeight / 2 - height
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.fillHeight: true
            Layout.preferredHeight: 7
            ImageButton {
                id: playPauseButton
                source: isPlaying ? "qrc:/resources/images/pause.png" : "qrc:/resources/images/play.png"
                Layout.preferredWidth: buttonSize
                Layout.preferredHeight: buttonSize
                onClicked: playPauseButtonClicked()
            }
            ImageButton {
                id: volumeButton
                source: mpv.volume === 0 ? "qrc:/resources/images/mute_volume.png" :
                                           mpv.volume < 25 ? "qrc:/resources/images/low_volume.png" :
                                                             mpv.volume < 75 ? "qrc:/resources/images/mid_volume.png" : "qrc:/resources/images/high_volume.png"
                Layout.preferredWidth: buttonSize
                Layout.preferredHeight: buttonSize
                onClicked: volumeButtonClicked()
            }
            ImageButton {
                id: serversButton
                source: "qrc:/resources/images/servers.png"
                Layout.preferredWidth: buttonSize
                Layout.preferredHeight: buttonSize
                onClicked: serversButtonClicked()
            }

            Text {
                id: timeText
                text: `${toHHMMSS(time)} / ${toHHMMSS(duration)}`
                color: "white"
                font.pixelSize: buttonSize * 0.7

            }
            //spacer
            Item{
                Layout.fillWidth: true
            }

            ImageButton {
                id: pipButton
                source: "qrc:/resources/images/pip.png"
                // hoverImage: "qrc:/resources/images/pip_hover.png"
                Layout.preferredWidth: buttonSize
                Layout.preferredHeight: buttonSize
                onClicked: root.pipMode = true
            }
            ImageButton {
                id: explorerButton
                source: "qrc:/resources/images/folder.png"
                // hoverImage: "qrc:/resources/images/folder_hover.png"
                Layout.preferredWidth: buttonSize
                Layout.preferredHeight: buttonSize
                onClicked: folderButtonClicked()
            }

            ImageButton {
                id: settingsButton
                source: "qrc:/resources/images/player_settings.png"
                // hoverImage: "qrc:/resources/images/player_settings_hover.png"
                Layout.preferredWidth: buttonSize
                Layout.preferredHeight: buttonSize
                onClicked: settingsButtonClicked()
            }

            ImageButton {
                id: sidebarButton
                source: "qrc:/resources/images/playlist.png"
                // hoverImage: "qrc:/resources/images/playlist_hover.png"
                Layout.preferredWidth: buttonSize
                Layout.preferredHeight: buttonSize
                onClicked: sidebarButtonClicked()
            }

        }
    }



}
