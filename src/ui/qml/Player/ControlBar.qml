pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../Components"
import AoNami
import ".."

Item {
    id: controlBar

    readonly property bool hovered: hoverHandler.hovered || sliderHovered || volPopup.visible
    readonly property bool sliderHovered: timeSlider.hovered || timeSlider.pressed

    required property MpvPlayer player

    readonly property bool isPlaying: player.state === MpvPlayer.Playing
    readonly property int time: player.time
    readonly property int duration: player.duration
    readonly property int volume: player.volume

    signal playlistRequested()
    signal panelRequested(string tab)
    signal openFileRequested()

    onTimeChanged: if (!timeSlider.pressed) timeSlider.value = time

    function toHHMMSS(seconds) {
        let h = Math.floor(seconds / 3600)
        let m = Math.floor((seconds % 3600) / 60)
        let s = seconds % 60
        let pad = (n) => n < 10 ? "0" + n : "" + n
        return (h > 0 ? pad(h) + ":" : "") + pad(m) + ":" + pad(s)
    }

    readonly property int btnSize: 36

    component CtrlBtn: Item {
        id: cb
        property string icon: ""
        property string tip: ""
        property int iconSize: 22
        signal clicked()
        Layout.preferredWidth: controlBar.btnSize
        Layout.preferredHeight: controlBar.btnSize
        AppIcon {
            anchors.centerIn: parent
            name: cb.icon
            size: cb.iconSize
            color: cbHover.hovered ? Theme.onOverlay : Theme.onOverlayMuted
            Behavior on color { ColorAnimation { duration: 120 } }
            scale: cbArea.pressed ? 0.86 : (cbHover.hovered ? 1.12 : 1.0)
            Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutBack } }
        }
        HoverHandler { id: cbHover }
        MouseArea { id: cbArea; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: cb.clicked() }
        AppToolTip { text: cb.tip; visible: cb.tip !== "" && cbHover.hovered }
    }

    component Divider: Rectangle {
        Layout.preferredWidth: 1; Layout.preferredHeight: 18
        Layout.leftMargin: 5; Layout.rightMargin: 5
        color: Theme.overlayFillHover
    }

    Timer {
        id: volCloseTimer
        interval: 120
        onTriggered: if (!volBtnHover.hovered && !volPopupHover.hovered) volPopup.close()
    }

    Popup {
        id: volPopup
        parent: Overlay.overlay
        width: 68; height: 214
        padding: 0; margins: 0
        modal: false
        closePolicy: Popup.NoAutoClose

        property bool _up: false
        onAboutToShow: {
            _up = false
            var ov = Overlay.overlay
            if (ov && volumeArea.visible) {
                var p = volumeArea.mapToItem(ov, 0, 0)
                x = p.x + (volumeArea.width - width) / 2
                y = p.y - height - 10
            }
            Qt.callLater(() => { _up = true })
        }
        onAboutToHide: _up = false

        background: null

        HoverHandler {
            id: volPopupHover
            onHoveredChanged: {
                if (hovered) volCloseTimer.stop()
                else if (!volBtnHover.hovered) volCloseTimer.restart()
            }
        }

        enter: Transition { NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 200; easing.type: Easing.OutCubic } }
        exit:  Transition { NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: 170; easing.type: Easing.InCubic } }

        Item {
            id: popupBody
            anchors.fill: parent
            transformOrigin: Item.Bottom
            scale: volPopup._up ? 1.0 : 0.72
            Behavior on scale { NumberAnimation { duration: 290; easing.type: Easing.OutBack; easing.overshoot: 1.45 } }

            readonly property real over: Math.max(0, controlBar.volume - 100) / 100.0
            readonly property color cTop:    Qt.rgba(0.545 + over*0.420, 0.471 + over*0.153, 0.961 - over*0.918, 1.0)
            readonly property color cBottom: Qt.rgba(0.306 + over*0.631, 0.357 + over*0.267, 0.949 - over*0.906, 1.0)

            Rectangle {
                anchors.centerIn: parent
                width: parent.width + 22; height: parent.height + 22; radius: width / 2
                color: "transparent"
                border.color: Qt.rgba(popupBody.cBottom.r, popupBody.cBottom.g, popupBody.cBottom.b, 0.18)
                border.width: 11
            }

            Rectangle { anchors.fill: parent; radius: 18; color: Theme.overlayScrim }
            Rectangle { anchors { fill: parent; margins: 1 }
                radius: 17; color: "transparent"; border.color: Theme.overlayFillActive; border.width: 1 }
            Rectangle { anchors { fill: parent; margins: 2 }
                radius: 16; color: "transparent"; border.color: Theme.overlayLine; border.width: 1 }

            Rectangle {
                anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 2 }
                width: parent.width * 0.50; height: 2; radius: 1; color: Theme.overlayFillActive
            }

            Text {
                id: pctLabel
                anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 10 }
                text: controlBar.volume + "%"
                color: Theme.onOverlay
                font { pixelSize: Globals.sp(16); family: "monospace"; bold: true }
            }

            Slider {
                id: volSlider
                orientation: Qt.Vertical
                from: 0; to: 200; stepSize: 1
                value: controlBar.volume
                focusPolicy: Qt.NoFocus
                live: true
                anchors {
                    horizontalCenter: parent.horizontalCenter
                    top: pctLabel.bottom; bottom: parent.bottom
                    topMargin: 8; bottomMargin: 14
                }
                width: 40
                onMoved: controlBar.player.volume = value

                background: Item {
                    x: volSlider.leftPadding + (volSlider.availableWidth - width) / 2
                    y: volSlider.topPadding
                    implicitWidth: 6; implicitHeight: 130
                    width: 6; height: volSlider.availableHeight

                    Rectangle { anchors.fill: parent; radius: 3; color: Theme.overlayFillActive }

                    Rectangle {
                        id: volFill
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: (1.0 - volSlider.visualPosition) * parent.height
                        radius: 3
                        gradient: Gradient {
                            orientation: Gradient.Vertical
                            GradientStop { position: 0.0; color: popupBody.cTop;    Behavior on color { ColorAnimation { duration: 350 } } }
                            GradientStop { position: 1.0; color: popupBody.cBottom; Behavior on color { ColorAnimation { duration: 350 } } }
                        }

                        Rectangle {
                            visible: volFill.height > 4
                            anchors { top: parent.top; horizontalCenter: parent.horizontalCenter }
                            width: parent.width + 10; height: 10; radius: 5
                            color: Qt.rgba(popupBody.cTop.r, popupBody.cTop.g, popupBody.cTop.b, 0.65)
                            SequentialAnimation on opacity {
                                running: volPopup.visible
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.2; duration: 950; easing.type: Easing.InOutSine }
                                NumberAnimation { to: 1.0; duration: 950; easing.type: Easing.InOutSine }
                            }
                        }
                    }

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: parent.height * 0.5
                        width: parent.width + 10; height: 1
                        color: Theme.overlayFillActive
                    }
                }

                handle: Rectangle {
                    x: volSlider.leftPadding + (volSlider.availableWidth - width) / 2
                    y: volSlider.topPadding + volSlider.visualPosition * (volSlider.availableHeight - height)
                    width: 18; height: 18; radius: 9
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Theme.onOverlay }
                        GradientStop { position: 1.0; color: Theme.onOverlayMuted }
                    }
                    border.color: popupBody.cBottom; border.width: 2
                    Behavior on border.color { ColorAnimation { duration: 300 } }

                    scale: volSlider.pressed ? 1.35 : (volSlider.hovered ? 1.15 : 1.0)
                    Behavior on scale { NumberAnimation { duration: 130; easing.type: Easing.OutBack } }

                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width + 10; height: parent.height + 10; radius: height / 2
                        color: "transparent"
                        border.color: Qt.rgba(popupBody.cBottom.r, popupBody.cBottom.g, popupBody.cBottom.b, 0.45)
                        border.width: 2
                        visible: volSlider.pressed || volSlider.hovered
                        Behavior on border.color { ColorAnimation { duration: 300 } }
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent

        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.15; color: Theme.overlayScrimSoft }
            GradientStop { position: 0.5; color: Theme.overlayScrimMid }
            GradientStop { position: 1.0; color: Theme.overlayScrim }
        }

        Rectangle {
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: 1
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.3; color: Theme.overlayFill }
                GradientStop { position: 0.7; color: Theme.overlayFill }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        HoverHandler {
            id: hoverHandler
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        }

        Slider {
            id: timeSlider
            from: 0
            to: controlBar.duration
            focusPolicy: Qt.NoFocus
            hoverEnabled: true
            live: true
            enabled: !controlBar.player.isLoading
            z: 1
            anchors {
                left: parent.left; right: parent.right
                bottom: buttonRow.top
                leftMargin: 10; rightMargin: 10
            }
            height: 28
            onPressedChanged: if (!pressed) controlBar.player.seek(value)

            MouseArea {
                id: seekHoverArea
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
            }

            background: Item {
                x: timeSlider.leftPadding
                width: timeSlider.availableWidth
                anchors.verticalCenter: parent.verticalCenter

                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width
                    height: controlBar.sliderHovered ? 9 : 5
                    radius: height / 2
                    color: Theme.overlayFillActive
                    Behavior on height { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

                    Rectangle {
                        width: timeSlider.visualPosition * parent.width
                        height: parent.height
                        radius: height / 2
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: Theme.accentLight }
                            GradientStop { position: 1.0; color: Theme.accent }
                        }
                    }
                }
            }

            handle: Rectangle {
                width: controlBar.sliderHovered ? 16 : 12
                height: width; radius: width / 2
                x: timeSlider.leftPadding + timeSlider.visualPosition * (timeSlider.availableWidth - width)
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.onOverlay
                Behavior on width { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }
                scale: timeSlider.pressed ? 1.25 : 1.0
                Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutBack } }

                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width * 0.42; height: width; radius: width / 2
                    color: Theme.accent
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width + 10; height: width; radius: width / 2
                    color: "transparent"; border.color: Qt.alpha(Theme.accent, 0.5); border.width: 2
                    visible: controlBar.sliderHovered
                }
            }

            Rectangle {
                visible: controlBar.sliderHovered
                z: 5
                height: 26
                width: seekTipText.implicitWidth + 16
                radius: 6
                color: Theme.overlayScrim
                border.color: Theme.overlayLine
                border.width: 1
                y: -34
                x: Math.max(0, Math.min(timeSlider.width - width, seekHoverArea.mouseX - width / 2))
                Text {
                    id: seekTipText
                    anchors.centerIn: parent
                    text: controlBar.toHHMMSS(Math.max(0, Math.round((seekHoverArea.mouseX / Math.max(1, timeSlider.width)) * controlBar.duration)))
                    color: Theme.onOverlay
                    font.pixelSize: Globals.sp(16)
                    font.weight: Font.Medium
                }
            }
        }

        RowLayout {
            id: buttonRow
            anchors {
                left: parent.left; right: parent.right; bottom: parent.bottom
                leftMargin: 10; rightMargin: 10; bottomMargin: 6
            }
            height: controlBar.btnSize
            spacing: 2

            CtrlBtn {
                icon: "skip-back"
                tip: qsTr("Previous episode")
                onClicked: App.playlist.stepItem(-1)
            }
            CtrlBtn {
                icon: controlBar.isPlaying ? "pause" : "play"
                tip: controlBar.isPlaying ? qsTr("Pause") : qsTr("Play")
                onClicked: controlBar.player.togglePlayPause()
            }
            CtrlBtn {
                icon: "skip-forward"
                tip: qsTr("Next episode")
                onClicked: App.playlist.stepItem(1)
            }
            CtrlBtn {
                icon: "square"
                tip: qsTr("Stop")
                iconSize: 19
                onClicked: controlBar.player.stop()
            }

            Divider {}

            Item {
                id: volumeArea
                Layout.preferredWidth: controlBar.btnSize
                Layout.preferredHeight: controlBar.btnSize

                HoverHandler {
                    id: volBtnHover
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onHoveredChanged: {
                        if (hovered) {
                            volCloseTimer.stop()
                            if (!volPopup.visible) volPopup.open()
                        } else if (!volPopupHover.hovered) {
                            volCloseTimer.restart()
                        }
                    }
                }

                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    // MpvPlayer clamps to 0..200.
                    onWheel: (event) => controlBar.player.volume += (event.angleDelta.y > 0 ? 5 : -5)
                }

                AppIcon {
                    anchors.centerIn: parent
                    size: 22
                    name: controlBar.volume === 0 ? "volume-x"
                        : controlBar.volume < 50  ? "volume-1" : "volume-2"
                    color: volBtnHover.hovered ? Theme.onOverlay : Theme.onOverlayMuted
                    Behavior on color { ColorAnimation { duration: 120 } }
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: controlBar.player.muted = !controlBar.player.muted }
            }

            Rectangle {
                Layout.preferredHeight: 28
                Layout.preferredWidth: timeText.implicitWidth + 16
                radius: 14
                color: Theme.overlayFill; border.color: Theme.overlayLine; border.width: 1
                Text {
                    id: timeText
                    anchors.centerIn: parent
                    text: controlBar.toHHMMSS(controlBar.time) + " / " + controlBar.toHHMMSS(controlBar.duration)
                    color: Theme.onOverlay
                    font { pixelSize: Globals.sp(20); family: "monospace" }
                }
            }

            Item { Layout.fillWidth: true }

            Row {
                spacing: 8
                visible: App.playlist.isLoading || controlBar.player.isLoading
                Layout.alignment: Qt.AlignHCenter
                AppSpinner { width: 24; height: 24; running: parent.visible; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Loading..."; color: Theme.onOverlayDim; font.pixelSize: Globals.sp(20); anchors.verticalCenter: parent.verticalCenter }
            }

            Item { Layout.fillWidth: true }

            CtrlBtn {
                icon: controlBar.player.subVisible ? "captions" : "captions-off"
                tip: controlBar.player.subVisible ? qsTr("Hide subtitles") : qsTr("Show subtitles")
                onClicked: controlBar.player.subVisible = !controlBar.player.subVisible
            }
            CtrlBtn {
                icon: "server"
                tip: qsTr("Servers")
                onClicked: controlBar.panelRequested("servers")
            }
            CtrlBtn {
                icon: "list-video"
                tip: qsTr("Episodes")
                onClicked: controlBar.playlistRequested()
            }

            Divider {}

            CtrlBtn {
                icon: "folder"
                tip: qsTr("Open file")
                onClicked: controlBar.openFileRequested()
            }
            CtrlBtn {
                icon: "settings"
                tip: qsTr("Settings")
                onClicked: controlBar.panelRequested("general")
            }

            Divider {}

            CtrlBtn {
                icon: Globals.pipMode ? "picture-in-picture-2" : "picture-in-picture"
                tip: Globals.pipMode ? qsTr("Exit picture-in-picture") : qsTr("Picture-in-picture")
                onClicked: Globals.togglePip()
            }
            CtrlBtn {
                icon: Globals.fullscreen ? "minimize" : "maximize"
                tip: Globals.fullscreen ? qsTr("Exit fullscreen (F)") : qsTr("Fullscreen (F)")
                onClicked: Globals.toggleFullscreen()
            }
        }
    }
}
