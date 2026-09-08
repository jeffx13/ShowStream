import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import "../Components"
import AoNami
import ".."

Item {
    id: mpvPage
    focus: true
    property int   volumeStep: 5
    property real  normalSpeed: 1.0
    property bool  isDoubleSpeed: false

    // 0 = sidebar hidden, 1 = fully open; animating this drives the smooth push.
    property real sbAnim: playlistBar.shown ? 1 : 0
    Behavior on sbAnim { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
    readonly property real sidebarW: Math.round(width * 0.27 * sbAnim)

    MpvPlayer {
        id: mpv
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: mpvPage.width - mpvPage.sidebarW
        onPlayNext: App.playlist.stepItem(1)
        onPlaybackError: App.playlist.tryNextServer()
        Component.onCompleted: {
            Globals.mpv = mpv
            mpv.setMpvProperty("sub-scale", App.settings.subFontSize / 40.0)
            mpv.setSubPos(App.settings.subPos)
        }

        function copyVideoLink() {
            let url = mpv.currentVideoUrl().toString()
            App.copyToClipboard(url)
            mpv.showText("Copied " + url)
        }

        function peek(time) {
            controlBar.shown = true
            inactivityTimer.interval = time || 2000
            inactivityTimer.restart()
        }

        Connections {
            target: mpv
            function onIsLoadingChanged() {
                if (!mpv.isLoading) Globals.gotoPage(AppShell.Player)
            }
        }

        DropArea {
            anchors.fill: parent
            onEntered: (drag) => drag.accept(Qt.LinkAction)
            onDropped: (drop) => {
                for (let i = 0; i < drop.urls.length; i++)
                    App.playlist.openUrl(drop.urls[i], false)
            }
        }

        MouseArea {
            id: mouseArea
            property point pressPos: Qt.point(0, 0)
            property bool  pipDragging: false

            anchors {
                top: mpv.top
                bottom: controlBar.visible ? controlBar.top : mpv.bottom
                left: mpv.left; right: mpv.right
            }
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Globals.pipMode || controlBar.visible ? Qt.ArrowCursor : Qt.BlankCursor

            onPressed: (mouse) => {
                if (mouse.button === Qt.LeftButton && Globals.pipMode) {
                    pressPos = Qt.point(mouse.x, mouse.y)
                    pipDragging = false
                }
            }
            onDoubleClicked: Globals.pipMode ? Globals.togglePip() : Globals.toggleFullscreen()
            onClicked: (mouse) => { if (mouse.button === Qt.RightButton) contextMenu.popup() }
            onPositionChanged: (mouse) => {
                mpv.peek()
                if (Globals.pipMode && mouseArea.pressed && !pipDragging) {
                    if (Math.abs(mouse.x - pressPos.x) > 1 || Math.abs(mouse.y - pressPos.y) > 1) {
                        pipDragging = true
                        Globals.root.startSystemMove()
                    }
                }
            }
            onCanceled: pipDragging = false
            onReleased: (mouse) => {
                if (Globals.pipMode && pipDragging) {
                    Globals.root.ensureFullyVisibleOnScreen()
                    pipDragging = false
                } else if (!pipDragging && mouse.button === Qt.LeftButton) {
                    mpv.togglePlayPause()
                }
            }

            Timer {
                id: inactivityTimer
                interval: 2000
                onTriggered: {
                    if (mpv.visible && !mouseArea.pressed && !controlBar.hovered)
                        controlBar.shown = false
                }
            }
        }

        PlayerPanel {
            id: playerPanel
            anchors.centerIn: parent
            player: mpv
            width:  Math.min(720, Math.max(400, parent.width * 0.55))
            height: Math.min(540, Math.max(300, parent.height * 0.65))
            visible: false
            onClosed: mpvPage.forceActiveFocus()

            function toggle() {
                if (Globals.pipMode) Globals.togglePip()   // panel needs the full window
                if (opened) close()
                else { open(); playlistBar.shown = false }
                mpvPage.forceActiveFocus()
            }
        }

        ControlBar {
            id: controlBar
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
            z: mpv.z + 1
            player: mpv
            property bool shown: false
            visible: shown
            height: 64

            onPlaylistRequested: playlistBar.toggle()
            onPanelRequested: playerPanel.toggle()
            onOpenFileRequested: folderDialog.open()
        }

        Item {
            anchors.fill: parent
            z: controlBar.z + 1

            Rectangle {
                property bool active: mpv.hasOP && !App.settings.aniskipAuto
                                      && mpv.time >= mpv.aniOPStart
                                      && mpv.time < mpv.aniOPStart + mpv.aniOPLength
                anchors {
                    right: parent.right
                    bottom: parent.bottom
                    rightMargin: 28
                    bottomMargin: controlBar.visible ? controlBar.height + 18 : 28
                }
                width: introLabel.implicitWidth + 36
                height: 44
                radius: 22
                color: introArea.containsMouse ? Theme.accent : Theme.overlayScrim
                border.color: Theme.accent
                border.width: 1.5
                visible: active
                Text {
                    id: introLabel
                    anchors.centerIn: parent
                    text: "Skip Intro"
                    color: "white"
                    font.pixelSize: Globals.sp(20)
                    font.weight: Font.Medium
                }
                MouseArea {
                    id: introArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { mpv.seek(mpv.aniOPStart + mpv.aniOPLength); mpv.peek() }
                }
            }

            Rectangle {
                property bool active: mpv.hasED && !App.settings.aniskipAuto && mpv.duration > 0
                                      && mpv.time >= mpv.duration - mpv.aniEDLength
                anchors {
                    right: parent.right
                    bottom: parent.bottom
                    rightMargin: 28
                    bottomMargin: controlBar.visible ? controlBar.height + 18 : 28
                }
                width: nextLabel.implicitWidth + 36
                height: 44
                radius: 22
                color: nextArea.containsMouse ? Theme.accent : Theme.overlayScrim
                border.color: Theme.accent
                border.width: 1.5
                visible: active
                Text {
                    id: nextLabel
                    anchors.centerIn: parent
                    text: "Next Episode  ▶"
                    color: "white"
                    font.pixelSize: Globals.sp(20)
                    font.weight: Font.Medium
                }
                MouseArea {
                    id: nextArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: App.playlist.stepItem(1)
                }
            }
        }

        AppMenu {
            id: contextMenu
            modal: true

            // Menu labels are rich text, so the shortcut is dimmed inline.
            function withKey(label, key) {
                return label + " <font color='" + Theme.textMuted + "'>(" + key + ")</font>"
            }

            AppMenu {
                title: "Open"; modal: false
                Action { text: contextMenu.withKey("Open File", "E"); onTriggered: fileDialog.open() }
                Action { text: contextMenu.withKey("Open Folder", "Ctrl+E"); onTriggered: folderDialog.open() }
            }
            Action { text: contextMenu.withKey("Paste link", "Ctrl+V"); onTriggered: App.playlist.openUrl("", true) }
            Action { text: contextMenu.withKey("Copy link", "Ctrl+C"); onTriggered: mpv.copyVideoLink() }
            Action { text: contextMenu.withKey("Screenshot", "F12"); onTriggered: mpv.screenshot() }
            Action { text: contextMenu.withKey("Reload", "Ctrl+R"); onTriggered: App.playlist.reload() }
        }
    }

    PlaylistSidebar {
        id: playlistBar
        property bool shown: false
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
        width: mpvPage.sidebarW
        clip: true
        visible: mpvPage.sbAnim > 0.001
        onHideRequested: playlistBar.toggle()
        onEditorReleased: mpvPage.forceActiveFocus()
        function toggle() {
            if (Globals.pipMode) Globals.togglePip()   // leave pip to show the playlist
            shown = !shown
            mpvPage.forceActiveFocus()
        }
    }

    Connections {
        target: Globals
        function onPipModeChanged() {
            if (Globals.pipMode) { playlistBar.shown = false; playerPanel.close() }
        }
    }

    FolderDialog {
        id: folderDialog
        currentFolder: "file:///" + App.settings.downloadDir
        onAccepted: { App.playlist.openUrl(selectedFolder, true); mpvPage.forceActiveFocus() }
    }
    FileDialog {
        id: fileDialog
        currentFolder: "file:///" + App.settings.downloadDir
        fileMode: FileDialog.OpenFile
        nameFilters: [
            "All files (*)",
            "Video and Audio files (*.mp4 *.mkv *.avi *.mp3 *.flac *.wav *.ogg *.webm *.m3u8 *.ts *.mov)",
            "Subtitle files (*.srt *.ass *.ssa *.vtt *.sub *.idx)"
        ]
        onAccepted: { App.playlist.openUrl(selectedFile, true); mpvPage.forceActiveFocus() }
    }

    onVisibleChanged: if (visible) playlistBar.scrollToIndex(playlistBar.treeView.currentIndex)

    onIsDoubleSpeedChanged: {
        if (isDoubleSpeed) { normalSpeed = mpv.speed; mpv.setSpeed(mpv.speed * 2) }
        else mpv.setSpeed(normalSpeed)
    }
    function increaseSpeed(increment) {
        if (isDoubleSpeed) { normalSpeed += increment; mpv.setSpeed(mpv.speed + increment * 2) }
        else mpv.setSpeed(mpv.speed + increment)
    }

    Keys.enabled: true
    Keys.onReleased: (event) => {
        if (event.isAutoRepeat) return
        if (event.key === Qt.Key_Shift) isDoubleSpeed = false
    }
    Keys.onPressed: (event) => {
        if (!visible) return
        if (event.modifiers & Qt.ControlModifier) handleCtrlKey(event)
        else handleKey(event)
    }

    function handleKey(event) {
        if (event.modifiers & Qt.AltModifier) return
        switch (event.key) {
        case Qt.Key_V:
        case Qt.Key_G:         playerPanel.toggle(); break
        case Qt.Key_M:         mpv.muted = !mpv.muted; break
        case Qt.Key_Z:
        case Qt.Key_Left:      mpv.seek(mpv.time - 5); break
        case Qt.Key_X:
        case Qt.Key_Right:     mpv.seek(mpv.time + 5); break
        case Qt.Key_Tab:
        case Qt.Key_Asterisk:  App.playlist.showCurrentItemName(); break
        case Qt.Key_Slash:     mpv.peek(); break
        case Qt.Key_F12:       mpv.screenshot(); break
        case Qt.Key_E:         fileDialog.open(); break
        case Qt.Key_Shift:     isDoubleSpeed = true; break
        case Qt.Key_P:
        case Qt.Key_W:         playlistBar.toggle(); break
        case Qt.Key_Up:
        case Qt.Key_Q:         mpv.volume += volumeStep; break
        case Qt.Key_Down:
        case Qt.Key_A:         mpv.volume -= volumeStep; break
        case Qt.Key_Space:
        case Qt.Key_Clear:     mpv.togglePlayPause(); break
        case Qt.Key_PageUp:    App.playlist.stepItem(1); break
        case Qt.Key_Home:      App.playlist.stepItem(-1); break
        case Qt.Key_PageDown:  mpv.seek(mpv.time + 90); break
        case Qt.Key_End:       mpv.seek(mpv.time - 90); break
        case Qt.Key_Plus:
        case Qt.Key_D:         increaseSpeed(0.1); break
        case Qt.Key_Minus:
        case Qt.Key_S:         increaseSpeed(-0.1); break
        case Qt.Key_Escape:
            // The page holds focus while the panel is open, so CloseOnEscape never fires.
            if (playerPanel.opened) playerPanel.close()
            else if (Globals.pipMode) Globals.togglePip()
            else if (Globals.fullscreen) Globals.toggleFullscreen()
            break
        case Qt.Key_C:
            mpv.subVisible = !mpv.subVisible
            mpv.showText(mpv.subVisible ? "Subtitles enabled" : "Subtitles disabled")
            break
        case Qt.Key_R:         mpv.setSpeed(mpv.speed > 1.0 ? 1.0 : 2.0); break
        case Qt.Key_F:
            if (event.isAutoRepeat) return
            if (Globals.pipMode) Globals.togglePip()
            else Globals.toggleFullscreen()
            break
        default: mpv.sendKeyPress(event.text); break
        }
    }

    function handleCtrlKey(event) {
        switch (event.key) {
        case Qt.Key_Z:       mpv.seek(mpv.time - 90); break
        case Qt.Key_X:       mpv.seek(mpv.time + 90); break
        case Qt.Key_V:       App.playlist.openUrl("", true); break
        case Qt.Key_R:       App.playlist.reload(); break
        case Qt.Key_A:       playlistBar.shown = false; Globals.togglePip(); break
        case Qt.Key_C:       mpv.copyVideoLink(); break
        case Qt.Key_Control: break
        case Qt.Key_S:
            if (event.modifiers & Qt.ShiftModifier) App.playlist.stepPlaylist(-1)
            else App.playlist.stepItem(-1)
            break
        case Qt.Key_D:
            if (event.modifiers & Qt.ShiftModifier) App.playlist.stepPlaylist(1)
            else App.playlist.stepItem(1)
            break
        case Qt.Key_E:
            if (event.modifiers & Qt.ShiftModifier) Qt.openUrlExternally("file:///" + App.settings.downloadDir)
            else folderDialog.open()
            break
        default: mpv.sendKeyPress("CTRL+" + event.text); break
        }
    }
}