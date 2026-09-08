pragma ComponentBehavior: Bound
import QtQuick
import ".."
import AoNami

Rectangle {
    id: bar

    property bool   canGoBack: false
    property bool   canGoForward: false
    property string nowPlayingTitle: ""
    property string nowPlayingEpisode: ""

    signal moveRequested()
    signal maximiseToggled()
    signal minimiseRequested()
    signal closeRequested()
    signal historyStep(int delta)
    signal playerRequested()

    visible: height > 0
    focus: false

    gradient: Gradient {
        GradientStop { position: 0.0; color: Theme.surface }
        GradientStop { position: 1.0; color: Theme.surfaceDeep }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onPressed: bar.moveRequested()
        onDoubleClicked: bar.maximiseToggled()
    }

    Row {
        anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 10 }
        spacing: 2

        Repeater {
            model: [{ icon: "chevron-left", forward: false }, { icon: "chevron-right", forward: true }]
            delegate: IconButton {
                required property var modelData
                implicitWidth: 30
                implicitHeight: 30
                boxRadius: 8
                enabled: modelData.forward ? bar.canGoForward : bar.canGoBack
                opacity: enabled ? 1.0 : 0.4
                iconName: modelData.icon
                iconSize: 18
                hoverColor: Qt.alpha(Theme.accent, 0.14)
                iconColor: enabled ? Theme.textSecondary : Theme.textMuted
                iconHoverColor: Theme.textSecondary
                onClicked: bar.historyStep(modelData.forward ? 1 : -1)
            }
        }
    }

    Rectangle {
        id: npPill
        anchors { verticalCenter: parent.verticalCenter; horizontalCenter: parent.horizontalCenter }
        visible: bar.nowPlayingTitle !== "" && Globals.page !== AppShell.Player
        height: 32
        width: Math.min(npRow.implicitWidth + 24, parent.width - 240)
        radius: height / 2
        clip: true
        color: Qt.alpha(Theme.accent, 0.12)
        border.color: Theme.accent
        border.width: 1

        readonly property bool playing: Globals.mpv && Globals.mpv.state === MpvPlayer.Playing
        readonly property real progress: Globals.mpv && Globals.mpv.duration > 0
                                         ? Globals.mpv.time / Globals.mpv.duration : 0

        component NpBtn: IconButton {
            implicitWidth: 24
            implicitHeight: 24
            boxRadius: 12
            iconSize: 14
            hoverColor: Qt.alpha(Theme.accent, 0.30)
            iconColor: Theme.textPrimary
            iconHoverColor: Theme.textPrimary
        }

        Rectangle {
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: parent.width * npPill.progress
            radius: parent.radius
            Behavior on width { NumberAnimation { duration: 250 } }
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Qt.alpha(Theme.accent, 0.12) }
                GradientStop { position: 1.0; color: Qt.alpha(Theme.accent, 0.35) }
            }
            Rectangle {
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom; topMargin: 5; bottomMargin: 5 }
                width: 2
                radius: 1
                color: Theme.accent
                opacity: npPill.progress > 0.01 ? 0.9 : 0
            }
        }

        Row {
            id: npRow
            anchors.centerIn: parent
            spacing: 5
            NpBtn { iconName: "skip-back"; onClicked: App.playlist.stepItem(-1) }
            NpBtn {
                iconName: npPill.playing ? "pause" : "play"
                onClicked: Globals.mpv.togglePlayPause()
            }
            NpBtn { iconName: "skip-forward"; onClicked: App.playlist.stepItem(1) }
            Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 1; height: 16; color: Qt.alpha(Theme.accent, 0.45) }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: bar.nowPlayingTitle
                color: Theme.textPrimary
                font.pixelSize: Globals.sp(15)
                elide: Text.ElideRight
                width: Math.min(implicitWidth, 220)
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: bar.playerRequested() }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: bar.nowPlayingEpisode !== ""
                text: "·  " + bar.nowPlayingEpisode
                color: Theme.textMuted
                font.pixelSize: Globals.sp(14)
                elide: Text.ElideRight
                width: Math.min(implicitWidth, 150)
            }
        }
    }

    Row {
        anchors { verticalCenter: parent.verticalCenter; right: parent.right; rightMargin: 12 }
        spacing: 8
        layoutDirection: Qt.RightToLeft
        HoverHandler { id: groupHandler }
        Repeater {
            // Fixed traffic-light colours on purpose: these read as window controls, not as chrome.
            model: [
                { dotColor: "#ff5f57", groupColor: "#fa564d", borderColor: "#e0443e" },
                { dotColor: "#febc2e", groupColor: "#ffbf39", borderColor: "#dfa020" },
                { dotColor: "#28c840", groupColor: "#53cb43", borderColor: "#1aab29" }
            ]
            delegate: Rectangle {
                id: windowDot
                required property var modelData
                required property int index
                width: 18
                height: 18
                radius: 9
                HoverHandler { id: dotHover }
                color: dotHover.hovered     ? modelData.dotColor
                     : groupHandler.hovered ? modelData.groupColor
                                            : Theme.surfaceAlt
                border.color: dotHover.hovered ? modelData.borderColor : Theme.border
                border.width: 1
                AppIcon {
                    anchors.centerIn: parent
                    visible: groupHandler.hovered
                    name: windowDot.index === 0 ? "x"
                        : windowDot.index === 1 ? (Globals.maximised ? "minimize-2" : "expand")
                                                : "minus"
                    size: 11
                    color: Theme.scrim
                }
                TapHandler {
                    onTapped: {
                        if (windowDot.index === 0)      bar.closeRequested()
                        else if (windowDot.index === 1) bar.maximiseToggled()
                        else                            bar.minimiseRequested()
                    }
                }
            }
        }
    }
}
