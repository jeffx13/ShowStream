pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../Components"
import AoNami
import ".."

Item {
    id: root
    required property MpvPlayer player

    Flickable {
        anchors {
            fill: parent
            margins: 12
        }
        contentHeight: generalCol.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: generalCol
            width: parent.width
            spacing: 6

            component SettingRow: Rectangle {
                Layout.fillWidth: true
                implicitHeight: 48
                radius: 10
                color: settingHover.hovered ? Theme.overlayLine : "transparent"
                Behavior on color { ColorAnimation { duration: 100 } }

                property alias label: settingLabel.text
                default property alias content: settingSlot.children

                HoverHandler { id: settingHover }

                RowLayout {
                    anchors {
                        fill: parent
                        leftMargin: 14
                        rightMargin: 14
                    }
                    spacing: 12

                    Text {
                        id: settingLabel
                        color: Theme.onOverlayMuted
                        font.pixelSize: Globals.sp(20)
                        Layout.fillWidth: true
                    }

                    Item {
                        id: settingSlot
                        Layout.preferredWidth: childrenRect.width
                        Layout.preferredHeight: parent.height
                    }
                }
            }

            component PanelSlider: LabeledSlider {
                stacked: true
                labelColor: Theme.onOverlayDim
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                Layout.topMargin: 6
            }

            SettingRow {
                label: qsTr("Subtitles")
                AppSwitch {
                    anchors.verticalCenter: parent.verticalCenter
                    focusPolicy: Qt.NoFocus
                    checked: root.player.subVisible
                    onToggled: root.player.subVisible = checked
                }
            }

            SettingRow {
                label: qsTr("Mute")
                AppSwitch {
                    anchors.verticalCenter: parent.verticalCenter
                    focusPolicy: Qt.NoFocus
                    checked: root.player.muted
                    onToggled: root.player.muted = checked
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                Layout.preferredHeight: 1
                color: Theme.overlayLine
            }

            PanelSlider {
                label: qsTr("Volume")
                from: 0; to: 200
                unitSuffix: "%"
                value: root.player.volume
                onMoved: (v) => root.player.volume = v
            }

            PanelSlider {
                label: qsTr("Speed")
                from: 0.1; to: 4.0; stepSize: 0.05
                unitSuffix: "x"; decimals: 2
                value: root.player.speed
                onMoved: (v) => root.player.speed = v
            }

            PanelSlider {
                label: qsTr("Sub Size")
                from: 20; to: 80; stepSize: 1
                unitSuffix: "px"
                value: App.settings.subFontSize
                onMoved: (v) => {
                    // sub-scale resizes ASS + text subs; 40 = 1.0× so the value reads as px.
                    root.player.setMpvProperty("sub-scale", v / 40.0)
                    App.settings.subFontSize = v
                }
            }

            PanelSlider {
                label: qsTr("Sub Position")
                from: 0; to: 100; stepSize: 1
                unitSuffix: "%"
                value: App.settings.subPos
                onMoved: (v) => {
                    root.player.setSubPos(v)
                    App.settings.subPos = v
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 56
                radius: 10
                color: "transparent"

                ColumnLayout {
                    anchors { fill: parent; leftMargin: 14; rightMargin: 14 }
                    spacing: 2

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: qsTr("Sub Delay")
                            color: Theme.onOverlayDim
                            font.pixelSize: Globals.sp(20)
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: qsTr("Reset")
                            color: resetDelayArea.containsMouse ? Theme.accent : Theme.onOverlayDim
                            font.pixelSize: Globals.sp(16)
                            visible: root.player.subDelay !== 0

                            MouseArea {
                                id: resetDelayArea
                                anchors.fill: parent
                                anchors.margins: -6
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.player.subDelay = 0
                            }
                        }
                    }

                    AppSlider {
                        id: subDelaySlider
                        Layout.fillWidth: true
                        from: -60; to: 60; stepSize: 0.1   // matches the clamp in setSubDelay
                        // Without this, stepSize only applies to keys and wheel; dragging stays continuous.
                        snapMode: Slider.SnapAlways
                        value: root.player.subDelay
                        unitSuffix: "s"
                        decimals: 1
                        onMoved: root.player.subDelay = value

                        // Dragging drops the binding above, and the handle then ignores Reset and mpv's z/Z.
                        Connections {
                            target: root.player
                            function onSubDelayChanged() {
                                if (!subDelaySlider.pressed)
                                    subDelaySlider.value = root.player.subDelay
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 6
                implicitHeight: 1
                color: Theme.overlayFillHover
            }

            Text {
                Layout.leftMargin: 14
                Layout.topMargin: 4
                text: qsTr("Danmaku")
                color: Theme.onOverlayMuted
                font.pixelSize: Globals.sp(20)
                font.bold: true
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                text: qsTr("Independent of Sub Size and Sub Position.")
                color: Theme.onOverlayDim
                font.pixelSize: Globals.sp(15)
                wrapMode: Text.WordWrap
            }

            PanelSlider {
                label: qsTr("Danmaku Opacity")
                from: 10; to: 100; stepSize: 5
                unitSuffix: "%"
                value: App.settings.danmakuOpacity
                onMoved: (v) => App.settings.danmakuOpacity = v
            }

            PanelSlider {
                label: qsTr("Danmaku Size")
                from: 50; to: 200; stepSize: 10
                unitSuffix: "%"
                value: App.settings.danmakuFontScale
                onMoved: (v) => App.settings.danmakuFontScale = v
            }

            PanelSlider {
                label: qsTr("Danmaku Speed")
                from: 25; to: 400; stepSize: 25
                unitSuffix: "%"
                value: App.settings.danmakuSpeed
                onMoved: (v) => App.settings.danmakuSpeed = v
            }

            PanelSlider {
                label: qsTr("Danmaku Area")
                from: 10; to: 100; stepSize: 5
                unitSuffix: "%"
                value: App.settings.danmakuArea
                onMoved: (v) => App.settings.danmakuArea = v
            }

            PanelSlider {
                label: qsTr("Danmaku Density")
                from: 0; to: 200; stepSize: 10
                value: App.settings.danmakuMaxOnScreen
                onMoved: (v) => App.settings.danmakuMaxOnScreen = v
            }

            SettingRow {
                label: qsTr("Bold Danmaku")
                AppSwitch {
                    anchors.verticalCenter: parent.verticalCenter
                    focusPolicy: Qt.NoFocus
                    checked: App.settings.danmakuBold
                    onToggled: App.settings.danmakuBold = checked
                }
            }

            Item {
                Layout.fillWidth: true
                implicitHeight: 40

                AppButton {
                    anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                    text: qsTr("Reset Danmaku Appearance")
                    backgroundDefaultColor: Theme.overlayFillActive
                    contentItemTextColor: Theme.onOverlay
                    onClicked: App.settings.resetDanmakuAppearance()
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
