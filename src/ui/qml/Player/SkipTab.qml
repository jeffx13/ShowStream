pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "../Components"
import AoNami
import ".."

Item {
    id: root
    required property MpvPlayer player

    component SkipCard: Rectangle {
        id: skipCard
        property string label
        property bool   active
        default property alias content: extraSlot.data

        signal cardToggled()

        Layout.fillWidth: true
        implicitHeight: skipContent.implicitHeight + 20
        radius: 12
        color: Theme.overlayFillSoft
        border.color: skipCard.active ? Theme.accent : Theme.overlayFill
        border.width: 1
        Behavior on border.color { ColorAnimation { duration: 150 } }

        ColumnLayout {
            id: skipContent
            anchors {
                fill: parent
                margins: 10
            }
            spacing: 8

            RowLayout {
                spacing: 8
                Text {
                    text: skipCard.label
                    color: Theme.onOverlayMuted
                    font.pixelSize: Globals.sp(20)
                    Layout.fillWidth: true
                }
                AppCheckBox {
                    focusPolicy: Qt.NoFocus
                    checked: skipCard.active
                    onToggled: skipCard.cardToggled()
                }
            }

            Item {
                id: extraSlot
                Layout.fillWidth: true
                implicitHeight: childrenRect.height
            }
        }
    }

    Flickable {
        anchors {
            fill: parent
            margins: 12
        }
        contentHeight: skipCol.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: skipCol
            width: parent.width
            spacing: 8

            Rectangle {
                id: aniskipCard
                Layout.fillWidth: true
                implicitHeight: cardCol.implicitHeight + 20
                radius: 10
                readonly property bool aniskipOn: App.settings.aniskipEnabled
                readonly property bool detected: aniskipOn && (root.player.hasOP || root.player.hasED)
                color: detected ? Theme.successSoft : Theme.overlayFillSoft
                border.color: detected ? Theme.success : Theme.overlayLine
                border.width: 1
                Behavior on border.color { ColorAnimation { duration: 150 } }

                ColumnLayout {
                    id: cardCol
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                              leftMargin: 12; rightMargin: 10 }
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Rectangle {
                            Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4
                            color: aniskipCard.detected ? Theme.success
                                 : aniskipCard.aniskipOn ? Theme.onOverlayDim : Theme.onOverlayFaint
                        }
                        Text {
                            Layout.fillWidth: true
                            text: !aniskipCard.aniskipOn ? "AniSkip · off"
                                  : (App.skipTimes.status.length > 0 ? App.skipTimes.status : "AniSkip")
                            color: Theme.onOverlayMuted
                            font.pixelSize: Globals.sp(17)
                            elide: Text.ElideRight
                        }
                        AppSpinner {
                            visible: App.skipTimes.busy
                            running: App.skipTimes.busy
                            radius: 7; dotSize: 4; dotCount: 8
                            Layout.preferredWidth: 18; Layout.preferredHeight: 18
                        }
                        AppSwitch {
                            focusPolicy: Qt.NoFocus
                            checked: App.settings.aniskipEnabled
                            onToggled: App.settings.aniskipEnabled = checked
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: aniskipCard.aniskipOn
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Text { text: "Auto-skip"; color: Theme.onOverlayMuted; font.pixelSize: Globals.sp(17) }
                                Text { text: "Jump past intro & outro automatically"; color: Theme.onOverlayDim; font.pixelSize: Globals.sp(13) }
                            }
                            AppSwitch {
                                focusPolicy: Qt.NoFocus
                                checked: App.settings.aniskipAuto
                                onToggled: App.settings.aniskipAuto = checked
                            }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.overlayLine }

                        Text {
                            text: "Search query"
                            color: Theme.onOverlayDim
                            font.pixelSize: Globals.sp(14)
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            AppTextField {
                                id: queryField
                                Layout.fillWidth: true
                                fontSize: 16
                                placeholderText: "Search title..."
                                Component.onCompleted: text = App.skipTimes.searchQuery
                                onAccepted: App.skipTimes.searchQuery = text
                                Connections {
                                    target: App.skipTimes
                                    function onSearchQueryChanged() {
                                        if (!queryField.activeFocus) queryField.text = App.skipTimes.searchQuery
                                    }
                                }
                            }
                            AppButton {
                                id: researchBtn
                                Layout.preferredWidth: 38
                                Layout.preferredHeight: 36
                                text: ""
                                AppToolTip { text: qsTr("Search again"); visible: researchBtn.hovered }
                                onClicked: { App.skipTimes.searchQuery = queryField.text; App.skipTimes.rematch() }
                                AppIcon { anchors.centerIn: parent; name: "refresh-cw"; size: 17; color: Theme.onOverlay }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text { text: "Matched show"; color: Theme.onOverlayDim; font.pixelSize: Globals.sp(14) }
                                AppComboBox {
                                    Layout.fillWidth: true
                                    text: ""
                                    fontSize: 15
                                    placeholderText: "No match"
                                    model: App.skipTimes.showTitles
                                    currentIndex: App.skipTimes.selectedShowIndex
                                    onActivated: App.skipTimes.selectedShowIndex = currentIndex
                                }
                            }
                            ColumnLayout {
                                Layout.preferredWidth: 96
                                spacing: 2
                                Text { text: "Episode"; color: Theme.onOverlayDim; font.pixelSize: Globals.sp(14) }
                                AppSpinBox {
                                    Layout.fillWidth: true
                                    from: 1
                                    to: Math.max(1, App.skipTimes.episodeCount, App.skipTimes.selectedEpisodeIndex)
                                    value: App.skipTimes.selectedEpisodeIndex
                                    stepSize: 1
                                    focusPolicy: Qt.NoFocus
                                    onValueModified: App.skipTimes.selectedEpisodeIndex = value
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 2
                            spacing: 8
                            visible: App.skipTimes.introRange.length > 0 || App.skipTimes.outroRange.length > 0

                            Repeater {
                                model: [
                                    { tag: "Intro", range: App.skipTimes.introRange },
                                    { tag: "Outro", range: App.skipTimes.outroRange }
                                ]
                                delegate: Rectangle {
                                    id: skipRange
                                    required property var modelData
                                    visible: skipRange.modelData.range.length > 0
                                    Layout.fillWidth: true
                                    implicitHeight: 30
                                    radius: 8
                                    color: Theme.successSoft
                                    border.color: Theme.successMuted
                                    border.width: 1
                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 6
                                        Text {
                                            text: skipRange.modelData.tag
                                            color: Theme.success
                                            font { pixelSize: Globals.sp(13); weight: Font.DemiBold; letterSpacing: 0.5 }
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Text {
                                            text: skipRange.modelData.range
                                            color: Theme.onOverlay
                                            font.pixelSize: Globals.sp(15)
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            SkipCard {
                label: "Skip Opening"
                active: root.player.skipOP
                onCardToggled: root.player.skipOP = !root.player.skipOP

                RowLayout {
                    width: parent.width
                    spacing: 8
                    Text { text: "Start";  color: Theme.onOverlayDim; font.pixelSize: Globals.sp(20) }
                    AppSpinBox {
                        Layout.fillWidth: true
                        value: root.player.skipOPStart
                        from: 0
                        to: root.player.duration
                        focusPolicy: Qt.NoFocus
                        stepSize: 10
                        onValueModified: root.player.skipOPStart = value
                    }
                    Text { text: "Length"; color: Theme.onOverlayDim; font.pixelSize: Globals.sp(20) }
                    AppSpinBox {
                        Layout.fillWidth: true
                        value: root.player.skipOPLength
                        from: 0
                        to: root.player.duration
                        focusPolicy: Qt.NoFocus
                        stepSize: 10
                        onValueModified: root.player.skipOPLength = value
                    }
                }
            }

            SkipCard {
                label: "Skip Ending"
                active: root.player.skipED
                onCardToggled: root.player.skipED = !root.player.skipED

                RowLayout {
                    width: parent.width
                    spacing: 8
                    Text { text: "Length"; color: Theme.onOverlayDim; font.pixelSize: Globals.sp(20) }
                    AppSpinBox {
                        Layout.fillWidth: true
                        value: root.player.skipEDLength
                        from: 0
                        to: root.player.duration
                        focusPolicy: Qt.NoFocus
                        stepSize: 10
                        onValueModified: root.player.skipEDLength = value
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 48
                radius: 12
                color: (root.player.skipOP && root.player.skipED) ? Theme.overlayFill : Theme.overlayFillSoft
                border.color: (root.player.skipOP && root.player.skipED) ? Theme.accent : Theme.overlayFill
                border.width: 1
                Behavior on color { ColorAnimation { duration: 150 } }
                Behavior on border.color { ColorAnimation { duration: 150 } }

                RowLayout {
                    anchors {
                        fill: parent
                        leftMargin: 14
                        rightMargin: 14
                    }
                    spacing: 8

                    Text {
                        text: "Skip Both"
                        color: Theme.onOverlayMuted
                        font.pixelSize: Globals.sp(20)
                        Layout.fillWidth: true
                    }

                    AppCheckBox {
                        id: skipBothCb
                        focusPolicy: Qt.NoFocus
                        checked: root.player.skipED && root.player.skipOP
                        onToggled: {
                            let v = !(root.player.skipED && root.player.skipOP)
                            root.player.skipED = v
                            root.player.skipOP = v
                        }
                        AppToolTip { text: qsTr("Toggle both OP and ED skip"); visible: skipBothCb.hovered }
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
