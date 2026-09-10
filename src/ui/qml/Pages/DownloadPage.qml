pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "./../Components"
import QtQuick.Layouts
import QtQuick.Dialogs
import AoNami
import ".."

Item {
    id: downloadPage

    FolderDialog {
        id: folderDialog
        currentFolder: "file:///" + workDirField.text
        onAccepted: {
            let path = selectedFolder.toString().replace(/^file:\/\/\//, "")
            App.settings.downloadDir = path
            workDirField.text = App.settings.downloadDir
        }
    }
    HoverHandler {
        cursorShape: Qt.ArrowCursor
    }

    function download() {
        if (nameField.text === "" || urlField.text === "") return
        App.downloads.downloadLink(nameField.text, urlField.text)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.maximumHeight: 40
            spacing: 6

            AppTextField {
                id: workDirField
                text: App.settings.downloadDir
                fontSize: 20
                placeholderText: qsTr("Working directory")
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 8
                onAccepted: {
                    App.settings.downloadDir = text
                    text = App.settings.downloadDir
                }
            }

            AppButton {
                text: "Browse"
                fontSize: 20
                Layout.fillHeight: true
                Layout.preferredWidth: 80
                onClicked: folderDialog.open()
            }

            AppButton {
                text: "Open"
                fontSize: 20
                Layout.fillHeight: true
                Layout.preferredWidth: 80
                onClicked: Qt.openUrlExternally("file:///" + App.settings.downloadDir)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.maximumHeight: 40
            spacing: 6

            AppTextField {
                id: nameField
                fontSize: 20
                placeholderText: qsTr("Filename")
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 3
                onAccepted: downloadPage.download()
            }

            AppTextField {
                id: urlField
                fontSize: 20
                placeholderText: qsTr("m3u8 / video URL")
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 6
                onAccepted: downloadPage.download()
            }

            AppButton {
                text: "Download"
                fontSize: 20
                Layout.fillHeight: true
                Layout.preferredWidth: 100
                onClicked: downloadPage.download()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.maximumHeight: 36
            spacing: 8

            Text {
                text: taskList.count + (taskList.count === 1 ? " task" : " tasks")
                color: Theme.textMuted
                font.pixelSize: Globals.sp(20)
            }

            Item { Layout.fillWidth: true }

            Text {
                text: qsTr("Max concurrent")
                color: Theme.textSecondary
                font.pixelSize: Globals.sp(20)
            }
            AppSpinBox {
                from: 1
                to: 8
                value: App.downloads.maxDownloads
                onValueModified: App.downloads.maxDownloads = value
                Layout.preferredWidth: 104
                Layout.preferredHeight: 34
            }

            AppButton {
                text: qsTr("Pause all"); fontSize: 20; radius: 6
                secondary: true
                enabled: taskList.count > 0; opacity: enabled ? 1.0 : 0.45
                Layout.preferredHeight: 34; leftPadding: 18; rightPadding: 18
                onClicked: App.downloads.pauseAll()
            }
            AppButton {
                text: qsTr("Resume all"); fontSize: 20; radius: 6
                secondary: true
                enabled: taskList.count > 0; opacity: enabled ? 1.0 : 0.45
                Layout.preferredHeight: 34; leftPadding: 18; rightPadding: 18
                onClicked: App.downloads.resumeAll()
            }
            AppButton {
                text: qsTr("Cancel all"); fontSize: 20; radius: 6
                backgroundDefaultColor: Qt.alpha(Theme.danger, 0.9)
                enabled: taskList.count > 0; opacity: enabled ? 1.0 : 0.45
                Layout.preferredHeight: 34; leftPadding: 18; rightPadding: 18
                onClicked: App.downloads.cancelAll()
            }
        }

        ListView {
            id: taskList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            boundsBehavior: Flickable.StopAtBounds
            model: App.downloads

            ScrollBar.vertical: AppScrollBar { width: 6 }

            delegate: Card {
                id: task
                required property int    progressValue
                required property string progressText
                required property string downloadName
                required property string downloadPath
                required property int    status
                required property string stats
                required property int    index

                width: taskList.width
                height: taskCol.implicitHeight + 20
                radius: 10

                ColumnLayout {
                    id: taskCol
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

                    RowLayout {
                        spacing: 8

                        Text {
                            text: task.downloadName
                            color: Theme.textPrimary
                            font.pixelSize: Globals.sp(20)
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        AppButton {
                            readonly property bool failed: task.status === DownloadTask.Failed
                            readonly property bool stalled: failed || task.status === DownloadTask.Paused

                            text: failed ? "Retry" : task.status === DownloadTask.Paused ? "Resume" : "Pause"
                            fontSize: 20
                            backgroundDefaultColor: failed ? Theme.warning : Theme.surfaceAlt
                            contentItemTextColor: failed ? Theme.onColor(Theme.warning) : Theme.textPrimary
                            radius: 6
                            Layout.preferredWidth: 92
                            Layout.preferredHeight: 32
                            onClicked: {
                                if (stalled) App.downloads.resumeTask(task.index)
                                else App.downloads.pauseTask(task.index)
                            }
                        }

                        AppButton {
                            text: "Cancel"
                            fontSize: 20
                            backgroundDefaultColor: Theme.surfaceAlt
                            contentItemTextColor: Theme.danger
                            radius: 6
                            Layout.preferredWidth: 92
                            Layout.preferredHeight: 32
                            onClicked: App.downloads.cancelTask(task.index)
                        }
                    }

                    Text {
                        text: task.downloadPath
                        color: Theme.textMuted
                        font.pixelSize: Globals.sp(20)
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    ProgressBar {
                        id: taskProgress
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        value: task.progressValue
                        indeterminate: task.progressValue === 0

                        background: Rectangle {
                            implicitHeight: 8
                            radius: 4
                            color: Theme.border
                        }

                        contentItem: Item {
                            implicitHeight: 8
                            Rectangle {
                                width: taskProgress.visualPosition * parent.width
                                height: parent.height
                                radius: 4
                                color: Theme.accent
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: task.progressText
                            color: Theme.textMuted
                            font.pixelSize: Globals.sp(20)
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Text {
                            visible: task.stats !== ""
                            text: task.stats
                            color: Theme.accent
                            font.pixelSize: Globals.sp(20)
                            font.bold: true
                        }
                    }
                }
            }
        }
    }
}
